// Uspravni sustav: scena mora stajati uspravno, a da se nijedna reprojekcija ne pomakne.
//
// Tri tvrdnje, i svaka moze tiho promasiti:
//
//   1. GORE SE PRONADJE. Scena se namjerno zakrene nasumicnom rotacijom i pomakne; nakon
//      poravnanja gornje osi kamera moraju biti +Y, a prva kamera mora gledati niz -Z.
//   2. NISTA SE NE MIJENJA U KVALITETI. Svaka tocka se projicira kroz svaku kameru prije i poslije;
//      pikseli moraju biti isti. Da transformacija nije kruta - ili da je rotacija kamere
//      primijenjena s krive strane - scena bi izgledala uspravno a solve bi bio pokvaren.
//   3. ODBIJA KAD NEMA SMISLA. Kamere koje se ne slazu oko smjera "gore" (nasumican nagib oko
//      osi pogleda) ne smiju dati nasumicno okrenutu scenu - poravnanje se tada ne primijeni.
//
// Plus robusnost ishodista: jedna tocka odbjegla na 1e5 ne smije odvuci srediste scene - to je
// greska koju smo vec jednom napravili na pravim podacima, u prikazu.
#include "TestHarness.h"

#include <Engine/SyntheticScene.h>
#include <Engine/Upright.h>

#include <glm/gtc/quaternion.hpp>

#include <cmath>
#include <cstdint>
#include <vector>

namespace{

struct Rng{
    uint64_t state = 0x2545F4914F6CDD1Dull;
    float next(){
        state ^= state << 13; state ^= state >> 7; state ^= state << 17;
        return float(double(state >> 11) / double(1ull << 53));
    }
    float signedUnit(){ return next() * 2.0f - 1.0f; }
};

//Kamere na luku oko scene, sve na istoj visini i VODORAVNE - gornja os im je tocno +Y. Kasnije se
//cijela scena zakrene, pa je poznato sto poravnanje mora vratiti.
//
//SUM IZ RUKE ima dva dijela i nisu jednaki: nagib gore-dolje i zakret lijevo-desno su veliki i
//nitko ih ne kontrolira, a horizont (vrtnja oko osi pogleda) snimatelj drzi ravno. Model sa sest
//stupnjeva oko NASUMICNE osi je prvo stavljao i sest stupnjeva na horizont - to je gore od snimke
void levelScene(std::vector<Engine::Pose>& poses, std::vector<glm::vec3>& points, Rng& rng,
                float pitchYawNoiseDegrees, float rollNoiseDegrees = -1.0f){
    if(rollNoiseDegrees < 0.0f) rollNoiseDegrees = pitchYawNoiseDegrees;
    poses.clear(); points.clear();
    for(int i = 0; i < 24; ++i){
        const float angle = -0.7f + 1.4f * float(i) / 23.0f;
        Engine::Pose pose;
        pose.position = glm::vec3(std::sin(angle) * 8.0f, 0.0f, std::cos(angle) * 8.0f);
        const glm::vec3 forward = glm::normalize(-glm::vec3(pose.position.x, 0.0f, pose.position.z));
        pose.orientation = glm::quatLookAt(forward, glm::vec3(0.0f, 1.0f, 0.0f));

        //Kamera-lokalne osi: x desno (nagib), y gore (zakret), z unazad (horizont)
        const float pitch = glm::radians(pitchYawNoiseDegrees * rng.signedUnit());
        const float yaw   = glm::radians(pitchYawNoiseDegrees * rng.signedUnit());
        const float roll  = glm::radians(rollNoiseDegrees * rng.signedUnit());
        pose.orientation = pose.orientation *
            glm::angleAxis(yaw, glm::vec3(0.0f, 1.0f, 0.0f)) *
            glm::angleAxis(pitch, glm::vec3(1.0f, 0.0f, 0.0f)) *
            glm::angleAxis(roll, glm::vec3(0.0f, 0.0f, 1.0f));
        poses.push_back(pose);
    }
    for(int i = 0; i < 400; ++i){
        points.push_back(glm::vec3(rng.signedUnit() * 3.0f, rng.signedUnit() * 2.0f, rng.signedUnit() * 3.0f));
    }
}

//Nasumicna kruta transformacija - ovako izgleda sustav prve kamere naspram svijeta
glm::quat scramble(std::vector<Engine::Pose>& poses, std::vector<glm::vec3>& points, Rng& rng){
    const glm::vec3 axis = glm::normalize(glm::vec3(rng.signedUnit(), rng.signedUnit(), rng.signedUnit()));
    const glm::quat rotation = glm::angleAxis(0.4f + rng.next() * 2.0f, axis);
    const glm::vec3 shift(rng.signedUnit() * 20.0f, rng.signedUnit() * 20.0f, rng.signedUnit() * 20.0f);
    for(Engine::Pose& pose : poses){
        pose.position = rotation * pose.position + shift;
        pose.orientation = glm::normalize(rotation * pose.orientation);
    }
    for(glm::vec3& point : points) point = rotation * point + shift;
    return rotation;
}

float worstUpDegrees(const std::vector<Engine::Pose>& poses){
    float worst = 0.0f;
    for(const Engine::Pose& pose : poses){
        const glm::vec3 up = pose.orientation * glm::vec3(0.0f, 1.0f, 0.0f);
        worst = std::max(worst, std::acos(std::clamp(up.y, -1.0f, 1.0f)) * 57.2957795f);
    }
    return worst;
}

}

