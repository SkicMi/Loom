#pragma once
//=============================================================================================
// RENDER IZ KAMERE: most izmedju scene editora (Warp) i LoomTracera.
//
// Pogled editora je raster za provjeru - brz, s dogovorenim svjetlom. Render odgovara na drugo
// pitanje: kako bi taj objekt STVARNO izgledao, snimljen tom kamerom, pod tim nebom, preko te
// snimke. Ovdje se scena u zadanom kadru prevede u ono sto tracer razumije:
//
//   kocke i ravnine    Warp::Mesh -> trokuti, materijal iz biblioteke ili boja tijela
//   glTF modeli        geometrija iz datoteke, kost po kost deformirana u tom kadru (skin)
//   materijali         Warp::Material -> principijelni materijal; mape iz datoteka ili iz GLB-a
//   kamera             rijesena kamera: poza u kadru, zarisna i glavna tocka u pikselima snimke
//   snimka             kadar snimke koji stoji iza tog kadra timelinea (VideoReader)
//   nebo i sunce       fizikalno nebo (Preetham) sa suncem, HDRI ili jedna boja - iz postavki
//
// STO JE STVARNA SCENA, A STO CG. Snimka vec sadrzi pravi pod i zidove; CG objekt mora na njih
// baciti sjenu, ali se oni ne smiju iscrtati preko snimke. Zato su SHADOW CATCHERI (kad je snimka
// iza ili je pozadina prozirna): ravnine (Warp::Shape::Plane), proxy mesh i blokeri iz splata
// (_proxy / _blocker u imenu datoteke) i sve cije ime sadrzi "catcher". Ostalo je CG.
//
// Jedan kadar je jedan Tracer::Scene. Datoteke (glTF, slike, HDRI, snimka) cita RenderAssets i
// cuva izmedju kadrova - sekvenca od 200 kadrova ne dekodira istu 4K teksturu 200 puta.
//
// Ovaj header ne zna za Vulkan: koristi ga i editor (render u pozadinskoj niti, slika se cisti
// pred ocima) i `loom-render` iz terminala (render bez prozora, na stroju bez kartice).
//=============================================================================================
#include <Spool/ExrFile.h>
#include <Spool/GaussianPly.h>
#include <Spool/Gltf.h>
#include <Spool/ImageFile.h>
#include <Spool/VideoFile.h>
#include <Tracer/Denoise.h>
#include <Tracer/Temporal.h>
#include <Tracer/Post.h>
#include <Tracer/Environment.h>
#include <Tracer/Renderer.h>
#include <Warp/Stage.h>

#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cctype>
#include <cmath>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <future>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace Loom{

//---------------------------------------------------------------------------------------------
// POSTAVKE RENDERA - sve sto umjetnik bira u panelu RENDER, i sto `loom-render` prima zastavicama
//---------------------------------------------------------------------------------------------
struct RenderOptions{
    Warp::Id camera = Warp::None;           //None: prva kamera u sceni
    bool sequence = false;                  //false: samo trenutni kadar
    double firstFrame = 1.0, lastFrame = 1.0;
    float resolutionScale = 1.0f;           //udio rezolucije kamere (snimke)

    bool pathTraced = true;                 //false: slika pogleda (raster), samo u editoru
    bool gpu = true;                        //LoomTracer na kartici kad je netko prikljucio GPU pogon
                                            //(editor, loom-render); inace i kao rezerva: procesor

    //Sto se vidi u slici
    bool plate = true;                      //snimka iza CG-a
    bool transparent = false;               //pozadina prozirna (alfa) umjesto neba
    bool shadowCatcher = true;              //ravnine i proxy hvataju sjenu (uz snimku ili alfu)
    bool skyVisible = true;                 //kamera vidi nebo (bez snimke i alfe)
    //HOLDOUT IZ SPLATA: splat scene se projicira kroz kameru u dubinu stvarne scene, pa CG iza
    //stvarnog zida, auta ili stupa nestane i vidi se snimka - bez modeliranja blockera
    bool splatHoldout = false;

    //Dodatni slojevi
    bool depth = true;
    bool normal = false;
    bool albedo = false;

    //Kvaliteta
    uint32_t samples = 128;
    uint32_t maxBounces = 12;
    float indirectClamp = 16.0f;
    //Prag suma za prilagodljivo uzorkovanje (Tracer::RenderSettings::adaptiveThreshold); 0 = svi
    //pikseli dobiju sve uzorke. Mirni dijelovi kadra (nebo, snimka) stanu rano
    float noiseThreshold = 0.01f;
    //Kaustike putanjama (tocno, sumovito) ili staklene sjene (svjetlo kroz staklo zrakom sjene:
    //svijetla obojena sjena bez suma, bez fokusiranja iza lece) - vidi RenderSettings::glassShadows
    bool caustics = false;
    //Ekviangularno uzorkovanje u magli prema lokalnim svjetlima (RenderSettings::equiangular)
    bool equiangular = true;
    //ReSTIR DI na kartici kad je uzoraka po pikselu najvise 16 (tamo 1.2-2.5x manja greska;
    //na vise uzoraka stratificirani RIS konvergira brze) - RenderSettings::restirSamples
    bool restir = true;
    bool denoise = true;
    //Auto: Intel OIDN kad je ucitan (tools/oidn/fetch.sh), inace A-trous
    Tracer::Denoiser denoiser = Tracer::Denoiser::Auto;
    //VREMENSKA STABILNOST sekvence poslije filtra suma (Tracer::stabilize): tezina prebacenog
    //proslog kadra, 0 = iskljuceno. Samo kad je kadrova vise i filtar ukljucen
    float temporal = 0.5f;
    uint32_t threads = 0;                   //0 = sve jezgre

    //MOTION BLUR: zatvarac otvoren `shutter` kadra (0.5 = 180 st), sredinom na kadru. Scena se
    //gradi u `motionSteps` trenutaka unutar otvora (kamera, objekti, kosti) i uzorci se podijele
    //medju njima; snimka ostaje ona samog kadra - njena mutnoca je vec u njoj
    bool motionBlur = false;
    float shutter = 0.5f;
    uint32_t motionSteps = 16;

    //DUBINSKA OSTRINA: otvor iz zarisne i f-broja (promjer = zarisna / N), zarisna u mm iz kuta
    //kamere i sirine senzora. Jedinica scene se uzima kao metar - solve bez mjerila treba grupu
    //skalirati na metre (ili podesiti f-broj na oko). focusDistance duz -Z kamere
    bool depthOfField = false;
    float fStop = 2.8f;
    float focusDistance = 5.0f;
    float sensorWidth = 36.0f;              //mm, puni format

    //Svjetlo
    //Scene: nebo je kupola (Warp::Light Dome) iz scene, a svjetla samo ona iz scene. Svjetla scene
    //(sunce, kugle, reflektori, pravokutnici) su u renderu UVIJEK, uz bilo koje nebo
    enum class Sky{ Physical, Hdri, Uniform, Scene };
    Sky sky = Sky::Physical;
    float sunElevation = 40.0f;             //stupnjevi iznad horizonta
    float sunAzimuth = 135.0f;              //stupnjevi, 0 = prema -Z, 90 = prema +X
    float sunIntensity = 4.0f;              //ozracenost okomite plohe
    float sunSize = 0.53f;                  //kutni promjer u stupnjevima; vise = mekse sjene
    float turbidity = 3.0f;                 //2 kristalno cisto, 10 izmaglica
    float skyIntensity = 0.15f;             //luminancija zenita neba; vedar dan ~ sunce / 30
    glm::vec3 uniformColor{0.6f, 0.65f, 0.7f};
    std::string hdri;
    float hdriIntensity = 1.0f;
    float hdriRotation = 0.0f;              //stupnjevi

    //Prikaz i zapis
    Tracer::ViewTransform view = Tracer::ViewTransform::Standard;
    float exposure = 0.0f;                  //blende

    //Post processing (bloom, vinjeta, aberacija, balans bijele, zrno) - samo PNG i prozor; EXR je
    //sirovo svjetlo za kompozitora. Ekspozicija gore vrijedi i za post
    Tracer::PostSettings post = []{
        Tracer::PostSettings p;
        p.enabled = false;
        p.bloom = 0.04f;
        p.vignette = 0.15f;
        return p;
    }();
    std::string outputFolder;               //prazno: mapa projekta / render
    std::string name = "render";
    bool writeExr = true;
    bool writePng = true;
};

//Smjer PREMA suncu iz elevacije i azimuta (stupnjevi)
inline glm::vec3 sunDirection(float elevation, float azimuth){
    const float e = glm::radians(elevation), a = glm::radians(azimuth);
    return glm::normalize(glm::vec3(std::cos(e) * std::sin(a), std::sin(e), -std::cos(e) * std::cos(a)));
}

//Je li entitet dio STVARNE scene (hvata sjenu) - vidi zaglavlje
inline bool isRealSceneGeometry(const Warp::Entity& entity){
    auto contains = [](std::string text, const char* what){
        std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c){ return char(std::tolower(c)); });
        return text.find(what) != std::string::npos;
    };
    if(contains(entity.name, "catcher")) return true;
    if(entity.mesh && entity.mesh->shape == Warp::Shape::Plane) return true;
    if(entity.model){
        const std::string file = std::filesystem::path(entity.model->path).filename().string();
        if(contains(file, "_proxy") || contains(file, "_blocker")) return true;
    }
    return false;
}

