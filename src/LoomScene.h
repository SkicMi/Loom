#pragma once
//=============================================================================================
// REZULTAT SOLVEA ULAZI U SCENU.
//
// Mapa koju VideoSolve ostavi postaje grana stabla:
//
//   /<snimka>              grupa; NJEZINA TRANSFORMACIJA JE USPRAVNOST
//       Kamera             animirana, kljuc na svakom kadru iz kamera.usda
//       Tocke              oblak iz snimka ili COLMAP-a
//       Splat              kad je trening vec napravio scena.ply
//
// USPRAVNOST JE TRANSFORMACIJA GRUPE, ne preracun podataka. Djeca ostaju u koordinatama solvea,
// bit po bit iste kao na disku, a grupa ih zakrene i pomakne. Tako se vidi sto je napravljeno,
// umjetnik to moze popraviti (i skalirati scenu u metre) na jednom mjestu - bas kao "scene orient"
// u matchmove alatima - i kocka stavljena pod grupu ide sa scenom kad se orijentacija promijeni.
//
// KAMERA IZ USD-a, NE IZ COLMAP-a. COLMAP nosi samo kljucne kadrove (svaki n-ti); kamera.usda i
// medjukadrove. Vrijeme je vrijeme SNIMKE: timeCode t je kadar snimke t - 1 (vidi VideoSolve).
// Kad usda nema - stara mapa, ili tudji COLMAP - kljucni kadrovi idu redom od 1
//=============================================================================================
#include "LoomResult.h"

#include "Engine/ColmapImport.h"
#include "Engine/Upright.h"

#include <Warp/Stage.h>
#include <Warp/UsdCamera.h>

#include <glm/gtc/quaternion.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <string>

