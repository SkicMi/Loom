// Izvoz rijesene kamere u USD - most prema alatu u kojem se radi kompozit (Nuke, Houdini, Blender).
//
// Jedino sto ovdje moze tiho promasiti je KONVENCIJA MATRICE. USD je po RETCIMA, s vektorom
// retkom (v' = v * M), a glm je po stupcima. Tko transponira krivo, dobije kameru koja se vrti oko
// ishodista umjesto da stoji gdje je stajala - i to ne padne nego samo izgleda krivo.
//
// Zato test cita natrag VLASTITI zapis i projicira po USD-ovim pravilima: piksel mora ispasti isti
// kao iz nase project(). Uz to ide NEGATIVNA KONTROLA: ista projekcija s matricom citanom po
// stupcima mora ispasti ocito kriva. Bez nje test ne bi razlikovao dvije konvencije, a bas ih
// treba razlikovati.
#include "TestHarness.h"

#include <Engine/Reconstruct.h>
#include <Engine/SyntheticScene.h>
#include <Engine/UsdExport.h>

#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace{

struct Sample{
    int frame = 0;
    glm::dvec3 row[4];    //right, up, back, position - onako kako su zapisani
};

//Svi brojevi iz jednog retka vremenskog uzorka. Oblik je
//   <kadar>: ( (a, b, c, 0), (d, e, f, 0), (g, h, i, 0), (x, y, z, 1) ),
bool parseSample(const std::string& line, Sample& out){
    const size_t colon = line.find(':');
    if(colon == std::string::npos) return false;

    out.frame = std::atoi(line.c_str());

    std::vector<double> numbers;
    const char* text = line.c_str() + colon + 1;
    while(*text){
        if((*text >= '0' && *text <= '9') || *text == '-' || *text == '+' ||
           (*text == '.' && text[1] >= '0' && text[1] <= '9')){
            char* end = nullptr;
            numbers.push_back(std::strtod(text, &end));
            text = end;
        }else{
            ++text;
        }
    }
    if(numbers.size() != 16) return false;

    for(int row = 0; row < 4; ++row){
        out.row[row] = glm::dvec3(numbers[size_t(row * 4 + 0)],
                                  numbers[size_t(row * 4 + 1)],
                                  numbers[size_t(row * 4 + 2)]);
    }
    return true;
}

//Piksel po USD-ovim pravilima, iz procitane matrice. Matrica je kamera->svijet po retcima, pa je
//svijet->kamera samo projekcija razlike na njezina tri retka
glm::dvec2 pixelFromRows(const Sample& sample, const Engine::Intrinsics& intrinsics,
                         const glm::dvec3& point, bool& infront){
    const glm::dvec3 offset = point - sample.row[3];
    const glm::dvec3 inCamera(glm::dot(offset, sample.row[0]),
                              glm::dot(offset, sample.row[1]),
                              glm::dot(offset, sample.row[2]));

    const double depth = -inCamera.z;           //kamera gleda niz -Z
    infront = depth > 0.0;
    if(!infront) return {};

    return glm::dvec2(double(intrinsics.cx) + double(intrinsics.fx) * (inCamera.x / depth),
                      double(intrinsics.cy) + double(intrinsics.fy) * (-inCamera.y / depth));
}

}

