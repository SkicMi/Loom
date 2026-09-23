#pragma once
//=============================================================================================
// POVRSINA IZ ODABRANIH TOCAKA: pravokutnik u pogledu -> tocke oblaka i sredista gaussiana u
// njemu -> ravnina kroz njih -> kocka koja stoji na toj plohi.
//
// Zid, stol, stepenica u sceni iz snimke nisu nigdje zapisani kao ploha - postoje samo kao oblak.
// Da kocka stoji NA njima (a ne pod kutom kroz njih), mora znati njihovu normalu. Umjetnik oznaci
// komad plohe, editor kroz njega provuce ravninu i kocku postavi na nju.
//
// PRAVOKUTNIK GLEDA DUZ POGLEDA, pa bi uz zid obuhvatio i sve iza njega. Zato se uzima samo PRVA
// ploha po dubini: od najblizih tocaka (5. percentil, da jedna odbjegla tocka ispred ne odredi
// "najblize") dalje, sve do prvog SKOKA u dubini - praznine vece od 5 % dubine i deset puta vece
// od uobicajenog razmaka medju susjednim dubinama. Fiksni prozor (prva verzija: 20 %) je odrezao
// daleki kraj stola gledanog ukoso i nagnuo mu os za 2.5 st; ploha iza je od prve odvojena
// prazninom, a sama ploha nije.
//
// RAVNINA JE OTPORNA: kroz sve tocke PCA (najmanja os kovarijance je normala), pa se odbace tocke
// dalje od ravnine od 2.5 medijana udaljenosti i ravnina se provuce iznova. Rub stola ili
// susjedni zid koji udje u pravokutnik tako ne nagne plohu. Normala gleda prema kameri - ploha
// koja se vidi je strana na koju se stavlja.
//
// KOCKA: lokalni +Y po normali, lokalni +X po najduljoj osi plohe (kocka prati smjer komada, npr.
// ruba stola), srediste pola velicine iznad plohe - lezi na njoj
//=============================================================================================
#include "LoomViewport.h"

#include <Engine/Dense.h>
#include <Warp/Stage.h>

#include <algorithm>
#include <cmath>
#include <functional>
#include <vector>

