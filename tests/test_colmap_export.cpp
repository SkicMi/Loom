// Izvoz rekonstrukcije u COLMAP-ov format - most prema treningu gaussian splattinga.
//
// Treneri ocekuju bas taj format, pa je ovo tocka na kojoj nas solve prelazi dalje. Jedino sto
// ovdje moze tiho promasiti je KONVENCIJA: COLMAP zapisuje rotaciju i pomak iz svijeta u kameru, u
// klasicnoj konvenciji (+Z naprijed, +Y dolje), a nasa Pose je obrnuta.
//
// Zato test cita natrag VLASTITI zapis i projicira po COLMAP-ovim pravilima. Uz to ide pozitivna
// kontrola: ista projekcija BEZ zrcaljenja mora ispasti ocito kriva. Bez nje test ne bi razlikovao
// dvije konvencije - a kriva konvencija znaci trening koji da kasu i krivnju koja se trazi drugdje.
#include "TestHarness.h"

#include <Engine/ColmapExport.h>
#include <Engine/Reconstruct.h>
#include <Engine/SyntheticScene.h>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace{

struct ColmapImage{
    glm::dquat rotation{1.0, 0.0, 0.0, 0.0};
    glm::dvec3 translation{0.0};
    std::string name;
    std::vector<std::pair<glm::dvec2, uint32_t>> pixels;   //piksel i id tocke
};

}

