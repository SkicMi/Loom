// Odabir plohe i kocka na njoj: nagnuti stol u sceni, zid iza njega, sum i odbjegle tocke.
//
// ZASTO SE OVO TESTIRA. Kocka "on top of surface" je tocna samo ako je normala tocna - a tri
// stvari je tiho nagnu:
//
//   ZID IZA        pravokutnik gleda duz pogleda i uhvati i ono iza plohe
//   RUB            komad susjedne plohe (noga stola, pod) koji udje u pravokutnik
//   STRANA         normala mora gledati prema kameri, inace kocka visi ispod stola
//
// Ploha je nagnuta 25 st oko kose osi, pa ni jedna os svijeta nije slucajno tocna.
#include "TestHarness.h"

#include "../src/LoomSurface.h"

#include <cmath>

int main(){
    TestReport report("E4 ploha iz odabira");

    const glm::vec3 trueNormal = glm::normalize(glm::angleAxis(glm::radians(25.0f), glm::normalize(glm::vec3(1.0f, 0.0f, 0.4f))) *
                                                glm::vec3(0.0f, 1.0f, 0.0f));
    const glm::vec3 u = glm::normalize(glm::cross(trueNormal, glm::vec3(0.0f, 0.0f, 1.0f)));
    const glm::vec3 v = glm::cross(u, trueNormal);
    const glm::vec3 tableCentre(0.3f, 1.0f, -0.2f);

    Warp::Stage stage;
    const Warp::Id cloud = stage.create("Tocke");
    Warp::Points points;
    uint32_t seed = 12345u;
    auto random = [&](){ seed ^= seed << 13; seed ^= seed >> 17; seed ^= seed << 5; return float(seed % 20001) / 10000.0f - 1.0f; };
    //Stol: izduzen po u (2 x 0.8), sum 2 mm okomito
    for(int i = 0; i < 3000; ++i){
        points.positions.push_back(tableCentre + u * random() * 1.0f + v * random() * 0.4f + trueNormal * random() * 0.002f);
    }
    //Zid daleko iza stola, gledano iz kamere
    for(int i = 0; i < 3000; ++i) points.positions.push_back(glm::vec3(random() * 3.0f, random() * 2.0f + 1.0f, -6.0f));
    //Noga stola: okomit komad uz rub, koji udje u pravokutnik
    for(int i = 0; i < 150; ++i) points.positions.push_back(tableCentre + u * 0.95f + glm::vec3(0.0f, -0.3f * (random() + 1.0f), 0.0f));
    stage.get(cloud)->points = points;

    //Kamera sprijeda i odozgo
    Loom::ViewportState state;
    state.orbit.target = tableCentre;
    state.orbit.distance = 4.0f;
    state.orbit.yaw = 0.2f;
    state.orbit.pitch = 0.6f;
    const Treadle::Rect viewport{0.0f, 0.0f, 800.0f, 600.0f};
    const Loom::ViewCamera camera = Loom::viewCameraFor(stage, 1.0, viewport, state);

    //Pravokutnik oko stola
    glm::vec2 low(1e9f), high(-1e9f);
    for(float a : {-1.0f, 1.0f}) for(float b : {-1.0f, 1.0f}){
        glm::vec2 p;
        Loom::project(camera, tableCentre + u * a + v * b * 0.4f, p);
        low = glm::min(low, p);
        high = glm::max(high, p);
    }
    const Treadle::Rect rect{low.x, low.y, high.x - low.x, high.y - low.y};

    //-- 1. prva ploha: zid iza ne ulazi ----------------------------------------------------------
    const std::vector<glm::vec3> selected = Loom::selectFrontPoints(stage, 1.0, camera, rect);
    size_t wall = 0;
    for(const glm::vec3& p : selected) if(p.z < -5.0f) ++wall;
    report.check("odabir uzme stol, a ne zid iza njega", selected.size() > 2000 && wall == 0,
        fmt("%zu tocaka, od toga sa zida %zu", selected.size(), wall));

    //-- 2. ravnina: normala tocna, i uz nogu stola u odabiru ---------------------------------------
    const Loom::SurfaceFit fit = Loom::fitSurface(selected, camera.eye);
    const float normalError = glm::degrees(std::acos(std::min(1.0f, glm::dot(fit.normal, trueNormal))));
    const float axisError = glm::degrees(std::acos(std::min(1.0f, std::fabs(glm::dot(fit.tangent, u)))));
    report.check("normala plohe tocna i gleda prema kameri (noga stola ne nagne)", fit.valid && normalError < 0.5f,
        fmt("promasaj %.3f st, u ravnini %zu od %zu", normalError, fit.used, fit.total));
    //Isti racun na SAMIM tockama stola: koliko os promasi sam uzorak, bez odabira
    const std::vector<glm::vec3> tableOnly(points.positions.begin(), points.positions.begin() + 3000);
    const Loom::SurfaceFit reference = Loom::fitSurface(tableOnly, camera.eye);
    const float referenceError = glm::degrees(std::acos(std::min(1.0f, std::fabs(glm::dot(reference.tangent, u)))));
    report.check("duza os plohe = duza strana stola", axisError < 2.0f && std::fabs(fit.extent - 1.15f) < 0.15f,
        fmt("promasaj osi %.2f st (sam stol %.2f), polovica duljine %.2f (stol 1.0)", axisError, referenceError, fit.extent));

    //Bez odbacivanja bi noga nagnula ravninu - negativna kontrola da odbacivanje nesto radi
    {
        std::vector<glm::vec3> withLeg = selected;
        glm::dvec3 mean(0.0);
        for(const glm::vec3& p : withLeg) mean += glm::dvec3(p);
        mean /= double(withLeg.size());
        double m[3][3] = {{0.0}};
        for(const glm::vec3& p : withLeg){ const glm::dvec3 d = glm::dvec3(p) - mean; for(int r = 0; r < 3; ++r) for(int c = 0; c < 3; ++c) m[r][c] += d[r] * d[c]; }
        double values[3], vectors[3][3];
        Engine::symmetricEigen3(m, values, vectors);
        const glm::vec3 naive = glm::normalize(glm::vec3(float(vectors[0][2]), float(vectors[1][2]), float(vectors[2][2])));
        const float naiveError = glm::degrees(std::acos(std::min(1.0f, std::fabs(glm::dot(naive, trueNormal)))));
        report.check("bez odbacivanja noga stola nagne normalu (kontrola)", naiveError > normalError * 3.0f,
            fmt("obican PCA promasi %.3f st, otporni %.3f st", naiveError, normalError));
    }

    //-- 3. kocka lezi na plohi ----------------------------------------------------------------------
    {
        const float size = 0.2f;
        const Warp::Transform t = Loom::onSurface(fit, size, Warp::Shape::Cube);
        const glm::vec3 up = t.rotation * glm::vec3(0.0f, 1.0f, 0.0f);
        //Donja ploha kocke (lokalni y = -0.5) mora lezati u ravnini stola
        float worst = 0.0f;
        for(float a : {-0.5f, 0.5f}) for(float b : {-0.5f, 0.5f}){
            const glm::vec3 corner = t.translation + t.rotation * (glm::vec3(a, -0.5f, b) * size);
            worst = std::max(worst, std::fabs(glm::dot(corner - tableCentre, trueNormal)));
        }
        report.check("kocka: gore je normala, donja ploha lezi na stolu",
            glm::degrees(std::acos(std::min(1.0f, glm::dot(up, trueNormal)))) < 0.5f && worst < 0.005f,
            fmt("donji vrhovi od stola najvise %.4f", worst));
    }

    //-- 4. zid: panel uz njega, uspravan, i kad ispred lebde gaussiani ---------------------------------
    //Zid okrenut 20 st oko svjetskog gore, s komadom poda u dnu odabira i oblakom odbjeglih tocaka
    //ispred (40 % broja tocaka zida - toliko ih splat bez maske ima ispred teksturiranog zida).
    //Panel mora: lezati u zidu, imati okomite rubove (lokalni -Z = gore), i pokriti odabrani komad
    {
        const glm::quat turn = glm::angleAxis(glm::radians(20.0f), glm::vec3(0.0f, 1.0f, 0.0f));
        const glm::vec3 wallNormal = turn * glm::vec3(0.0f, 0.0f, 1.0f);
        const glm::vec3 along = turn * glm::vec3(1.0f, 0.0f, 0.0f);
        const glm::vec3 wallBase(0.0f, 0.0f, -3.0f);
        Warp::Stage room;
        Warp::Points cloud;
        for(int i = 0; i < 6000; ++i){
            cloud.positions.push_back(wallBase + along * random() * 2.0f + glm::vec3(0.0f, 1.2f + random() * 1.2f, 0.0f) + wallNormal * random() * 0.003f);
        }
        for(int i = 0; i < 1500; ++i){
            const glm::vec3 p = wallBase + along * random() * 2.0f + wallNormal * (random() + 1.0f) * 0.8f;
            cloud.positions.push_back(glm::vec3(p.x, 0.0f, p.z));
        }
        const glm::vec3 eye = wallBase + wallNormal * 3.0f + along * 1.0f + glm::vec3(0.0f, 1.3f, 0.0f);
        for(int i = 0; i < 2400; ++i){
            const glm::vec3 onWall = wallBase + along * random() * 1.2f + glm::vec3(0.0f, 1.2f + random() * 0.7f, 0.0f);
            cloud.positions.push_back(glm::mix(onWall, eye, 0.05f + 0.45f * (random() + 1.0f) * 0.5f));
        }
        room.get(room.create("Tocke"))->points = cloud;

        Loom::ViewportState look;
        look.orbit.target = wallBase + glm::vec3(0.0f, 1.2f, 0.0f);
        look.orbit.distance = glm::length(eye - look.orbit.target);
        const glm::vec3 d = glm::normalize(eye - look.orbit.target);
        look.orbit.yaw = std::atan2(d.x, d.z);
        look.orbit.pitch = std::asin(d.y);
        const Loom::ViewCamera wallCamera = Loom::viewCameraFor(room, 1.0, viewport, look);
        //Odabir: komad zida od dna do 1.9 visine, i ispod dna, da zahvati pod
        glm::vec2 a(1e9f), b(-1e9f);
        for(float s : {-1.0f, 1.0f}) for(float h : {-0.25f, 1.9f}){
            glm::vec2 p;
            Loom::project(wallCamera, wallBase + along * s + glm::vec3(0.0f, h, 0.0f), p);
            a = glm::min(a, p);
            b = glm::max(b, p);
        }
        const Treadle::Rect wallRect{a.x, a.y, b.x - a.x, b.y - a.y};
        const std::vector<glm::vec3> picked = Loom::selectFrontPoints(room, 1.0, wallCamera, wallRect);
        const Loom::SurfaceFit wall = Loom::fitSurface(picked, wallCamera.eye);
        const float wallError = glm::degrees(std::acos(std::min(1.0f, glm::dot(wall.normal, wallNormal))));
        report.check("zid: normala tocna i uz odbjegle tocke ispred i pod u odabiru", wall.valid && wall.wall && wallError < 0.5f,
            fmt("promasaj %.3f st, u ravnini %zu od %zu", wallError, wall.used, wall.total));

        const Warp::Transform panel = Loom::panelOnSurface(wall);
        const glm::vec3 panelUp = panel.rotation * glm::vec3(0.0f, 0.0f, -1.0f);
        const glm::vec3 panelSide = panel.rotation * glm::vec3(1.0f, 0.0f, 0.0f);
        const float tilt = glm::degrees(std::asin(std::min(1.0f, std::fabs(panelSide.y))));
        float off = 0.0f;
        for(float s : {-0.5f, 0.5f}) for(float t : {-0.5f, 0.5f}){
            const glm::vec3 corner = panel.translation + panel.rotation * (glm::vec3(s, 0.0f, t) * panel.scale);
            off = std::max(off, std::fabs(glm::dot(corner - wallBase, wallNormal)));
        }
        report.check("panel na zidu: uspravan, vodoravni rubovi, lezi u zidu", panelUp.y > 0.999f && tilt < 0.3f && off < 0.01f,
            fmt("gore %.4f, nagib ruba %.3f st, kut panela od zida %.4f", panelUp.y, tilt, off));
        //Zid u odabiru je od dna (0) do oko 1.9, siri od 2 (perspektiva); pod ne smije napuhati visinu
        report.check("panel pokrije odabrani komad zida", panel.scale.x > 1.9f && panel.scale.x < 2.8f &&
                     panel.scale.z > 1.7f && panel.scale.z < 2.2f,
            fmt("%.2f x %.2f", panel.scale.x, panel.scale.z));
    }

    //-- 5. premalo tocaka: nema plohe ------------------------------------------------------------------
    {
        const Loom::SurfaceFit none = Loom::fitSurface({glm::vec3(0.0f), glm::vec3(1.0f, 0.0f, 0.0f)}, glm::vec3(0.0f, 1.0f, 0.0f));
        const Loom::SurfaceFit line = Loom::fitSurface({glm::vec3(0.0f), glm::vec3(1.0f, 0.0f, 0.0f), glm::vec3(2.0f, 0.0f, 0.0f),
                                                        glm::vec3(3.0f, 0.0f, 0.0f)}, glm::vec3(0.0f, 1.0f, 0.0f));
        report.check("dvije tocke ili pravac ne odredjuju plohu", !none.valid && !line.valid, "odbijeno");
    }

    return report.result();
}
