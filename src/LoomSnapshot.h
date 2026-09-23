#pragma once
//=============================================================================================
// Snimak onoga sto je solver dosad nasao: format na disku.
//
// ZASTO ODVOJENO OD CRTANJA. Isti ovaj kod PISE snimak u VideoSolveu i CITA ga u loom-u. Dok su
// pisac i citac bili dva komada koda, mogli su se tiho razici - promijeni se redoslijed polja na
// jednoj strani i scena postane smece, a nijedan test ne padne. Ovdje nema nicega od grafike, pa
// ga moze ukljuciti i program koji ne crta.
//
// DVIJE VERZIJE:
//
//   LOOMPRG1   kamere kao polozaji, tocke kao polozaji. Pisale su ga starije verzije VideoSolvea
//   LOOMPRG2   kamere nose i ORIJENTACIJU, tocke po zelji i BOJU
//
// Orijentacija je ono sto je prikazu falilo: bez nje se ne zna kamo kamera gleda ni gdje je
// "gore", pa se scena crtala u sustavu prve kamere - nagnuta koliko god je prva kamera bila
// nagnuta. Boja postoji tek na kraju solvea (trazi slike), pa tijekom rasta ne ide.
//
// ZAPIS JE ATOMSKI: prvo .tmp pa preimenovanje, jer citatelj gleda isti fajl i ne smije uhvatiti
// polovicu
//=============================================================================================
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace Loom{

struct Snapshot{
    std::vector<glm::vec3> cameras;           //polozaji posavljenih kamera, redom kojim idu u snimci
    std::vector<glm::quat> orientations;      //prazno kod v1; inace jedna po kameri, kamera -> svijet
    std::vector<glm::vec3> points;
    std::vector<glm::u8vec3> colours;         //prazno kad boje jos nema; inace jedna po tocki
};

inline bool writeSnapshot(const std::string& path, const Snapshot& snapshot){
    const std::string temporary = path + ".tmp";
    {
        std::ofstream file(temporary, std::ios::binary);
        if(!file) return false;

        const uint32_t cameraCount = uint32_t(snapshot.cameras.size());
        const uint32_t pointCount = uint32_t(snapshot.points.size());
        const bool haveOrientations = snapshot.orientations.size() == snapshot.cameras.size();
        const bool haveColours = !snapshot.colours.empty() && snapshot.colours.size() == snapshot.points.size();
        const uint32_t flags = (haveOrientations ? 1u : 0u) | (haveColours ? 2u : 0u);

        file.write("LOOMPRG2", 8);
        file.write(reinterpret_cast<const char*>(&cameraCount), 4);
        file.write(reinterpret_cast<const char*>(&pointCount), 4);
        file.write(reinterpret_cast<const char*>(&flags), 4);
        for(uint32_t i = 0; i < cameraCount; ++i){
            file.write(reinterpret_cast<const char*>(&snapshot.cameras[i]), 12);
            //Kvaternion kao w, x, y, z - redoslijed je zapisan, ne prepusten rasporedu u memoriji
            const glm::quat q = haveOrientations ? snapshot.orientations[i] : glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
            const float wxyz[4] = {q.w, q.x, q.y, q.z};
            file.write(reinterpret_cast<const char*>(wxyz), 16);
        }
        if(pointCount) file.write(reinterpret_cast<const char*>(snapshot.points.data()), std::streamsize(pointCount) * 12);
        if(haveColours) file.write(reinterpret_cast<const char*>(snapshot.colours.data()), std::streamsize(pointCount) * 3);
        if(!file) return false;
    }
    std::error_code error;
    std::filesystem::rename(temporary, path, error);
    return !error;
}

inline bool readSnapshot(const std::string& path, Snapshot& out){
    std::ifstream file(path, std::ios::binary);
    if(!file) return false;

    char magic[8] = {0};
    file.read(magic, 8);
    const bool v1 = std::memcmp(magic, "LOOMPRG1", 8) == 0;
    const bool v2 = std::memcmp(magic, "LOOMPRG2", 8) == 0;
    if(!v1 && !v2) return false;

    uint32_t cameraCount = 0, pointCount = 0, flags = 0;
    file.read(reinterpret_cast<char*>(&cameraCount), 4);
    file.read(reinterpret_cast<char*>(&pointCount), 4);
    if(v2) file.read(reinterpret_cast<char*>(&flags), 4);
    if(!file || cameraCount > 100000 || pointCount > 20000000) return false;

    out = Snapshot{};
    out.cameras.resize(cameraCount);
    if(v1){
        if(cameraCount) file.read(reinterpret_cast<char*>(out.cameras.data()), std::streamsize(cameraCount) * 12);
    }else{
        if(flags & 1u) out.orientations.resize(cameraCount);
        for(uint32_t i = 0; i < cameraCount; ++i){
            file.read(reinterpret_cast<char*>(&out.cameras[i]), 12);
            float wxyz[4];
            file.read(reinterpret_cast<char*>(wxyz), 16);
            if(flags & 1u) out.orientations[i] = glm::quat(wxyz[0], wxyz[1], wxyz[2], wxyz[3]);
        }
    }
    out.points.resize(pointCount);
    if(pointCount) file.read(reinterpret_cast<char*>(out.points.data()), std::streamsize(pointCount) * 12);
    if(v2 && (flags & 2u)){
        out.colours.resize(pointCount);
        file.read(reinterpret_cast<char*>(out.colours.data()), std::streamsize(pointCount) * 3);
    }
    return bool(file);
}

}
