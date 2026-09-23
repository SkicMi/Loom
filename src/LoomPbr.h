#pragma once
//=============================================================================================
// MESHEVI S PBR MATERIJALIMA U POGLEDU EDITORA: glTF modeli, kocke i ravnine iz Warpa.
//
// Crta se na kartici, pravim rasterom s dubinom (shaders/pbr.slang), u zasebnu metu velicine
// pogleda - pa se slika slozi preko ploce i splata, a crte scene (kamere, tocke, strelice) idu
// preko nje. DrawList pogleda to ne moze: nema dubinu ni teksture.
//
// KAMERA JE ISTA KAO U POGLEDU. Pogled editora projicira pinholeom u pikselima (vidi
// LoomViewport.h); Loomova Camera zna vidno polje i pomak glavne tocke u pikselima. Iz jednog se
// dobije drugo tocno - test (test_editor_pbr) usporedjuje rub kocke na slici s pogledovom
// projekcijom njezinih vrhova, pa se kocka kroz rijesenu kameru poklapa sa snimkom kao i crte.
//
// STO SE GDJE DRZI:
//
//   modeli     glTF se cita u zasebnoj niti (slike su 2K-4K i dekodiraju se sekundama); geometrija
//              ide na karticu u komadima do 65535 vrhova - Loomov Mesh ima 16-bitne indekse
//   teksture   po (datoteka, slika, sRGB ili linearno): boja i emisija su sRGB, metalnost,
//              hrapavost, normal i occlusion linearni. Mapa koje nema je 1x1 neutralna
//   materijali po materijalu scene; faktori se salju svaki kadar (uredjivanje se vidi odmah),
//              a Loomov Material se gradi iznova samo kad se promijene mape ili nacin crtanja
//=============================================================================================
#include "LoomViewport.h"

#include "Core/Camera.h"
#include "Core/Light.h"
#include "Core/LoomInitializer.h"
#include "Vulkan/Material.h"
#include "Vulkan/Mesh.h"
#include "Vulkan/RenderTarget.h"
#include "Vulkan/Texture.h"

#include <Spool/Gltf.h>
#include <Spool/ImageFile.h>
#include <Warp/Stage.h>

#include <algorithm>
#include <cstdio>
#include <atomic>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace Loom{

//Isti raspored kao PbrData u pbr.slang (std140: cetiri vec4 pa osam skalara)
struct PbrData{
    glm::vec4 baseColor{1.0f};
    glm::vec4 emissive{0.0f};
    glm::vec4 skyColor{0.55f, 0.62f, 0.72f, 1.0f};      //a = izlozenost
    glm::vec4 groundColor{0.22f, 0.20f, 0.18f, 1.0f};
    float metallic = 0.0f;
    float roughness = 0.5f;
    float normalScale = 1.0f;
    float occlusionStrength = 1.0f;
    float alphaCutoff = 0.5f;
    uint32_t alphaMode = 0;
    float padding0 = 0.0f, padding1 = 0.0f;
};

