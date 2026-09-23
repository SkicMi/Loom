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

#include <filesystem>
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

//Pod zadanog roditelja stavlja rezultat iz mape. plate je snimka iz koje je rezultat nastao;
//smije biti prazna
inline ImportReport importResult(Warp::Stage& stage, const std::filesystem::path& directory,
                                 const std::string& plate = "", Warp::Id parent = Warp::None){
    ImportReport report;

    Snapshot snapshot;
    if(loadResult(directory, snapshot) == ResultSource::None){
        report.problem = "u mapi nema rezultata: " + directory.string();
        return report;
    }

    //Objektiv je u pikselima snimke, iz cameras.txt; bez njega se pretpostavlja 60 st
    Warp::Camera lens;
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

    std::string name = directory.filename().string();
    if(name.size() > 5 && name.substr(name.size() - 5) == "_loom") name.resize(name.size() - 5);
    report.group = stage.create(name.empty() ? "Solve" : name, parent);

    report.camera = stage.create("Kamera", report.group);
    Warp::Entity& camera = *stage.get(report.camera);
    camera.camera = lens;

    Warp::UsdCamera usd;
    if(Warp::readUsdCamera((directory / "kamera.usda").string(), usd)){
        for(size_t i = 0; i < usd.times.size(); ++i){
            const glm::mat4& m = usd.transforms[i];
            camera.translationKeys.set(usd.times[i], glm::vec3(m[3]));
            camera.rotationKeys.set(usd.times[i], glm::normalize(glm::quat_cast(glm::mat3(m))));
        }
        report.fromUsd = true;
        stage.startFrame = usd.startTimeCode;
        stage.endFrame = usd.endTimeCode;
        stage.framesPerSecond = usd.framesPerSecond;
    }else{
        for(size_t i = 0; i < snapshot.cameras.size(); ++i){
            camera.translationKeys.set(double(i + 1), snapshot.cameras[i]);
            if(i < snapshot.orientations.size()) camera.rotationKeys.set(double(i + 1), snapshot.orientations[i]);
        }
        stage.startFrame = 1.0;
        stage.endFrame = double(std::max<size_t>(1, snapshot.cameras.size()));
    }
    report.cameraKeys = camera.translationKeys.size();

    report.points = stage.create("Tocke", report.group);
    Warp::Entity& cloud = *stage.get(report.points);
    cloud.points = Warp::Points{snapshot.points, snapshot.colours};

    std::error_code error;
    if(std::filesystem::is_regular_file(directory / "scena.ply", error)){
        report.splat = stage.create("Splat", report.group);
        stage.get(report.splat)->splat = Warp::Splat{(directory / "scena.ply").string()};
    }

    //USPRAVNOST iz istih poza i tocaka koje se crtaju. Nova mapa je vec uspravna pa je nagib
    //nula, ali ishodiste i smjer "naprijed" se i dalje poravnaju
    if(!snapshot.orientations.empty()){
        std::vector<Engine::Pose> poses(snapshot.cameras.size());
        for(size_t i = 0; i < poses.size(); ++i){
            poses[i].position = snapshot.cameras[i];
            poses[i].orientation = snapshot.orientations[i];
        }
        const Engine::UprightFrame frame = Engine::uprightFrame(poses, {}, snapshot.points, {});
        if(frame.applied){
            Warp::Entity& group = *stage.get(report.group);
            group.local.rotation = frame.rotation;
            group.local.translation = -(frame.rotation * frame.origin);
            report.upright = true;
            report.tiltDegrees = frame.tiltDegrees;
        }
    }
    return report;
}

}
