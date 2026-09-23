// Projekt: scena spremljena u .usda i otvorena natrag mora biti ISTA scena.
//
// ZASTO SE OVO TESTIRA. Spremanje koje nesto tiho izgubi ne rusi nista: kocka dodje natrag
// milimetar pomaknuta, kamera izgubi pomak glavne tocke, boje oblaka se pomaknu za jednu
// nijansu. Svako od toga se vidi tek tjednima kasnije, kad matchmove vise ne stoji - pa se trazi
// u solveru. Zato se usporedjuje SVE, i to bit po bit gdje god format to dopusta.
//
// NEGATIVNE KONTROLE:
//   - odrezana datoteka se odbija s retkom, a scena u editoru ostaje netaknuta
//   - USD koji nije pisao Loom (Engineov kamera.usda, prim s metapodacima, rel, komentari) se
//     procita bez pada - projekt koji je netko otvorio i spremio u Houdiniju ne smije srusiti loom
#include "TestHarness.h"

#include <Warp/Project.h>
#include <Warp/Usda.h>

#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>

namespace{

bool sameTrack(const Warp::Track<glm::vec3>& a, const Warp::Track<glm::vec3>& b){
    return a.times == b.times && a.values == b.values;
}

bool sameTrack(const Warp::Track<glm::quat>& a, const Warp::Track<glm::quat>& b){
    return a.times == b.times && a.values == b.values;
}

}