//Trokuti u komade od najvise 65535 vrhova, u Loomov format vrha. Normale se izracunaju kad ih
//datoteka nema (glatke: zbroj normala susjednih lica)
inline std::vector<std::pair<std::vector<Vertex>, std::vector<uint16_t>>> meshChunks(const Spool::GltfPrimitive& p){
    const size_t count = p.vertexCount();
    std::vector<float> normals = p.normals;
    if(normals.size() != count * 3){
        normals.assign(count * 3, 0.0f);
        for(size_t t = 0; t + 2 < p.indices.size(); t += 3){
            const uint32_t a = p.indices[t], b = p.indices[t + 1], c = p.indices[t + 2];
            const glm::vec3 pa(p.positions[a * 3], p.positions[a * 3 + 1], p.positions[a * 3 + 2]);
            const glm::vec3 pb(p.positions[b * 3], p.positions[b * 3 + 1], p.positions[b * 3 + 2]);
            const glm::vec3 pc(p.positions[c * 3], p.positions[c * 3 + 1], p.positions[c * 3 + 2]);
            const glm::vec3 n = glm::cross(pb - pa, pc - pa);
            for(uint32_t v : {a, b, c}) for(int k = 0; k < 3; ++k) normals[v * 3 + size_t(k)] += n[k];
        }
    }
    auto vertexAt = [&](uint32_t i){
        Vertex v;
        v.position = glm::vec3(p.positions[i * 3], p.positions[i * 3 + 1], p.positions[i * 3 + 2]);
        v.color = p.colors.size() == count * 4 ? glm::vec3(p.colors[i * 4], p.colors[i * 4 + 1], p.colors[i * 4 + 2]) : glm::vec3(1.0f);
        v.texCoord = p.uv0.size() == count * 2 ? glm::vec2(p.uv0[i * 2], p.uv0[i * 2 + 1]) : glm::vec2(0.0f);
        const glm::vec3 n(normals[i * 3], normals[i * 3 + 1], normals[i * 3 + 2]);
        v.normal = glm::dot(n, n) > 1e-20f ? glm::normalize(n) : glm::vec3(0.0f, 1.0f, 0.0f);
        return v;
    };

    std::vector<std::pair<std::vector<Vertex>, std::vector<uint16_t>>> chunks;
    std::unordered_map<uint32_t, uint16_t> local;
    std::vector<Vertex> vertices;
    std::vector<uint16_t> indices;
    for(size_t t = 0; t + 2 < p.indices.size(); t += 3){
        if(vertices.size() + 3 > 65535){
            chunks.push_back({std::move(vertices), std::move(indices)});
            vertices.clear(); indices.clear(); local.clear();
        }
        for(int k = 0; k < 3; ++k){
            const uint32_t original = p.indices[t + size_t(k)];
            auto found = local.find(original);
            if(found == local.end()){
                found = local.emplace(original, uint16_t(vertices.size())).first;
                vertices.push_back(vertexAt(original));
            }
            indices.push_back(found->second);
        }
    }
    if(!indices.empty()) chunks.push_back({std::move(vertices), std::move(indices)});
    return chunks;
}

//Jedinicna kocka oko ishodista (24 vrha: svaka ploha svoje normale i UV) i ravnina 1x1 u XZ
inline Spool::GltfPrimitive unitShape(Warp::Shape shape){
    Spool::GltfPrimitive p;
    auto quad = [&](glm::vec3 centre, glm::vec3 u, glm::vec3 v){
        const glm::vec3 n = glm::normalize(glm::cross(u, v));
        const uint32_t base = uint32_t(p.vertexCount());
        const glm::vec3 corners[4] = {centre - u - v, centre + u - v, centre + u + v, centre - u + v};
        const float uvs[8] = {0, 1, 1, 1, 1, 0, 0, 0};
        for(int k = 0; k < 4; ++k){
            p.positions.insert(p.positions.end(), {corners[k].x, corners[k].y, corners[k].z});
            p.normals.insert(p.normals.end(), {n.x, n.y, n.z});
            p.uv0.insert(p.uv0.end(), {uvs[k * 2], uvs[k * 2 + 1]});
        }
        p.indices.insert(p.indices.end(), {base, base + 1, base + 2, base, base + 2, base + 3});
    };
    if(shape == Warp::Shape::Plane){
        quad(glm::vec3(0.0f), glm::vec3(0.0f, 0.0f, 0.5f), glm::vec3(0.5f, 0.0f, 0.0f));
        return p;
    }
    const float h = 0.5f;
    quad({0, 0, h}, {h, 0, 0}, {0, h, 0});      //+Z
    quad({0, 0, -h}, {-h, 0, 0}, {0, h, 0});    //-Z
    quad({h, 0, 0}, {0, 0, -h}, {0, h, 0});     //+X
    quad({-h, 0, 0}, {0, 0, h}, {0, h, 0});     //-X
    quad({0, h, 0}, {h, 0, 0}, {0, 0, -h});     //+Y
    quad({0, -h, 0}, {h, 0, 0}, {0, 0, h});     //-Y
    return p;
}

//Loomova Camera iz kamere pogleda: isti polozaj, isti smjer, vidno polje i glavna tocka iz
//zarisne u pikselima. area je pravokutnik prozora u koji se crta; pixelScale okvir/prozor
inline Camera loomCameraFor(const ViewCamera& view, const Treadle::Rect& area, float pixelScale, float nearPlane, float farPlane){
    const glm::mat4 world = glm::inverse(view.view);
    CameraConfig config;
    config.nearPlane = nearPlane;
    config.farPlane = farPlane;
    const float height = area.height * pixelScale;
    config.fovY = 2.0f * std::atan(0.5f * height / (view.focal * pixelScale));
    config.principalPointX = (view.centre.x - (area.x + 0.5f * area.width)) * pixelScale;
    config.principalPointY = (view.centre.y - (area.y + 0.5f * area.height)) * pixelScale;
    Camera camera(config);
    camera.setPosition(glm::vec3(world[3]));
    camera.setOrientation(glm::normalize(glm::quat_cast(glm::mat3(glm::normalize(glm::vec3(world[0])),
                                                                  glm::normalize(glm::vec3(world[1])),
                                                                  glm::normalize(glm::vec3(world[2]))))));
    return camera;
}

