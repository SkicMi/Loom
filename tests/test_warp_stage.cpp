// Scena: stablo, imena, transformacije kroz vrijeme.
//
// ZASTO SE OVO TESTIRA. Sve sto editor radi prolazi kroz ovo: hijerarhija crta walk(), viewport
// crta worldMatrix(), timeline pita kljuceve. Greska ovdje ne rusi nista - kocka samo stoji malo
// krivo, ili kamera izmedju dva kadra napravi krug. Upravo to se golim okom ne primijeti dok se
// ne pokusa matchmove, a tada se trazi u solveru, ne ovdje.
//
// NEGATIVNE KONTROLE su dvije greske koje se u ovakvom kodu stvarno dogode:
//   - slerp izmedju q i -q (ista rotacija, suprotan predznak) bez okretanja predznaka vrti za
//     cijeli krug. Solve daje takve susjedne kadrove
//   - premjestanje entiteta pod vlastito dijete napravi petlju; worldMatrix se tada vrti zauvijek
#include "TestHarness.h"

#include <Warp/Stage.h>

#include <glm/gtc/matrix_transform.hpp>

#include <cmath>

namespace{

float distance(const glm::mat4& a, const glm::mat4& b){
    float worst = 0.0f;
    for(int c = 0; c < 4; ++c) for(int r = 0; r < 4; ++r) worst = std::max(worst, std::fabs(a[c][r] - b[c][r]));
    return worst;
}

float angleDegrees(const glm::quat& a, const glm::quat& b){
    return glm::degrees(2.0f * std::acos(std::min(1.0f, std::fabs(glm::dot(a, b)))));
}

}

