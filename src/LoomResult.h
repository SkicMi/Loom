#pragma once
//=============================================================================================
// GOTOV REZULTAT s diska: mapa koju je solve ostavio, otvorena bez ponovnog solvea.
//
// ZASTO. Do sada se scena mogla vidjeti samo DOK solve tece - zatvori se prozor i jedini put do nje
// bio je solvati snimku ispocetka, sto traje minutama. A mapa na disku ima sve.
//
// ODAKLE SE CITA, tim redom:
//
//   napredak.bin    zadnji snimak koji je solver zapisao: kamere s orijentacijom, tocke, boje
//   COLMAP tekst    cameras.txt, images.txt, points3D.txt - ima ga svaka mapa, i ona iz vremena
//                   prije snimka, i ona koju je napravio netko drugi (COLMAP sam)
//
// Snimak ima prednost SAMO KAD JE KONACAN - a konacan je onaj s bojama, jer boje VideoSolve dodaje
// tek na kraju. Snimak bez boja je zapisan usred solvea: na snimci joysticka nosio je 115 kamera,
// a gotov COLMAP model 120. Takav se uzima tek kad COLMAP-a nema (solve je prekinut).
//
// USPRAVNOST SE NE RJESAVA OVDJE. Stare mape su zapisane u sustavu prve kamere, nove uspravno -
// prikaz svejedno racuna uspravni sustav sam (prepareScene), pa izgledaju isto
//=============================================================================================
#include "LoomSnapshot.h"

#include "Engine/ColmapImport.h"

#include <algorithm>
#include <filesystem>
#include <string>
#include <vector>

namespace Loom{

//Je li mapa rezultat solvea: ima snimak ili cijeli COLMAP model
inline bool isResultDirectory(const std::filesystem::path& directory){
    std::error_code error;
    if(std::filesystem::is_regular_file(directory / "napredak.bin", error)) return true;
    return std::filesystem::is_regular_file(directory / "cameras.txt", error) &&
           std::filesystem::is_regular_file(directory / "images.txt", error) &&
           std::filesystem::is_regular_file(directory / "points3D.txt", error);
}

//Podmape zadane mape koje su rezultati, po imenu
inline std::vector<std::filesystem::path> resultsIn(const std::filesystem::path& directory){
    std::vector<std::filesystem::path> found;
    std::error_code error;
    for(const auto& entry : std::filesystem::directory_iterator(directory, error)){
        if(error) break;
        if(entry.is_directory(error) && isResultDirectory(entry.path())) found.push_back(entry.path());
    }
    std::sort(found.begin(), found.end());
    return found;
}

enum class ResultSource{ None, Snapshot, Colmap };

//Cita rezultat u snimak. Vraca odakle je procitan; None kad mapa nema nista citljivo
inline ResultSource loadResult(const std::filesystem::path& directory, Snapshot& out){
    Snapshot snapshot;
    const bool haveSnapshot = readSnapshot((directory / "napredak.bin").string(), snapshot) && !snapshot.points.empty();
    const bool final = haveSnapshot && snapshot.colours.size() == snapshot.points.size();
    if(final){ out = std::move(snapshot); return ResultSource::Snapshot; }

    Engine::ColmapModel model;
    if(!Engine::readColmapText(directory.string(), model)){
        if(!haveSnapshot) return ResultSource::None;
        out = std::move(snapshot);
        return ResultSource::Snapshot;
    }

    const Engine::Reconstruction& r = model.reconstruction;
    out = Snapshot{};
    for(size_t camera = 0; camera < r.poses.size(); ++camera){
        if(camera < r.posed.size() && !r.posed[camera]) continue;
        out.cameras.push_back(r.poses[camera].position);
        out.orientations.push_back(r.poses[camera].orientation);
    }
    for(size_t point = 0; point < r.points.size(); ++point){
        if(point < r.solved.size() && !r.solved[point]) continue;
        out.points.push_back(r.points[point]);
    }
    return out.points.empty() ? ResultSource::None : ResultSource::Colmap;
}

}
