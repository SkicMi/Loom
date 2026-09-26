// Hvat predmeta (LoomHold.h + Warp::Hold): predmet prati saku od On do Off i ostaje gdje je pusten.
//
// Lik: Rig -> Hand, saka se giba x = kadar/10 m kroz kljuceve. Predmet Sword stoji 5 cm od sake.
//   - najbliza saka se nadje samo unutar dosega
//   - hvat u kadru 10: predmet ne skoci (isti svijet u kadru hvata) i prati saku u kadru 20
//   - iza Off predmet ostaje tocno gdje ga je saka pustila; prije On je na svom mjestu
//   - prebacivanje u drugu ruku zavrsi prvi hvat kadar prije
//   - pomicanje predmeta u ruci mijenja pomak, ne kljuceve
//   - rub hvata ne prelazi drugi rub; saka ne moze drzati predmet u kojem je
//   - projekt spremi i procita hvat, isti svijet predmeta nakon otvaranja
#include "TestHarness.h"

#include "../src/LoomHold.h"

#include <Warp/Project.h>

#include <cmath>
#include <filesystem>

namespace{

struct Scene{ Warp::Stage stage; Warp::Id rig = Warp::None, hand = Warp::None, other = Warp::None, sword = Warp::None; };

Scene makeScene(){
    Scene s;
    s.rig = s.stage.create("Rig");
    s.hand = s.stage.create("Hand", s.rig);
    s.other = s.stage.create("OtherHand", s.rig);
    for(Warp::Id joint : {s.hand, s.other}) s.stage.get(joint)->joint = Warp::Joint{};
    for(int f = 0; f <= 100; ++f) s.stage.get(s.hand)->translationKeys.set(double(f), glm::vec3(float(f) / 10.0f, 1.0f, 0.0f));
    s.stage.get(s.other)->local.translation = glm::vec3(0.0f, 1.0f, 3.0f);
    s.sword = s.stage.create("Sword");
    s.stage.get(s.sword)->local.translation = glm::vec3(1.05f, 1.0f, 0.0f);   //uz saku u kadru 10
    return s;
}

glm::vec3 at(const Scene& s, Warp::Id id, double frame){ return glm::vec3(s.stage.worldMatrix(id, frame)[3]); }
float gap(glm::vec3 a, glm::vec3 b){ return glm::length(a - b); }

}

