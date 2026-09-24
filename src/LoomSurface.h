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
// ploha po dubini: od najblizih tocaka (skok se trazi tek iza prve petine, da skupina odbjeglih
// gaussiana ispred ne bude "prva ploha") dalje, sve do prvog SKOKA u dubini - praznine vece od 5 % dubine i deset puta vece
// od uobicajenog razmaka medju susjednim dubinama. Fiksni prozor (prva verzija: 20 %) je odrezao
// daleki kraj stola gledanog ukoso i nagnuo mu os za 2.5 st; ploha iza je od prve odvojena
// prazninom, a sama ploha nije.
//
// RAVNINA JE OTPORNA, u dva koraka:
//   1. RANSAC s medijanom (LMedS): ravnine kroz nasumicne trojke tocaka, pobjeduje ona s
//      najmanjim MEDIJANOM udaljenosti. Ne treba prag u metrima (solve ih nema), a podnosi do pola
//      tocaka koje ne pripadaju plohi - gaussiani koji lebde ispred zida, rub stola, susjedni zid.
//      Obicni PCA kroz sve tocke je prvi korak bio prije, i odbjegli gaussiani su ga nagnuli
//   2. od te ravnine: odbace se tocke dalje od 2.5 medijana udaljenosti i ravnina se provuce PCA-om
//      kroz ostale (tri puta). RANSAC nade PRAVU plohu, PCA je tocno poravna po svim njenim tockama
//   Normala gleda prema kameri - ploha koja se vidi je strana na koju se stavlja.
//
// ZAKRET OKO NORMALE:
//   ZID (ploha nagnuta vise od 45 st): lokalni -Z je svjetsko GORE spusteno u plohu, lokalni +X
//        vodoravno uz zid. Panel na zidu tako ima okomite rubove, kao sve sto na zidu visi. Prije
//        je +X isao po najduljoj osi odabira, a na zidu je to dijagonala pravokutnika: panel je
//        stajao ukoso (izmjereno na C0257: 31 st)
//   POD/STOL: lokalni +X po najduljoj osi plohe (kocka prati rub stola) kad je odabir izduzen, a
//        inace okrenut prema kameri (prednja strana kocke gleda u pogled)
//
// VELICINA: pravokutnik odabranih tocaka u tim osima (2. do 98. percentil), i srediste u njegovoj
// sredini. Ravnina na plohu je bas taj komad, kocka stane u njega.
//
// KOCKA: lokalni +Y po normali, srediste pola velicine iznad plohe - lezi na njoj
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
    bool wall = false;                      //strma ploha: zakret po svjetskom gore
    //Pravokutnik tocaka u ravnini: polovice po tangent (lokalni X) i po lokalnom Z, oko centre
    float halfWidth = 0.0f, halfDepth = 0.0f;
    glm::vec3 depthAxis() const{ return glm::normalize(glm::cross(tangent, normal)); }   //lokalni +Z
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

//Ravnina kroz tri tocke; false kad su (skoro) na pravcu
inline bool planeThrough(const glm::vec3& a, const glm::vec3& b, const glm::vec3& c, glm::vec3& normal){
    const glm::vec3 n = glm::cross(b - a, c - a);
    const float length = glm::length(n);
    if(!(length > 1e-12f) || length < 1e-6f * glm::length(b - a) * glm::length(c - a)) return false;
    normal = n / length;
    return true;
}

//LMedS: ravnina kroz nasumicne trojke s najmanjim medijanom udaljenosti. Na najvise 4000 tocaka
//(odabir na gustom splatu ima ih stotine tisuca, a medijan se ne mijenja). Zrno je fiksno: isti
//odabir daje istu plohu
inline bool medianPlane(const std::vector<glm::vec3>& points, glm::vec3& centre, glm::vec3& normal){
    const size_t n = points.size();
    if(n < 3) return false;
    uint32_t seed = 0x9e3779b9u;
    auto next = [&](){ seed ^= seed << 13; seed ^= seed >> 17; seed ^= seed << 5; return seed; };
    std::vector<glm::vec3> sample;
    const size_t stride = std::max<size_t>(1, n / 4000);
    for(size_t i = 0; i < n; i += stride) sample.push_back(points[i]);
    std::vector<float> distances(sample.size());
    float best = INFINITY;
    for(int attempt = 0; attempt < 400; ++attempt){
        const glm::vec3& a = sample[next() % sample.size()];
        const glm::vec3& b = sample[next() % sample.size()];
        const glm::vec3& c = sample[next() % sample.size()];
        glm::vec3 candidate;
        if(!planeThrough(a, b, c, candidate)) continue;
        for(size_t i = 0; i < sample.size(); ++i) distances[i] = std::fabs(glm::dot(sample[i] - a, candidate));
        std::nth_element(distances.begin(), distances.begin() + long(distances.size() / 2), distances.end());
        const float median = distances[distances.size() / 2];
        if(median < best){ best = median; centre = a; normal = candidate; }
    }
    return std::isfinite(best);
}

}

