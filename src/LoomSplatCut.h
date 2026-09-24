#pragma once
//=============================================================================================
// SPLAT U EDITORU: REZANJE KOCKOM I CISCENJE FLOATERA.
//
// Ono sto je SplatViewer imao kao zaseban preglednik (kocka za brisanje, spremanje), ovdje radi
// nad splatom u sceni:
//
//   REZNA KOCKA je obicna kocka iz scene - mice se, okrece i skalira gizmom i svojstvima, i ne
//   mora biti jednakih stranica. Dobije proziran zlatni materijal (vidi se sto je u njoj) i zicani
//   obrub dok je odabrana. Brise se ono unutar ili izvan nje, nad maskom u LoomSplat.h, s
//   korakom natrag; datoteka se ne dira dok se ne spremi (u <ime>_cut.ply, nikad preko izvora)
//
//   CISCENJE FLOATERA je tools/splat/clean_splats.py (izmjereno u floaters.py) kao posao u
//   pozadini. Treba kadrove iz kojih je splat treniran, pa radi samo za splat uz rezultat solvea
//   (images.txt, cameras.txt, points3D.txt u istoj mapi). Pise <ime>_clean.ply
//=============================================================================================
#include "LoomViewport.h"

#include <Warp/Stage.h>

#include <filesystem>
#include <string>

namespace Loom{

//Iz prostora splata u prostor jedinicne kocke (-0.5..0.5)
inline glm::mat4 cubeFromSplat(const Warp::Stage& stage, Warp::Id cube, Warp::Id splat, double frame){
    return glm::inverse(stage.worldMatrix(cube, frame)) * stage.worldMatrix(splat, frame);
}

inline bool isCube(const Warp::Entity* e){
    return e && e->mesh && e->mesh->shape == Warp::Shape::Cube;
}

//Kocka za rezanje: prozirna zlatna, na mjestu u koje se gleda, velicina iz scene
inline Warp::Id addCutBox(Warp::Stage& stage, const glm::vec3& centre, float size){
    int material = -1;
    for(size_t i = 0; i < stage.materials.size(); ++i) if(stage.materials[i].name == "Cut Box") material = int(i);
    if(material < 0){
        Warp::Material glass;
        glass.name = "Cut Box";
        glass.baseColor = glm::vec4(1.0f, 0.78f, 0.25f, 0.22f);
        glass.roughness = 0.8f;
        glass.alphaMode = Warp::Material::Alpha::Blend;
        glass.doubleSided = true;
        material = stage.addMaterial(glass);
    }
    const Warp::Id id = stage.create("Cut Box");
    Warp::Entity& box = *stage.get(id);
    box.mesh = Warp::Mesh{Warp::Shape::Cube, glm::vec3(1.0f), material};
    box.local.translation = centre;
    box.local.scale = glm::vec3(std::max(size, 1e-4f));
    return id;
}

//Dvanaest bridova kocke
inline void paintBoxWire(Treadle::DrawList& list, const ViewCamera& camera, const glm::mat4& world, const Treadle::Color& colour){
    glm::vec3 c[8];
    for(int i = 0; i < 8; ++i) c[i] = glm::vec3(world * glm::vec4(i & 1 ? 0.5f : -0.5f, i & 2 ? 0.5f : -0.5f, i & 4 ? 0.5f : -0.5f, 1.0f));
    for(int i = 0; i < 8; ++i){
        for(int bit : {1, 2, 4}) if(!(i & bit)) segment(list, camera, c[i], c[i | bit], 1.5f, colour);
    }
}

//Kamo ide izrezani splat: <ime>_cut.ply uz izvor. Kad je izvor vec izrezan, pise se u njega
//- to je nas izlaz, a ne izvorna datoteka treninga
inline std::string cutOutputPath(const std::string& source){
    const std::filesystem::path p(source);
    const std::string stem = p.stem().string();
    if(stem.size() > 4 && stem.substr(stem.size() - 4) == "_cut") return source;
    return (p.parent_path() / (stem + "_cut.ply")).string();
}

//Ima li splat uz sebe kadrove iz kojih je treniran (bez njih se floateri ne daju izmjeriti)
inline bool canCleanFloaters(const std::string& splat){
    const std::filesystem::path dir = std::filesystem::path(splat).parent_path();
    std::error_code e;
    return std::filesystem::is_regular_file(dir / "images.txt", e) && std::filesystem::is_regular_file(dir / "cameras.txt", e) &&
           std::filesystem::is_regular_file(dir / "points3D.txt", e) && std::filesystem::is_directory(dir / "images", e);
}

//Naredba za ciscenje i kamo pise. Venv se aktivira (gsplat trazi ninju u PATH-u, vidi startTrain)
inline std::string cleanFloatersCommand(const std::string& root, const std::string& splat, std::string& output){
    const std::filesystem::path p(splat);
    std::string stem = p.stem().string();
    if(stem.size() > 6 && stem.substr(stem.size() - 6) == "_clean") output = splat;
    else output = (p.parent_path() / (stem + "_clean.ply")).string();
    return "cd \"" + root + "\" && PATH=\"" + root + "/.venv/bin:$PATH\" ./.venv/bin/python tools/splat/clean_splats.py \"" +
           p.parent_path().string() + "\" \"" + splat + "\" \"" + output + "\"";
}

}