int main(){
    TestReport report("hold");
    Scene s = makeScene();
    const std::vector<Loom::HoldHand> hands{{s.rig, s.hand, Warp::None, true}, {s.rig, s.other, Warp::None, false}};

    report.check("saka unutar dosega se nade, izvan ne",
                 Loom::nearestHoldHand(s.stage, hands, s.sword, at(s, s.sword, 10), 10.0, 0.12f) == 0 &&
                 Loom::nearestHoldHand(s.stage, hands, s.sword, at(s, s.sword, 50), 50.0, 0.12f) == -1, "");

    //Na ekranu (pogled niz -z, 100 px po metru): 2 m dublje, ali uz saku kako se vidi - nade se
    auto project = [](const glm::vec3& p, glm::vec2& pixel){ pixel = glm::vec2(p.x, p.y) * 100.0f; return true; };
    report.check("na ekranu: uz saku kako se vidi, iako je dublje",
                 Loom::nearestHoldHandOnScreen(s.stage, hands, s.sword, at(s, s.sword, 10) + glm::vec3(0.0f, 0.0f, -2.0f), 10.0, 20.0f, project) == 0 &&
                 Loom::nearestHoldHandOnScreen(s.stage, hands, s.sword, at(s, s.sword, 10) + glm::vec3(0.5f, 0.0f, 0.0f), 10.0, 20.0f, project) == -1, "");
    {
        Scene snap = makeScene();
        snap.stage.get(snap.sword)->local.translation = glm::vec3(1.0f, 1.0f, -2.0f);
        const glm::vec3 palm(1.0f, 1.0f, 0.0f);
        Loom::grabItem(snap.stage, snap.sword, snap.hand, 10.0, 60.0, &palm);
        report.check("hvat s dlanom: predmet sjedne u dlan i prati ga",
                     gap(at(snap, snap.sword, 10), palm) < 1e-5f && gap(at(snap, snap.sword, 20), glm::vec3(2.0f, 1.0f, 0.0f)) < 1e-5f &&
                     gap(at(snap, snap.sword, 5), glm::vec3(1.0f, 1.0f, -2.0f)) < 1e-6f, "");
    }

    const glm::vec3 before = at(s, s.sword, 10);
    report.check("hvat nastane", Loom::grabItem(s.stage, s.sword, s.hand, 10.0, 60.0), "");
    report.check("u kadru hvata predmet ne skoci", gap(at(s, s.sword, 10), before) < 1e-5f,
                 fmt("%.2e m", gap(at(s, s.sword, 10), before)));
    report.check("u kadru 20 prati saku (x 2.05)", gap(at(s, s.sword, 20), glm::vec3(2.05f, 1.0f, 0.0f)) < 1e-5f,
                 fmt("x %.3f", at(s, s.sword, 20).x));
    report.check("iza Off ostaje gdje je pusten (x 6.05)", gap(at(s, s.sword, 90), glm::vec3(6.05f, 1.0f, 0.0f)) < 1e-5f,
                 fmt("x %.3f", at(s, s.sword, 90).x));
    report.check("prije On na svom mjestu", gap(at(s, s.sword, 5), before) < 1e-6f, "");

    //Prebacivanje u drugu ruku u kadru 30: prvi hvat zavrsi u 29, predmet u 30 ne skoci
    const glm::vec3 at30 = at(s, s.sword, 30);
    Loom::grabItem(s.stage, s.sword, s.other, 30.0, 80.0);
    const auto& holds = s.stage.get(s.sword)->holds;
    report.check("prebacivanje: prvi hvat zavrsi kadar prije, bez skoka",
                 holds.size() == 2 && holds[0].offFrame == 29.0 && holds[1].onFrame == 30.0 && gap(at(s, s.sword, 30), at30) < 1e-5f,
                 fmt("%zu hvata", holds.size()));
    report.check("druga ruka stoji, predmet s njom stoji", gap(at(s, s.sword, 70), at30) < 1e-5f, "");

    //Pomak u ruci: predmet pomaknut 10 cm gore u kadru 40 ostaje tako i u 70
    glm::mat4 lifted = s.stage.worldMatrix(s.sword, 40.0);
    lifted[3] += glm::vec4(0.0f, 0.1f, 0.0f, 0.0f);
    Loom::moveHeldItem(s.stage, s.sword, 40.0, lifted);
    report.check("pomak u ruci mijenja pomak, bez kljuceva", gap(at(s, s.sword, 70), at30 + glm::vec3(0.0f, 0.1f, 0.0f)) < 1e-5f &&
                 s.stage.get(s.sword)->translationKeys.empty(), "");

    Loom::moveHeldItem(s.stage, s.sword, 40.0, s.stage.worldMatrix(s.sword, 40.0) * glm::mat4(1.0f));
    Loom::moveHoldEdge(*s.stage.get(s.sword), 1, 0, 10.0);
    report.check("rub ne prelazi susjedni hvat", s.stage.get(s.sword)->holds[1].onFrame == 30.0,
                 fmt("On %.0f", s.stage.get(s.sword)->holds[1].onFrame));
    Loom::releaseItem(s.stage, s.sword, 50.0);
    report.check("pusti u kadru 50", s.stage.get(s.sword)->holds[1].offFrame == 50.0, "");

    const Warp::Id inside = s.stage.create("Grip", s.sword);
    s.stage.get(inside)->joint = Warp::Joint{};
    report.check("kost unutar predmeta ga ne moze drzati", !s.stage.canHold(s.sword, inside) && !Loom::grabItem(s.stage, s.sword, inside, 5.0, 9.0), "");

    const std::string path = (std::filesystem::temp_directory_path() / "loom_test_hold.usda").string();
    std::string error;
    Warp::Stage loaded;
    const bool saved = Warp::saveProject(s.stage, path, error) && Warp::loadProject(path, loaded, error);
    const Warp::Id loadedSword = loaded.find("/Sword");
    bool same = saved && loadedSword != Warp::None && loaded.get(loadedSword)->holds.size() == 2;
    float worst = 0.0f;
    if(same) for(double f : {5.0, 10.0, 20.0, 29.0, 30.0, 45.0, 70.0, 95.0})
        worst = std::max(worst, gap(glm::vec3(loaded.worldMatrix(loadedSword, f)[3]), at(s, s.sword, f)));
    report.check("projekt spremi i procita hvat", same && worst < 1e-5f && loaded.fingerprint() == s.stage.fingerprint(),
                 fmt("%s najvise %.2e m", error.c_str(), worst));
    std::filesystem::remove(path);
    return report.result();
}
