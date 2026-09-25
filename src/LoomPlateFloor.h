#pragma once
//=============================================================================================
// LIK NA PODU SNIMKE: pod iz riješenog oblaka, metar iz visine kamere, i koliko stopalo klizi PO
// SNIMCI kroz riješenu kameru.
//
// ZASTO. Lik iz Kimoda zna samo svoj pod (y = 0) i metre. Scena iz solvea ima pod negdje u oblaku
// i proizvoljno mjerilo. Dok se to dvoje ne spoji, lik u kadru stoji malo iznad ili ispod poda
// snimke, a velicina mu je nagadjanje - i to je prvo sto oko vidi na kompozitu.
//
//   POD        najnizi gusti vodoravni sloj tocaka blizu lika. "5 % najnizih" (LoomScene.h) je
//              dovoljno za mrezu, ali ne za stopalo: tocke ispod poda (odsjaj, sum triangulacije)
//              ga spuste, a stol blizu kamere ga digne. Ovdje se trazi VRH histograma visina:
//              pod je ploha s mnogo tocaka, ne najniza tocka
//   METAR      solve ne zna koliko je metar. Zna koliko je kamera iznad poda; snimatelj iz ruke
//              drzi kameru na ~1.5 m (isti prior koji uvoz pokreta vec koristi). Visina se da
//              promijeniti - stativ, dron, nizak kut
//   ZAKLJUCANOST  stopalo koje stoji mora stajati NA SNIMCI. Mjeri se u pikselima snimke: za svaki
//              dodir se polozaj prsta u svakom kadru projicira kroz riješenu kameru tog kadra i
//              usporedi s projekcijom polozaja na pocetku dodira. Pokret kamere se tako ponisti
//              (nepomicna tocka ima nulu), a ostane samo klizanje stopala po plohi. Kriterij je 1 px
//=============================================================================================
#include <Warp/Stage.h>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

namespace Loom{

struct PlateFloor{
    bool valid = false;
    float height = 0.0f;            //y poda u svijetu
    float spread = 0.0f;            //raspon visina tocaka poda (MAD), u jedinicama scene
    size_t support = 0;             //tocaka na podu
    Warp::Id camera = Warp::None;   //riješena kamera iznad tog poda
    float cameraHeight = 0.0f;      //medijan visine kamere nad podom, u jedinicama scene
    std::string problem;
};

namespace detail{

inline float medianOf(std::vector<float> values){
    if(values.empty()) return 0.0f;
    std::nth_element(values.begin(), values.begin() + std::ptrdiff_t(values.size() / 2), values.end());
    return values[values.size() / 2];
}

inline std::string lower(std::string text){
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c){ return char(std::tolower(c)); });
    return text;
}

}

