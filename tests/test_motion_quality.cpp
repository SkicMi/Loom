// MotionQuality: ocjena pokreta mora naci ubrizganu gresku i izabrati cisti klip.
//
// Sinteticki hod s POZNATIM odgovorom: kukovi idu naprijed 1 m/s, prsti se izmjenjuju - dok jedan
// stoji na mjestu (2 cm iznad poda, kao u Kimodu), drugi se podigne i prenese. Prsti su izravna
// djeca Roota s kanalima polozaja, pa je njihov svjetski polozaj upravo ono sto test zada; FK kroz
// noge ovdje nije predmet provjere.
//
// Svaka greska se ubrizga zasebno i mora (1) podici svoju mjeru, (2) biti prijavljena kao najgora,
// (3) izgubiti od cistog klipa u bestOf. Negativna kontrola: cisti klip ima sve mjere ispod praga.
#include "TestHarness.h"

#include <Engine/MotionQuality.h>
#include <Engine/WeaverMotion.h>

#include <cmath>
#include <string>
#include <vector>

namespace{

using Engine::WeaverMotion::Clip;

enum class Defect{ None, Skate, Float, Sink, Teleport, Jitter };

Clip walk(Defect defect){
    Clip clip;
    clip.framesPerSecond = 30.0;
    clip.joints = {
        {"Root", -1, glm::vec3(0.0f)},
        {"Hips", 0, glm::vec3(0.0f, 1.0f, 0.0f)},
        {"Spine1", 1, glm::vec3(0.0f, 0.1f, 0.0f)},
        {"LeftToeBase", 0, glm::vec3(0.1f, 0.02f, 0.0f)},
        {"RightToeBase", 0, glm::vec3(-0.1f, 0.02f, 0.0f)},
    };
    const int frames = 120;
    const float speed = 1.0f;               //m/s
    const float cycle = 1.0f;               //s, jedan korak svake noge
    for(int f = 0; f < frames; ++f){
        const float t = float(f) / 30.0f;
        Engine::WeaverMotion::Pose pose;
        pose.translations.assign(clip.joints.size(), glm::vec3(0.0f));
        pose.rotations.assign(clip.joints.size(), glm::quat(1.0f, 0.0f, 0.0f, 0.0f));
        pose.translations[0] = glm::vec3(0.0f);
        pose.translations[1] = glm::vec3(0.0f, 0.94f, speed * t);
        for(int side = 0; side < 2; ++side){
            //Faza koraka: prva polovica stoji, druga se prenosi dvostruko brze od tijela
            const float phase = std::fmod(t / cycle + 0.5f * float(side), 1.0f);
            const float stepStart = std::floor(t / cycle + 0.5f * float(side));
            const float plantedZ = speed * cycle * (stepStart - 0.5f * float(side)) + speed * cycle * 0.5f;
            float z = plantedZ, y = 0.02f;
            if(phase >= 0.5f){
                const float swing = (phase - 0.5f) / 0.5f;
                const float smooth = swing * swing * (3.0f - 2.0f * swing);
                z = plantedZ + speed * cycle * smooth;
                y = 0.02f + 0.10f * std::sin(3.14159265f * swing);
            }
            pose.translations[size_t(3 + side)] = glm::vec3(side == 0 ? 0.1f : -0.1f, y, z);
        }
        switch(defect){
            case Defect::Skate:
                //Stojece stopalo klizi 30 cm/s unatrag - klasican moonwalk difuzijskih modela
                for(int side = 0; side < 2; ++side)
                    if(pose.translations[size_t(3 + side)].y < 0.021f) pose.translations[size_t(3 + side)].z -= 0.3f * t;
                break;
            case Defect::Float:
                for(size_t j = 1; j < clip.joints.size(); ++j) pose.translations[j].y += 0.15f;
                break;
            case Defect::Sink:
                if(f >= 40 && f < 50) pose.translations[3].y -= 0.08f;
                break;
            case Defect::Teleport:
                if(f >= 60) pose.translations[1].z += 1.0f;
                break;
            case Defect::Jitter:
                pose.translations[1].x += (f % 2 ? 0.012f : -0.012f);
                break;
            case Defect::None: break;
        }
        clip.frames.push_back(pose);
    }
    return clip;
}

}