int main(){
    TestReport report("izvoz u COLMAP");

    Engine::SyntheticConfig config;
    config.noisePixels = 0.5f;
    const Engine::SyntheticScene scene = Engine::makeSyntheticScene(config);
    const Engine::Reconstruction state = Engine::reconstruct(scene.observations, scene.poses.size(),
                                                             scene.points.size(), scene.intrinsics);

    const std::string directory = "colmap_export_test";
    std::filesystem::create_directories(directory);

    const bool written = Engine::writeColmapText(directory, state, scene.intrinsics, scene.observations);
    report.check("zapis je nastao", written &&
        std::filesystem::exists(directory + "/cameras.txt") &&
        std::filesystem::exists(directory + "/images.txt") &&
        std::filesystem::exists(directory + "/points3D.txt"),
        "cameras.txt, images.txt i points3D.txt");

    if(!written) return report.result();

    // -------------------------------------------------------------------------------
    // Citanje natrag
    // -------------------------------------------------------------------------------

    double fx = 0, fy = 0, cx = 0, cy = 0;
    uint32_t width = 0, height = 0;
    {
        std::ifstream file(directory + "/cameras.txt");
        std::string line;
        while(std::getline(file, line)){
            if(line.empty() || line[0] == '#') continue;
            std::istringstream stream(line);
            uint32_t id; std::string model;
            stream >> id >> model >> width >> height >> fx >> fy >> cx >> cy;
            break;
        }
    }

    std::vector<ColmapImage> images;
    {
        std::ifstream file(directory + "/images.txt");
        std::string line;
        while(std::getline(file, line)){
            if(line.empty() || line[0] == '#') continue;

            ColmapImage image;
            uint32_t id = 0, cameraId = 0;
            {
                std::istringstream stream(line);
                stream >> id >> image.rotation.w >> image.rotation.x >> image.rotation.y >> image.rotation.z
                       >> image.translation.x >> image.translation.y >> image.translation.z >> cameraId >> image.name;
            }
            if(!std::getline(file, line)) break;
            {
                std::istringstream stream(line);
                double x = 0, y = 0; uint32_t point = 0;
                while(stream >> x >> y >> point) image.pixels.push_back({glm::dvec2(x, y), point});
            }
            images.push_back(image);
        }
    }

    std::vector<glm::dvec3> points(scene.points.size() + 1, glm::dvec3(0.0));
    std::vector<uint8_t> known(scene.points.size() + 1, 0);
    size_t pointLines = 0;
    {
        std::ifstream file(directory + "/points3D.txt");
        std::string line;
        while(std::getline(file, line)){
            if(line.empty() || line[0] == '#') continue;
            std::istringstream stream(line);
            uint32_t id = 0; double x = 0, y = 0, z = 0;
            stream >> id >> x >> y >> z;
            if(id < points.size()){
                points[id] = glm::dvec3(x, y, z);
                known[id] = 1;
            }
            ++pointLines;
        }
    }

    report.check("zapisano je ono sto je rijeseno",
        images.size() == state.posedCameras && pointLines == state.solvedPoints,
        fmt("%zu kamera (rijeseno %u), %zu tocaka (rijeseno %u)",
            images.size(), state.posedCameras, pointLines, state.solvedPoints));

    report.check("intrinsike su prenesene",
        std::fabs(fx - scene.intrinsics.fx) < 1e-6 && std::fabs(cy - scene.intrinsics.cy) < 1e-6 &&
        width == scene.intrinsics.width && height == scene.intrinsics.height,
        fmt("fx %.3f, fy %.3f, cx %.3f, cy %.3f, %ux%u", fx, fy, cx, cy, width, height));

    // -------------------------------------------------------------------------------
    // Projekcija po COLMAP-ovim pravilima mora pogoditi zapisane piksele
    // -------------------------------------------------------------------------------

    //TRI ODVOJENE TVRDNJE, jer prva verzija ovog testa mijesala je dvije: usporedjivala je
    //projekciju rekonstruiranih tocaka sa ZASUMLJENIM opazanjima i trazila poklapanje na 0.01 px.
    //Ta razlika je sama reprojekcijska greska rekonstrukcije (medijan 0.53 px) - dakle mjerila se
    //kvaliteta solvea umjesto ispravnosti konvencije
    double worstAgainstEngine = 0.0;     //konvencija: isti broj kroz dva razlicita puta
    double worstAgainstStored = 0.0;     //sadrzaj: u fileu su prava opazanja
    double worstWithoutMirror = 0.0;     //kontrola: zapis bez zrcaljenja mora promasiti
    size_t compared = 0;

    for(const ColmapImage& image : images){
        const glm::dmat3 rotation = glm::mat3_cast(glm::normalize(image.rotation));

        //Ime kadra nosi redni broj kamere, isto kako ga je izvoz zapisao
        const uint32_t camera = uint32_t(std::stoul(image.name.substr(6, 4)));

        for(const auto& entry : image.pixels){
            if(entry.second >= points.size() || !known[entry.second]) continue;
            const uint32_t point = entry.second - 1;

            const glm::dvec3 inCamera = rotation * points[entry.second] + image.translation;
            if(inCamera.z <= 0.0) continue;   //COLMAP: +Z naprijed

            const glm::dvec2 projected(cx + fx * inCamera.x / inCamera.z,
                                       cy + fy * inCamera.y / inCamera.z);

            //Isti piksel, ali kroz Engineovu projekciju i nasu konvenciju
            glm::vec2 mine;
            if(!Engine::project(state.poses[camera], scene.intrinsics, state.points[point], mine)) continue;

            worstAgainstEngine = std::max(worstAgainstEngine, glm::length(projected - glm::dvec2(mine)));
            worstAgainstStored = std::max(worstAgainstStored, glm::length(projected - entry.first));
            ++compared;

            //KONTROLA: zapis bez zrcaljenja. M * R_colmap vraca R' - tocno ono sto bi zapisao
            //netko tko je preskocio prijelaz izmedju konvencija
            glm::dmat3 unmirrored = rotation;
            for(int column = 0; column < 3; ++column){
                unmirrored[column][1] = -unmirrored[column][1];
                unmirrored[column][2] = -unmirrored[column][2];
            }
            const glm::dvec3 wrongCamera = unmirrored * points[entry.second] + image.translation;
            if(wrongCamera.z > 0.0){
                const glm::dvec2 wrong(cx + fx * wrongCamera.x / wrongCamera.z,
                                       cy + fy * wrongCamera.y / wrongCamera.z);
                worstWithoutMirror = std::max(worstWithoutMirror, glm::length(wrong - entry.first));
            }
        }
    }

    report.check("konvencija je tocna: isti piksel kroz oba puta",
        compared > 1000 && worstAgainstEngine < 1e-3,
        fmt("najveca razlika %.2e px kroz %zu opazanja", worstAgainstEngine, compared));

    report.check("u fileu su prava opazanja",
        worstAgainstStored > 0.1 && worstAgainstStored < 5.0,
        fmt("najveci promasaj prema zapisanom pikselu %.2f px - to je reprojekcijska greska same "
            "rekonstrukcije (medijan %.3f px)", worstAgainstStored, state.medianReprojection));

    report.check("kontrola: zapis bez zrcaljenja bi se vidio",
        worstWithoutMirror > 50.0,
        fmt("promasaj %.1f px", worstWithoutMirror));

    std::filesystem::remove_all(directory);
    return report.result();
}