//Pod ispod tocke nearXZ (u svijetu). radius <= 0: cijela scena
inline PlateFloor findPlateFloor(const Warp::Stage& stage, double frame, glm::vec2 nearXZ = glm::vec2(0.0f),
                                 float radius = 0.0f){
    PlateFloor floor;
    std::vector<glm::vec3> points;
    Warp::Id camera = Warp::None;
    size_t cameraKeys = 0;
    stage.walk([&](const Warp::Entity& entity, int){
        if(entity.points){
            const glm::mat4 world = stage.worldMatrix(entity.id, frame);
            for(const glm::vec3& p : entity.points->positions) points.push_back(glm::vec3(world * glm::vec4(p, 1.0f)));
        }
        //Riješena kamera je ona s najvise kljuceva; rucno dodana ima jedan ili nijedan
        if(entity.camera && entity.translationKeys.size() > cameraKeys){
            camera = entity.id;
            cameraKeys = entity.translationKeys.size();
        }
    });
    if(points.size() < 50){
        floor.problem = "The scene has no solved point cloud to find a floor in.";
        return floor;
    }
    //Blizu lika ako tamo ima dovoljno tocaka; pod se kroz scenu smije malo nagnuti ili stepenicom
    if(radius > 0.0f){
        std::vector<glm::vec3> near;
        for(const glm::vec3& p : points)
            if(glm::length(glm::vec2(p.x, p.z) - nearXZ) <= radius) near.push_back(p);
        if(near.size() >= 200) points.swap(near);
    }

    std::vector<float> heights;
    heights.reserve(points.size());
    for(const glm::vec3& p : points) heights.push_back(p.y);
    std::sort(heights.begin(), heights.end());
    const float low = heights[heights.size() / 100], high = heights[heights.size() * 95 / 100];
    const float range = std::max(high - low, 1e-6f);
    //Pretinci od 1 % raspona: pod iz solvea ima debljinu od sumu triangulacije, oko 0.5-1 % scene
    const int bins = 100;
    std::vector<int> count(bins, 0);
    for(float h : heights){
        const int bin = int((h - low) / range * float(bins));
        if(bin >= 0 && bin < bins) ++count[size_t(bin)];
    }
    //Zagladjeno preko pet pretinaca: rijedak oblak (C0256 ima 1767 tocaka, 17 po pretincu) inace
    //ima sumne vrhove na dnu, i prvi pokusaj je bas takav uzeo za pod (59 tocaka, 28 cm ispod pravog)
    std::vector<float> smooth(bins, 0.0f);
    static constexpr float kernel[5] = {0.1f, 0.2f, 0.4f, 0.2f, 0.1f};
    for(int b = 0; b < bins; ++b)
        for(int k = -2; k <= 2; ++k)
            if(b + k >= 0 && b + k < bins) smooth[size_t(b)] += float(count[size_t(b + k)]) * kernel[k + 2];
    std::vector<float> sorted = smooth;
    const float typical = std::max(1.0f, detail::medianOf(sorted));
    //Najjaci sloj u donjoj polovici - pa najnizi sloj koji mu je barem napola jednak (pod ispod
    //stola). Sloj mora biti barem dvaput gusci od medijana visina: scena koja je sva zid (C0257,
    //kameni zid) ima visine razmazane jednoliko i omjer 1.8 - ondje poda NEMA, i to treba reci
    //umjesto da se izabere slucajan pojas
    int strongest = 0;
    for(int b = 1; b < bins / 2; ++b) if(smooth[size_t(b)] > smooth[size_t(strongest)]) strongest = b;
    if(smooth[size_t(strongest)] < 2.0f * typical){
        floor.problem = "No clear floor in the point cloud (the scene looks like walls or objects only).";
        return floor;
    }
    int chosen = strongest;
    for(int b = 0; b < strongest; ++b){
        const bool localPeak = (b == 0 || smooth[size_t(b)] >= smooth[size_t(b - 1)]) && smooth[size_t(b)] >= smooth[size_t(b + 1)];
        if(localPeak && smooth[size_t(b)] >= 0.5f * smooth[size_t(strongest)] && smooth[size_t(b)] >= 2.0f * typical){ chosen = b; break; }
    }
    const float binLow = low + range * float(chosen - 1) / float(bins);
    const float binHigh = low + range * float(chosen + 2) / float(bins);
    std::vector<float> slab;
    for(float h : heights) if(h >= binLow && h <= binHigh) slab.push_back(h);
    floor.height = detail::medianOf(slab);
    std::vector<float> deviation;
    for(float h : slab) deviation.push_back(std::fabs(h - floor.height));
    floor.spread = detail::medianOf(deviation);
    floor.support = slab.size();
    floor.valid = true;

    if(camera != Warp::None){
        const Warp::Entity* entity = stage.get(camera);
        std::vector<float> above;
        for(double time : entity->translationKeys.times)
            above.push_back(stage.worldMatrix(camera, time)[3].y - floor.height);
        floor.camera = camera;
        floor.cameraHeight = detail::medianOf(above);
    }
    return floor;
}