int main(){
    TestReport report("motion_quality");
    namespace MQ = Engine::MotionQuality;

    //Ni savrsen hod nema klizanje nula: prst pri spustanju i odizanju prolazi kroz pojas dodira
    //(2.5 cm) dok se jos krece, pa ta dva-tri kadra po koraku daju ~6 cm/s. Isto vrijedi za svaki
    //pravi klip, pa to pomice sve jednako i ne mijenja poredak varijanti - ali znaci da je dno
    //mjere oko 6, ne 0 (Kimodovi klipovi: 9-32)
    const MQ::Report clean = MQ::evaluate(walk(Defect::None));
    report.check("cisti hod: sve mjere ispod praga", clean.valid && clean.footSkateCmPerSecond < 8.0f &&
                 clean.penetrationCm < 0.1f && clean.floatingCm < 0.5f && clean.score < 2.0f,
                 fmt("skate %.2f sink %.2f float %.2f jerk %.1f ocjena %.2f", double(clean.footSkateCmPerSecond),
                     double(clean.penetrationCm), double(clean.floatingCm), double(clean.jerkMetersPerSecond3),
                     double(clean.score)));
    report.check("pod iz Roota, ne procijenjen", !clean.floorEstimated && clean.floorHeightCm == 0.0f,
                 fmt("%.1f cm", double(clean.floorHeightCm)));

    struct Case{ Defect defect; const char* name; const char* worst; };
    const Case cases[] = {
        {Defect::Skate, "klizanje", "foot skating"},
        {Defect::Float, "lebdenje", "floating"},
        {Defect::Sink, "propadanje", "floor penetration"},
        {Defect::Teleport, "teleport kukova", "root jump"},
        {Defect::Jitter, "trzaji", "jitter"},
    };
    for(const Case& c : cases){
        const MQ::Report bad = MQ::evaluate(walk(c.defect));
        const int best = MQ::bestOf({bad, clean});
        const std::string what = std::string(c.name) + ": najgora mjera i cisti klip pobijedi";
        report.check(what.c_str(),
                     bad.valid && bad.worst == c.worst && best == 1 && bad.score > clean.score + 1.0f,
                     fmt("najgore '%s', ocjena %.2f prema %.2f", bad.worst.c_str(), double(bad.score), double(clean.score)));
    }

    //Putanja: kukovi idu ravno po Z; ruta pomaknuta za metar u stranu mora to vidjeti
    {
        MQ::Options onRoute, offRoute;
        onRoute.route = {{0, 0.0f, 0.0f}, {119, 0.0f, 119.0f / 30.0f}};
        offRoute.route = {{0, 1.0f, 0.0f}, {119, 1.0f, 119.0f / 30.0f}};
        const MQ::Report on = MQ::evaluate(walk(Defect::None), onRoute);
        const MQ::Report off = MQ::evaluate(walk(Defect::None), offRoute);
        report.check("promasaj rute u cm", on.routeErrorCm >= 0.0f && on.routeErrorCm < 1.0f &&
                     std::fabs(off.routeErrorCm - 100.0f) < 1.0f && off.worst == "off the route",
                     fmt("na ruti %.1f, pomaknuta %.1f", double(on.routeErrorCm), double(off.routeErrorCm)));
    }

    //constraints.json kakav Loom zapise (writeMotionRootConstraints)
    {
        const std::string json = "[\n  {\n    \"type\": \"root2d\",\n    \"frame_indices\": [0, 60, 119],\n"
                                 "    \"smooth_root_2d\": [[0.000000, 0.000000], [0.500000, -1.250000], [1.000000, 4.000000]],\n"
                                 "    \"global_root_heading\": [[1.0, 0.0], [1.0, 0.0], [1.0, 0.0]]\n  }\n]\n";
        const std::vector<MQ::RoutePoint> route = MQ::routeFromConstraints(json);
        const bool ok = route.size() == 3 && route[1].frame == 60 && route[1].x == 0.5f && route[1].z == -1.25f &&
                        route[2].frame == 119 && route[2].z == 4.0f;
        report.check("putanja iz constraints.json", ok, fmt("%zu tocaka", route.size()));
        report.check("bez root2d nema putanje", MQ::routeFromConstraints("[{\"type\": \"fullbody\"}]").empty(), "prazno");
    }

    report.check("bestOf bez valjanih je -1", MQ::bestOf({MQ::Report{}}) == -1, "");
    return report.result();
}
