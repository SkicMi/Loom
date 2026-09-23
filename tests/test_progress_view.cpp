// Zivi prikaz onoga sto solver nalazi: zapis, citanje, uspravnost i projekcija.
//
// ZASTO SE OVO TESTIRA. Prozor se ne da snimiti na ovom sustavu, pa "vidjet ce se kad se otvori"
// nije provjera nego nada. Ovo se ipak da izmjeriti bez slike:
//
//   1. ZAPIS I CITANJE. Isti kod pise (VideoSolve) i cita (loom), ali binarni raspored svejedno
//      tiho pukne - promijeni netko redoslijed polja i scena postane smece.
//   2. STARI ZAPIS SE I DALJE CITA. Mape od prije ove izmjene imaju v1 snimak.
//   3. PRIKAZ JE USPRAVAN. Scena nagnuta koliko je prva kamera bila nagnuta je upravo ono sto je
//      prikaz prije radio; nakon pripreme gornje osi kamera moraju biti +Y.
//   4. PROJEKCIJA. Ako je mjerilo promaseno, sve padne u jedan piksel ili izvan kadra - broji se.
//   5. PUTANJA SE CRTA. Kamere spojene duzinama, ne samo kao tocke.
//
// NEGATIVNA KONTROLA je greska koju smo vec jednom napravili: jedna tocka odbjegla na 1e5. Ako se
// srediste racuna po min/max umjesto po postotcima, scena se skupi u tocku.
#include "TestHarness.h"

#include "../src/LoomProgress.h"
#include "../src/LoomResult.h"

#include "Engine/ColmapExport.h"

#include <glm/gtc/quaternion.hpp>

#include <cmath>
#include <filesystem>

namespace{

//Scena kakvu solver stvarno daje: tocke po kvadru, kamere na luku, vodoravne - pa zatim cijela
//nagnuta za zadani kut, kao da je prva kamera bila nagnuta
Loom::Snapshot tiltedScene(float tiltDegrees){
    Loom::Snapshot snapshot;
    for(int i = 0; i < 2000; ++i){
        const float a = float(i) * 0.7919f;
        snapshot.points.push_back(glm::vec3(std::sin(a) * 2.0f, std::cos(a * 1.7f) * 1.2f,
                                            std::sin(a * 0.3f) * 2.0f));
        snapshot.colours.push_back(glm::u8vec3(uint8_t(i % 256), uint8_t((i * 7) % 256), 200));
    }
    for(int i = 0; i < 20; ++i){
        const float a = -0.6f + float(i) * 0.06f;
        const glm::vec3 position(std::sin(a) * 6.0f, 0.5f, std::cos(a) * 6.0f);
        snapshot.cameras.push_back(position);
        snapshot.orientations.push_back(glm::quatLookAt(glm::normalize(-position), glm::vec3(0.0f, 1.0f, 0.0f)));
    }
    const glm::quat tilt = glm::angleAxis(glm::radians(tiltDegrees), glm::normalize(glm::vec3(1.0f, 0.2f, 0.3f)));
    for(glm::vec3& p : snapshot.points) p = tilt * p;
    for(glm::vec3& c : snapshot.cameras) c = tilt * c;
    for(glm::quat& q : snapshot.orientations) q = glm::normalize(tilt * q);
    return snapshot;
}

glm::vec2 spreadOf(const Treadle::DrawList& list){
    float lowX = 1e9f, highX = -1e9f, lowY = 1e9f, highY = -1e9f;
    for(const Treadle::Vertex& v : list.vertices){
        lowX = std::min(lowX, v.x); highX = std::max(highX, v.x);
        lowY = std::min(lowY, v.y); highY = std::max(highY, v.y);
    }
    return {highX - lowX, highY - lowY};
}

}