//Pod iz cijele scene, doradjen blizu lika: medijan tocaka unutar dvije visine kamere vodoravno i
//+-5 % visine kamere okomito od globalnog poda. Prvi pokusaj je blizu lika trazio sloj IZNOVA i na
//C0256 uhvatio hrpu tocaka iznad kamere (49 tocaka) - pa je mjerilo ispalo nepoznato. Dorada smije
//pomaknuti pod samo za debljinu sloja, ne izabrati drugi
inline PlateFloor refinePlateFloorNear(const Warp::Stage& stage, double frame, PlateFloor floor, glm::vec2 nearXZ){
    if(!floor.valid || floor.cameraHeight <= 0.0f) return floor;
    const float radius = 2.0f * floor.cameraHeight, band = 0.05f * floor.cameraHeight;
    std::vector<float> near;
    stage.walk([&](const Warp::Entity& entity, int){
        if(!entity.points) return;
        const glm::mat4 world = stage.worldMatrix(entity.id, frame);
        for(const glm::vec3& p : entity.points->positions){
            const glm::vec3 w = glm::vec3(world * glm::vec4(p, 1.0f));
            if(std::fabs(w.y - floor.height) <= band && glm::length(glm::vec2(w.x, w.z) - nearXZ) <= radius) near.push_back(w.y);
        }
    });
    if(near.size() < 30) return floor;
    const float refined = detail::medianOf(near);
    floor.cameraHeight += floor.height - refined;
    floor.height = refined;
    std::vector<float> deviation;
    for(float h : near) deviation.push_back(std::fabs(h - refined));
    floor.spread = detail::medianOf(deviation);
    floor.support = near.size();
    return floor;
}

//Koliko jedinica scene je jedan metar, iz visine kamere nad podom i pretpostavljene visine
//snimatelja. 0 kad kamera nije iznad poda
inline float plateUnitsPerMetre(const PlateFloor& floor, float cameraHeightMetres){
    if(!floor.valid || floor.camera == Warp::None || floor.cameraHeight <= 0.0f || cameraHeightMetres <= 0.0f) return 0.0f;
    return floor.cameraHeight / cameraHeightMetres;
}

//Cvor preko kojeg se lik postavlja: grupa modela (LoomModel.h - "mjesto i mjerilo modela u sceni"),
//ne sam rig. Rig nosi root motion u Animatoru, pa bi pomak postavljen njemu pregazio prvi kadar
//klipa - prvi pokusaj je lik smanjio, ali ga nije spustio. Ide se prema gore dokle god predak ne
//sadrzi i nesto sto nije lik (oblak ili kameru solvea)
inline Warp::Id characterPlacementRoot(const Warp::Stage& stage, Warp::Id rig){
    auto holdsScene = [&](Warp::Id id){
        std::vector<Warp::Id> pending{id};
        while(!pending.empty()){
            const Warp::Entity* entity = stage.get(pending.back());
            pending.pop_back();
            if(!entity) continue;
            if(entity->points || entity->camera || entity->splat) return true;
            pending.insert(pending.end(), entity->children.begin(), entity->children.end());
        }
        return false;
    };
    Warp::Id placement = rig;
    for(const Warp::Entity* entity = stage.get(rig); entity && entity->parent != Warp::None;){
        if(holdsScene(entity->parent)) break;
        placement = entity->parent;
        entity = stage.get(placement);
    }
    return placement;
}