//Ravnina kroz tocke; viewer je mjesto kamere (normala se okrene prema njoj), up svjetsko gore
inline SurfaceFit fitSurface(const std::vector<glm::vec3>& points, glm::vec3 viewer, glm::vec3 up = glm::vec3(0.0f, 1.0f, 0.0f)){
    SurfaceFit fit;
    fit.total = points.size();
    if(points.size() < 3) return fit;
    std::vector<uint8_t> keep(points.size(), 1);
    double values[3], vectors[3][3];
    glm::vec3 centre;
    surface::pca(points, keep, centre, values, vectors);
    glm::vec3 normal = surface::column(vectors, 2);
    if(values[1] <= 1e-12){ fit.centre = centre; return fit; }   //tocke na pravcu ne odredjuju ravninu
    surface::medianPlane(points, centre, normal);

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

    //Zakret oko normale: zid po svjetskom gore, pod po duljoj osi ili prema kameri
    const glm::vec3 longAxis = surface::column(vectors, 0);
    const glm::vec3 upInPlane = up - normal * glm::dot(up, normal);
    glm::vec3 tangent;
    fit.wall = glm::length(upInPlane) > 0.7071f;
    if(fit.wall){
        tangent = glm::cross(glm::normalize(upInPlane), normal);               //lokalni -Z = gore
    }else if(values[0] > 1.5 * values[1]){
        tangent = longAxis;
    }else{
        glm::vec3 towards = viewer - centre;
        towards -= normal * glm::dot(towards, normal);
        tangent = glm::length(towards) > 1e-6f ? glm::cross(normal, glm::normalize(towards)) : longAxis;   //lokalni +Z prema kameri
    }
    tangent = glm::normalize(tangent - normal * glm::dot(tangent, normal));

    //Pravokutnik tocaka u ravnini, u osima tijela
    const glm::vec3 depth = glm::normalize(glm::cross(tangent, normal));
    std::vector<float> along, across;
    for(size_t i = 0; i < points.size(); ++i){
        if(!keep[i]) continue;
        along.push_back(glm::dot(points[i] - centre, tangent));
        across.push_back(glm::dot(points[i] - centre, depth));
    }
    auto span = [](std::vector<float>& v, float& low, float& high){
        const size_t a = v.size() / 50, b = v.size() - 1 - v.size() / 50;
        std::nth_element(v.begin(), v.begin() + long(a), v.end());
        low = v[a];
        std::nth_element(v.begin(), v.begin() + long(b), v.end());
        high = v[b];
    };
    float x0 = 0.0f, x1 = 0.0f, z0 = 0.0f, z1 = 0.0f;
    if(!along.empty()){ span(along, x0, x1); span(across, z0, z1); }

    fit.valid = values[1] > 1e-12;
    fit.centre = centre + tangent * (0.5f * (x0 + x1)) + depth * (0.5f * (z0 + z1));
    fit.normal = normal;
    fit.tangent = tangent;
    fit.halfWidth = 0.5f * (x1 - x0);
    fit.halfDepth = 0.5f * (z1 - z0);
    fit.extent = 2.0f * float(std::sqrt(std::max(0.0, values[0])));
    fit.thickness = float(std::sqrt(std::max(0.0, values[2])));
    fit.used = used;
    return fit;
}

//Rotacija kojoj je lokalni +Y normala plohe, a lokalni +X tangent (na zidu vodoravno)
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

//Ravnina (jedinicna je 1 x 1 u XZ) preko odabranog komada plohe: njegova sirina i visina
inline Warp::Transform panelOnSurface(const SurfaceFit& fit){
    Warp::Transform t;
    t.rotation = surfaceRotation(fit);
    t.scale = glm::vec3(std::max(2.0f * fit.halfWidth, 1e-4f), 1.0f, std::max(2.0f * fit.halfDepth, 1e-4f));
    t.translation = fit.centre;
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
    //Skok se trazi tek iza prve PETINE tocaka: manja skupina ispred praznine nije ploha nego
    //odbjegli gaussiani ispred nje (splat ih ima), i rez bi inace bacio bas plohu. Oni sami
    //ostanu u odabiru, a ravnina ih odbaci
    const size_t start = inside.size() / 5;
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