class ViewportMeshes{
public:
    //readable: meta se moze procitati (test); tada se ne moze i prikazati
    explicit ViewportMeshes(LoomInitializer& loom, bool readable = false) : loom(loom), readable(readable){
        const uint8_t white[4] = {255, 255, 255, 255};
        const uint8_t flat[4] = {128, 128, 255, 255};
        TextureConfig srgb;
        srgb.generateMipmaps = false;
        TextureConfig linear = srgb;
        linear.format = vk::Format::eR8G8B8A8Unorm;
        whiteSrgb.emplace(loom.device, loom.command, white, vk::Extent2D{1, 1}, srgb);
        whiteLinear.emplace(loom.device, loom.command, white, vk::Extent2D{1, 1}, linear);
        flatNormal.emplace(loom.device, loom.command, flat, vk::Extent2D{1, 1}, linear);

        //Kljucno svjetlo odozgo sprijeda i slabije s druge strane: oblik se cita i bez okoline
        LightConfig key;
        key.direction = {-0.45f, -1.0f, -0.35f};
        key.color = {1.0f, 0.97f, 0.92f};
        key.intensity = 2.6f;
        keyLight.emplace(key);
        LightConfig fill;
        fill.direction = {0.6f, -0.3f, 0.7f};
        fill.color = {0.75f, 0.82f, 1.0f};
        fill.intensity = 0.6f;
        fillLight.emplace(fill);
        loom.renderer.addLight(*keyLight);
        loom.renderer.addLight(*fillLight);
    }

    ~ViewportMeshes(){
        for(auto& [path, asset] : assets) if(asset->loader.joinable()) asset->loader.join();
    }

    //Model iz datoteke: CPU scena kad je procitana, inace nullptr (i citanje krene)
    const Spool::GltfScene* scene(const std::string& path){
        Asset& a = asset(path);
        return a.state == 1 ? a.cpu.get() : nullptr;
    }
    bool loading() const{
        for(const auto& [path, a] : assets) if(a->state == 0) return true;
        return false;
    }
    //Greske citanja, jednom po datoteci
    std::vector<std::string> takeErrors(){
        std::vector<std::string> out;
        for(auto& [path, a] : assets){
            if(a->state == 2 && !a->reported){ a->reported = true; out.push_back(path + ": " + a->error); }
        }
        return out;
    }

    float exposure = 1.0f;
    size_t drawnPrimitives = 0;

    //PRIJE beginFrame: modeli na karticu, meta velicine pogleda, kamera. false: nema sto crtati
    bool prepare(const Warp::Stage& stage, double frame, const ViewCamera& view, float pixelScale,
                 float nearPlane, float farPlane){
        items.clear();
        drawnPrimitives = 0;
        for(auto& [path, a] : assets){
            if(a->state != 0 && a->loader.joinable()) a->loader.join();
            if(a->state == 1 && !a->uploaded) upload(*a);
        }

        //Sto se crta: tijela i modeli koji su na kartici
        stage.walk([&](const Warp::Entity& entity, int){
            if(!entity.visible) return;
            if(entity.mesh){
                GpuPrimitive& gpu = primitive(entity.mesh->shape);
                for(Mesh& mesh : gpu.chunks){
                    items.push_back({&mesh, stage.worldMatrix(entity.id, frame), entity.mesh->material, entity.mesh->colour,
                                     entity.mesh->shape == Warp::Shape::Plane});
                }
            }
            if(entity.model){
                Asset& a = asset(entity.model->path);
                if(a.state != 1 || !a.uploaded || entity.model->mesh < 0 || size_t(entity.model->mesh) >= a.gpu.size()) return;
                const glm::mat4 world = stage.worldMatrix(entity.id, frame);
                const std::vector<GpuPrimitive>& prims = a.gpu[size_t(entity.model->mesh)];
                for(size_t p = 0; p < prims.size(); ++p){
                    const int material = p < entity.model->materials.size() ? entity.model->materials[p] : -1;
                    for(Mesh& mesh : const_cast<GpuPrimitive&>(prims[p]).chunks){
                        items.push_back({&mesh, world, material, glm::vec3(0.8f), false});
                    }
                }
            }
        });
        if(items.empty()) return false;

        const vk::Extent2D extent{uint32_t(std::max(16.0f, view.rect.width * pixelScale)),
                                  uint32_t(std::max(16.0f, view.rect.height * pixelScale))};
        if(!target || extent.width != size.width || extent.height != size.height) build(extent);
        camera.emplace(loomCameraFor(view, view.rect, pixelScale, nearPlane, farPlane));
        eye = glm::vec3(glm::inverse(view.view)[3]);

        //Materijali: gradnja kad se promijene mape, faktori svaki kadar
        for(Item& item : items) item.gpu = &materialFor(stage, item.material, item.colour, item.forceDoubleSided);
        return true;
    }