//Postavi lik na pod snimke u pravom mjerilu. Lik je u metrima (glTF, Kimodo); vodoravni polozaj
//ostaje gdje jest. Vraca false kad pod ili mjerilo nisu poznati
inline bool standOnPlateFloor(Warp::Stage& stage, Warp::Id rig, double frame, const PlateFloor& floor,
                              float cameraHeightMetres, std::string* problem = nullptr){
    const Warp::Id character = stage.contains(rig) ? characterPlacementRoot(stage, rig) : Warp::None;
    Warp::Entity* entity = stage.get(character);
    const float unitsPerMetre = plateUnitsPerMetre(floor, cameraHeightMetres);
    if(!entity || unitsPerMetre <= 0.0f){
        if(problem) *problem = !entity ? "No character selected." : floor.problem.empty()
            ? "The solved camera is not above the floor, so the scene scale is unknown." : floor.problem;
        return false;
    }
    const glm::mat4 parentWorld = entity->parent != Warp::None ? stage.worldMatrix(entity->parent, frame) : glm::mat4(1.0f);
    const glm::vec3 current = glm::vec3(stage.worldMatrix(character, frame)[3]);
    const glm::vec3 wanted(current.x, floor.height, current.z);
    //Roditelj (grupa solvea) smije biti zakrenut i skaliran; lik se postavlja u SVIJETU pa se
    //vrati u roditeljev sustav. Mjerilo roditelja se podijeli, da lik u svijetu ima tocno metar
    const float parentScale = std::cbrt(std::fabs(glm::determinant(glm::mat3(parentWorld))));
    Warp::Transform local = stage.localAt(character, frame);
    local.translation = glm::vec3(glm::inverse(parentWorld) * glm::vec4(wanted, 1.0f));
    local.scale = glm::vec3(unitsPerMetre / std::max(parentScale, 1e-6f));
    entity->local = local;
    //Kljucevi pomaka/mjerila na liku bi pregazili novi polozaj u sljedecem kadru
    entity->translationKeys = {};
    entity->scaleKeys = {};
    return true;
}

struct PlateLockReport{
    bool valid = false;
    size_t contacts = 0;            //dodira stopala dovoljno dugih za mjeru (3+ kadra)
    float medianPixels = 0.0f;      //medijan najveceg klizanja po dodiru, u pikselima snimke
    float worstPixels = 0.0f;
    std::string problem;
};

//Prsti lika: zglobovi cije ime sadrzi "toe" ili "ball" (Unreal, Mixamo, Kimodo), bez krajnjih
inline std::vector<Warp::Id> characterToes(const Warp::Stage& stage, Warp::Id character){
    std::vector<Warp::Id> toes, pending{character};
    while(!pending.empty()){
        const Warp::Entity* entity = stage.get(pending.back());
        pending.pop_back();
        if(!entity) continue;
        const std::string name = detail::lower(entity->name);
        if(entity->joint && (name.find("toe") != std::string::npos || name.find("ball") != std::string::npos) &&
           name.find("end") == std::string::npos && name.find("ik") == std::string::npos) toes.push_back(entity->id);
        pending.insert(pending.end(), entity->children.begin(), entity->children.end());
    }
    return toes;
}