namespace Loom{

struct SurfaceFit{
    bool valid = false;
    glm::vec3 centre{0.0f};
    glm::vec3 normal{0.0f, 1.0f, 0.0f};
    glm::vec3 tangent{1.0f, 0.0f, 0.0f};    //najdulja os plohe
    float extent = 0.0f;                    //polovica duljine plohe po najduljoj osi (dvije sigme)
    float thickness = 0.0f;                 //koliko tocke odstupaju od ravnine (sigma)
    size_t used = 0, total = 0;             //tocke u ravnini / odabrane
};

namespace surface{

inline void pca(const std::vector<glm::vec3>& points, const std::vector<uint8_t>& keep, glm::vec3& centre,
                double values[3], double vectors[3][3]){
    glm::dvec3 sum(0.0);
    size_t n = 0;
    for(size_t i = 0; i < points.size(); ++i) if(keep[i]){ sum += glm::dvec3(points[i]); ++n; }
    const glm::dvec3 mean = sum / double(std::max<size_t>(1, n));
    double m[3][3] = {{0.0}};
    for(size_t i = 0; i < points.size(); ++i){
        if(!keep[i]) continue;
        const glm::dvec3 d = glm::dvec3(points[i]) - mean;
        for(int r = 0; r < 3; ++r) for(int c = 0; c < 3; ++c) m[r][c] += d[r] * d[c];
    }
    for(int r = 0; r < 3; ++r) for(int c = 0; c < 3; ++c) m[r][c] /= double(std::max<size_t>(1, n));
    Engine::symmetricEigen3(m, values, vectors);         //silazno, vektori u stupcima
    centre = glm::vec3(mean);
}

inline glm::vec3 column(const double vectors[3][3], int c){
    return glm::normalize(glm::vec3(float(vectors[0][c]), float(vectors[1][c]), float(vectors[2][c])));
}

}

//Ravnina kroz tocke; viewer je mjesto kamere (normala se okrene prema njoj)
inline SurfaceFit fitSurface(const std::vector<glm::vec3>& points, glm::vec3 viewer){
    SurfaceFit fit;
    fit.total = points.size();
    if(points.size() < 3) return fit;
    std::vector<uint8_t> keep(points.size(), 1);
    double values[3], vectors[3][3];
    glm::vec3 centre;
    surface::pca(points, keep, centre, values, vectors);
    glm::vec3 normal = surface::column(vectors, 2);

    //Odbacivanje: dalje od ravnine od 2.5 medijana udaljenosti, pa ravnina iznova - TRI PUTA, i
    //svaki put preko SVIH tocaka. Prva ravnina je nagnuta onim sto se odbacuje (noga stola), pa
    //jedan prolaz izbaci i dio prave plohe na jednom kraju; ostatak je asimetrican i nagne os
    //(izmjereno: 2.1 st umjesto 0.2). Idući prolazi te tocke vrate
    size_t used = points.size();
    for(int pass = 0; pass < 3; ++pass){
        std::vector<float> distances(points.size());
        for(size_t i = 0; i < points.size(); ++i) distances[i] = std::fabs(glm::dot(points[i] - centre, normal));
        std::vector<float> sorted = distances;
        std::nth_element(sorted.begin(), sorted.begin() + long(sorted.size() / 2), sorted.end());
        const float limit = std::max(2.5f * sorted[sorted.size() / 2], 1e-7f);
        size_t kept = 0;
        for(size_t i = 0; i < points.size(); ++i){ keep[i] = distances[i] <= limit; kept += keep[i]; }
        if(kept < 3) break;
        used = kept;
        surface::pca(points, keep, centre, values, vectors);
        normal = surface::column(vectors, 2);
    }

    if(glm::dot(normal, viewer - centre) < 0.0f) normal = -normal;
    glm::vec3 tangent = surface::column(vectors, 0);
    tangent = glm::normalize(tangent - normal * glm::dot(tangent, normal));

    fit.valid = values[1] > 1e-12;          //tocke na pravcu ne odredjuju ravninu
    fit.centre = centre;
    fit.normal = normal;
    fit.tangent = tangent;
    fit.extent = 2.0f * float(std::sqrt(std::max(0.0, values[0])));
    fit.thickness = float(std::sqrt(std::max(0.0, values[2])));
    fit.used = used;
    return fit;
}

//Rotacija kojoj je lokalni +Y normala plohe, a lokalni +X njezina najdulja os
inline glm::quat surfaceRotation(const SurfaceFit& fit){
    const glm::vec3 y = fit.normal;
    const glm::vec3 x = fit.tangent;
    const glm::vec3 z = glm::normalize(glm::cross(x, y));
    return glm::normalize(glm::quat_cast(glm::mat3(x, y, z)));
}

//Tijelo velicine size (jedinicna kocka je 1) koje lezi na plohi. U svijetu
inline Warp::Transform onSurface(const SurfaceFit& fit, float size, Warp::Shape shape){
    Warp::Transform t;
    t.rotation = surfaceRotation(fit);
    t.scale = glm::vec3(size);
    t.translation = fit.centre + fit.normal * (shape == Warp::Shape::Cube ? 0.5f * size : 0.0f);
    return t;
}

//Tocke u pravokutniku prozora (rect), samo prva ploha po dubini. extra: dodatne tocke u svijetu
//(sredista gaussiana), predane kao funkcija koja ih nabraja
inline std::vector<glm::vec3> selectFrontPoints(const Warp::Stage& stage, double frame, const ViewCamera& camera,
                                                const Treadle::Rect& rect, float depthWindow = 0.05f,
                                                const std::function<void(const std::function<void(const glm::vec3&)>&)>& extra = {}){
    std::vector<std::pair<float, glm::vec3>> inside;
    auto consider = [&](const glm::vec3& world){
        glm::vec2 pixel;
        float depth = 0.0f;
        if(!project(camera, world, pixel, &depth)) return;
        if(rect.contains(pixel.x, pixel.y)) inside.push_back({depth, world});
    };
    stage.walk([&](const Warp::Entity& entity, int){
        if(!entity.visible || !entity.points) return;
        const glm::mat4 world = stage.worldMatrix(entity.id, frame);
        for(const glm::vec3& p : entity.points->positions) consider(glm::vec3(world * glm::vec4(p, 1.0f)));
    });
    if(extra) extra(consider);
    if(inside.empty()) return {};

    std::sort(inside.begin(), inside.end(), [](const auto& a, const auto& b){ return a.first < b.first; });
    std::vector<float> gaps;
    for(size_t i = 1; i < inside.size(); ++i) gaps.push_back(inside[i].first - inside[i - 1].first);
    float typical = 0.0f;
    if(!gaps.empty()){
        std::vector<float> sorted = gaps;
        std::nth_element(sorted.begin(), sorted.begin() + long(sorted.size() / 2), sorted.end());
        typical = sorted[sorted.size() / 2];
    }
    const size_t start = inside.size() / 20;
    size_t end = inside.size();
    for(size_t i = start; i + 1 < inside.size(); ++i){
        const float gap = inside[i + 1].first - inside[i].first;
        if(gap > depthWindow * inside[i].first && gap > 10.0f * typical){ end = i + 1; break; }
    }
    std::vector<glm::vec3> out;
    for(size_t i = 0; i < end; ++i) out.push_back(inside[i].second);
    return out;
}

}