    //POSLIJE beginFrame, PRIJE prolaza u prozor: meshevi u metu
    void render(){
        loom.renderer.setCamera(*camera);
        loom.renderer.beginPass(*target);
        std::vector<const Item*> blended;
        for(const Item& item : items){
            if(item.gpu->kind == 2){ blended.push_back(&item); continue; }
            loom.renderer.draw(*item.mesh, item.world, *item.gpu->material);
            ++drawnPrimitives;
        }
        //Prozirno od straga prema naprijed, po sredistu tijela
        std::sort(blended.begin(), blended.end(), [&](const Item* a, const Item* b){
            return glm::length(glm::vec3(a->world[3]) - eye) > glm::length(glm::vec3(b->world[3]) - eye);
        });
        for(const Item* item : blended){
            loom.renderer.draw(*item->mesh, item->world, *item->gpu->material);
            ++drawnPrimitives;
        }
        loom.renderer.endPass();
    }

    //U prolazu u prozor: slika u pravokutnik (pikseli okvira), premultiplicirano preko onoga ispod
    void present(const vk::Rect2D& pixels, vk::Extent2D framebuffer){
        if(!presentMaterial) return;
        const vk::raii::CommandBuffer& commands = loom.renderer.borrowCommands();
        commands.setViewport(0, vk::Viewport{float(pixels.offset.x), float(pixels.offset.y),
                                             float(pixels.extent.width), float(pixels.extent.height), 0.0f, 1.0f});
        commands.setScissor(0, pixels);
        loom.renderer.drawFullscreen(*presentMaterial);
        commands.setViewport(0, vk::Viewport{0.0f, 0.0f, float(framebuffer.width), float(framebuffer.height), 0.0f, 1.0f});
        commands.setScissor(0, vk::Rect2D{{0, 0}, framebuffer});
    }

    //Za test: meta u RGBA16F
    ImageData readPixels() const{
        ImageData out;
        if(!target || !readable) return out;
        loom.waitIdle();
        const vk::DeviceSize bytes = vk::DeviceSize(size.width) * size.height * 8;
        VulkanBuffer staging(loom.device, bytes, vk::BufferUsageFlagBits::eTransferDst, MemoryUsage::GPU_TO_CPU);
        loom.command.copyImageToBuffer(target->getColorImage().getImage(), staging.getBuffer(), size);
        out.extent = size;
        out.format = vk::Format::eR16G16B16A16Sfloat;
        out.pixels.resize(size_t(bytes));
        staging.download(out.pixels.data(), bytes);
        return out;
    }

private:
    struct GpuPrimitive{ std::vector<Mesh> chunks; };

    struct Asset{
        std::shared_ptr<Spool::GltfScene> cpu = std::make_shared<Spool::GltfScene>();
        std::thread loader;
        std::atomic<int> state{0};          //0 cita se, 1 spremno, 2 greska
        std::string error;
        bool uploaded = false;
        bool reported = false;
        std::vector<std::vector<GpuPrimitive>> gpu;
    };

    struct GpuMaterial{
        std::unique_ptr<Material> material;
        std::string signature;
        int kind = 0;                       //0 neprozirno, 1 neprozirno dvostrano, 2 prozirno
    };

    struct Item{
        Mesh* mesh;
        glm::mat4 world;
        int material;
        glm::vec3 colour;
        bool forceDoubleSided;
        GpuMaterial* gpu = nullptr;
    };

    Asset& asset(const std::string& path){
        auto found = assets.find(path);
        if(found != assets.end()) return *found->second;
        auto created = std::make_unique<Asset>();
        Asset& a = *created;
        assets.emplace(path, std::move(created));
        a.loader = std::thread([&a, path]{
            std::string error;
            if(Spool::loadGltf(path, *a.cpu, error)) a.state = 1;
            else{ a.error = error; a.state = 2; }
        });
        return a;
    }