int main(){
    TestReport report("W1 scena");

    //-- 1. stablo, putovi i imena -------------------------------------------------------------
    {
        Warp::Stage stage;
        const Warp::Id solve = stage.create("Solve");
        const Warp::Id camera = stage.create("Kamera", solve);
        const Warp::Id cloud = stage.create("Tocke", solve);
        const Warp::Id cube = stage.create("Kocka");
        const Warp::Id cube2 = stage.create("Kocka");
        const Warp::Id odd = stage.create("C0256.MP4 moj", solve);

        report.check("putovi su USD-ovi", stage.path(camera) == "/Solve/Kamera" && stage.find("/Solve/Tocke") == cloud,
            stage.path(camera) + ", " + stage.path(cloud));
        report.check("ime medju bracom je jedinstveno", stage.get(cube2)->name == "Kocka1" && stage.find("/Kocka") == cube,
            stage.get(cube)->name + " i " + stage.get(cube2)->name);
        report.check("ime iz datoteke postane USD ime", stage.get(odd)->name == "C0256_MP4_moj",
            stage.get(odd)->name);

        std::string order;
        stage.walk([&](const Warp::Entity& e, int depth){ order += std::to_string(depth) + e.name + " "; });
        report.check("obilazak ide redom hijerarhije s dubinom",
            order == "0Solve 1Kamera 1Tocke 1C0256_MP4_moj 0Kocka 0Kocka1 ", order);

        const size_t removed = stage.remove(solve);
        report.check("brisanje nosi i djecu", removed == 4 && stage.size() == 2 && !stage.contains(camera) &&
                                              stage.roots().size() == 2,
            fmt("obrisano %zu, ostalo %zu", removed, stage.size()));

        const Warp::Id again = stage.create("Solve");
        report.check("id se ne ponavlja", again != solve && again != camera && again > odd, fmt("novi id %u", again));
    }

    //-- 2. svjetska transformacija: roditelj pa dijete ----------------------------------------
    {
        Warp::Stage stage;
        const Warp::Id parent = stage.create("Roditelj");
        const Warp::Id child = stage.create("Dijete", parent);
        stage.get(parent)->local.translation = glm::vec3(10.0f, 0.0f, 0.0f);
        stage.get(parent)->local.rotation = glm::angleAxis(glm::radians(90.0f), glm::vec3(0, 1, 0));
        stage.get(parent)->local.scale = glm::vec3(2.0f);
        stage.get(child)->local.translation = glm::vec3(0.0f, 0.0f, -1.0f);

        //Dijete stoji 1 ispred roditelja (-Z); roditelj je zakrenut 90 st oko Y pa -Z postaje -X,
        //mjerilo 2 to udvostruci: svijet (10 - 2, 0, 0)
        const glm::vec3 at = glm::vec3(stage.worldMatrix(child, 1.0) * glm::vec4(0, 0, 0, 1));
        report.check("dijete ide s roditeljem", glm::length(at - glm::vec3(8.0f, 0.0f, 0.0f)) < 1e-5f,
            fmt("(%.4f, %.4f, %.4f), ocekivano (8, 0, 0)", at.x, at.y, at.z));
    }

    //-- 3. premjestanje cuva mjesto u svijetu, i odbija petlju ---------------------------------
    {
        Warp::Stage stage;
        const Warp::Id a = stage.create("A");
        const Warp::Id b = stage.create("B", a);
        const Warp::Id c = stage.create("C");
        stage.get(a)->local.translation = glm::vec3(1.0f, 2.0f, 3.0f);
        stage.get(a)->local.rotation = glm::angleAxis(0.7f, glm::normalize(glm::vec3(1, 2, 0.5f)));
        stage.get(b)->local.translation = glm::vec3(0.5f, -1.0f, 2.0f);
        stage.get(b)->local.rotation = glm::angleAxis(-0.4f, glm::vec3(0, 0, 1));
        stage.get(c)->local.translation = glm::vec3(-3.0f, 0.0f, 1.0f);
        stage.get(c)->local.rotation = glm::angleAxis(1.1f, glm::vec3(0, 1, 0));

        const glm::mat4 before = stage.worldMatrix(b, 1.0);
        const bool moved = stage.reparent(b, c);
        const float drift = distance(before, stage.worldMatrix(b, 1.0));
        report.check("premjesten entitet ostaje gdje je bio", moved && stage.get(b)->parent == c && drift < 1e-5f &&
                                                             stage.get(a)->children.empty(),
            fmt("pomak u svijetu %.2e", drift));

        const bool loop = stage.reparent(c, b);
        const bool self = stage.reparent(c, c);
        report.check("petlja se odbija: roditelj ne smije biti vlastiti potomak", !loop && !self &&
                                                                                    stage.get(c)->parent == Warp::None,
            "C pod svoje dijete B, i C pod sebe");
    }

    //-- 4. kljucevi: postavljanje, interpolacija, rubovi --------------------------------------
    {
        Warp::Track<glm::vec3> track;
        track.set(10.0, glm::vec3(0.0f));
        track.set(20.0, glm::vec3(10.0f, 0.0f, 0.0f));
        track.set(15.0, glm::vec3(0.0f, 5.0f, 0.0f));
        track.set(15.0, glm::vec3(5.0f, 0.0f, 0.0f));      //isti kadar: zamjena, ne drugi kljuc
        const glm::vec3 middle = track.at(12.5);
        report.check("kljuc na istom kadru se zamijeni", track.size() == 3 && track.times[1] == 15.0,
            fmt("%zu kljuca", track.size()));
        report.check("izmedju kljuceva je pravac", glm::length(middle - glm::vec3(2.5f, 0.0f, 0.0f)) < 1e-6f,
            fmt("u 12.5: (%.3f, %.3f, %.3f)", middle.x, middle.y, middle.z));
        report.check("izvan raspona drzi rub", track.at(-100.0) == glm::vec3(0.0f) && track.at(1e6) == glm::vec3(10, 0, 0),
            "prije prvog i poslije zadnjeg");
    }

    //-- NEGATIVNA KONTROLA: q i -q su ista rotacija ---------------------------------------------
    {
        const glm::quat q = glm::angleAxis(0.3f, glm::normalize(glm::vec3(0.2f, 1.0f, 0.1f)));
        const glm::quat next = -glm::angleAxis(0.32f, glm::normalize(glm::vec3(0.2f, 1.0f, 0.1f)));
        Warp::Track<glm::quat> track;
        track.set(1.0, q);
        track.set(2.0, next);
        const glm::quat halfway = track.at(1.5);
        const float ours = angleDegrees(halfway, glm::angleAxis(0.31f, glm::normalize(glm::vec3(0.2f, 1.0f, 0.1f))));
        const float naive = angleDegrees(glm::normalize(glm::slerp(q, next, 0.5f)), q);
        //glm::slerp sam vec bira kraci put, pa se "naivno" mjeri obicnim mix-om bez okretanja
        const glm::quat mixed = glm::normalize(glm::mix(q, next, 0.5f));
        const float mixError = angleDegrees(mixed, q);
        report.check("izmedju q i -q nema kruga", ours < 0.01f && mixError > 90.0f,
            fmt("promasaj %.4f st; bez okretanja predznaka %.1f st (slerp %.2f)", ours, mixError, naive));
    }

    //-- 5. animirana kamera: kljucevi imaju prednost pred mirnom transformacijom ---------------
    {
        Warp::Stage stage;
        const Warp::Id solve = stage.create("Solve");
        const Warp::Id camera = stage.create("Kamera", solve);
        Warp::Entity& entity = *stage.get(camera);
        entity.camera = Warp::Camera{};
        entity.local.translation = glm::vec3(99.0f);          //ne smije se vidjeti - os ima kljuceve
        for(int frame = 1; frame <= 100; ++frame){
            entity.translationKeys.set(frame, glm::vec3(float(frame) * 0.1f, 0.0f, 0.0f));
            entity.rotationKeys.set(frame, glm::angleAxis(float(frame) * 0.01f, glm::vec3(0, 1, 0)));
        }
        stage.get(solve)->local.translation = glm::vec3(0.0f, 1.0f, 0.0f);

        const glm::mat4 world = stage.worldMatrix(camera, 50.5);
        const glm::vec3 position = glm::vec3(world[3]);
        const glm::vec3 forward = -glm::vec3(world[2]);
        const glm::vec3 expectedForward = glm::angleAxis(0.505f, glm::vec3(0, 1, 0)) * glm::vec3(0, 0, -1);
        report.check("kamera u kadru 50.5 je izmedju kljuceva, pod roditeljem",
            glm::length(position - glm::vec3(5.05f, 1.0f, 0.0f)) < 1e-4f && glm::length(forward - expectedForward) < 1e-4f,
            fmt("polozaj (%.3f, %.3f, %.3f), smjer promasen %.2e", position.x, position.y, position.z,
                glm::length(forward - expectedForward)));

        //Animirani entitet se premjesta bez preracuna kljuceva
        const Warp::Id rig = stage.create("Rig");
        stage.get(rig)->local.translation = glm::vec3(5.0f, 0.0f, 0.0f);
        stage.reparent(camera, rig);
        report.check("animiranom se kljucevi ne diraju pri premjestanju",
            stage.get(camera)->translationKeys.at(10.0) == glm::vec3(1.0f, 0.0f, 0.0f),
            "kljuc u kadru 10 ostao (1, 0, 0)");
    }

    //-- 6. uredjivanje kroz vrijeme -------------------------------------------------------------
    {
        Warp::Stage stage;
        const Warp::Id cube = stage.create("Kocka");
        Warp::Transform moved;
        moved.translation = glm::vec3(1.0f, 2.0f, 3.0f);
        stage.setLocalAt(cube, 10.0, moved);
        const bool staticEdit = !stage.get(cube)->animated() && stage.localAt(cube, 99.0).translation == moved.translation;

        //Kljuc u 10, pa pomak u 20: os s kljucevima dobiva drugi kljuc, a izmedju je pravac
        stage.keyAll(cube, 10.0);
        moved.translation = glm::vec3(3.0f, 2.0f, 3.0f);
        stage.setLocalAt(cube, 20.0, moved);
        const glm::vec3 middle = stage.localAt(cube, 15.0).translation;
        report.check("uredjivanje: mirno ostaje mirno, animirano dobiva kljuc",
            staticEdit && stage.get(cube)->translationKeys.size() == 2 && glm::length(middle - glm::vec3(2, 2, 3)) < 1e-5f,
            fmt("u 15: (%.2f, %.2f, %.2f)", middle.x, middle.y, middle.z));

        double next = 0.0, previous = 0.0;
        const bool haveNext = stage.neighbourKey(cube, 12.0, +1, next);
        const bool havePrevious = stage.neighbourKey(cube, 12.0, -1, previous);
        const bool noneAfter = !stage.neighbourKey(cube, 20.0, +1, next);
        report.check("skok na susjedni kljuc", haveNext && havePrevious && noneAfter && previous == 10.0,
            fmt("iz 12: prethodni %.0f, sljedeci %s", previous, haveNext ? "20" : "nema"));

        //Brisanje zadnjeg kljuca ostavi vrijednost koju je drzao, ne onu od prije animiranja
        stage.eraseKeysAt(cube, 10.0);
        const size_t erased = stage.eraseKeysAt(cube, 20.0);
        report.check("brisanje zadnjeg kljuca ne vraca kocku na staro mjesto",
            erased == 3 && !stage.get(cube)->animated() && stage.localAt(cube, 1.0).translation == glm::vec3(3, 2, 3),
            fmt("obrisano %zu, stoji na (%.0f, %.0f, %.0f)", erased, stage.localAt(cube, 1.0).translation.x,
                stage.localAt(cube, 1.0).translation.y, stage.localAt(cube, 1.0).translation.z));
    }

    //-- 7. Animator: aktivni klip daje lokalnu pozu zglobovima -------------------------------
    {
        Warp::Stage stage;
        const Warp::Id rig = stage.create("Mascot");
        stage.get(rig)->local.translation = glm::vec3(4.0f, 0.0f, 0.0f);
        const Warp::Id arm = stage.create("Arm", rig);
        stage.get(arm)->local.translation = glm::vec3(9.0f, 0.0f, 0.0f);
        Warp::Animator animator;
        Warp::AnimationClip wave;
        wave.name = "Wave"; wave.startFrame = 10.0; wave.endFrame = 20.0;
        Warp::AnimatorTrack rootMotionTrack;
        rootMotionTrack.target = rig; rootMotionTrack.targetPath = stage.path(rig); rootMotionTrack.rootMotion = true;
        rootMotionTrack.translationKeys.set(10.0, glm::vec3(4.0f, 0.0f, 0.0f));
        rootMotionTrack.translationKeys.set(20.0, glm::vec3(14.0f, 0.0f, 0.0f));
        wave.tracks.push_back(rootMotionTrack);
        Warp::AnimatorTrack waveTrack;
        waveTrack.target = arm; waveTrack.targetPath = stage.path(arm);
        waveTrack.translationKeys.set(10.0, glm::vec3(0.0f));
        waveTrack.translationKeys.set(20.0, glm::vec3(2.0f, 0.0f, 0.0f));
        wave.tracks.push_back(waveTrack);
        Warp::AnimationClip point;
        point.name = "Point"; point.startFrame = 30.0; point.endFrame = 40.0;
        Warp::AnimatorTrack pointTrack;
        pointTrack.target = arm; pointTrack.targetPath = stage.path(arm);
        pointTrack.translationKeys.set(30.0, glm::vec3(0.0f, 1.0f, 0.0f));
        pointTrack.translationKeys.set(40.0, glm::vec3(0.0f, 3.0f, 0.0f));
        point.tracks.push_back(pointTrack);
        animator.animations = {wave, point};
        stage.get(rig)->animator = animator;
        const glm::vec3 waveMiddle = stage.localAt(arm, 15.0).translation;
        const float rootMovesWithClip = stage.worldMatrix(rig, 15.0)[3].x;
        stage.get(rig)->animator->animations[0].inPlace = true;
        const float rootStaysInPlace = stage.worldMatrix(rig, 15.0)[3].x;
        stage.get(rig)->animator->activeAnimation = 1;
        const glm::vec3 pointMiddle = stage.localAt(arm, 35.0).translation;
        stage.get(rig)->animator->enabled = false;
        const glm::vec3 disabled = stage.localAt(arm, 35.0).translation;
        report.check("Animator odabire i interpolira klip, a iskljucen vraca mirnu transformaciju",
            glm::length(waveMiddle - glm::vec3(1.0f, 0.0f, 0.0f)) < 1e-5f &&
            std::fabs(rootMovesWithClip - 9.0f) < 1e-5f && std::fabs(rootStaysInPlace - 4.0f) < 1e-5f &&
            glm::length(pointMiddle - glm::vec3(0.0f, 2.0f, 0.0f)) < 1e-5f &&
            disabled == glm::vec3(9.0f, 0.0f, 0.0f),
            fmt("Wave (%.1f, %.1f, %.1f), Point (%.1f, %.1f, %.1f)", waveMiddle.x, waveMiddle.y, waveMiddle.z,
                pointMiddle.x, pointMiddle.y, pointMiddle.z));
    }

    //-- 8. biblioteka materijala: imena i brisanje ---------------------------------------------
    {
        Warp::Stage stage;
        Warp::Material m;
        m.name = "Celik";
        const int a = stage.addMaterial(m);
        const int b = stage.addMaterial(m);             //isto ime: dobije broj
        m.name = "Guma";
        const int c = stage.addMaterial(m);
        const Warp::Id cube = stage.create("Kocka");
        stage.get(cube)->mesh = Warp::Mesh{};
        stage.get(cube)->mesh->material = c;
        const Warp::Id model = stage.create("Model");
        stage.get(model)->model = Warp::Model{"x.glb", 0, {a, b, c}};
        stage.removeMaterial(b);
        report.check("brisanje materijala popravlja veze iza njega",
            stage.materials.size() == 2 && stage.materials[1].name == "Guma" && stage.get(cube)->mesh->material == 1 &&
            stage.get(model)->model->materials == std::vector<int>({0, -1, 1}) && stage.materials[0].name == "Celik",
            fmt("imena %s, %s; kocka pokazuje na %d", stage.materials[0].name.c_str(), stage.materials[1].name.c_str(),
                stage.get(cube)->mesh->material));
        report.check("isto ime materijala dobije broj", a == 0 && b == 1, "Celik, Celik1");
    }

    return report.result();
}