//---------------------------------------------------------------------------------------------
// SVJETLO IZ SNIMKE: tools/splat/relight.py nauci iz splata i snimke sunce (smjer, boja kao
// ozracenost) i nebo s gradijentom gore-dolje, u sustavu solvea. Ovdje to postaje sunce i kupola
// neba u sceni - pod istim roditeljem kao rijesena kamera, pa sjede u istom sustavu
//---------------------------------------------------------------------------------------------
namespace detail{
//Brojevi iza kljuca u JSON-u (redom, preskacuci [ , : i razmake). Dovoljno za relight.py izlaz
inline bool jsonNumbers(const std::string& text, const std::string& key, float* out, int count){
    size_t at = text.find("\"" + key + "\"");
    if(at == std::string::npos) return false;
    at = text.find(':', at);
    if(at == std::string::npos) return false;
    ++at;
    for(int i = 0; i < count; ++i){
        while(at < text.size() && (std::isspace(static_cast<unsigned char>(text[at])) || text[at] == '[' || text[at] == ',')) ++at;
        char* end = nullptr;
        const float value = std::strtof(text.c_str() + at, &end);
        if(end == text.c_str() + at) return false;
        out[i] = value;
        at = size_t(end - text.c_str());
    }
    return true;
}
//Rotacija koja os `from` okrene u `to` (i za suprotne vektore)
inline glm::quat renderRotationBetween(glm::vec3 from, glm::vec3 to){
    from = glm::normalize(from);
    to = glm::normalize(to);
    const float c = glm::dot(from, to);
    if(c < -0.9999f){
        glm::vec3 axis = glm::cross(glm::vec3(1.0f, 0.0f, 0.0f), from);
        if(glm::dot(axis, axis) < 1e-6f) axis = glm::cross(glm::vec3(0.0f, 1.0f, 0.0f), from);
        return glm::angleAxis(glm::pi<float>(), glm::normalize(axis));
    }
    const glm::vec3 axis = glm::cross(from, to);
    const glm::quat q(1.0f + c, axis.x, axis.y, axis.z);
    return glm::normalize(q);
}
}

//relight.json uz splat scene (<splat>_svjetlo.json), prazno kad ga nema
inline std::string findRelightJson(const Warp::Stage& stage){
    std::string found;
    stage.walk([&](const Warp::Entity& e, int){
        if(!found.empty() || !e.splat) return;
        std::filesystem::path p(e.splat->path);
        const std::filesystem::path candidate = p.parent_path() / (p.stem().string() + "_svjetlo.json");
        std::error_code error;
        if(std::filesystem::exists(candidate, error)) found = candidate.string();
    });
    return found;
}

struct RelightImport{
    Warp::Id sun = Warp::None, sky = Warp::None;
    std::string problem;
};