    void upload(Asset& a){
        a.gpu.clear();
        for(const Spool::GltfMesh& mesh : a.cpu->meshes){
            std::vector<GpuPrimitive> prims;
            for(const Spool::GltfPrimitive& p : mesh.primitives){
                GpuPrimitive gpu;
                for(auto& [vertices, indices] : meshChunks(p)) gpu.chunks.emplace_back(loom.device, loom.command, vertices, indices);
                prims.push_back(std::move(gpu));
            }
            a.gpu.push_back(std::move(prims));
        }
        a.uploaded = true;
    }

    GpuPrimitive& primitive(Warp::Shape shape){
        std::optional<GpuPrimitive>& slot = shape == Warp::Shape::Cube ? cube : plane;
        if(!slot){
            slot.emplace();
            for(auto& [vertices, indices] : meshChunks(unitShape(shape))) slot->chunks.emplace_back(loom.device, loom.command, vertices, indices);
        }
        return *slot;
    }

    void build(vk::Extent2D extent){
        loom.waitIdle();
        presentMaterial.reset();
        target.reset();
        RenderTargetConfig config;
        config.colorFormat = vk::Format::eR16G16B16A16Sfloat;
        config.enableDepth = true;
        config.extraColorUsage = vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferSrc;
        config.finalLayout = readable ? vk::ImageLayout::eTransferSrcOptimal : vk::ImageLayout::eShaderReadOnlyOptimal;
        target.emplace(loom.device, extent, config);
        size = extent;

        if(pipelines.empty()){
            const vk::Format depth = target->getDepthImage()->getFormat();
            for(int kind = 0; kind < 3; ++kind){
                PipelineConfig p;
                p.descriptorBindings = {Texture::getLayoutBinding(0), Texture::getLayoutBinding(1), Texture::getLayoutBinding(2),
                                        Texture::getLayoutBinding(3), Texture::getLayoutBinding(4), Material::getDataLayoutBinding(5)};
                p.vertShaderPath = std::string(LOOM_SHADER_DIR) + "/pbr.vert.spv";
                p.fragShaderPath = std::string(LOOM_SHADER_DIR) + "/pbr.frag.spv";
                p.colorFormat = config.colorFormat;
                p.depthTestEnable = true;
                p.depthWriteEnable = kind != 2;
                p.depthCompare = vk::CompareOp::eLessOrEqual;
                p.cullMode = kind == 0 ? vk::CullModeFlagBits::eBack : vk::CullModeFlagBits::eNone;
                p.blendMode = kind == 2 ? BlendMode::Premultiplied : BlendMode::None;
                p.allowShadingRateAttachment = false;
                pipelines.push_back(std::make_unique<VulkanGraphicsPipeline>(loom.device, p, config.colorFormat, depth));
            }
            PipelineConfig show;
            show.vertexBindings.clear();
            show.vertexAttributes.clear();
            show.descriptorBindings = {Texture::getLayoutBinding(), Material::getDataLayoutBinding()};
            show.vertShaderPath = std::string(LOOM_SHADER_DIR) + "/fullscreen.vert.spv";
            show.fragShaderPath = std::string(LOOM_SHADER_DIR) + "/fullscreen.frag.spv";
            show.cullMode = vk::CullModeFlagBits::eNone;
            show.blendMode = BlendMode::Premultiplied;
            show.pushConstantSize = 0;
            presentPipeline = std::make_unique<VulkanGraphicsPipeline>(loom.createPipeline(show));
        }
        if(!readable) presentMaterial.emplace(loom.device, loom.command, loom.getDescriptorPool(), *presentPipeline, target->getSampled());
    }

