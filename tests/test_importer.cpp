// Importer editora (LoomImporter.h): sto je koja datoteka, i materijal iz tekstura.
//
// ZASTO SE OVO TESTIRA. Materijal se slaze po IMENIMA slika, a imena dolaze iz tudjih biblioteka
// (Substance, Poly Haven, ambientCG, Quixel) - "brick_Normal_GL", "rock_rough", "metal_ARM". Kad
// bi "normal" u "abnormal" bio normala ili "col" u "colour_ramp" boja, materijal bi tiho dobio
// krivu mapu i izgledao bi samo cudno. Hrapavost, metalnost i AO u glTF-u dijele JEDNU sliku
// (R = AO, G = hrapavost, B = metalnost), pa se provjerava i da je spojena tocno tim redom.
#include "TestHarness.h"

#include "../src/LoomImporter.h"

#include <Spool/ImageFile.h>

#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

namespace{

void grey(const fs::path& path, uint8_t value, uint32_t size = 8){
    std::vector<uint8_t> pixels(size_t(size) * size * 4, value);
    for(size_t i = 3; i < pixels.size(); i += 4) pixels[i] = 255;
    Spool::savePng(path.string(), Spool::imageFromPixels(pixels.data(), size, size));
}

}

int main(){
    TestReport report("importer: vrste datoteka i materijal iz tekstura");
    using Loom::TextureRole;

    //-- uloge po imenu ----------------------------------------------------------------------
    struct Case{ const char* name; TextureRole role; };
    const Case cases[] = {
        {"brick_wall_basecolor.png", TextureRole::BaseColor}, {"Rock_Albedo.jpg", TextureRole::BaseColor},
        {"wood_diff_2k.png", TextureRole::BaseColor}, {"brick_Normal_GL.png", TextureRole::Normal},
        {"rock_nor_gl_2k.png", TextureRole::Normal}, {"metal_rough.png", TextureRole::Roughness},
        {"Metal012_Roughness.png", TextureRole::Roughness}, {"Metal012_Metalness.png", TextureRole::Metallic},
        {"brick_ao.png", TextureRole::Occlusion}, {"lamp_emissive.png", TextureRole::Emissive},
        {"rock_arm_4k.png", TextureRole::Packed}, {"abnormal_wall.png", TextureRole::Unknown},
        {"photo.jpg", TextureRole::Unknown},
    };
    int wrong = 0;
    std::string first;
    for(const Case& c : cases){
        if(Loom::textureRole(c.name) != c.role){
            ++wrong;
            if(first.empty()) first = c.name;
        }
    }
    report.check("uloga teksture po imenu", wrong == 0,
                 wrong ? fmt("%d krivih, prvi %s", wrong, first.c_str()) : fmt("%zu imena", sizeof(cases) / sizeof(cases[0])));

    //-- materijal iz mape tekstura ------------------------------------------------------------
    const fs::path directory = fs::temp_directory_path() / "loom_test_importer";
    fs::remove_all(directory);
    fs::create_directories(directory);
    grey(directory / "brick_wall_basecolor.png", 200);
    grey(directory / "brick_wall_normal_gl.png", 128);
    grey(directory / "brick_wall_roughness.png", 170);
    grey(directory / "brick_wall_metallic.png", 30);
    grey(directory / "brick_wall_ao.png", 90);

    Warp::Stage stage;
    const Loom::MaterialImportReport made = Loom::materialFromTextures(stage, Loom::imageFilesIn(directory));
    const bool haveMaterial = made.material == 0 && stage.materials.size() == 1;
    report.check("materijal nastaje, ime iz zajednickog dijela", haveMaterial && made.name == "brick_wall",
                 fmt("%zu materijala, ime '%s', %zu mapa", stage.materials.size(), made.name.c_str(), made.assigned.size()));
    if(haveMaterial){
        const Warp::Material& material = stage.materials[0];
        const bool slots = fs::path(material.baseColorMap.source).filename() == "brick_wall_basecolor.png" &&
                           fs::path(material.normalMap.source).filename() == "brick_wall_normal_gl.png" &&
                           fs::path(material.metallicRoughnessMap.source).filename() == "brick_wall_orm.png" &&
                           material.occlusionMap.source == material.metallicRoughnessMap.source;
        report.check("svaka slika u svoje mjesto, ORM za hrapavost/metalnost/AO", slots, made.packed);

        bool order = false;
        if(fs::exists(directory / "brick_wall_orm.png")){
            const Spool::Image orm = Spool::loadImage((directory / "brick_wall_orm.png").string());
            order = orm.pixels.size() >= 4 && orm.pixels[0] == 90 && orm.pixels[1] == 170 && orm.pixels[2] == 30;
            report.check("ORM je R = AO, G = hrapavost, B = metalnost", order,
                         orm.pixels.size() >= 4 ? fmt("%u %u %u", orm.pixels[0], orm.pixels[1], orm.pixels[2]) : "nema slike");
        }else{
            report.check("ORM je R = AO, G = hrapavost, B = metalnost", false, "nije zapisana");
        }
    }

    //Jedna fotografija bez uloge je materijal s tom slikom kao bojom
    Warp::Stage single;
    grey(directory / "photo.png", 60);
    const Loom::MaterialImportReport photo = Loom::materialFromTextures(single, {directory / "photo.png"});
    report.check("jedna slika bez uloge ide u boju", photo.material == 0 &&
                 fs::path(single.materials[0].baseColorMap.source).filename() == "photo.png" && photo.name == "photo",
                 fmt("ime '%s'", photo.name.c_str()));

    //Materijal na kocku, i nista na entitet bez tijela
    const Warp::Id cube = stage.create("Cube");
    stage.get(cube)->mesh = Warp::Mesh{Warp::Shape::Cube};
    const Warp::Id empty = stage.create("Empty");
    report.check("materijal na kocku da, na prazni ne", Loom::assignMaterial(stage, cube, 0) &&
                 stage.get(cube)->mesh->material == 0 && !Loom::assignMaterial(stage, empty, 0), "");

    //-- vrste u mapi ------------------------------------------------------------------------
    fs::create_directories(directory / "subfolder");
    std::ofstream(directory / "notes.txt") << "not importable";
    const std::vector<Loom::ImportEntry> entries = Loom::importEntriesIn(directory);
    size_t folders = 0, images = 0, other = 0;
    for(const Loom::ImportEntry& entry : entries){
        if(entry.kind == Loom::ImportKind::Folder) ++folders;
        else if(entry.kind == Loom::ImportKind::Image) ++images;
        else ++other;
    }
    report.check("mapa: podmapa, slike, a tekst ne", folders == 1 && images == 7 && other == 0 &&
                 !entries.empty() && entries.front().kind == Loom::ImportKind::Folder,
                 fmt("%zu mapa, %zu slika, %zu drugo", folders, images, other));

    //-- kamera iz pogleda ---------------------------------------------------------------------
    Warp::Stage cameras;
    glm::mat4 world(1.0f);
    world[3] = glm::vec4(1.0f, 2.0f, 3.0f, 1.0f);
    const Warp::Id camera = Loom::addCameraFromView(cameras, world, 540.0f, 540.0f, 1.0);
    const Warp::Entity* entity = cameras.get(camera);
    report.check("kamera iz pogleda: polozaj i zarisna za 1080 redaka",
                 entity && entity->camera && std::fabs(entity->camera->focalPixels - 1080.0f) < 1e-3f &&
                 glm::length(entity->local.translation - glm::vec3(1, 2, 3)) < 1e-5f,
                 entity && entity->camera ? fmt("f %.1f px", double(entity->camera->focalPixels)) : "nema kamere");

    fs::remove_all(directory);
    return report.result();
}