inline RelightImport importRelight(Warp::Stage& stage, const std::string& path, Warp::Id parent){
    RelightImport result;
    std::ifstream file(path);
    if(!file){ result.problem = "Cannot open " + path; return result; }
    const std::string text((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    float toward[3], sun[3], up[3] = {0, 1, 0}, top[3], bottom[3];
    if(!detail::jsonNumbers(text, "sunce_smjer_prema_svjetlu", toward, 3) || !detail::jsonNumbers(text, "sunce_boja", sun, 3) ||
       !detail::jsonNumbers(text, "nebo_gore", top, 3) || !detail::jsonNumbers(text, "nebo_dolje", bottom, 3)){
        result.problem = "Not a relight.py light file (missing sun or sky): " + path;
        return result;
    }
    detail::jsonNumbers(text, "gore", up, 3);
    //Sunce: boja kao ozracenost po kanalu -> boja jedinicne luminancije puta jakost
    const glm::vec3 irradiance(sun[0], sun[1], sun[2]);
    const float strength = 0.2126f * irradiance.r + 0.7152f * irradiance.g + 0.0722f * irradiance.b;
    result.sun = stage.create("Sun (from footage)", parent);
    Warp::Entity& sunEntity = *stage.get(result.sun);
    Warp::Light light;
    light.type = Warp::Light::Type::Distant;
    light.intensity = strength;
    light.color = strength > 0.0f ? irradiance / strength : glm::vec3(1.0f);
    sunEntity.light = light;
    //Svjetlo putuje niz lokalnu -Z, dakle lokalna +Z gleda prema suncu
    sunEntity.local.rotation = detail::renderRotationBetween(glm::vec3(0.0f, 0.0f, 1.0f), glm::vec3(toward[0], toward[1], toward[2]));

    result.sky = stage.create("Sky (from footage)", parent);
    Warp::Entity& skyEntity = *stage.get(result.sky);
    Warp::Light dome;
    dome.type = Warp::Light::Type::Dome;
    dome.intensity = 1.0f;
    dome.skyTop = glm::max(glm::vec3(top[0], top[1], top[2]), glm::vec3(0.0f));
    dome.skyBottom = glm::max(glm::vec3(bottom[0], bottom[1], bottom[2]), glm::vec3(0.0f));
    skyEntity.light = dome;
    skyEntity.local.rotation = detail::renderRotationBetween(glm::vec3(0.0f, 1.0f, 0.0f), glm::vec3(up[0], up[1], up[2]));
    return result;
}

//Kadar snimke (od nule) koji stoji iza kadra timelinea - isto pravilo kao ploca u pogledu
inline int64_t plateIndexFor(const Warp::Camera& camera, double frame){
    return int64_t(camera.plateFirstFrame) + int64_t(std::llround(frame)) - 1;
}

//---------------------------------------------------------------------------------------------
// DATOTEKE IZMEDJU KADROVA
//---------------------------------------------------------------------------------------------
class RenderAssets{
public:
    const Spool::GltfScene* gltf(const std::string& path, std::string& error){
        auto found = scenes.find(path);
        if(found != scenes.end()) return found->second.get();
        auto scene = std::make_unique<Spool::GltfScene>();
        if(!Spool::loadGltf(path, *scene, error)){
            scenes.emplace(path, nullptr);
            return nullptr;
        }
        return scenes.emplace(path, std::move(scene)).first->second.get();
    }

    //Slika mape: iz GLB-a (image >= 0) ili iz datoteke. nullptr kad se ne da procitati
    const Spool::Image* image(const Warp::TextureSlot& slot){
        const std::string key = slot.source + "#" + std::to_string(slot.image);
        auto found = images.find(key);
        if(found != images.end()) return found->second.isValid() ? &found->second : nullptr;
        Spool::Image decoded;
        if(slot.image >= 0){
            std::string error;
            const Spool::GltfScene* s = gltf(slot.source, error);
            if(s && size_t(slot.image) < s->images.size()) decoded = s->images[size_t(slot.image)].pixels;
        }else{
            try{ decoded = Spool::loadImage(slot.source); }catch(const std::exception&){}
        }
        Spool::Image& stored = images.emplace(key, std::move(decoded)).first->second;
        return stored.isValid() ? &stored : nullptr;
    }

    const Spool::FloatImage* hdri(const std::string& path, std::string& error){
        auto found = hdris.find(path);
        if(found != hdris.end()) return found->second.isValid() ? &found->second : nullptr;
        Spool::FloatImage decoded;
        try{ decoded = Spool::loadImageLinear(path); }catch(const std::exception& e){ error = e.what(); }
        Spool::FloatImage& stored = hdris.emplace(path, std::move(decoded)).first->second;
        return stored.isValid() ? &stored : nullptr;
    }

    //Kadar snimke u punoj rezoluciji. Za sekvencu je sljedeci kadar citanje naprijed, jeftino
    bool plate(const std::string& video, int64_t index, Spool::Image& out, std::string& error){
        try{
            if(!reader || video != readerPath){
                reader = std::make_unique<Spool::VideoReader>(video);
                readerPath = video;
            }
            out = reader->readFrame(std::max<int64_t>(0, index));
            return out.isValid();
        }catch(const std::exception& e){
            error = e.what();
            reader.reset();
            readerPath.clear();
            return false;
        }
    }

    //Splat za holdout: samo sredista, neprozirnost i velicina (ne boje ni harmonici) - 20 bajta po
    //gaussianu umjesto 250. nullptr kad se datoteka ne da procitati (razlog u error)
    struct SplatPoints{
        std::vector<glm::vec3> position;
        std::vector<float> alpha;           //sigmoid(opacity)
        std::vector<float> sigma;           //najveca os, exp(scale), u koordinatama splata
    };
    const SplatPoints* splat(const std::string& path, std::string& error){
        auto found = splats.find(path);
        if(found != splats.end()) return found->second.get();
        std::unique_ptr<SplatPoints> points;
        try{
            const Spool::GaussianCloud cloud = Spool::loadGaussianPly(path);
            points = std::make_unique<SplatPoints>();
            points->position.reserve(cloud.count());
            points->alpha.reserve(cloud.count());
            points->sigma.reserve(cloud.count());
            for(const Spool::Gaussian& g : cloud.gaussians){
                points->position.emplace_back(g.position[0], g.position[1], g.position[2]);
                points->alpha.push_back(1.0f / (1.0f + std::exp(-g.opacity)));
                points->sigma.push_back(std::exp(std::max({g.scale[0], g.scale[1], g.scale[2]})));
            }
        }catch(const std::exception& e){ error = e.what(); points.reset(); }
        return splats.emplace(path, std::move(points)).first->second.get();
    }

    //Tekstura za tracer s mipmapama, jednom po slici: 4K mapa s razinama je desetine milisekundi,
    //a sekvenca i motion blur grade scenu stotine puta
    const Tracer::Texture& mipmapped(const Warp::TextureSlot& slot, const Spool::Image& image, bool srgb);

private:
    std::map<std::string, std::unique_ptr<SplatPoints>> splats;
    std::map<std::string, Tracer::Texture> textures;
    std::map<std::string, std::unique_ptr<Spool::GltfScene>> scenes;
    std::map<std::string, Spool::Image> images;
    std::map<std::string, Spool::FloatImage> hdris;
    std::unique_ptr<Spool::VideoReader> reader;
    std::string readerPath;
};

inline Tracer::Texture tracerTexture(const Spool::Image& image, bool srgb){
    Tracer::Texture t;
    t.width = image.width;
    t.height = image.height;
    t.bytes = image.pixels;
    t.srgb = srgb;
    return t;
}

inline const Tracer::Texture& RenderAssets::mipmapped(const Warp::TextureSlot& slot, const Spool::Image& image, bool srgb){
    const std::string key = slot.source + "#" + std::to_string(slot.image) + (srgb ? "#s" : "#l");
    auto found = textures.find(key);
    if(found != textures.end()) return found->second;
    Tracer::Texture t = tracerTexture(image, srgb);
    t.buildMips();
    return textures.emplace(key, std::move(t)).first->second;
}

//Prva kamera u sceni (redom hijerarhije)
inline Warp::Id firstCameraIn(const Warp::Stage& stage){
    Warp::Id found = Warp::None;
    stage.walk([&](const Warp::Entity& e, int){ if(found == Warp::None && e.camera) found = e.id; });
    return found;
}

//Velicina slike koju ce render dati
inline void renderSize(const Warp::Camera& camera, float scale, uint32_t& width, uint32_t& height){
    width = std::max(1u, uint32_t(std::lround(float(camera.width) * scale)));
    height = std::max(1u, uint32_t(std::lround(float(camera.height) * scale)));
}

//---------------------------------------------------------------------------------------------
// SCENA U KADRU
//---------------------------------------------------------------------------------------------
struct BuiltScene{
    Tracer::Scene scene;
    bool plateLoaded = false;
    bool catchersUsed = false;
    size_t objects = 0, catchers = 0;
    size_t holdoutSplats = 0;               //splatova u holdoutu (RenderOptions::splatHoldout)
    std::vector<std::string> warnings;
};

//DUBINA SPLATA KROZ KAMERU (za holdout). Svaki dovoljno neproziran gaussian (alfa >= 0.4) je
//krug polumjera jedne sigme oko svoje projekcije, z-buffer uzme najblizi. Rupe (piksel koji
//nijedan ne pokrije, a vecina susjeda je pokrivena) se popune medijanom susjeda - inace bi
//CG procurio kroz rijedak zid. Dretve pisu svaka u svoj buffer, spoje se minimumom
inline void splatDepth(const RenderAssets::SplatPoints& points, const glm::mat4& splatWorld, const Tracer::Camera& camera,
                       const glm::mat4& worldToCamera, std::vector<float>& depth){
    const uint32_t width = camera.width, height = camera.height;
    const glm::mat4 toCamera = worldToCamera * splatWorld;
    const float scale = std::cbrt(std::abs(glm::determinant(glm::mat3(splatWorld))));
    const size_t count = points.position.size();
    const uint32_t threads = std::max(1u, std::min(16u, std::thread::hardware_concurrency()));
    std::vector<std::vector<float>> buffers(threads, std::vector<float>(size_t(width) * height, Tracer::NoDepth));
    std::vector<std::thread> workers;
    for(uint32_t t = 0; t < threads; ++t){
        workers.emplace_back([&, t]{
            std::vector<float>& own = buffers[t];
            for(size_t i = count * t / threads; i < count * (t + 1) / threads; ++i){
                if(points.alpha[i] < 0.4f) continue;
                const glm::vec3 local = glm::vec3(toCamera * glm::vec4(points.position[i], 1.0f));
                const float z = -local.z;
                if(z <= 1e-4f) continue;
                const glm::vec2 centre = camera.pixelOf(local);
                const float r = std::clamp(camera.focalPixels * points.sigma[i] * scale / z, 0.5f, 64.0f);
                const int x0 = std::max(0, int(std::floor(centre.x - r))), x1 = std::min(int(width) - 1, int(std::floor(centre.x + r)));
                const int y0 = std::max(0, int(std::floor(centre.y - r))), y1 = std::min(int(height) - 1, int(std::floor(centre.y + r)));
                for(int y = y0; y <= y1; ++y) for(int x = x0; x <= x1; ++x){
                    const float dx = float(x) + 0.5f - centre.x, dy = float(y) + 0.5f - centre.y;
                    if(dx * dx + dy * dy > r * r) continue;
                    float& d = own[size_t(y) * width + size_t(x)];
                    d = std::min(d, z);
                }
            }
        });
    }
    for(std::thread& w : workers) w.join();
    depth = std::move(buffers[0]);
    for(uint32_t t = 1; t < threads; ++t) for(size_t i = 0; i < depth.size(); ++i) depth[i] = std::min(depth[i], buffers[t][i]);
    //Rupe: dva prolaza, piksel bez dubine s barem 5 od 8 pokrivenih susjeda dobije njihov medijan
    for(int pass = 0; pass < 2; ++pass){
        std::vector<float> filled = depth;
        for(uint32_t y = 1; y + 1 < height; ++y) for(uint32_t x = 1; x + 1 < width; ++x){
            if(depth[size_t(y) * width + x] < Tracer::NoDepth * 0.5f) continue;
            float around[8];
            int n = 0;
            for(int dy = -1; dy <= 1; ++dy) for(int dx = -1; dx <= 1; ++dx){
                if(!dx && !dy) continue;
                const float v = depth[size_t(int(y) + dy) * width + size_t(int(x) + dx)];
                if(v < Tracer::NoDepth * 0.5f) around[n++] = v;
            }
            if(n < 5) continue;
            std::nth_element(around, around + n / 2, around + n);
            filled[size_t(y) * width + x] = around[n / 2];
        }
        depth.swap(filled);
    }
}

//Polumjer otvora u jedinicama scene (1 = metar): zarisna f = focalPixels / sirina * senzor (mm),
//promjer otvora f / N
inline float apertureRadiusFor(const Warp::Camera& lens, float fStop, float sensorWidthMm){
    if(lens.width == 0 || fStop <= 0.0f) return 0.0f;
    const float focalMm = lens.focalPixels / float(lens.width) * sensorWidthMm;
    return 0.5f * focalMm / fStop / 1000.0f;
}

//OSTRINA NA KLIK: dubina (duz -Z kamere) pod tockom (x, y) u pikselima rendera. Medijan 5x5
//pogodaka, da klik na rub objekta ne uzme pozadinu; < 0 kad oko tocke nista nije pogodjeno
inline float focusFromDepth(const Tracer::Frame& frame, float x, float y){
    std::vector<float> found;
    const int cx = int(std::floor(x)), cy = int(std::floor(y));
    for(int dy = -2; dy <= 2; ++dy) for(int dx = -2; dx <= 2; ++dx){
        const int px = cx + dx, py = cy + dy;
        if(px < 0 || py < 0 || px >= int(frame.width) || py >= int(frame.height)) continue;
        const size_t i = size_t(py) * frame.width + size_t(px);
        if(i < frame.depth.size() && frame.depth[i] > 0.0f && frame.depth[i] < Tracer::NoDepth * 0.5f) found.push_back(frame.depth[i]);
    }
    if(found.empty()) return -1.0f;
    std::nth_element(found.begin(), found.begin() + found.size() / 2, found.end());
    return found[found.size() / 2];
}

//plateFrame: kadar ciju snimku uzeti (motion blur gradi scenu u trenucima izmedju kadrova, a
//snimka je i dalje ona jednog kadra); < 0 znaci isti kao frame
inline bool buildTracerScene(const Warp::Stage& stage, double frame, const RenderOptions& options, RenderAssets& assets,
                             BuiltScene& out, std::string& error, double plateFrame = -1.0){
    out = BuiltScene{};
    Tracer::Scene& scene = out.scene;
    const Warp::Id cameraId = options.camera != Warp::None ? options.camera : firstCameraIn(stage);
    const Warp::Entity* cameraEntity = stage.get(cameraId);
    if(!cameraEntity || !cameraEntity->camera){
        error = "No camera to render from. Solve a video or add a camera first.";
        return false;
    }
    const Warp::Camera& lens = *cameraEntity->camera;
    if(lens.width == 0 || lens.height == 0 || lens.focalPixels <= 0.0f){
        error = "Camera '" + cameraEntity->name + "' has no lens (width, height or focal is zero).";
        return false;
    }

    //-- kamera: poza bez mjerila (entitet kamere moze biti pod skaliranom grupom) ---------------
    {
        const glm::mat4 world = stage.worldMatrix(cameraId, frame);
        glm::mat4 pose(1.0f);
        for(int k = 0; k < 3; ++k){
            const glm::vec3 axis(world[k]);
            pose[k] = glm::vec4(glm::length(axis) > 0.0f ? glm::normalize(axis) : glm::vec3(k == 0, k == 1, k == 2), 0.0f);
        }
        pose[3] = world[3];
        const float scale = std::clamp(options.resolutionScale, 0.05f, 4.0f);
        Tracer::Camera& c = scene.camera;
        c.cameraToWorld = pose;
        renderSize(lens, scale, c.width, c.height);
        const float sx = float(c.width) / float(lens.width), sy = float(c.height) / float(lens.height);
        c.focalPixels = lens.focalPixels * sx;
        c.centre = glm::vec2(lens.centreX * sx, lens.centreY * sy);
        if(options.depthOfField){
            c.apertureRadius = apertureRadiusFor(lens, options.fStop, options.sensorWidth);
            c.focusDistance = std::max(1e-3f, options.focusDistance);
        }
        if(lens.distorted()){
            c.lens = glm::vec4(lens.distortionFx * sx, lens.distortionFy * sy, lens.distortionCx * sx, lens.distortionCy * sy);
            c.k1 = lens.k1;
            c.k2 = lens.k2;
        }
    }

    //-- snimka ------------------------------------------------------------------------------------
    const bool wantPlate = options.plate && !lens.plate.empty();
    if(wantPlate){
        Spool::Image plate;
        std::string problem;
        if(assets.plate(lens.plate, plateIndexFor(lens, plateFrame >= 0.0 ? plateFrame : frame), plate, problem)){
            scene.backplate = tracerTexture(plate, true);
            scene.backplate.repeat = false;
            out.plateLoaded = true;
        }else{
            out.warnings.push_back("Plate not read (" + problem + ") - rendering over the sky instead.");
        }
    }else if(options.plate && lens.plate.empty()){
        out.warnings.push_back("Camera '" + cameraEntity->name + "' has no video plate.");
    }
    const bool catching = options.shadowCatcher && (out.plateLoaded || options.transparent);
    out.catchersUsed = catching;

    //-- materijali ----------------------------------------------------------------------------
    std::map<std::string, int> textureIndex;
    auto texture = [&](const Warp::TextureSlot& slot, bool srgb){
        if(slot.empty()) return -1;
        const std::string key = slot.source + "#" + std::to_string(slot.image) + (srgb ? "s" : "l");
        auto found = textureIndex.find(key);
        if(found != textureIndex.end()) return found->second;
        const Spool::Image* image = assets.image(slot);
        int index = -1;
        if(image) index = int(scene.addTexture(assets.mipmapped(slot, *image, srgb)));
        else out.warnings.push_back("Texture not read: " + slot.source);
        textureIndex.emplace(key, index);
        return index;
    };
    std::vector<int> materialIndex(stage.materials.size(), -1);
    auto material = [&](int warpIndex, const glm::vec3& fallbackColour){
        if(warpIndex < 0 || size_t(warpIndex) >= stage.materials.size()){
            Tracer::Material m;
            m.baseColor = fallbackColour;
            m.roughness = 0.5f;
            return scene.addMaterial(m);
        }
        if(materialIndex[size_t(warpIndex)] >= 0) return uint32_t(materialIndex[size_t(warpIndex)]);
        const Warp::Material& w = stage.materials[size_t(warpIndex)];
        Tracer::Material m;
        m.name = w.name;
        m.baseColor = glm::vec3(w.baseColor);
        m.opacity = w.baseColor.a;
        m.baseColorTexture = texture(w.baseColorMap, true);
        m.metallic = w.metallic;
        m.roughness = w.roughness;
        m.metallicRoughnessTexture = texture(w.metallicRoughnessMap, false);
        m.normalTexture = texture(w.normalMap, false);
        m.normalScale = w.normalMap.amount;
        m.emission = w.emissive;
        m.emissionStrength = w.emissiveStrength;
        m.emissionTexture = texture(w.emissiveMap, true);
        m.alphaMode = w.alphaMode == Warp::Material::Alpha::Mask ? Tracer::Material::Alpha::Mask
                    : w.alphaMode == Warp::Material::Alpha::Blend ? Tracer::Material::Alpha::Blend : Tracer::Material::Alpha::Opaque;
        m.alphaCutoff = w.alphaCutoff;
        m.transmission = w.transmission;
        m.ior = w.ior;
        m.specular = w.specular;
        m.clearcoat = w.clearcoat;
        m.clearcoatRoughness = w.clearcoatRoughness;
        const uint32_t index = scene.addMaterial(m);
        materialIndex[size_t(warpIndex)] = int(index);
        return index;
    };

    //-- geometrija ------------------------------------------------------------------------------
    const Warp::Entity* dome = nullptr;         //prva vidljiva kupola neba
    glm::mat4 domeWorld(1.0f);
    auto flagsFor = [&](const Warp::Entity& entity){
        Tracer::ObjectFlags flags;
        if(catching && isRealSceneGeometry(entity)){ flags.shadowCatcher = true; ++out.catchers; }
        return flags;
    };
    auto shapeMesh = [](Warp::Shape shape){ return shape == Warp::Shape::Plane ? Tracer::unitPlane() : Tracer::unitCube(); };
    //Vidljivost se nasljeduje: skriven roditelj skriva i djecu (kao u pogledu)
    auto visible = [&](const Warp::Entity& entity){
        for(const Warp::Entity* e = &entity; e; e = e->parent != Warp::None ? stage.get(e->parent) : nullptr)
            if(!e->visible) return false;
        return true;
    };
    stage.walk([&](const Warp::Entity& entity, int){
        if(!visible(entity)) return;
        //Magla u kutiji: kutija entiteta (kao kocka) u svijetu u tom kadru
        if(entity.volume && entity.volume->density > 0.0f){
            Tracer::Volume v;
            v.toWorld = stage.worldMatrix(entity.id, frame);
            v.albedo = glm::clamp(entity.volume->color, glm::vec3(0.0f), glm::vec3(1.0f));
            v.density = entity.volume->density;
            v.anisotropy = std::clamp(entity.volume->anisotropy, -0.95f, 0.95f);
            v.anisotropy2 = std::clamp(entity.volume->anisotropy2, -0.95f, 0.95f);
            v.lobeMix = std::clamp(entity.volume->lobeMix, 0.0f, 1.0f);
            v.shape = entity.volume->shape == Warp::Volume::Shape::Height ? Tracer::Volume::Shape::Height : Tracer::Volume::Shape::Box;
            v.height = std::max(1e-4f, entity.volume->height);
            v.edge = std::clamp(entity.volume->edge, 0.0f, 0.5f);
            v.noise = std::clamp(entity.volume->noise, 0.0f, 1.0f);
            v.noiseScale = std::max(1e-4f, entity.volume->noiseScale);
            scene.volumes.push_back(v);
        }
        if(entity.mesh){
            const glm::mat4 world = stage.worldMatrix(entity.id, frame);
            const Tracer::ObjectFlags flags = flagsFor(entity);
            //Catcher ne smije obojiti CG svojom bojom tijela (narancasta kocka u pogledu): neutralno siv
            const uint32_t m = flags.shadowCatcher && entity.mesh->material < 0
                ? material(-1, glm::vec3(0.35f)) : material(entity.mesh->material, entity.mesh->colour);
            scene.addMesh(shapeMesh(entity.mesh->shape), world, m, entity.name, flags);
            ++out.objects;
        }
        if(entity.light){
            const glm::mat4 world = stage.worldMatrix(entity.id, frame);
            const Warp::Light& source = *entity.light;
            const glm::vec3 forward = glm::normalize(glm::mat3(world) * glm::vec3(0.0f, 0.0f, -1.0f));
            const float scale = std::max({glm::length(glm::vec3(world[0])), glm::length(glm::vec3(world[1])), glm::length(glm::vec3(world[2]))});
            Tracer::Light light;
            light.color = source.color;
            light.intensity = source.intensity;
            switch(source.type){
            case Warp::Light::Type::Distant:
                light.type = Tracer::Light::Type::Distant;
                light.direction = forward;
                light.angle = glm::radians(std::max(0.0f, source.angle));
                scene.lights.push_back(light);
                break;
            case Warp::Light::Type::Sphere: case Warp::Light::Type::Spot:
                light.type = source.type == Warp::Light::Type::Spot ? Tracer::Light::Type::Spot : Tracer::Light::Type::Sphere;
                light.position = glm::vec3(world[3]);
                light.radius = std::max(0.0f, source.radius) * scale;
                light.direction = forward;
                light.spotAngle = glm::radians(std::clamp(source.coneAngle, 0.1f, 180.0f));
                light.spotBlend = std::clamp(source.coneSoftness, 0.0f, 1.0f);
                scene.lights.push_back(light);
                break;
            case Warp::Light::Type::Rect:{
                //Svijetla ploha u lokalnoj XY, lice (i normale vrhova) prema -Z: jednostrana emisija
                Tracer::MeshData quad;
                const float hw = 0.5f * source.width, hh = 0.5f * source.height;
                quad.positions = {{-hw, -hh, 0.0f}, {hw, -hh, 0.0f}, {hw, hh, 0.0f}, {-hw, hh, 0.0f}};
                quad.normals.assign(4, glm::vec3(0.0f, 0.0f, -1.0f));
                quad.indices = {0, 2, 1, 0, 3, 2};
                Tracer::Material m;
                m.baseColor = glm::vec3(0.0f);
                m.specular = 0.0f;
                m.emission = source.color * source.intensity;
                m.emissionTwoSided = false;
                //Kamera ne vidi samu plohu svjetla (kao Arnold/Cycles zadano): nalicje bi bilo crna ploca
                //usred kadra. U odrazima i lomu se vidi, a svjetli jednako
                Tracer::ObjectFlags flags;
                flags.castsShadows = false;
                flags.cameraVisible = false;
                scene.addMesh(quad, world, scene.addMaterial(m), entity.name, flags);
                break;
            }
            case Warp::Light::Type::Dome:
                if(!dome){ dome = &entity; domeWorld = world; }
                break;
            }
        }
        if(entity.model && entity.model->mesh >= 0){
            std::string problem;
            const Spool::GltfScene* gltf = assets.gltf(entity.model->path, problem);
            if(!gltf || size_t(entity.model->mesh) >= gltf->meshes.size()){
                out.warnings.push_back("Model not read: " + entity.model->path + (problem.empty() ? "" : " (" + problem + ")"));
                return;
            }
            const glm::mat4 world = stage.worldMatrix(entity.id, frame);
            const Spool::GltfMesh& mesh = gltf->meshes[size_t(entity.model->mesh)];

            //SKIN: paleta kostiju u prostoru mreze, u OVOM kadru (isto kao pogled, LoomPbr.h)
            std::vector<glm::mat4> palette;
            const Spool::GltfSkin* skin = entity.model->skin >= 0 && size_t(entity.model->skin) < gltf->skins.size()
                ? &gltf->skins[size_t(entity.model->skin)] : nullptr;
            if(skin){
                std::vector<Warp::Id> joints = entity.model->skinJoints;
                if(joints.size() != skin->joints.size() && entity.model->skinJointPaths.size() == skin->joints.size()){
                    joints.clear();
                    for(const std::string& path : entity.model->skinJointPaths) joints.push_back(stage.find(path));
                }
                if(joints.size() == skin->joints.size() && skin->inverseBindMatrices.size() == joints.size() * 16u &&
                   std::abs(glm::determinant(glm::mat3(world))) > 1e-12f){
                    const glm::mat4 inverseMesh = glm::inverse(world);
                    for(size_t j = 0; j < joints.size(); ++j){
                        if(!stage.contains(joints[j])){ palette.clear(); break; }
                        glm::mat4 inverseBind(1.0f);
                        for(int c = 0; c < 4; ++c) for(int r = 0; r < 4; ++r)
                            inverseBind[c][r] = skin->inverseBindMatrices[j * 16u + size_t(c) * 4u + size_t(r)];
                        palette.push_back(inverseMesh * stage.worldMatrix(joints[j], frame) * inverseBind);
                    }
                }
            }

            const Tracer::ObjectFlags flags = flagsFor(entity);
            for(size_t p = 0; p < mesh.primitives.size(); ++p){
                const Spool::GltfPrimitive& primitive = mesh.primitives[p];
                const size_t count = primitive.vertexCount();
                Tracer::MeshData data;
                data.positions.resize(count);
                for(size_t v = 0; v < count; ++v)
                    data.positions[v] = glm::vec3(primitive.positions[v * 3], primitive.positions[v * 3 + 1], primitive.positions[v * 3 + 2]);
                if(primitive.normals.size() == count * 3){
                    data.normals.resize(count);
                    for(size_t v = 0; v < count; ++v)
                        data.normals[v] = glm::vec3(primitive.normals[v * 3], primitive.normals[v * 3 + 1], primitive.normals[v * 3 + 2]);
                }
                if(primitive.uv0.size() == count * 2){
                    data.uvs.resize(count);
                    for(size_t v = 0; v < count; ++v) data.uvs[v] = glm::vec2(primitive.uv0[v * 2], primitive.uv0[v * 2 + 1]);
                }
                data.indices = primitive.indices;
                const size_t influences = count ? primitive.jointIndices.size() / count : 0;
                if(!palette.empty() && (influences == 4 || influences == 8) && primitive.jointWeights.size() == primitive.jointIndices.size()){
                    for(size_t v = 0; v < count; ++v){
                        glm::mat4 blend(0.0f);
                        float total = 0.0f;
                        for(size_t k = 0; k < influences; ++k){
                            const float w = primitive.jointWeights[v * influences + k];
                            const size_t joint = primitive.jointIndices[v * influences + k];
                            if(w <= 0.0f || joint >= palette.size()) continue;
                            blend += palette[joint] * w;
                            total += w;
                        }
                        if(total <= 1e-8f) continue;
                        blend /= total;
                        data.positions[v] = glm::vec3(blend * glm::vec4(data.positions[v], 1.0f));
                        if(!data.normals.empty()) data.normals[v] = glm::transpose(glm::inverse(glm::mat3(blend))) * data.normals[v];
                    }
                }
                const int warpMaterial = p < entity.model->materials.size() ? entity.model->materials[p] : -1;
                const uint32_t m = flags.shadowCatcher && warpMaterial < 0 ? material(-1, glm::vec3(0.35f))
                                                                           : material(warpMaterial, glm::vec3(0.8f));
                scene.addMesh(data, world, m, entity.name, flags);
            }
            ++out.objects;
        }
    });

    //-- nebo i sunce -------------------------------------------------------------------------------
    Tracer::Environment& env = scene.environment;
    env.cameraVisible = options.skyVisible;
    switch(options.sky){
    case RenderOptions::Sky::Physical:{
        const glm::vec3 toward = sunDirection(options.sunElevation, options.sunAzimuth);
        //Tlo ispod horizonta: sivo-smedja ploha (albedo ~0.2) pod istim suncem i nebom, L = a * E / pi.
        //Nebo daje ozracenost ~ pi * 1.6 * zenit (prosjek Preethamova neba je oko 1.6 zenita)
        const float groundIrradiance = options.sunIntensity * std::max(0.0f, toward.y) + glm::pi<float>() * 1.6f * options.skyIntensity;
        const glm::vec3 ground = glm::vec3(0.22f, 0.20f, 0.17f) * groundIrradiance / glm::pi<float>();
        env.map = Tracer::makeSky(toward, options.turbidity, options.skyIntensity, ground);
        Tracer::Light sun;
        sun.type = Tracer::Light::Type::Distant;
        sun.direction = -toward;
        //Boja sunca: toplija sto je nize (dulji put kroz zrak), priblizno
        const float warm = std::clamp(1.0f - options.sunElevation / 60.0f, 0.0f, 1.0f);
        sun.color = glm::mix(glm::vec3(1.0f, 0.97f, 0.92f), glm::vec3(1.0f, 0.62f, 0.34f), warm * warm);
        sun.intensity = options.sunElevation > -2.0f ? options.sunIntensity : 0.0f;
        sun.angle = glm::radians(std::max(0.0f, options.sunSize));
        scene.lights.push_back(sun);
        break;
    }
    case RenderOptions::Sky::Hdri:{
        std::string problem;
        const Spool::FloatImage* image = options.hdri.empty() ? nullptr : assets.hdri(options.hdri, problem);
        if(image){
            env.map.width = image->width;
            env.map.height = image->height;
            env.map.floats = image->pixels;
            env.map.srgb = false;
            env.intensity = options.hdriIntensity;
            env.rotation = glm::radians(options.hdriRotation);
        }else{
            out.warnings.push_back(options.hdri.empty() ? "No HDRI chosen - using a uniform sky."
                                                        : "HDRI not read: " + problem + " - using a uniform sky.");
            env.color = options.uniformColor;
        }
        break;
    }
    case RenderOptions::Sky::Uniform:
        env.color = options.uniformColor;
        break;
    case RenderOptions::Sky::Scene:{
        if(!dome){
            out.warnings.push_back("Sky 'Scene': the scene has no Sky Dome light - the sky is black.");
            break;
        }
        const Warp::Light& d = *dome->light;
        std::string problem;
        const Spool::FloatImage* image = d.texture.empty() ? nullptr : assets.hdri(d.texture, problem);
        if(image){
            env.map.width = image->width;
            env.map.height = image->height;
            env.map.floats = image->pixels;
            env.map.srgb = false;
            env.intensity = d.intensity;
            //Kupola zakrenuta oko Y: lokalna X os ode u (cos, 0, -sin)
            const glm::vec3 x = glm::vec3(domeWorld[0]);
            env.rotation = std::atan2(-x.z, x.x);
        }else{
            if(!d.texture.empty()) out.warnings.push_back("Sky Dome HDRI not read: " + problem + " - using its gradient.");
            //Gradijent: od donje do gornje boje po kosinusu prema lokalnoj +Y kupole
            const glm::vec3 up = glm::normalize(glm::mat3(domeWorld) * glm::vec3(0.0f, 1.0f, 0.0f));
            const uint32_t w = 256, h = 128;
            env.map.width = w;
            env.map.height = h;
            env.map.srgb = false;
            env.map.floats.assign(size_t(w) * h * 4, 1.0f);
            for(uint32_t y = 0; y < h; ++y) for(uint32_t x = 0; x < w; ++x){
                const glm::vec3 dir = Tracer::latLongToDirection(glm::vec2((float(x) + 0.5f) / float(w), (float(y) + 0.5f) / float(h)));
                const glm::vec3 c = glm::mix(d.skyBottom, d.skyTop, 0.5f * (glm::dot(dir, up) + 1.0f)) * d.intensity;
                float* p = env.map.floats.data() + (size_t(y) * w + x) * 4;
                p[0] = c.r; p[1] = c.g; p[2] = c.b;
            }
        }
        break;
    }
    }
    //-- holdout iz splata ------------------------------------------------------------------------------
    if(options.splatHoldout){
        std::vector<float> depth;
        const glm::mat4 worldToCamera = glm::inverse(scene.camera.cameraToWorld);
        size_t used = 0;
        stage.walk([&](const Warp::Entity& e, int){
            if(!e.splat || !visible(e)) return;
            std::string problem;
            const RenderAssets::SplatPoints* points = assets.splat(e.splat->path, problem);
            if(!points){ out.warnings.push_back("Splat holdout: could not read " + e.splat->path + " (" + problem + ")"); return; }
            std::vector<float> one;
            splatDepth(*points, stage.worldMatrix(e.id, frame), scene.camera, worldToCamera, one);
            if(depth.empty()) depth.swap(one);
            else for(size_t i = 0; i < depth.size(); ++i) depth[i] = std::min(depth[i], one[i]);
            ++used;
        });
        if(used == 0) out.warnings.push_back("Splat holdout is on, but the scene has no visible splat.");
        else{
            Tracer::Texture& h = scene.holdout;
            h.width = scene.camera.width;
            h.height = scene.camera.height;
            h.srgb = false;
            h.repeat = false;
            h.floats.assign(depth.size() * 4, 0.0f);
            for(size_t i = 0; i < depth.size(); ++i) h.floats[i * 4] = depth[i];
            out.holdoutSplats = used;
        }
    }
    if(scene.triangles.empty()) out.warnings.push_back("Nothing to render in front of the camera: the scene has no meshes or models.");
    return true;
}

//---------------------------------------------------------------------------------------------
// SLIKA I DATOTEKE
//---------------------------------------------------------------------------------------------
inline Tracer::Backdrop backdropFor(const RenderOptions& options, bool plateLoaded){
    if(plateLoaded) return Tracer::Backdrop::Plate;
    if(options.transparent) return Tracer::Backdrop::Transparent;
    return Tracer::Backdrop::Environment;
}

//SLIKA KAKO SE VIDI: slozeno (snimka/nebo/prozirno), post, pa prikaz. Ekspozicija ide u post kad
//je post ukljucen (bloom i prag moraju vidjeti eksponirano svjetlo), inace u prikaz
inline std::vector<uint8_t> displayImage(const Tracer::Frame& frame, const Tracer::Texture* plate, const RenderOptions& options,
                                         bool plateLoaded, double frameNumber){
    std::vector<float> beauty = Tracer::composite(frame, backdropFor(options, plateLoaded), plateLoaded ? plate : nullptr);
    Tracer::PostSettings post = options.post;
    post.exposure = options.exposure;
    post.grainSeed = uint32_t(std::llround(frameNumber)) * 7919u + options.post.grainSeed;
    if(post.active()){
        Tracer::applyPost(beauty, frame.width, frame.height, post);
        return Tracer::toDisplay(beauty, frame.width, frame.height, options.view, 0.0f);
    }
    return Tracer::toDisplay(beauty, frame.width, frame.height, options.view, options.exposure);
}

//MOTION BLUR JEDNIM STABLOM: scena kadra (snimka, svjetla, magla, holdout) plus kljucevi pomaka
//kroz otvor - vrhovi, normale i kamera u motionSteps jednako razmaknutih trenutaka. Tracer iz
//toga gradi JEDAN BVH, a svaki uzorak dobije svoj trenutak. false (i razlog) kad se broj vrhova
//ili trokuta mijenja kroz otvor (objekt se pojavi ili nestane) - tada odsjeci u vremenu
inline bool buildMotionScene(const Warp::Stage& stage, double frame, const RenderOptions& options, RenderAssets& assets,
                             BuiltScene& out, std::string& problem){
    std::string error;
    if(!buildTracerScene(stage, frame, options, assets, out, error)){ problem = error; return false; }
    //Kljucevima treba samo geometrija i kamera: bez snimke, holdouta i neba
    RenderOptions keyOptions = options;
    keyOptions.plate = false;
    keyOptions.splatHoldout = false;
    keyOptions.sky = RenderOptions::Sky::Uniform;
    const uint32_t keys = std::clamp(options.motionSteps, 2u, 32u);
    Tracer::Motion motion;
    for(uint32_t k = 0; k < keys; ++k){
        const double time = frame + double(options.shutter) * (double(k) / double(keys - 1) - 0.5);
        BuiltScene key;
        if(!buildTracerScene(stage, time, keyOptions, assets, key, error)){ problem = error; return false; }
        if(key.scene.positions.size() != out.scene.positions.size() || key.scene.triangles.size() != out.scene.triangles.size()){
            problem = "the scene changes (objects appear or disappear) inside the shutter";
            return false;
        }
        motion.positions.push_back(std::move(key.scene.positions));
        motion.normals.push_back(std::move(key.scene.normals));
        motion.cameras.push_back(key.scene.camera.cameraToWorld);
    }
    //Bez stvarnog pomaka nema kljuceva - obicni render
    bool moves = false;
    for(size_t k = 1; k < keys && !moves; ++k) moves = motion.positions[k] != motion.positions[0] || motion.cameras[k] != motion.cameras[0];
    if(moves) out.scene.motion = std::move(motion);
    return true;
}

//Ime kadra u sekvenci: render_0042. Jedan kadar bez broja
inline std::string frameStem(const RenderOptions& options, double frame){
    if(!options.sequence) return options.name;
    char digits[16];
    std::snprintf(digits, sizeof(digits), "_%04ld", long(std::lround(frame)));
    return options.name + digits;
}

//Zapis jednog kadra: PNG (kako se vidi), EXR (linearno, svi slojevi), i PNG-ovi slojeva
inline std::vector<std::string> writeRender(const Tracer::Frame& frame, const Tracer::Scene& scene, const RenderOptions& options,
                                            bool plateLoaded, bool catchers, const std::string& folder, const std::string& stem,
                                            std::string& error, double frameNumber = 1.0){
    std::vector<std::string> written;
    namespace fs = std::filesystem;
    try{
        std::error_code problem;
        fs::create_directories(folder, problem);
        const Tracer::Backdrop backdrop = backdropFor(options, plateLoaded);
        const std::vector<float> beauty = Tracer::composite(frame, backdrop, plateLoaded ? &scene.backplate : nullptr);
        const size_t n = frame.pixelCount();
        //SVAKA DATOTEKA U SVOJOJ NITI. PNG sabijanje i EXR su nezavisni i svaki drzi jednu jezgru;
        //redom su bili sekunda po kadru 720p (izmjereno), sto je u sekvenci vise od samog rendera
        std::vector<std::future<std::string>> jobs;
        auto save = [&](std::string path, auto makePixels){
            jobs.push_back(std::async(std::launch::async, [&frame, path, makePixels]{
                const std::vector<uint8_t> rgba = makePixels();
                Spool::saveImage(path, Spool::imageFromPixels(rgba.data(), frame.width, frame.height));
                return path;
            }));
        };
        const std::string base = (fs::path(folder) / stem).string();
        if(options.writePng) save(base + ".png", [&]{ return displayImage(frame, &scene.backplate, options, plateLoaded, frameNumber); });
        if(options.writePng && options.depth) save((fs::path(folder) / (stem + "_depth.png")).string(), [&]{ return Tracer::depthToDisplay(frame); });
        if(options.writePng && options.normal) save((fs::path(folder) / (stem + "_normal.png")).string(), [&]{ return Tracer::normalToDisplay(frame); });
        if(options.writePng && options.albedo) save((fs::path(folder) / (stem + "_albedo.png")).string(), [&]{ return Tracer::albedoToDisplay(frame); });
        if(options.writeExr) jobs.push_back(std::async(std::launch::async, [&]{
            std::vector<Spool::ExrChannel> channels;
            auto channel = [&](const std::string& name, const std::vector<float>& source, size_t stride, size_t offset, bool half = true){
                Spool::ExrChannel c;
                c.name = name;
                c.half = half;
                c.values.resize(n);
                for(size_t i = 0; i < n; ++i) c.values[i] = source[i * stride + offset];
                channels.push_back(std::move(c));
            };
            //Glavna slika (kako je slozena, s ekspozicijom), pa CG sam premultipliciran
            std::vector<float> exposed = beauty;
            const float gain = std::exp2(options.exposure);
            for(size_t i = 0; i < n; ++i) for(int k = 0; k < 3; ++k) exposed[i * 4 + size_t(k)] *= gain;
            channel("R", exposed, 4, 0); channel("G", exposed, 4, 1); channel("B", exposed, 4, 2); channel("A", exposed, 4, 3);
            channel("cg.R", frame.cg, 4, 0); channel("cg.G", frame.cg, 4, 1); channel("cg.B", frame.cg, 4, 2); channel("cg.A", frame.cg, 4, 3);
            if(catchers){ channel("shadow.R", frame.shadow, 3, 0); channel("shadow.G", frame.shadow, 3, 1); channel("shadow.B", frame.shadow, 3, 2); }
            if(options.depth) channel("Z", frame.depth, 1, 0, false);
            if(options.normal){ channel("N.X", frame.normal, 3, 0); channel("N.Y", frame.normal, 3, 1); channel("N.Z", frame.normal, 3, 2); }
            if(options.albedo){ channel("albedo.R", frame.albedo, 3, 0); channel("albedo.G", frame.albedo, 3, 1); channel("albedo.B", frame.albedo, 3, 2); }
            Spool::saveExr(base + ".exr", frame.width, frame.height, std::move(channels));
            return base + ".exr";
        }));
        //ST-MAPA ZA NUKE kad je leca zakrivljena: render je vec zakrivljen kao snimka, ali kompozitor
        //cesto treba ravnu plocu (track, paint) pa natrag. Konvencija STMap cvora: izlazni piksel
        //uzme ulaz na (R*sirina, G*visina), G raste PREMA GORE.
        //   R, G                  undistort: zakrivljena snimka -> ravna (pinhole) slika
        //   redistort.R, .G       obrnuto: ravni CG -> zakrivljen kao snimka
        const Tracer::Camera& camera = scene.camera;
        if(options.writeExr && camera.distorted()){
            jobs.push_back(std::async(std::launch::async, [&frame, &camera, folder, name = options.name]{
                const uint32_t w = frame.width, h = frame.height;
                std::vector<Spool::ExrChannel> channels(4);
                const char* names[4] = {"R", "G", "redistort.R", "redistort.G"};
                for(int k = 0; k < 4; ++k){ channels[size_t(k)].name = names[k]; channels[size_t(k)].half = false; channels[size_t(k)].values.resize(size_t(w) * h); }
                for(uint32_t y = 0; y < h; ++y) for(uint32_t x = 0; x < w; ++x){
                    const glm::vec2 p(float(x) + 0.5f, float(y) + 0.5f);
                    const glm::vec2 from = camera.distortPixel(p), back = camera.undistortPixel(p);
                    const size_t i = size_t(y) * w + x;
                    channels[0].values[i] = from.x / float(w);
                    channels[1].values[i] = 1.0f - from.y / float(h);
                    channels[2].values[i] = back.x / float(w);
                    channels[3].values[i] = 1.0f - back.y / float(h);
                }
                const std::string path = (std::filesystem::path(folder) / (name + "_stmap.exr")).string();
                Spool::saveExr(path, w, h, std::move(channels));
                return path;
            }));
        }
        //Sve se saceka i prije nego se javi greska - nit ne smije nadzivjeti okvir koji je cita
        std::string firstProblem;
        for(std::future<std::string>& job : jobs){
            try{ written.push_back(job.get()); }
            catch(const std::exception& e){ if(firstProblem.empty()) firstProblem = e.what(); }
        }
        if(!firstProblem.empty()) error = firstProblem;
    }catch(const std::exception& e){
        error = e.what();
    }
    return written;
}

//---------------------------------------------------------------------------------------------
// RENDER U POZADINI (editor): kopija scene, nit, slika koja se cisti, prekid
//---------------------------------------------------------------------------------------------
class RenderSession{
public:
    ~RenderSession(){ cancel(); join(); if(styler.joinable()) styler.join(); }

    //=========================================================================================
    // GPU POGON. Kartica se smije dirati samo iz niti koja crta (editor) - pa render nit ne racuna
    // sama nego OBJAVI posao: prevedenu scenu kadra. Pogon (LoomRenderGpu.h) ga u svojoj niti
    // preuzme, rasporedi preko kadrova, objavljuje sliku za prikaz i vrati gotov film. Render nit
    // za to vrijeme ceka, a poslije sama filtrira i pise datoteke - EXR od 50 MB ne koci prozor.
    //
    // Bez prikljucenog pogona (testovi, stroj bez Vulkana) sve ide na procesoru, isto kao prije
    //=========================================================================================
    struct GpuJob{
        std::shared_ptr<const Tracer::CompiledScene> scene;
        Tracer::RenderSettings settings;
        RenderOptions options;
        bool plateLoaded = false;
        double frame = 1.0;
        size_t index = 0, count = 1;
        size_t slice = 0, slices = 1;       //vremenski odsjecak (motion blur)
        uint64_t id = 0;
    };
    void attachGpu(bool attached){ gpuAttached = attached; }
    bool hasGpu() const {return gpuAttached;}
    //Pogon: novi posao, ako ga ima
    bool takeGpuJob(GpuJob& job){
        std::lock_guard<std::mutex> guard(lock);
        if(!gpuPending) return false;
        job = *gpuPending;
        gpuPending.reset();
        return true;
    }
    void gpuProgress(const GpuJob& job, uint32_t samples, double mraysPerSecond = 0.0){
        std::lock_guard<std::mutex> guard(lock);
        const uint32_t done = uint32_t(job.slice) * job.settings.samples + samples;
        const uint32_t total = uint32_t(job.slices) * job.settings.samples;
        state.samples = done;
        state.frameDone = uint32_t(job.index);
        state.progress = (float(job.index) + float(done) / float(std::max(1u, total))) / float(job.count);
        char text[192];
        std::snprintf(text, sizeof(text), "Rendering frame %.0f (%zu/%zu) on GPU: %u/%u samples%s", job.frame, job.index + 1,
                      job.count, done, total, mraysPerSecond > 0.0 ? "" : "");
        state.status = text;
    }
    void publishPreview(std::vector<uint8_t> rgba, uint32_t width, uint32_t height){
        std::lock_guard<std::mutex> guard(lock);
        preview.swap(rgba);
        previewWidth = width;
        previewHeight = height;
        previewFresh = true;
    }
    void finishGpuJob(const GpuJob& job, Tracer::Frame frame){
        std::lock_guard<std::mutex> guard(lock);
        if(job.id != gpuJobId) return;
        gpuResult = std::move(frame);
        gpuDone = true;
        gpuWake.notify_all();
    }
    void failGpuJob(const GpuJob& job, const std::string& reason){
        std::lock_guard<std::mutex> guard(lock);
        if(job.id != gpuJobId) return;
        gpuError = reason.empty() ? "GPU error" : reason;
        gpuDone = true;
        gpuWake.notify_all();
    }
    bool cancelled() const {return stopFlag;}

    //=========================================================================================
    // POST POSLIJE RENDERA. Zadnji gotov kadar ostaje u memoriji (linearni film); promjena posta,
    // prikaza ili ekspozicije ga samo ponovno slozi - milisekunde umjesto minuta rendera. Racuna
    // se u svojoj niti, pa klizac ostaje glatak; novija promjena preskoci stariju
    //=========================================================================================
    bool hasResult() const{
        std::lock_guard<std::mutex> guard(lock);
        return last != nullptr && !state.running;
    }
    //Linearni film zadnjeg gotovog kadra (prazan dok ga nema)
    Tracer::Frame lastFrame() const{
        std::lock_guard<std::mutex> guard(lock);
        return last ? last->frame : Tracer::Frame{};
    }
    void restyle(const RenderOptions& options){
        std::shared_ptr<Last> frame;
        {
            std::lock_guard<std::mutex> guard(lock);
            if(!last || state.running) return;
            frame = last;
            pendingStyle = options;
            if(styling) return;                     //nit koja radi uzet ce najnovije postavke
            styling = true;
        }
        if(styler.joinable()) styler.join();
        styler = std::thread([this, frame]{
            while(true){
                RenderOptions options;
                {
                    std::lock_guard<std::mutex> guard(lock);
                    if(!pendingStyle){ styling = false; return; }
                    options = *pendingStyle;
                    pendingStyle.reset();
                }
                Tracer::Scene holder;
                holder.backplate = frame->plate;
                publish(frame->frame, holder, options, frame->plateLoaded, frame->frameNumber);
            }
        });
    }
    //Ponovno zapise PNG zadnjeg kadra s trenutnim postom. Vraca put ili prazno (razlog u error)
    std::string rewritePng(const RenderOptions& options, std::string& error){
        std::shared_ptr<Last> frame;
        {
            std::lock_guard<std::mutex> guard(lock);
            frame = last;
        }
        if(!frame){ error = "No finished render to save."; return {}; }
        try{
            const std::vector<uint8_t> rgba = displayImage(frame->frame, &frame->plate, options, frame->plateLoaded, frame->frameNumber);
            const std::string path = (std::filesystem::path(frame->folder) / (frame->stem + ".png")).string();
            Spool::saveImage(path, Spool::imageFromPixels(rgba.data(), frame->frame.width, frame->frame.height));
            return path;
        }catch(const std::exception& e){
            error = e.what();
            return {};
        }
    }

    //Pocinje render. Scena se KOPIRA: umjetnik smije dalje uredjivati, render ostaje ono sto je bilo
    void start(const Warp::Stage& stage, const RenderOptions& options, const std::string& folder){
        cancel();
        join();
        stopFlag = false;
        {
            std::lock_guard<std::mutex> guard(lock);
            state = State{};
            state.running = true;
            state.status = "Preparing scene...";
            state.log.clear();
        }
        worker = std::thread([this, stage, options, folder]{ run(stage, options, folder); });
    }

    void cancel(){ stopFlag = true; gpuWake.notify_all(); }
    void join(){ if(worker.joinable()) worker.join(); }

    struct State{
        bool running = false;
        bool finished = false;
        bool failed = false;
        std::string status;
        float progress = 0.0f;              //0..1 kroz cijeli posao
        uint32_t frameDone = 0, frameCount = 0;
        uint32_t samples = 0, samplesTotal = 0;
        double seconds = 0.0;
        std::vector<std::string> log;       //poruke i zapisane datoteke, redom
    };
    State snapshot() const{
        std::lock_guard<std::mutex> guard(lock);
        return state;
    }
    //Nova slika za prikaz od zadnjeg poziva (RGBA8, sRGB)
    bool takePreview(std::vector<uint8_t>& rgba, uint32_t& width, uint32_t& height){
        std::lock_guard<std::mutex> guard(lock);
        if(!previewFresh) return false;
        rgba = preview;
        width = previewWidth;
        height = previewHeight;
        previewFresh = false;
        return true;
    }
    //Zadnja slika bez obzira je li nova (za ponovno otvaranje prozora)
    bool lastPreview(std::vector<uint8_t>& rgba, uint32_t& width, uint32_t& height) const{
        std::lock_guard<std::mutex> guard(lock);
        if(preview.empty()) return false;
        rgba = preview;
        width = previewWidth;
        height = previewHeight;
        return true;
    }

private:
    mutable std::mutex lock;
    State state;
    std::vector<uint8_t> preview;
    uint32_t previewWidth = 0, previewHeight = 0;
    bool previewFresh = false;
    std::atomic<bool> stopFlag{false};
    std::thread worker;
    struct Last{
        Tracer::Frame frame;
        Tracer::Texture plate;
        bool plateLoaded = false;
        double frameNumber = 1.0;
        std::string folder, stem;
    };
    std::shared_ptr<Last> last;
    std::optional<RenderOptions> pendingStyle;
    bool styling = false;
    std::thread styler;
    std::atomic<bool> gpuAttached{false};
    std::condition_variable gpuWake;
    std::optional<GpuJob> gpuPending;
    std::optional<Tracer::Frame> gpuResult;
    std::string gpuError;
    bool gpuDone = false;
    uint64_t gpuJobId = 0;

    //Posao kartici, pa cekanje. false: kartica je pala (razlog u error) ili je prekinuto
    bool renderOnGpu(GpuJob job, Tracer::Frame& out, std::string& error){
        std::unique_lock<std::mutex> guard(lock);
        job.id = ++gpuJobId;
        gpuPending = job;
        gpuResult.reset();
        gpuError.clear();
        gpuDone = false;
        gpuWake.wait(guard, [&]{ return gpuDone || stopFlag.load(); });
        gpuPending.reset();
        if(!gpuDone){ ++gpuJobId; return false; }          //prekid: kasni rezultat se odbaci
        if(!gpuError.empty()){ error = gpuError; return false; }
        out = std::move(*gpuResult);
        gpuResult.reset();
        return true;
    }

    void say(const std::string& line){
        std::lock_guard<std::mutex> guard(lock);
        state.log.push_back(line);
        state.status = line;
    }

    void publish(const Tracer::Frame& frame, const Tracer::Scene& scene, const RenderOptions& options, bool plateLoaded,
                 double frameNumber = 1.0){
        std::vector<uint8_t> display = displayImage(frame, &scene.backplate, options, plateLoaded, frameNumber);
        //Prozirno se u prozoru pokaze preko sahovnice, da se vidi sto je alfa
        for(uint32_t y = 0; y < frame.height; ++y) for(uint32_t x = 0; x < frame.width; ++x){
            uint8_t* p = display.data() + (size_t(y) * frame.width + x) * 4;
            const float a = float(p[3]) / 255.0f;
            if(a >= 1.0f) continue;
            const uint8_t checker = ((x / 16 + y / 16) & 1) ? 70 : 45;
            for(int k = 0; k < 3; ++k) p[k] = uint8_t(float(p[k]) * a + float(checker) * (1.0f - a));
            p[3] = 255;
        }
        std::lock_guard<std::mutex> guard(lock);
        preview.swap(display);
        previewWidth = frame.width;
        previewHeight = frame.height;
        previewFresh = true;
    }

    //Tekuci prosjek vremenskih odsjecaka motion blura (slice od nule). Svi imaju isti broj uzoraka,
    //pa je tezina novog 1 / (slice + 1); varijanca prosjeka neovisnih procjena je zbroj kvadrata
    //tezina puta varijance
    static void blendSlice(Tracer::Frame& sum, const Tracer::Frame& add, uint32_t slice, bool takeDepth){
        if(slice == 0 || sum.pixelCount() != add.pixelCount()){
            sum = add;
            return;
        }
        const float w = 1.0f / float(slice + 1), keep = 1.0f - w;
        auto mix = [&](std::vector<float>& into, const std::vector<float>& from){
            for(size_t i = 0; i < into.size() && i < from.size(); ++i) into[i] = into[i] * keep + from[i] * w;
        };
        mix(sum.cg, add.cg);
        mix(sum.background, add.background);
        mix(sum.shadow, add.shadow);
        mix(sum.albedo, add.albedo);
        mix(sum.normal, add.normal);
        for(size_t i = 0; i < sum.variance.size() && i < add.variance.size(); ++i)
            sum.variance[i] = keep * keep * sum.variance[i] + w * w * add.variance[i];
        if(takeDepth) sum.depth = add.depth;
    }

    void run(Warp::Stage stage, RenderOptions options, std::string folder){
        const auto started = std::chrono::steady_clock::now();
        RenderAssets assets;
        std::vector<double> frames;
        if(options.sequence){
            for(double f = std::round(options.firstFrame); f <= std::round(options.lastFrame) + 1e-9; f += 1.0) frames.push_back(f);
        }else frames.push_back(options.firstFrame);
        {
            std::lock_guard<std::mutex> guard(lock);
            state.frameCount = uint32_t(frames.size());
            state.samplesTotal = options.samples;
        }
        bool failed = false;
        std::shared_ptr<const Tracer::CompiledScene> previousScene;     //za refit BVH-a u sekvenci
        Tracer::TemporalHistory history;
        for(size_t index = 0; index < frames.size() && !stopFlag; ++index){
            const double frame = frames[index];
            std::string error;
            //Vremenski odsjeci: jedan bez motion blura, inace motionSteps trenutaka unutar otvora
            const bool blur = options.motionBlur && options.shutter > 0.0f;
            const uint32_t samples = std::max(1u, options.samples);
            //Prvo jednim stablom (kljucevi pomaka); odsjeci samo kad se topologija mijenja kroz otvor
            std::optional<BuiltScene> motionScene;
            if(blur){
                BuiltScene keyed;
                std::string problem;
                if(buildMotionScene(stage, frame, options, assets, keyed, problem)) motionScene = std::move(keyed);
                else if(index == 0 && !problem.empty()) say("Motion blur in time slices: " + problem);
            }
            const uint32_t slices = blur && !motionScene ? std::clamp(options.motionSteps, 2u, std::max(2u, samples)) : 1u;
            const uint32_t perSlice = std::max(1u, (samples + slices - 1) / slices);
            Tracer::Frame result;
            std::shared_ptr<const Tracer::CompiledScene> compiled;       //srednji odsjecak: kamera i snimka za zapis
            bool plateLoaded = false, catchers = false, sliceFailed = false;
            for(uint32_t slice = 0; slice < slices && !stopFlag; ++slice){
                const double time = blur ? frame + double(options.shutter) * ((double(slice) + 0.5) / double(slices) - 0.5) : frame;
                BuiltScene built;
                if(motionScene){ built = std::move(*motionScene); motionScene.reset(); }
                else if(!buildTracerScene(stage, time, options, assets, built, error, frame)){ say("Render failed: " + error); sliceFailed = true; break; }
                if(slice == 0){
                    if(index == 0) for(const std::string& w : built.warnings) say("Warning: " + w);
                    char line[256];
                    std::snprintf(line, sizeof(line), "Frame %.0f: %zu triangles, %zu objects (%zu shadow catchers), %zu fog boxes, %ux%u%s",
                                  frame, built.scene.triangles.size(), built.objects, built.catchers, built.scene.volumes.size(),
                                  built.scene.camera.width, built.scene.camera.height,
                                  !blur ? "" : built.scene.motion.active()
                                      ? (", motion blur " + std::to_string(std::max(built.scene.motion.positions.size(), built.scene.motion.cameras.size())) + " keys, one BVH").c_str()
                                      : (", motion blur " + std::to_string(slices) + " time slices").c_str());
                    say(line);
                    plateLoaded = built.plateLoaded;
                    catchers = built.catchersUsed && built.catchers > 0;
                }
                //Sekvenca: isti trokuti kao prosli kadar (ili odsjecak) - stablo se osvjezi, ne gradi
                const std::shared_ptr<const Tracer::CompiledScene> sliceScene = Tracer::compile(std::move(built.scene), previousScene.get());
                previousScene = sliceScene;
                if(slice == slices / 2) compiled = sliceScene;
                Tracer::RenderSettings settings;
                settings.samples = perSlice;
                settings.maxBounces = options.maxBounces;
                settings.indirectClamp = options.indirectClamp;
                settings.adaptiveThreshold = std::max(0.0f, options.noiseThreshold);
                settings.glassShadows = !options.caustics;
                settings.equiangular = options.equiangular;
                settings.restirSamples = options.restir && settings.samples <= 16 ? settings.samples : 0;
                settings.threads = options.threads;
                settings.seed = slice * 7919u;          //svaki odsjecak svoj sum, inace bi se isti uzorci ponovili
                Tracer::Frame raw;
                bool rendered = false;
                if(options.gpu && gpuAttached){
                    GpuJob job;
                    job.scene = sliceScene;
                    job.settings = settings;
                    job.options = options;
                    job.plateLoaded = plateLoaded;
                    job.frame = frame;
                    job.index = index;
                    job.count = frames.size();
                    job.slice = slice;
                    job.slices = slices;
                    std::string problem;
                    rendered = renderOnGpu(job, raw, problem);
                    if(stopFlag) break;
                    if(!rendered) say("GPU render failed (" + problem + ") - rendering on the CPU.");
                }
                if(!rendered){
                    Tracer::Renderer renderer(sliceScene);
                    auto lastPublish = std::chrono::steady_clock::now() - std::chrono::seconds(10);
                    renderer.render(settings, [&](const Tracer::RenderProgress& progress){
                        {
                            std::lock_guard<std::mutex> guard(lock);
                            state.samples = slice * perSlice + progress.samplesDone;
                            state.frameDone = uint32_t(index);
                            state.progress = (float(index) + (float(slice) + float(progress.samplesDone) / float(progress.samplesTotal)) /
                                              float(slices)) / float(frames.size());
                            state.seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
                            char text[160];
                            std::snprintf(text, sizeof(text), "Rendering frame %.0f (%zu/%zu) on CPU: %u/%u samples, %.1f Mrays/s", frame,
                                          index + 1, frames.size(), slice * perSlice + progress.samplesDone, perSlice * slices,
                                          progress.seconds > 0.0 ? double(progress.rays) / progress.seconds / 1e6 : 0.0);
                            state.status = text;
                        }
                        //Slika u prozor najvise dvaput u sekundi: kompozit velikog kadra nije besplatan
                        const auto now = std::chrono::steady_clock::now();
                        if(slices == 1 && (now - lastPublish > std::chrono::milliseconds(500) || progress.samplesDone == progress.samplesTotal)){
                            lastPublish = now;
                            publish(renderer.frame(false), renderer.scene(), options, plateLoaded, frame);
                        }
                    }, &stopFlag);
                    if(stopFlag) break;
                    raw = renderer.frame(false);
                }
                //Tekuci prosjek odsjecaka; dubina iz srednjeg (prosjek dubina je dubina na kojoj nista ne stoji)
                blendSlice(result, raw, slice, slice == slices / 2);
                if(slices > 1 && compiled) publish(result, compiled->world, options, plateLoaded, frame);
            }
            if(stopFlag){ say("Render cancelled."); break; }
            if(sliceFailed || !compiled){ failed = true; break; }
            if(slices > 1) for(size_t i = 0; i < result.pixelCount(); ++i){
                glm::vec3 n(result.normal[i * 3], result.normal[i * 3 + 1], result.normal[i * 3 + 2]);
                if(glm::dot(n, n) > 0.0f) n = glm::normalize(n);
                for(int k = 0; k < 3; ++k) result.normal[i * 3 + size_t(k)] = n[k];
            }
            result.samples = perSlice * slices;
            if(options.denoise){
                const auto denoiseStart = std::chrono::steady_clock::now();
                Tracer::denoiseFrame(result, options.denoiser);
                if(index == 0){
                    const bool oidn = options.denoiser != Tracer::Denoiser::ATrous && Tracer::oidnAvailable();
                    char line[128];
                    const std::string denoiser = oidn ? "OIDN (" + Tracer::oidnDevice() + ")" : "A-trous";
                    std::snprintf(line, sizeof(line), "Denoised with %s in %.2f s", denoiser.c_str(),
                                  std::chrono::duration<double>(std::chrono::steady_clock::now() - denoiseStart).count());
                    say(line);
                }
                //Ocisceni kadrovi ne titraju: prosli kadar prebacen po dubini u ovaj
                if(frames.size() > 1 && options.temporal > 0.0f){
                    const Tracer::TemporalStats stats = Tracer::stabilize(result, compiled->world.camera, history, options.temporal);
                    if(index == 1){
                        char line[128];
                        std::snprintf(line, sizeof(line), "Temporal stability %.2f: %.0f %% of pixels from the previous frame", options.temporal,
                                      100.0 * double(stats.reused) / double(std::max<size_t>(1, result.pixelCount())));
                        say(line);
                    }
                }
            }
            publish(result, compiled->world, options, plateLoaded, frame);
            const std::vector<std::string> files = writeRender(result, compiled->world, options, plateLoaded, catchers, folder,
                                                               frameStem(options, frame), error, frame);
            {
                //Zadnji kadar ostaje za post poslije rendera (restyle) - bez ponovnog racunanja
                std::lock_guard<std::mutex> guard(lock);
                last = std::make_shared<Last>();
                last->frame = result;
                last->plate = compiled->world.backplate;
                last->plateLoaded = plateLoaded;
                last->frameNumber = frame;
                last->folder = folder;
                last->stem = frameStem(options, frame);
            }
            if(!error.empty()){ say("Could not write render: " + error); failed = true; break; }
            for(const std::string& file : files) say("Wrote " + file);
        }
        std::lock_guard<std::mutex> guard(lock);
        state.running = false;
        state.finished = !failed && !stopFlag;
        state.failed = failed;
        state.progress = state.finished ? 1.0f : state.progress;
        state.seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
        if(state.finished){
            char text[128];
            std::snprintf(text, sizeof(text), "Render finished in %.1f s", state.seconds);
            state.status = text;
            state.log.push_back(text);
        }
    }
};

}