    //Tekstura mape; neutralna kad mape nema ili se (jos) ne da procitati
    SampledImage textureFor(const Warp::TextureSlot& slot, bool srgb, const Texture& fallback, std::string& signature){
        if(slot.empty()){ signature += "-|"; return fallback.getSampled(); }
        const std::string key = slot.source + "#" + std::to_string(slot.image) + (srgb ? "s" : "l");
        auto found = textures.find(key);
        if(found != textures.end()){ signature += key + "|"; return found->second->getSampled(); }
        if(failed.count(key)){ signature += "x|"; return fallback.getSampled(); }

        Spool::Image image;
        if(slot.image >= 0){
            const Spool::GltfScene* s = scene(slot.source);
            if(!s){
                if(asset(slot.source).state == 2) failed.insert(key);
                signature += "?|";                    //jos se cita: gradi se ponovno kad stigne
                return fallback.getSampled();
            }
            if(size_t(slot.image) < s->images.size()) image = s->images[size_t(slot.image)].pixels;
        }else{
            try{ image = Spool::loadImage(slot.source); }catch(const std::exception&){}
        }
        if(!image.isValid()){ failed.insert(key); signature += "x|"; return fallback.getSampled(); }
        TextureConfig config;
        config.format = srgb ? vk::Format::eR8G8B8A8Srgb : vk::Format::eR8G8B8A8Unorm;
        config.anisotropyEnable = true;
        config.maxAnisotropy = 8.0f;
        auto texture = std::make_unique<Texture>(loom.device, loom.command, image.pixels.data(), vk::Extent2D{image.width, image.height}, config);
        const SampledImage sampled = texture->getSampled();
        textures.emplace(key, std::move(texture));
        signature += key + "|";
        return sampled;
    }

    GpuMaterial& materialFor(const Warp::Stage& stage, int index, glm::vec3 colour, bool forceDoubleSided){
        Warp::Material m;
        const bool known = index >= 0 && size_t(index) < stage.materials.size();
        if(known) m = stage.materials[size_t(index)];
        else{ m.baseColor = glm::vec4(colour, 1.0f); m.roughness = 0.6f; }
        if(forceDoubleSided) m.doubleSided = true;

        const int kind = m.alphaMode == Warp::Material::Alpha::Blend ? 2 : (m.doubleSided ? 1 : 0);
        std::string signature = std::to_string(kind) + ":";
        const SampledImage images[5] = {
            textureFor(m.baseColorMap, true, *whiteSrgb, signature),
            textureFor(m.metallicRoughnessMap, false, *whiteLinear, signature),
            textureFor(m.normalMap, false, *flatNormal, signature),
            textureFor(m.occlusionMap, false, *whiteLinear, signature),
            textureFor(m.emissiveMap, true, *whiteSrgb, signature)};

        char fallbackKey[96];
        std::snprintf(fallbackKey, sizeof(fallbackKey), "%.4f %.4f %.4f %d", colour.r, colour.g, colour.b, kind);
        GpuMaterial& gpu = known ? stageMaterials[index] : fallbacks[fallbackKey];
        if(!gpu.material || gpu.signature != signature){
            loom.waitIdle();
            PbrData data;
            gpu.material = std::make_unique<Material>(loom.device, loom.command, loom.getDescriptorPool(), *pipelines[size_t(kind)],
                                                      std::vector<SampledImage>(images, images + 5), &data, sizeof(data));
            gpu.signature = signature;
            gpu.kind = kind;
        }
        PbrData data;
        data.baseColor = m.baseColor;
        data.emissive = glm::vec4(m.emissive * m.emissiveStrength, 0.0f);
        data.skyColor.a = exposure;
        data.metallic = m.metallic;
        data.roughness = m.roughness;
        data.normalScale = m.normalMap.empty() ? 1.0f : m.normalMap.amount;
        data.occlusionStrength = m.occlusionMap.empty() ? 0.0f : m.occlusionMap.amount;
        data.alphaCutoff = m.alphaCutoff;
        data.alphaMode = m.alphaMode == Warp::Material::Alpha::Mask ? 1u : m.alphaMode == Warp::Material::Alpha::Blend ? 2u : 0u;
        gpu.material->setData(&data, sizeof(data));
        return gpu;
    }

    LoomInitializer& loom;
    bool readable;
    std::optional<Texture> whiteSrgb, whiteLinear, flatNormal;
    std::optional<Light> keyLight, fillLight;
    std::map<std::string, std::unique_ptr<Asset>> assets;
    std::map<std::string, std::unique_ptr<Texture>> textures;
    std::set<std::string> failed;
    std::optional<GpuPrimitive> cube, plane;
    std::vector<std::unique_ptr<VulkanGraphicsPipeline>> pipelines;
    std::unique_ptr<VulkanGraphicsPipeline> presentPipeline;
    std::optional<RenderTarget> target;
    std::optional<Material> presentMaterial;
    //Po indeksu u sceni. std::map a ne vektor: stavke kadra drze pokazivace na materijale, a
    //vektor koji naraste usred kadra bi ih sve pomaknuo
    std::map<int, GpuMaterial> stageMaterials;
    std::map<std::string, GpuMaterial> fallbacks;
    std::optional<Camera> camera;
    glm::vec3 eye{0.0f};
    vk::Extent2D size{0, 0};
    std::vector<Item> items;

};

}