int main(){
    TestReport report("W3 projekt");
    namespace fs = std::filesystem;
    const fs::path directory = fs::temp_directory_path() / "loom_projekt_test";
    fs::remove_all(directory);
    fs::create_directories(directory);
    const std::string path = (directory / "projekt.usda").string();

    //-- scena sa svime sto editor zna --------------------------------------------------------
    Warp::Stage original;
    original.startFrame = 12.0;
    original.endFrame = 2301.0;
    original.framesPerSecond = 50.0;
    const Warp::Id group = original.create("C0257");
    original.get(group)->local.translation = glm::vec3(0.123456789f, -3.3f, 1e-4f);
    original.get(group)->local.rotation = glm::normalize(glm::quat(0.9f, 0.1f, -0.3f, 0.2f));
    original.get(group)->local.scale = glm::vec3(2.5f);

    const Warp::Id camera = original.create("Kamera", group);
    Warp::Camera lens;
    lens.focalPixels = 3153.05566f;
    lens.width = 2160; lens.height = 3840;
    lens.centreX = 1081.25f; lens.centreY = 1919.5f;
    lens.plate = "/home/netko/snimke/C0257 \"prva\".MP4";
    lens.plateFirstFrame = 3;
    original.get(camera)->camera = lens;
    for(int f = 1; f <= 400; ++f){
        const float t = float(f) * 0.013f;
        original.get(camera)->translationKeys.set(f, glm::vec3(std::sin(t) * 3.1f, 0.2f * t, std::cos(t) * 1e-3f));
        original.get(camera)->rotationKeys.set(f, glm::normalize(glm::angleAxis(t, glm::normalize(glm::vec3(0.1f, 1.0f, 0.3f)))));
    }

    const Warp::Id cloud = original.create("Tocke", group);
    Warp::Points points;
    for(int i = 0; i < 5000; ++i){
        points.positions.push_back(glm::vec3(std::sin(i * 0.37f) * 12.345f, std::cos(i * 0.11f) * 1e-3f, float(i) * 1e-5f));
        points.colours.push_back(glm::u8vec3(uint8_t(i % 256), uint8_t((i * 7) % 256), uint8_t((i * 13) % 256)));
    }
    original.get(cloud)->points = points;
    const Warp::Id splat = original.create("Splat", group);
    original.get(splat)->splat = Warp::Splat{"/tmp/scena.ply"};

    const Warp::Id cube = original.create("Kocka");
    original.get(cube)->mesh = Warp::Mesh{Warp::Shape::Cube, glm::vec3(0.2f, 0.4f, 0.9f)};
    original.get(cube)->local.scale = glm::vec3(0.3f, 0.3f, 0.7f);
    original.get(cube)->scaleKeys.set(100.5, glm::vec3(1.0f, 2.0f, 3.0f));
    original.get(cube)->scaleKeys.set(200.0, glm::vec3(0.5f));
    const Warp::Id plane = original.create("Ravnina", cube);
    original.get(plane)->mesh = Warp::Mesh{Warp::Shape::Plane};
    original.get(plane)->visible = false;
    original.create("Nul");

    Warp::Media media;
    media.path = "/home/netko/snimke/C0257.MP4";
    media.result = "/home/netko/snimke/C0257_loom";
    media.frames = 2304; media.framesPerSecond = 50.0; media.width = 2160; media.height = 3840;
    original.media.push_back(media);

    //-- 1. spremi i otvori ----------------------------------------------------------------------
    std::string error;
    const bool saved = Warp::saveProject(original, path, error);
    Warp::Stage loaded;
    const bool opened = saved && Warp::loadProject(path, loaded, error);
    report.check("projekt se sprema i otvara", saved && opened && Warp::isProjectFile(path),
        error.empty() ? fmt("%.1f MB", double(fs::file_size(path)) / 1e6) : error);

    //-- 2. stablo i imena ----------------------------------------------------------------------
    std::string before, after;
    original.walk([&](const Warp::Entity& e, int d){ before += std::to_string(d) + e.name + " "; });
    loaded.walk([&](const Warp::Entity& e, int d){ after += std::to_string(d) + e.name + " "; });
    report.check("stablo je isto, istim redom", before == after, after);

    //-- 3. transformacije i kljucevi, bit po bit ------------------------------------------------
    {
        bool same = true;
        original.walk([&](const Warp::Entity& a, int){
            const Warp::Entity* b = loaded.get(loaded.find(original.path(a.id)));
            if(!b){ same = false; return; }
            if(a.local.translation != b->local.translation || a.local.scale != b->local.scale ||
               a.local.rotation != b->local.rotation) same = false;
            if(!sameTrack(a.translationKeys, b->translationKeys) || !sameTrack(a.rotationKeys, b->rotationKeys) ||
               !sameTrack(a.scaleKeys, b->scaleKeys)) same = false;
            if(a.visible != b->visible) same = false;
        });
        const glm::mat4 worldA = original.worldMatrix(camera, 123.4);
        const glm::mat4 worldB = loaded.worldMatrix(loaded.find("/C0257/Kamera"), 123.4);
        float drift = 0.0f;
        for(int c = 0; c < 4; ++c) for(int r = 0; r < 4; ++r) drift = std::max(drift, std::fabs(worldA[c][r] - worldB[c][r]));
        report.check("transformacije i kljucevi bit po bit, vidljivost", same && drift == 0.0f,
            fmt("kamera u kadru 123.4 odstupa %.1e", drift));
    }

    //-- 4. komponente ---------------------------------------------------------------------------
    {
        const Warp::Entity* c = loaded.get(loaded.find("/C0257/Kamera"));
        const bool lensSame = c && c->camera && c->camera->focalPixels == lens.focalPixels && c->camera->width == 2160 &&
                              c->camera->height == 3840 && c->camera->centreX == lens.centreX &&
                              c->camera->centreY == lens.centreY && c->camera->plate == lens.plate &&
                              c->camera->plateFirstFrame == 3;
        report.check("objektiv u pikselima, pomak glavne tocke i ploca", lensSame,
            c && c->camera ? fmt("zarisna %.5f, sredina (%.2f, %.2f), ploca %s", c->camera->focalPixels,
                                 c->camera->centreX, c->camera->centreY, c->camera->plate.c_str()) : "nema kamere");

        const Warp::Entity* p = loaded.get(loaded.find("/C0257/Tocke"));
        const bool pointsSame = p && p->points && p->points->positions == points.positions && p->points->colours == points.colours;
        report.check("oblak: polozaji i boje bit po bit", pointsSame,
            p && p->points ? fmt("%zu tocaka, %zu boja", p->points->positions.size(), p->points->colours.size()) : "nema");

        const Warp::Entity* k = loaded.get(loaded.find("/Kocka"));
        const Warp::Entity* r = loaded.get(loaded.find("/Kocka/Ravnina"));
        const Warp::Entity* s = loaded.get(loaded.find("/C0257/Splat"));
        const Warp::Entity* n = loaded.get(loaded.find("/Nul"));
        report.check("kocka, ravnina, splat i nul",
            k && k->mesh && k->mesh->shape == Warp::Shape::Cube && k->mesh->colour == glm::vec3(0.2f, 0.4f, 0.9f) &&
            r && r->mesh && r->mesh->shape == Warp::Shape::Plane && s && s->splat && s->splat->path == "/tmp/scena.ply" &&
            n && !n->mesh && !n->camera && !n->points,
            "sve cetiri vrste");

        report.check("snimke projekta i timeline",
            loaded.media.size() == 1 && loaded.media[0].path == media.path && loaded.media[0].result == media.result &&
            loaded.media[0].frames == 2304 && loaded.media[0].width == 2160 && loaded.startFrame == 12.0 &&
            loaded.endFrame == 2301.0 && loaded.framesPerSecond == 50.0 && loaded.find("/LoomMedia") == Warp::None,
            fmt("%zu snimka, kadrovi %.0f-%.0f @ %.0f", loaded.media.size(), loaded.startFrame, loaded.endFrame,
                loaded.framesPerSecond));
    }

    //-- NEGATIVNA KONTROLA: odrezana datoteka --------------------------------------------------
    {
        std::ifstream in(path);
        std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        const std::string cut = (directory / "odrezano.usda").string();
        std::ofstream(cut) << text.substr(0, text.size() / 3);
        Warp::Stage untouched;
        untouched.create("Ostaje");
        std::string problem;
        const bool refused = !Warp::loadProject(cut, untouched, problem);
        report.check("odrezana datoteka se odbija, scena ostaje", refused && untouched.find("/Ostaje") != Warp::None &&
                                                                   problem.find("redak") != std::string::npos,
            problem.substr(0, 90));
    }

    //-- tudji USD se cita bez pada -------------------------------------------------------------
    {
        const std::string foreign =
            "#usda 1.0\n(\n    \"napisao netko drugi\"\n    defaultPrim = \"Svijet\"\n    doc = \"opis\"\n)\n\n"
            "def Xform \"Svijet\" (\n    kind = \"assembly\"\n    prepend apiSchemas = [\"GeomModelAPI\"]\n)\n{\n"
            "    # komentar usred prima\n"
            "    rel material:binding = </Materijali/Siva>\n"
            "    matrix4d xformOp:transform.timeSamples = {\n"
            "        1: ( (1, 0, 0, 0), (0, 1, 0, 0), (0, 0, 1, 0), (0.5, -2, 3e-2, 1) ),\n"
            "        2: ( (1, 0, 0, 0), (0, 1, 0, 0), (0, 0, 1, 0), (0.6, -2, 3e-2, 1) ),\n"
            "    }\n"
            "    def Cube \"Kutija\"\n    {\n        double size = 2\n        float3[] extent = [(-1, -1, -1), (1, 1, 1)]\n    }\n}\n";
        const std::string foreignPath = (directory / "tudji.usda").string();
        std::ofstream(foreignPath) << foreign;
        Warp::Stage stage;
        std::string problem;
        const bool read = Warp::loadProject(foreignPath, stage, problem);
        const Warp::Entity* box = stage.get(stage.find("/Svijet/Kutija"));
        report.check("USD koji nije pisao Loom se procita bez pada", read && box && box->mesh && !Warp::isProjectFile(foreignPath),
            read ? fmt("%zu entiteta", stage.size()) : problem);
    }

    fs::remove_all(directory);
    return report.result();
}