inline PlateLockReport measurePlateLock(const Warp::Stage& stage, Warp::Id character, const PlateFloor& floor,
                                        double firstFrame, double lastFrame, float contactHeight){
    PlateLockReport report;
    const Warp::Entity* cameraEntity = floor.camera != Warp::None ? stage.get(floor.camera) : nullptr;
    if(!floor.valid || !cameraEntity || !cameraEntity->camera){
        report.problem = "Needs a solved camera and floor.";
        return report;
    }
    const std::vector<Warp::Id> toes = characterToes(stage, character);
    if(toes.empty()){
        report.problem = "The character has no toe joints.";
        return report;
    }
    const Warp::Camera& lens = *cameraEntity->camera;
    auto project = [&](const glm::mat4& worldToCamera, const glm::vec3& world, glm::vec2& pixel){
        const glm::vec3 p = glm::vec3(worldToCamera * glm::vec4(world, 1.0f));
        if(p.z > -1e-6f) return false;
        pixel = glm::vec2(lens.centreX + lens.focalPixels * p.x / -p.z, lens.centreY - lens.focalPixels * p.y / -p.z);
        return true;
    };
    const int first = int(std::ceil(firstFrame)), last = int(std::floor(lastFrame));
    std::vector<float> slides;
    for(Warp::Id toe : toes){
        //Visina mirovanja prsta: najniza visina kroz klip (zglob lezi iznad tabana)
        float rest = 1e9f;
        for(int f = first; f <= last; ++f) rest = std::min(rest, stage.worldMatrix(toe, double(f))[3].y - floor.height);
        int start = -1;
        glm::vec3 anchor(0.0f);
        float worst = 0.0f;
        auto close = [&](int end){
            if(start >= 0 && end - start >= 3) slides.push_back(worst);
            start = -1;
            worst = 0.0f;
        };
        for(int f = first; f <= last; ++f){
            const glm::vec3 at = glm::vec3(stage.worldMatrix(toe, double(f))[3]);
            const bool contact = at.y - floor.height - rest < contactHeight;
            if(!contact){ close(f); continue; }
            if(start < 0){ start = f; anchor = at; continue; }
            const glm::mat4 worldToCamera = glm::inverse(stage.worldMatrix(floor.camera, double(f)));
            glm::vec2 now, planted;
            if(project(worldToCamera, at, now) && project(worldToCamera, anchor, planted))
                worst = std::max(worst, glm::length(now - planted));
        }
        close(last + 1);
    }
    if(slides.empty()){
        report.problem = "No foot contacts in this range.";
        return report;
    }
    report.contacts = slides.size();
    report.medianPixels = detail::medianOf(slides);
    report.worstPixels = *std::max_element(slides.begin(), slides.end());
    report.valid = true;
    return report;
}

}

namespace Loom{

//Sto editor drzi izmedju kadrova: pod ispod lika i zakljucanost njegovog klipa. Racuna se kad se
//scena promijeni (Stage::fingerprint) i najvise jednom u sekundi - oblak solvea ima stotine
//tisuca tocaka, a zakljucanost trazi svijetski polozaj prstiju u svakom kadru klipa
struct PlateFloorWatch{
    PlateFloor floor;
    PlateLockReport lock;
    Warp::Id character = Warp::None;
    uint64_t fingerprint = 0;
    double checkedAt = -1.0;

    void update(const Warp::Stage& stage, double frame, Warp::Id target, double nowSeconds){
        const uint64_t print = stage.fingerprint();
        if(print == fingerprint && target == character && nowSeconds - checkedAt < 5.0) return;
        if(checkedAt >= 0.0 && nowSeconds - checkedAt < 1.0) return;
        fingerprint = print;
        character = target;
        checkedAt = nowSeconds;
        glm::vec2 at(0.0f);
        if(stage.contains(target)){
            const glm::vec4 world = stage.worldMatrix(target, frame)[3];
            at = glm::vec2(world.x, world.z);
        }
        floor = refinePlateFloorNear(stage, frame, findPlateFloor(stage, frame), at);
        lock = PlateLockReport{};
        const Warp::Entity* rig = stage.get(target);
        if(!floor.valid || !rig || !rig->animator || rig->animator->animations.empty()) return;
        const Warp::AnimationClip& clip = rig->animator->animations[std::min(rig->animator->activeAnimation,
                                                                             rig->animator->animations.size() - 1)];
        //Dodir: prst unutar 2.5 cm (u metrima lika) iznad svoje najnize visine - isti prag kao
        //Engine::MotionQuality, da obje mjere broje iste kadrove
        const float metre = std::max(1e-6f, plateUnitsPerMetre(floor, 1.5f));
        const glm::mat4 rigWorld = stage.worldMatrix(target, frame);
        const float rigScale = glm::length(glm::vec3(rigWorld[0]));
        lock = measurePlateLock(stage, target, floor, clip.startFrame, clip.endFrame,
                                0.025f * (rigScale > 0.0f ? rigScale : metre));
    }
};

}