int main(){
    TestReport report("S9 zivi prikaz napretka");

    const Loom::Snapshot original = tiltedScene(25.0f);
    const std::filesystem::path path = std::filesystem::temp_directory_path() / "loom_napredak_test.bin";

    //-- 1. zapis i citanje, do zadnjeg bita ---------------------------------------------------
    {
        const bool wrote = Loom::writeSnapshot(path.string(), original);
        Loom::Snapshot read;
        const bool ok = wrote && Loom::readSnapshot(path.string(), read);

        bool same = ok && read.cameras.size() == original.cameras.size() &&
                    read.points.size() == original.points.size() &&
                    read.orientations.size() == original.orientations.size() &&
                    read.colours.size() == original.colours.size();
        for(size_t i = 0; same && i < original.points.size(); ++i){
            if(read.points[i] != original.points[i] || read.colours[i] != original.colours[i]) same = false;
        }
        for(size_t i = 0; same && i < original.cameras.size(); ++i){
            if(read.cameras[i] != original.cameras[i] || read.orientations[i] != original.orientations[i]) same = false;
        }
        report.check("v2 zapis i citanje daju isto", same,
            fmt("%zu kamera s orijentacijom, %zu tocaka s bojom", read.cameras.size(), read.points.size()));
    }

    //-- 2. stari v1 zapis se i dalje cita ------------------------------------------------------
    {
        const std::filesystem::path old = std::filesystem::temp_directory_path() / "loom_napredak_v1.bin";
        {
            std::ofstream file(old, std::ios::binary);
            const uint32_t cameraCount = uint32_t(original.cameras.size());
            const uint32_t pointCount = uint32_t(original.points.size());
            file.write("LOOMPRG1", 8);
            file.write(reinterpret_cast<const char*>(&cameraCount), 4);
            file.write(reinterpret_cast<const char*>(&pointCount), 4);
            file.write(reinterpret_cast<const char*>(original.cameras.data()), cameraCount * 12);
            file.write(reinterpret_cast<const char*>(original.points.data()), pointCount * 12);
        }
        Loom::Snapshot read;
        const bool ok = Loom::readSnapshot(old.string(), read);
        report.check("stari v1 zapis se cita", ok && read.points.size() == original.points.size() &&
                                              read.orientations.empty() && read.colours.empty(),
            fmt("%zu tocaka, bez orijentacija i boja", read.points.size()));
        std::filesystem::remove(old);

        const std::filesystem::path bad = std::filesystem::temp_directory_path() / "loom_napredak_lose.bin";
        std::ofstream(bad, std::ios::binary) << "NIJETO12" << "smece";
        Loom::Snapshot ignored;
        report.check("kriv potpis se odbija", !Loom::readSnapshot(bad.string(), ignored), "NIJETO12");
        std::filesystem::remove(bad);
    }

    //-- 3. prikaz je uspravan ------------------------------------------------------------------
    //
    //Dvije stvari koje je prva verzija ovog testa krivo ocekivala, zapisane da se ne ponove:
    //  - nagib od 25 st oko osi (1, 0.2, 0.3) NE nagne os Y za 25 st nego za
    //    acos(cos25 + (1 - cos25) * 0.188^2) = 24.55 st; to je sto se ocekuje
    //  - kamere gledaju 4.8 st prema dolje u srediste, pa smjer POGLEDA nije vodoravan. Vodoravne su
    //    im DESNE osi - to je ono sto uspravnost znaci
    {
        const glm::vec3 axis = glm::normalize(glm::vec3(1.0f, 0.2f, 0.3f));
        const float expected = glm::degrees(std::acos(std::cos(glm::radians(25.0f)) +
                                            (1.0f - std::cos(glm::radians(25.0f))) * axis.y * axis.y));

        const Loom::PreparedScene scene = Loom::prepareScene(original);
        float worst = 0.0f;
        for(const glm::vec3& forward : scene.forwards) worst = std::max(worst, std::fabs(forward.y));
        //Pogled u srediste s visine 0.5 na udaljenosti 6: 4.76 st prema dolje, isti za sve kamere
        const float lookDown = glm::degrees(std::asin(worst));
        report.check("scena nagnuta 25 st oko kose osi se crta uspravno",
            scene.upright && std::fabs(scene.tiltDegrees - expected) < 0.05f && std::fabs(lookDown - 4.76f) < 0.05f,
            fmt("prepoznat nagib %.2f st (tocno %.2f), pogled dolje %.2f st (tocno 4.76)",
                scene.tiltDegrees, expected, lookDown));
    }

    //-- 4. projekcija: u kadar i razasuto ------------------------------------------------------
    {
        const Loom::PreparedScene scene = Loom::prepareScene(original);
        Loom::ViewState view;
        view.showGrid = view.showAxes = view.showPath = view.showDirections = false;

        Treadle::DrawList list;
        const Loom::PaintReport painted = Loom::paintScene(scene, list, 1180.0f, 760.0f, view);
        report.check("vecina tocaka padne u kadar", painted.drawnPoints > original.points.size() / 2,
            fmt("%u od %zu", painted.drawnPoints, original.points.size()));

        const glm::vec2 spread = spreadOf(list);
        report.check("scena zauzima kadar, nije tocka", spread.x > 200.0f && spread.y > 120.0f,
            fmt("%.0f x %.0f px na prozoru 1180 x 760", spread.x, spread.y));
    }

    //-- 5. putanja se crta kao linija ----------------------------------------------------------
    {
        const Loom::PreparedScene scene = Loom::prepareScene(original);
        Loom::ViewState view;
        view.showGrid = view.showAxes = view.showDirections = false;

        view.showPath = false;
        Treadle::DrawList without;
        Loom::paintScene(scene, without, 1180.0f, 760.0f, view);
        view.showPath = true;
        Treadle::DrawList with;
        Loom::paintScene(scene, with, 1180.0f, 760.0f, view);

        //Svaka duzina je jedan cetverokut, dakle cetiri vrha; 20 kamera daje 19 duzina
        const size_t added = (with.vertices.size() - without.vertices.size()) / 4;
        report.check("putanja je 19 duzina izmedju 20 kamera", added == 19, fmt("%zu duzina", added));
    }

    //-- NEGATIVNA KONTROLA: odbjegla tocka ne smije skupiti scenu -------------------------------
    {
        Loom::Snapshot strayed = original;
        strayed.points.push_back(glm::vec3(1.0e5f, -4.0e5f, 9.0e5f));
        strayed.colours.push_back(glm::u8vec3(255));

        const Loom::PreparedScene scene = Loom::prepareScene(strayed);
        Loom::ViewState view;
        view.showGrid = view.showAxes = view.showPath = view.showDirections = false;
        Treadle::DrawList list;
        const Loom::PaintReport painted = Loom::paintScene(scene, list, 1180.0f, 760.0f, view);
        const glm::vec2 spread = spreadOf(list);
        report.check("odbjegla tocka ne skuplja scenu",
            painted.drawnPoints > original.points.size() / 2 && spread.x > 200.0f,
            fmt("%u nacrtano, raspon %.0f px", painted.drawnPoints, spread.x));
    }

    //-- premalo tocaka se ne crta, ne pada ------------------------------------------------------
    {
        Loom::Snapshot tiny;
        tiny.points.assign(3, glm::vec3(0.0f, 0.0f, -5.0f));
        Treadle::DrawList list;
        const Loom::PaintReport painted = Loom::paintScene(Loom::prepareScene(tiny), list, 1180.0f, 760.0f, Loom::ViewState{});
        report.check("premalo tocaka se preskace", painted.drawnPoints == 0 && list.vertices.empty(), "tri tocke");
    }

    //-- 6. GOTOV REZULTAT se otvara iz mape: snimak kad ga ima, inace COLMAP ------------------------
    //
    //Stara mapa nema napredak.bin, a nova ima oba. Snimak mora imati prednost jer nosi boje; bez
    //njega se mora procitati COLMAP i kamere moraju doci na ista mjesta
    {
        namespace fs = std::filesystem;
        const fs::path root = fs::temp_directory_path() / "loom_rezultati_test";
        fs::remove_all(root);
        const fs::path directory = root / "snimka_loom";
        fs::create_directories(directory);
        fs::create_directories(root / "obicna_mapa");

        Engine::Reconstruction reconstruction;
        for(size_t i = 0; i < original.cameras.size(); ++i){
            Engine::Pose pose;
            pose.position = original.cameras[i];
            pose.orientation = original.orientations[i];
            reconstruction.poses.push_back(pose);
            reconstruction.posed.push_back(1);
        }
        reconstruction.points = original.points;
        reconstruction.solved.assign(original.points.size(), 1);
        //Svaka kamera vidi po neku tocku i ima ime kakvo solve daje: uvoz ih slaze PO IMENU
        std::vector<Engine::Observation> observations;
        std::vector<std::string> names;
        for(size_t i = 0; i < original.cameras.size(); ++i) names.push_back(fmt("kadar_%05zu.jpg", i));
        for(uint32_t point = 0; point < original.points.size(); ++point){
            for(uint32_t camera = point % 20; camera < original.cameras.size(); camera += 7){
                Engine::Observation observation;
                observation.camera = camera;
                observation.point = point;
                observations.push_back(observation);
            }
        }
        const bool wrote = Engine::writeColmapText(directory.string(), reconstruction, Engine::Intrinsics{}, observations, names);

        Loom::Snapshot fromColmap;
        const Loom::ResultSource first = Loom::loadResult(directory, fromColmap);
        float worst = 0.0f;
        for(size_t i = 0; i < std::min(fromColmap.cameras.size(), original.cameras.size()); ++i){
            worst = std::max(worst, glm::length(fromColmap.cameras[i] - original.cameras[i]));
        }
        report.check("stara mapa bez snimka se otvara iz COLMAP-a",
            wrote && first == Loom::ResultSource::Colmap &&
            fromColmap.cameras.size() == original.cameras.size() &&
            fromColmap.points.size() == original.points.size() && worst < 1e-3f,
            fmt("%zu kamera, %zu tocaka, kamera najdalje %.2e od prave", fromColmap.cameras.size(),
                fromColmap.points.size(), worst));

        //Snimak BEZ boja je zapisan usred solvea i gubi od gotovog modela
        Loom::Snapshot midway = original;
        midway.colours.clear();
        midway.points.resize(100);
        Loom::writeSnapshot((directory / "napredak.bin").string(), midway);
        Loom::Snapshot stale;
        const Loom::ResultSource staleSource = Loom::loadResult(directory, stale);
        report.check("snimak iz sredine solvea gubi od gotovog modela",
            staleSource == Loom::ResultSource::Colmap && stale.points.size() == original.points.size(),
            fmt("%zu tocaka procitano, snimak ih je imao 100", stale.points.size()));

        Loom::writeSnapshot((directory / "napredak.bin").string(), original);
        Loom::Snapshot fromSnapshot;
        const Loom::ResultSource second = Loom::loadResult(directory, fromSnapshot);
        report.check("konacan snimak ima prednost jer nosi boje",
            second == Loom::ResultSource::Snapshot && fromSnapshot.colours.size() == original.points.size(),
            fmt("%zu boja", fromSnapshot.colours.size()));

        const std::vector<fs::path> listed = Loom::resultsIn(root);
        Loom::Snapshot nothing;
        report.check("popis nudi samo mape s rezultatom",
            listed.size() == 1 && listed[0].filename() == "snimka_loom" &&
            Loom::loadResult(root / "obicna_mapa", nothing) == Loom::ResultSource::None,
            fmt("%zu ponudjeno", listed.size()));
        fs::remove_all(root);
    }

    std::filesystem::remove(path);
    return report.result();
}