int main(){
    TestReport report("S10 uspravni sustav");
    Rng rng;

    Engine::Intrinsics intrinsics;
    intrinsics.fx = intrinsics.fy = 700.0f;
    intrinsics.cx = 640.0f; intrinsics.cy = 360.0f;
    intrinsics.width = 1280; intrinsics.height = 720;

    //-- 1. gore se pronadje, i prva kamera gleda naprijed ------------------------------------
    {
        std::vector<Engine::Pose> poses;
        std::vector<glm::vec3> points;
        levelScene(poses, points, rng, 0.0f);
        scramble(poses, points, rng);

        const float before = worstUpDegrees(poses);
        const std::vector<uint8_t> posed(poses.size(), 1), solved(points.size(), 1);
        const Engine::UprightFrame frame = Engine::uprightFrame(poses, posed, points, solved);
        Engine::applyUpright(frame, poses, points);
        const float after = worstUpDegrees(poses);

        report.check("zakrenuta scena se vrati uspravno", frame.applied && after < 0.01f,
            fmt("nagib %.1f st -> %.4f st, sloznost %.3f", before, after, frame.coherence));

        const glm::vec3 forward = poses[0].orientation * glm::vec3(0.0f, 0.0f, -1.0f);
        report.check("prva kamera gleda niz -Z", forward.z < -0.999f && std::fabs(forward.y) < 1e-3f,
            fmt("smjer (%.4f, %.4f, %.4f)", forward.x, forward.y, forward.z));
    }

    //-- 2. nijedna reprojekcija se ne pomakne -------------------------------------------------
    {
        std::vector<Engine::Pose> poses;
        std::vector<glm::vec3> points;
        levelScene(poses, points, rng, 4.0f, 1.5f);
        scramble(poses, points, rng);

        std::vector<glm::vec2> before;
        std::vector<uint8_t> seen;
        for(const Engine::Pose& pose : poses){
            for(const glm::vec3& point : points){
                glm::vec2 pixel;
                seen.push_back(Engine::project(pose, intrinsics, point, pixel) ? 1 : 0);
                before.push_back(pixel);
            }
        }

        const std::vector<uint8_t> posed(poses.size(), 1), solved(points.size(), 1);
        const Engine::UprightFrame frame = Engine::uprightFrame(poses, posed, points, solved);
        Engine::applyUpright(frame, poses, points);

        float worst = 0.0f;
        size_t compared = 0, flipped = 0, index = 0;
        for(const Engine::Pose& pose : poses){
            for(const glm::vec3& point : points){
                glm::vec2 pixel;
                const bool visible = Engine::project(pose, intrinsics, point, pixel);
                if(visible != bool(seen[index])) ++flipped;
                if(visible && seen[index]){
                    worst = std::max(worst, glm::length(pixel - before[index]));
                    ++compared;
                }
                ++index;
            }
        }
        report.check("reprojekcija ostaje ista", frame.applied && compared > 1000 && flipped == 0 && worst < 2e-3f,
            fmt("najgore %.2e px na %zu projekcija, %zu promijenilo stranu", worst, compared, flipped));
    }

    //-- 3. kamera iz ruke: procijenjeni "gore" mora biti blizu PRAVOG ----------------------
    //
    //Mjeri se protiv ISTINE: pravi "gore" scene bio je +Y prije nasumicne rotacije; kroz obje
    //rotacije mora zavrsiti blizu +Y. Uz to se ISPISUJE osjetljivost na horizont - to je slaba
    //tocka ove metode i vrijedi je znati kao broj, ne kao prolaz ili pad
    {
        auto worstOver = [&](float pitchYaw, float roll){
            float worst = 0.0f;
            for(int trial = 0; trial < 20; ++trial){
                std::vector<Engine::Pose> poses;
                std::vector<glm::vec3> points;
                levelScene(poses, points, rng, pitchYaw, roll);
                const glm::quat scrambled = scramble(poses, points, rng);
                const std::vector<uint8_t> posed(poses.size(), 1), solved(points.size(), 1);
                const Engine::UprightFrame frame = Engine::uprightFrame(poses, posed, points, solved);
                const glm::vec3 trueUp = frame.rotation * (scrambled * glm::vec3(0.0f, 1.0f, 0.0f));
                worst = std::max(worst, std::acos(std::clamp(trueUp.y, -1.0f, 1.0f)) * 57.2957795f);
            }
            return worst;
        };
        const float realistic = worstOver(8.0f, 1.5f);
        const float badHorizon = worstOver(8.0f, 6.0f);
        report.check("iz ruke (nagib i zakret 8 st, horizont 1.5 st): gore unutar 1 st", realistic < 1.0f,
            fmt("najgore %.2f st u 20 pokusaja; uz horizont 6 st bilo bi %.2f st", realistic, badHorizon));
    }

    //-- 4. NEGATIVNA KONTROLA: kamere se ne slazu oko smjera "gore" --------------------------
    {
        std::vector<Engine::Pose> poses;
        std::vector<glm::vec3> points;
        levelScene(poses, points, rng, 0.0f);
        //Svaka kamera zakrenuta oko svoje osi pogleda za nasumican kut do 180 st
        for(Engine::Pose& pose : poses){
            const glm::vec3 look = pose.orientation * glm::vec3(0.0f, 0.0f, -1.0f);
            pose.orientation = glm::angleAxis(3.14159f * rng.signedUnit(), look) * pose.orientation;
        }
        const std::vector<Engine::Pose> original = poses;

        const std::vector<uint8_t> posed(poses.size(), 1), solved(points.size(), 1);
        const Engine::UprightFrame frame = Engine::uprightFrame(poses, posed, points, solved);
        Engine::applyUpright(frame, poses, points);

        bool untouched = true;
        for(size_t i = 0; i < poses.size(); ++i){
            if(poses[i].position != original[i].position) untouched = false;
        }
        report.check("kosi horizonti: ne dira se nista", !frame.applied && untouched,
            fmt("sloznost gornjih osi %.3f (prag 0.5) - desne osi bi same dale vodoravni smjer pogleda",
                frame.coherence));
    }

    //-- 6. OVO JE SRUSILO PRVU METODU: kamera gleda DOLJE u predmet s djelomicnog luka --------
    //
    //Snimka predmeta na stolu: kamera iz ruke, 30 st prema dolje, luk od 100 st. Gornje osi su
    //nagnute prema predmetu i njihov se prosjek ne ponisti; desne osi ostaju vodoravne
    {
        float worstRight = 0.0f, worstMean = 0.0f;
        for(int trial = 0; trial < 10; ++trial){
            std::vector<Engine::Pose> poses;
            std::vector<glm::vec3> points;
            for(int i = 0; i < 30; ++i){
                const float angle = -0.87f + 1.74f * float(i) / 29.0f;
                Engine::Pose pose;
                pose.position = glm::vec3(std::sin(angle) * 1.2f, 0.7f, std::cos(angle) * 1.2f);
                const glm::vec3 forward = glm::normalize(glm::vec3(0.0f, 0.0f, 0.0f) - pose.position);
                pose.orientation = glm::quatLookAt(forward, glm::vec3(0.0f, 1.0f, 0.0f));
                poses.push_back(pose);
            }
            for(int i = 0; i < 300; ++i) points.push_back(glm::vec3(rng.signedUnit() * 0.3f, rng.next() * 0.1f, rng.signedUnit() * 0.3f));
            const glm::quat scrambled = scramble(poses, points, rng);

            const std::vector<uint8_t> posed(poses.size(), 1), solved(points.size(), 1);
            const Engine::UprightFrame frame = Engine::uprightFrame(poses, posed, points, solved);
            const glm::vec3 trueUp = frame.rotation * (scrambled * glm::vec3(0.0f, 1.0f, 0.0f));
            worstRight = std::max(worstRight, std::acos(std::clamp(trueUp.y, -1.0f, 1.0f)) * 57.2957795f);

            //Za usporedbu: sto bi dao prosjek gornjih osi
            glm::vec3 mean(0.0f);
            for(const Engine::Pose& pose : poses) mean += pose.orientation * glm::vec3(0.0f, 1.0f, 0.0f);
            const glm::vec3 wrongUp = glm::normalize(mean);
            const glm::vec3 truth = scrambled * glm::vec3(0.0f, 1.0f, 0.0f);
            worstMean = std::max(worstMean, std::acos(std::clamp(glm::dot(wrongUp, truth), -1.0f, 1.0f)) * 57.2957795f);
        }
        report.check("pogled 30 st dolje u predmet: gore tocan", worstRight < 0.05f,
            fmt("desne osi promase %.3f st; prosjek gornjih bi promasio %.1f st", worstRight, worstMean));
    }

    //-- 7. kamera se nikad ne zakrene: desne osi ne odredjuju nista ----------------------------
    //
    //Cisti dolly ravno naprijed. Sve desne osi su iste, pa okomitih smjerova ima cijela ravnina -
    //tada se uzima prosjek gornjih osi, i on je ovdje tocan jer je kamera vodoravna
    {
        std::vector<Engine::Pose> poses;
        std::vector<glm::vec3> points;
        for(int i = 0; i < 20; ++i){
            Engine::Pose pose;
            pose.position = glm::vec3(0.0f, 0.0f, 10.0f - float(i) * 0.3f);
            pose.orientation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
            poses.push_back(pose);
        }
        for(int i = 0; i < 200; ++i) points.push_back(glm::vec3(rng.signedUnit() * 3.0f, rng.signedUnit() * 2.0f, rng.signedUnit() * 3.0f - 5.0f));
        const glm::quat scrambled = scramble(poses, points, rng);

        const std::vector<uint8_t> posed(poses.size(), 1), solved(points.size(), 1);
        const Engine::UprightFrame frame = Engine::uprightFrame(poses, posed, points, solved);
        const glm::vec3 trueUp = frame.rotation * (scrambled * glm::vec3(0.0f, 1.0f, 0.0f));
        const float error = std::acos(std::clamp(trueUp.y, -1.0f, 1.0f)) * 57.2957795f;
        report.check("dolly bez zakretanja: zamjenski put", frame.applied && !frame.fromRightAxes && error < 0.05f,
            fmt("gore iz %s, promasaj %.3f st", frame.fromRightAxes ? "desnih osi" : "prosjeka gornjih", error));
    }

    //-- 5. odbjegla tocka ne odvuce srediste --------------------------------------------------
    {
        std::vector<Engine::Pose> poses;
        std::vector<glm::vec3> points;
        levelScene(poses, points, rng, 0.0f);
        points.push_back(glm::vec3(1.0e5f, -4.0e5f, 9.0e5f));

        const std::vector<uint8_t> posed(poses.size(), 1), solved(points.size(), 1);
        const Engine::UprightFrame frame = Engine::uprightFrame(poses, posed, points, solved);
        report.check("odbjegla tocka ne odvuce srediste", frame.applied && glm::length(frame.origin) < 0.5f,
            fmt("srediste %.3f od pravog (scena +-3)", glm::length(frame.origin)));
    }

    return report.result();
}