namespace Loom{

struct ImportReport{
    Warp::Id group = Warp::None;
    Warp::Id camera = Warp::None;
    Warp::Id points = Warp::None;
    Warp::Id splat = Warp::None;
    size_t cameraKeys = 0;
    bool fromUsd = false;               //kamera s kljucem na svakom kadru
    bool upright = false;
    float tiltDegrees = 0.0f;
    std::string problem;                //prazno kad je sve procitano
};

//Uspravnost i pod na nuli kao transformacija grupe. Vraca nagib koji je ispravljen, ili -1 kad
//kamere i scena ne odredjuju gore
inline float orientGroup(Warp::Stage& stage, Warp::Id group, const Snapshot& snapshot){
    if(snapshot.orientations.size() != snapshot.cameras.size() || snapshot.cameras.empty()) return -1.0f;
    std::vector<Engine::Pose> poses(snapshot.cameras.size());
    for(size_t i = 0; i < poses.size(); ++i){
        poses[i].position = snapshot.cameras[i];
        poses[i].orientation = snapshot.orientations[i];
    }
    const Engine::UprightFrame frame = Engine::uprightFrame(poses, {}, snapshot.points, {});
    if(!frame.applied) return -1.0f;

    //POD NA NULI. Ishodiste uspravnog sustava je medijan tocaka - sredina scene, u visini pola
    //zida. Kocka postavljena u ishodiste tada lebdi; zato se grupa spusti tako da donjih 5 %
    //tocaka lezi ispod y = 0, pa mreza i nova kocka stoje na podu snimke
    std::vector<float> heights;
    heights.reserve(snapshot.points.size());
    for(const glm::vec3& p : snapshot.points) heights.push_back((frame.rotation * (p - frame.origin)).y);
    float floor = 0.0f;
    if(!heights.empty()){
        std::nth_element(heights.begin(), heights.begin() + long(heights.size() / 20), heights.end());
        floor = heights[heights.size() / 20];
    }
    Warp::Entity& entity = *stage.get(group);
    entity.local.rotation = frame.rotation;
    entity.local.translation = -(frame.rotation * frame.origin) - glm::vec3(0.0f, floor, 0.0f);
    return frame.tiltDegrees;
}

//Snimak kao grana stabla: grupa, kamera s kljucem po kameri snimka (redom od 1) i tocke. Isto
//sluzi za zivi prikaz solvea koji jos tece i za mapu bez kamera.usda
inline ImportReport addSnapshot(Warp::Stage& stage, const Snapshot& snapshot, const std::string& name,
                                Warp::Id parent = Warp::None){
    ImportReport report;
    report.group = stage.create(name.empty() ? "Solve" : name, parent);
    report.camera = stage.create("Camera", report.group);
    Warp::Entity& camera = *stage.get(report.camera);
    camera.camera = Warp::Camera{};
    for(size_t i = 0; i < snapshot.cameras.size(); ++i){
        camera.translationKeys.set(double(i + 1), snapshot.cameras[i]);
        if(i < snapshot.orientations.size()) camera.rotationKeys.set(double(i + 1), snapshot.orientations[i]);
    }
    report.cameraKeys = camera.translationKeys.size();

    report.points = stage.create("Points", report.group);
    stage.get(report.points)->points = Warp::Points{snapshot.points, snapshot.colours};

    const float tilt = orientGroup(stage, report.group, snapshot);
    report.upright = tilt >= 0.0f;
    report.tiltDegrees = std::max(0.0f, tilt);
    return report;
}

//Pod zadanog roditelja stavlja rezultat iz mape. plate je snimka iz koje je rezultat nastao;
//smije biti prazna
inline ImportReport importResult(Warp::Stage& stage, const std::filesystem::path& directory,
                                 const std::string& plate = "", Warp::Id parent = Warp::None){
    Snapshot snapshot;
    if(loadResult(directory, snapshot) == ResultSource::None){
        ImportReport report;
        report.problem = "No solve result in folder: " + directory.string();
        return report;
    }

    std::string name = directory.filename().string();
    if(name.size() > 5 && name.substr(name.size() - 5) == "_loom") name.resize(name.size() - 5);
    ImportReport report = addSnapshot(stage, snapshot, name, parent);
    stage.startFrame = 1.0;
    stage.endFrame = double(std::max<size_t>(1, snapshot.cameras.size()));

    //Objektiv je u pikselima snimke, iz cameras.txt; bez njega se pretpostavlja 60 st
    Warp::Entity& camera = *stage.get(report.camera);
    Warp::Camera& lens = *camera.camera;
    {
        Engine::Intrinsics intrinsics;
        std::string model;
        if(Engine::readColmapCamera((directory / "cameras.txt").string(), intrinsics, model)){
            lens.focalPixels = intrinsics.fx;
            lens.centreX = intrinsics.cx;
            lens.centreY = intrinsics.cy;
            lens.width = intrinsics.width;
            lens.height = intrinsics.height;
        }else{
            lens.focalPixels = float(lens.width) / (2.0f * std::tan(glm::radians(30.0f)));
        }
    }
    lens.plate = plate;
    lens.plateFirstFrame = 0;
    //Objektiv snimke (VideoSolve ga zapise kad je leca zakrivljena): render ga koristi da CG zakrivi
    //isto kao snimku
    {
        std::ifstream lensFile(directory / "lens.txt");
        std::string word;
        while(lensFile >> word){
            if(word[0] == '#'){ std::string rest; std::getline(lensFile, rest); continue; }
            if(word == "radial") lensFile >> lens.distortionFx >> lens.distortionFy >> lens.distortionCx >> lens.distortionCy >> lens.k1 >> lens.k2;
        }
    }

    //Kamera s kljucem na SVAKOM kadru snimke, kad ju je VideoSolve zapisao
    Warp::UsdCamera usd;
    if(Warp::readUsdCamera((directory / "kamera.usda").string(), usd)){
        camera.translationKeys = {};
        camera.rotationKeys = {};
        for(size_t i = 0; i < usd.times.size(); ++i){
            const glm::mat4& m = usd.transforms[i];
            camera.translationKeys.set(usd.times[i], glm::vec3(m[3]));
            camera.rotationKeys.set(usd.times[i], glm::normalize(glm::quat_cast(glm::mat3(m))));
        }
        report.fromUsd = true;
        report.cameraKeys = camera.translationKeys.size();
        stage.startFrame = usd.startTimeCode;
        stage.endFrame = usd.endTimeCode;
        stage.framesPerSecond = usd.framesPerSecond;
    }

    std::error_code error;
    if(std::filesystem::is_regular_file(directory / "scena.ply", error)){
        report.splat = stage.create("Splat", report.group);
        stage.get(report.splat)->splat = Warp::Splat{(directory / "scena.ply").string()};
    }
    return report;
}

}
