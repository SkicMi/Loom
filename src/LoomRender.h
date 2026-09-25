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
#include <Spool/Gltf.h>
#include <Spool/ImageFile.h>
#include <Spool/VideoFile.h>
#include <Tracer/Denoise.h>
#include <Tracer/Renderer.h>
#include <Warp/Stage.h>

#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdio>
#include <filesystem>
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

    //Dodatni slojevi
    bool depth = true;
    bool normal = false;
    bool albedo = false;

    //Kvaliteta
    uint32_t samples = 128;
    uint32_t maxBounces = 12;
    float indirectClamp = 16.0f;
    bool denoise = true;
    uint32_t threads = 0;                   //0 = sve jezgre

    //Svjetlo
    enum class Sky{ Physical, Hdri, Uniform };
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

private:
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
    std::vector<std::string> warnings;
};

inline bool buildTracerScene(const Warp::Stage& stage, double frame, const RenderOptions& options, RenderAssets& assets,
                             BuiltScene& out, std::string& error){
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
    }

    //-- snimka ------------------------------------------------------------------------------------
    const bool wantPlate = options.plate && !lens.plate.empty();
    if(wantPlate){
        Spool::Image plate;
        std::string problem;
        if(assets.plate(lens.plate, plateIndexFor(lens, frame), plate, problem)){
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
        if(image) index = int(scene.addTexture(tracerTexture(*image, srgb)));
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
        if(entity.mesh){
            const glm::mat4 world = stage.worldMatrix(entity.id, frame);
            const Tracer::ObjectFlags flags = flagsFor(entity);
            //Catcher ne smije obojiti CG svojom bojom tijela (narancasta kocka u pogledu): neutralno siv
            const uint32_t m = flags.shadowCatcher && entity.mesh->material < 0
                ? material(-1, glm::vec3(0.35f)) : material(entity.mesh->material, entity.mesh->colour);
            scene.addMesh(shapeMesh(entity.mesh->shape), world, m, entity.name, flags);
            ++out.objects;
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
                                            std::string& error){
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
        if(options.writePng) save(base + ".png", [&]{ return Tracer::toDisplay(beauty, frame.width, frame.height, options.view, options.exposure); });
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
    ~RenderSession(){ cancel(); join(); }

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
        state.samples = samples;
        state.frameDone = uint32_t(job.index);
        state.progress = (float(job.index) + float(samples) / float(std::max(1u, job.settings.samples))) / float(job.count);
        char text[192];
        std::snprintf(text, sizeof(text), "Rendering frame %.0f (%zu/%zu) on GPU: %u/%u samples%s", job.frame, job.index + 1,
                      job.count, samples, job.settings.samples, mraysPerSecond > 0.0 ? "" : "");
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

    void publish(const Tracer::Frame& frame, const Tracer::Scene& scene, const RenderOptions& options, bool plateLoaded){
        const std::vector<float> beauty = Tracer::composite(frame, backdropFor(options, plateLoaded),
                                                            plateLoaded ? &scene.backplate : nullptr);
        std::vector<uint8_t> display = Tracer::toDisplay(beauty, frame.width, frame.height, options.view, options.exposure);
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
        for(size_t index = 0; index < frames.size() && !stopFlag; ++index){
            const double frame = frames[index];
            BuiltScene built;
            std::string error;
            if(!buildTracerScene(stage, frame, options, assets, built, error)){ say("Render failed: " + error); failed = true; break; }
            if(index == 0) for(const std::string& w : built.warnings) say("Warning: " + w);
            char line[256];
            std::snprintf(line, sizeof(line), "Frame %.0f: %zu triangles, %zu objects (%zu shadow catchers), %ux%u",
                          frame, built.scene.triangles.size(), built.objects, built.catchers,
                          built.scene.camera.width, built.scene.camera.height);
            say(line);
            const bool plateLoaded = built.plateLoaded;
            const bool catchers = built.catchersUsed && built.catchers > 0;
            const std::shared_ptr<const Tracer::CompiledScene> compiled = Tracer::compile(std::move(built.scene));
            Tracer::RenderSettings settings;
            settings.samples = std::max(1u, options.samples);
            settings.maxBounces = options.maxBounces;
            settings.indirectClamp = options.indirectClamp;
            settings.threads = options.threads;
            Tracer::Frame result;
            bool rendered = false;
            if(options.gpu && gpuAttached){
                GpuJob job;
                job.scene = compiled;
                job.settings = settings;
                job.options = options;
                job.plateLoaded = plateLoaded;
                job.frame = frame;
                job.index = index;
                job.count = frames.size();
                std::string problem;
                rendered = renderOnGpu(job, result, problem);
                if(stopFlag){ say("Render cancelled."); break; }
                if(!rendered) say("GPU render failed (" + problem + ") - rendering this frame on the CPU.");
                else if(options.denoise) Tracer::denoiseFrame(result);
            }
            if(!rendered){
                Tracer::Renderer renderer(compiled);
                auto lastPublish = std::chrono::steady_clock::now() - std::chrono::seconds(10);
                renderer.render(settings, [&](const Tracer::RenderProgress& progress){
                    {
                        std::lock_guard<std::mutex> guard(lock);
                        state.samples = progress.samplesDone;
                        state.frameDone = uint32_t(index);
                        state.progress = (float(index) + float(progress.samplesDone) / float(progress.samplesTotal)) / float(frames.size());
                        state.seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
                        char text[160];
                        std::snprintf(text, sizeof(text), "Rendering frame %.0f (%zu/%zu) on CPU: %u/%u samples, %.1f Mrays/s", frame,
                                      index + 1, frames.size(), progress.samplesDone, progress.samplesTotal,
                                      progress.seconds > 0.0 ? double(progress.rays) / progress.seconds / 1e6 : 0.0);
                        state.status = text;
                    }
                    //Slika u prozor najvise dvaput u sekundi: kompozit velikog kadra nije besplatan
                    const auto now = std::chrono::steady_clock::now();
                    if(now - lastPublish > std::chrono::milliseconds(500) || progress.samplesDone == progress.samplesTotal){
                        lastPublish = now;
                        publish(renderer.frame(false), renderer.scene(), options, plateLoaded);
                    }
                }, &stopFlag);
                if(stopFlag){ say("Render cancelled."); break; }
                result = renderer.frame(options.denoise);
            }
            publish(result, compiled->world, options, plateLoaded);
            const std::vector<std::string> files = writeRender(result, compiled->world, options, plateLoaded, catchers, folder,
                                                               frameStem(options, frame), error);
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
