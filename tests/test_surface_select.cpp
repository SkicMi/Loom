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

    //-- 4. premalo tocaka: nema plohe ------------------------------------------------------------------
    {
        const Loom::SurfaceFit none = Loom::fitSurface({glm::vec3(0.0f), glm::vec3(1.0f, 0.0f, 0.0f)}, glm::vec3(0.0f, 1.0f, 0.0f));
        const Loom::SurfaceFit line = Loom::fitSurface({glm::vec3(0.0f), glm::vec3(1.0f, 0.0f, 0.0f), glm::vec3(2.0f, 0.0f, 0.0f),
                                                        glm::vec3(3.0f, 0.0f, 0.0f)}, glm::vec3(0.0f, 1.0f, 0.0f));
        report.check("dvije tocke ili pravac ne odredjuju plohu", !none.valid && !line.valid, "odbijeno");
    }

    return report.result();
}