int main(){
    TestReport report("S7 izvoz u USD");

    Engine::SyntheticConfig config;
    config.cameraCount = 6;
    config.pointCount = 120;
    const Engine::SyntheticScene scene = Engine::makeSyntheticScene(config);

    //Rekonstrukcija koja je BAS istinita scena - ovdje se ne ispituje solver nego zapis
    Engine::Reconstruction truth;
    truth.poses = scene.poses;
    truth.points = scene.points;
    truth.posed.assign(scene.poses.size(), uint8_t(1));
    truth.solved.assign(scene.points.size(), uint8_t(1));
    truth.posedCameras = uint32_t(scene.poses.size());
    truth.solvedPoints = uint32_t(scene.points.size());
    truth.ok = true;

    const std::filesystem::path out = std::filesystem::temp_directory_path() / "loom_usd_test.usda";

    Engine::UsdExportConfig usd;
    usd.firstFrame = 1;
    usd.frameStep = 10;                    //kao VideoSolve koji uzima svaki deseti kadar
    usd.framesPerSecond = 50.0;
    usd.sensorWidthMillimetres = 23.5;     //APS-C, kao Sony s kojim snimamo

    const bool wrote = Engine::writeUsdScene(out.string(), truth, scene.intrinsics,
                                             scene.intrinsics.width, scene.intrinsics.height, {}, usd);
    report.check("zapis je uspio", wrote, out.string());

    //Procitaj natrag
    std::vector<Sample> samples;
    double focalLength = 0.0, horizontalAperture = 0.0;
    {
        std::ifstream file(out);
        std::string line;
        while(std::getline(file, line)){
            if(line.find("float focalLength") != std::string::npos){
                focalLength = std::strtod(line.c_str() + line.find('=') + 1, nullptr);
            }else if(line.find("float horizontalAperture") != std::string::npos){
                horizontalAperture = std::strtod(line.c_str() + line.find('=') + 1, nullptr);
            }else{
                Sample sample;
                if(line.find(": (") != std::string::npos && parseSample(line, sample)){
                    samples.push_back(sample);
                }
            }
        }
    }

    report.check("jedan uzorak po rijesenoj kameri",
        samples.size() == scene.poses.size(),
        fmt("%zu uzoraka naspram %zu kamera", samples.size(), scene.poses.size()));

    //Kadrovi moraju pratiti korak, inace bi izvoz tvrdio da je snimka deset puta kraca
    bool framesRight = !samples.empty();
    for(size_t index = 0; index < samples.size(); ++index){
        if(samples[index].frame != int(1 + index * 10)) framesRight = false;
    }
    report.check("kadrovi prate korak snimke", framesRight,
        samples.empty() ? "nema uzoraka" : fmt("prvi %d, drugi %d", samples[0].frame,
                                               samples.size() > 1 ? samples[1].frame : -1));

    //ZARISNA. Jedino sto mora vrijediti je omjer zarisna/otvor = fx/sirina
    const double wanted = double(scene.intrinsics.fx) / double(scene.intrinsics.width);
    const double got = horizontalAperture > 0.0 ? focalLength / horizontalAperture : 0.0;
    report.check("omjer zarisne i otvora odgovara fx", std::fabs(got - wanted) < 1e-6,
        fmt("%.6f naspram %.6f (zarisna %.3f mm na senzoru %.1f mm)",
            got, wanted, focalLength, horizontalAperture));

    //KONVENCIJA. Piksel iz procitane matrice mora biti isti kao iz nase projekcije
    double worstCorrect = 0.0;
    double worstSwapped = 0.0;
    size_t compared = 0;

    for(size_t index = 0; index < samples.size() && index < scene.poses.size(); ++index){
        const Sample& sample = samples[index];

        //Ista matrica, ali citana po STUPCIMA - negativna kontrola
        Sample swapped;
        swapped.frame = sample.frame;
        for(int row = 0; row < 3; ++row){
            swapped.row[row] = glm::dvec3(sample.row[0][row], sample.row[1][row], sample.row[2][row]);
        }
        swapped.row[3] = sample.row[3];

        for(const glm::vec3& point : scene.points){
            glm::vec2 ours;
            if(!Engine::project(scene.poses[index], scene.intrinsics, point, ours)) continue;

            bool infront = false;
            const glm::dvec2 theirs = pixelFromRows(sample, scene.intrinsics, glm::dvec3(point), infront);
            if(!infront) continue;

            worstCorrect = std::max(worstCorrect,
                glm::length(theirs - glm::dvec2(double(ours.x), double(ours.y))));
            ++compared;

            bool swappedInFront = false;
            const glm::dvec2 wrong = pixelFromRows(swapped, scene.intrinsics, glm::dvec3(point), swappedInFront);
            if(swappedInFront){
                worstSwapped = std::max(worstSwapped,
                    glm::length(wrong - glm::dvec2(double(ours.x), double(ours.y))));
            }
        }
    }

    report.check("matrica po USD-ovim pravilima daje iste piksele",
        compared > 100 && worstCorrect < 1e-3,
        fmt("najgore %.2e px na %zu usporedbi", worstCorrect, compared));

    //Bez ovoga test ne bi razlikovao dvije konvencije
    report.check("po stupcima citana matrica je ocito kriva", worstSwapped > 10.0,
        fmt("najgore %.1f px", worstSwapped));

    //NEPOSTAVLJENA KAMERA NEMA UZORAK. USD izmedju susjednih interpolira, a to je bolje nego
    //zapisati pozu koja ne znaci nista - ali mora se stvarno preskociti
    {
        Engine::Reconstruction partial = truth;
        partial.posed[2] = 0;
        --partial.posedCameras;

        const std::filesystem::path gap = std::filesystem::temp_directory_path() / "loom_usd_gap.usda";
        Engine::writeUsdScene(gap.string(), partial, scene.intrinsics, scene.intrinsics.width, scene.intrinsics.height, {}, usd);

        std::vector<Sample> gapSamples;
        std::ifstream file(gap);
        std::string line;
        while(std::getline(file, line)){
            Sample sample;
            if(line.find(": (") != std::string::npos && parseSample(line, sample)){
                gapSamples.push_back(sample);
            }
        }

        bool missing = true;
        for(const Sample& sample : gapSamples) if(sample.frame == 21) missing = false;

        report.check("nerijesena kamera nema uzorak",
            gapSamples.size() == samples.size() - 1 && missing,
            fmt("%zu uzoraka, kadar 21 %s", gapSamples.size(), missing ? "izostavljen" : "JOS TU"));
        std::filesystem::remove(gap);
    }

    std::filesystem::remove(out);
    return report.result();
}
