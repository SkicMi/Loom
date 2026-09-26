#include "TracerGpu/GpuTracer.h"

#include "Core/LoomInitializer.h"
#include "Vulkan/ComputeMaterial.h"
#include "Vulkan/VulkanBuffer.h"
#include "Vulkan/VulkanComputePipeline.h"

#include <Tracer/Bsdf.h>
#include <Tracer/Denoise.h>

#include <glm/gtc/type_ptr.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <optional>
#include <stdexcept>

namespace TracerGpu{

namespace{

constexpr uint32_t None = 0xFFFFFFFFu;
constexpr uint32_t TraceBindings = 26;
constexpr uint32_t ResolveBindings = 7;
constexpr uint32_t FinishBindings = 16;

//Raspored mora biti isti kao Params u shaders/tracer.slang: samo vec4 i uvec4, pa std430 nema
//sto poravnati drukcije nego C++
struct Params{
    glm::vec4 cameraX, cameraY, cameraZ, cameraPos;
    glm::vec4 inverseX, inverseY, inverseZ, inversePos;
    glm::vec4 lens, lens2;
    glm::uvec4 counts, env;
    glm::vec4 envColor;
    glm::vec4 toMap0, toMap1, toMap2;
    glm::vec4 fromMap0, fromMap1, fromMap2;
    glm::uvec4 flags;
    glm::vec4 values;
    glm::uvec4 extra;
    glm::vec4 distortion;
    glm::vec4 holdout;      //tekstura (bitovi, NONE), bias, broj kutija magle (bitovi), neprozirnih u BLAS-u
    glm::uvec4 motion;      //kljucevi geometrije, kljucevi kamere, slotova, vrhova
};
static_assert(sizeof(Params) == 25 * 16, "Params mora odgovarati shaders/tracer.slang");

struct GpuMaterial{
    glm::vec4 baseColor, surface, layers, emission;
    glm::ivec4 textures;
    glm::uvec4 mode;
};
static_assert(sizeof(GpuMaterial) == 96, "Material mora odgovarati shaders/tracer.slang");

struct GpuLight{
    glm::vec4 radiance, position, axis, cone, pick;
};
static_assert(sizeof(GpuLight) == 80, "Light mora odgovarati shaders/tracer.slang");

struct TracePush{ uint32_t sampleIndex, rowStart, rowCount, padding; };
struct ResolvePush{ uint32_t width, height, backdrop, view; float gain; uint32_t plate, checker, padding; };
struct FinishPush{
    uint32_t mode, width, height, step;
    uint32_t srcOffset, dstOffset, srcWidth, srcHeight;
    uint32_t dstWidth, dstHeight, backdrop, view;
    uint32_t plate, flags, grainSeed, padding;
    float gain, bloom, bloomThreshold, bloomScale;
    float chromatic, vignette, contrast, saturation;
    float grain, balanceR, balanceG, balanceB;
};
static_assert(sizeof(FinishPush) == 112, "FinishPush mora odgovarati shaders/tracer_finish.slang");

float bitsToFloat(uint32_t bits){ float f; std::memcpy(&f, &bits, 4); return f; }

vk::DescriptorSetLayoutBinding storageBinding(uint32_t index){
    vk::DescriptorSetLayoutBinding binding;
    binding.binding = index;
    binding.descriptorType = vk::DescriptorType::eStorageBuffer;
    binding.descriptorCount = 1;
    binding.stageFlags = vk::ShaderStageFlagBits::eCompute;
    return binding;
}

ComputePipelineConfig configFor(const char* shader, uint32_t bindings, uint32_t push){
    ComputePipelineConfig config;
    config.shaderPath = std::string(LOOM_SHADER_DIR) + "/" + shader;
    for(uint32_t i = 0; i < bindings; ++i) config.descriptorBindings.push_back(storageBinding(i));
    config.pushConstantSize = push;
    return config;
}

}

//Ray query: vezanja tracera, pa akceleracijska struktura (22) i slot po primitivu (23)
ComputePipelineConfig rayQueryConfig(){
    ComputePipelineConfig config = configFor("tracer_rq.comp.spv", TraceBindings, sizeof(TracePush));
    vk::DescriptorSetLayoutBinding accel;
    accel.binding = TraceBindings;
    accel.descriptorType = vk::DescriptorType::eAccelerationStructureKHR;
    accel.descriptorCount = 1;
    accel.stageFlags = vk::ShaderStageFlagBits::eCompute;
    config.descriptorBindings.push_back(accel);
    config.descriptorBindings.push_back(storageBinding(TraceBindings + 1));
    return config;
}

struct Pipelines::State{
    VulkanComputePipeline trace, resolve, finish;
    std::optional<VulkanComputePipeline> traceRayQuery;     //samo kad kartica ima hardverske zrake
    explicit State(const VulkanDevice& device)
    : trace(device, configFor("tracer.comp.spv", TraceBindings, sizeof(TracePush))),
      resolve(device, configFor("tracer_resolve.comp.spv", ResolveBindings, sizeof(ResolvePush))),
      finish(device, configFor("tracer_finish.comp.spv", FinishBindings, sizeof(FinishPush))){
        if(device.hasRayQuery()) traceRayQuery.emplace(device, rayQueryConfig());
    }
};

Pipelines::Pipelines(LoomInitializer& loom) : state(std::make_unique<State>(loom.device)){}
Pipelines::~Pipelines() = default;

struct GpuTracer::Buffers{
    std::vector<std::shared_ptr<VulkanBuffer>> owned;    //sve, redom vezanja shadera tracera (neki dijeljeni kroz UploadCache)
    std::optional<VulkanBuffer> display;
    std::optional<ComputeMaterial> trace, resolve, finish;
    //Hardverske zrake: BLAS (dvije geometrije), TLAS s jednom instancom i njihovi spremnici
    std::vector<VulkanBuffer> rayQuery;
    std::optional<vk::raii::AccelerationStructureKHR> bottom, top;
    //Za filtar i post na kartici, stvoreno kad prvi put zatreba: A, B, normala+dubina,
    //albedo+pokrivenost, slika, piramida bloom-a
    std::vector<VulkanBuffer> finishing;
    std::vector<glm::uvec4> levels;         //piramida: pocetak, sirina, visina
};

GpuTracer::GpuTracer(LoomInitializer& loom_, Pipelines& pipelines_, std::shared_ptr<const Tracer::CompiledScene> scene,
                     const Tracer::RenderSettings& settings_, bool allowRayQuery, UploadCache* cache)
: loom(loom_), pipelines(pipelines_), compiled(std::move(scene)), settings(settings_), buffers(std::make_unique<Buffers>()){
    const Tracer::CompiledScene& c = *compiled;
    const Tracer::Scene& world = c.world;
    size[0] = world.camera.width;
    size[1] = world.camera.height;
    settings.samples = std::max(1u, settings.samples);

    //-- teksture: scena, pa nebo, pa snimka. Osam bita u jedno polje, float u drugo ----------------
    std::vector<glm::uvec4> textureInfo;
    std::vector<uint32_t> texelsScene;
    std::vector<glm::vec4> texelsFloatScene;
    //frame: snimka i holdout - mijenjaju se svaki kadar, pa idu u svoje spremnike (24, 25) i ne
    //sprijece da teksture scene ostanu na kartici izmedju kadrova (UploadCache). Zastavica 1 << 16
    std::vector<uint32_t> frameTexels;
    std::vector<glm::vec4> frameTexelsFloat;
    auto addTexture = [&](const Tracer::Texture& t, bool frame = false){
        std::vector<uint32_t>& texels = frame ? frameTexels : texelsScene;
        std::vector<glm::vec4>& texelsFloat = frame ? frameTexelsFloat : texelsFloatScene;
        if(!t.valid()){ textureInfo.push_back(glm::uvec4(0)); return uint32_t(textureInfo.size() - 1); }
        //Mipmape iza osnovne razine, redom; broj razina u bitovima 8..15
        const uint32_t levels = uint32_t(std::min<size_t>(255, 1 + t.mips.size()));
        uint32_t flags = (t.srgb ? 1u : 0u) | (t.repeat ? 4u : 0u) | (levels << 8) | (frame ? (1u << 16) : 0u);
        uint32_t start = 0;
        const bool floating = !t.floats.empty();
        if(floating){ flags |= 2u; start = uint32_t(texelsFloat.size()); }
        else start = uint32_t(texels.size());
        for(uint32_t level = 0; level < levels; ++level){
            const Tracer::Texture& l = level == 0 ? t : t.mips[level - 1];
            const size_t count = size_t(l.width) * l.height;
            if(floating){
                for(size_t i = 0; i < count; ++i) texelsFloat.push_back(glm::make_vec4(l.floats.data() + i * 4));
            }else{
                const size_t at = texels.size();
                texels.resize(at + count);
                std::memcpy(texels.data() + at, l.bytes.data(), count * 4);    //RGBA8 = r | g<<8 | b<<16 | a<<24
            }
        }
        textureInfo.push_back(glm::uvec4(start, t.width, t.height, flags));
        return uint32_t(textureInfo.size() - 1);
    };
    for(const Tracer::Texture& t : world.textures) addTexture(t);
    const bool envTextured = c.sky.isTextured();
    const uint32_t envTexture = envTextured ? addTexture(world.environment.map) : None;
    if(world.backplate.valid()) backplateTexture = addTexture(world.backplate, true);
    const uint32_t holdoutTexture = world.holdout.valid() ? addTexture(world.holdout, true) : None;
    if(frameTexels.empty()) frameTexels.push_back(0);
    if(frameTexelsFloat.empty()) frameTexelsFloat.push_back(glm::vec4(0.0f));

    //-- trokuti redom BVH-a i ostalo po izvornom trokutu --------------------------------------------
    const std::vector<Tracer::Bvh::Node>& bvhNodes = c.tree.nodeArray();
    const std::vector<Tracer::Bvh::Prepared>& prepared = c.tree.preparedArray();
    const std::vector<uint32_t>& order = c.tree.orderArray();
    std::vector<glm::vec4> nodes;
    nodes.reserve(bvhNodes.size() * 2);
    for(const Tracer::Bvh::Node& n : bvhNodes){
        nodes.push_back(glm::vec4(n.min, bitsToFloat(n.leftOrFirst)));
        nodes.push_back(glm::vec4(n.max, bitsToFloat(n.count)));
    }
    std::vector<uint32_t> slotOf(world.triangles.size(), None);
    std::vector<glm::vec4> triangles;
    triangles.reserve(prepared.size() * 3);
    for(size_t slot = 0; slot < prepared.size(); ++slot){
        const uint32_t original = order[slot];
        slotOf[original] = uint32_t(slot);
        const int emitter = c.emitterOfTriangle[original];
        triangles.push_back(glm::vec4(prepared[slot].v0, bitsToFloat(original)));
        triangles.push_back(glm::vec4(prepared[slot].e1, bitsToFloat(c.triangleFlags[original])));
        triangles.push_back(glm::vec4(prepared[slot].e2, bitsToFloat(emitter >= 0 ? uint32_t(emitter) : None)));
    }
    std::vector<glm::uvec4> triangleInfo;
    triangleInfo.reserve(world.triangles.size());
    for(const Tracer::Triangle& t : world.triangles) triangleInfo.push_back(glm::uvec4(t.v[0], t.v[1], t.v[2], t.material));
    std::vector<glm::vec4> vertices;
    vertices.reserve(world.positions.size() * 3);
    for(size_t i = 0; i < world.positions.size(); ++i){
        const glm::vec3 n = world.normals[i];
        const glm::vec2 uv = world.uvs[i];
        const glm::vec4 t = world.tangents[i];
        vertices.push_back(glm::vec4(n, uv.x));
        vertices.push_back(glm::vec4(uv.y, t.x, t.y, t.z));
        vertices.push_back(glm::vec4(t.w, 0.0f, 0.0f, 0.0f));
    }
    std::vector<GpuMaterial> materials;
    for(const Tracer::Material& m : world.materials){
        GpuMaterial g;
        g.baseColor = glm::vec4(m.baseColor, m.opacity);
        g.surface = glm::vec4(m.metallic, m.roughness, m.normalScale, m.ior);
        g.layers = glm::vec4(m.specular, m.transmission, m.clearcoat, m.clearcoatRoughness);
        g.emission = glm::vec4(m.emission * m.emissionStrength, m.alphaCutoff);
        g.textures = glm::ivec4(m.baseColorTexture, m.metallicRoughnessTexture, m.normalTexture, m.emissionTexture);
        g.mode = glm::uvec4(m.alphaMode == Tracer::Material::Alpha::Mask ? 1u : m.alphaMode == Tracer::Material::Alpha::Blend ? 2u : 0u,
                            m.emissionTwoSided ? 1u : 0u, 0, 0);
        materials.push_back(g);
    }

    //-- svjetla ---------------------------------------------------------------------------------------
    std::vector<GpuLight> lights;
    uint32_t skyLight = None;
    for(size_t i = 0; i < c.lights.size(); ++i){
        const Tracer::LightRecord& l = c.lights[i];
        GpuLight g;
        g.radiance = glm::vec4(l.radiance, bitsToFloat(uint32_t(l.kind)));
        g.position = glm::vec4(l.position, l.radius);
        g.axis = glm::vec4(l.axis, l.cosMax);
        g.cone = glm::vec4(l.oneMinusCos, l.cosOuter, l.cosInner, bitsToFloat((l.delta ? 1u : 0u) | (l.spot ? 2u : 0u)));
        const uint32_t slot = l.kind == Tracer::LightRecord::Triangle ? slotOf[l.triangle] : 0u;
        g.pick = glm::vec4(c.lightPick[i], c.lightCumulative[i], bitsToFloat(slot), l.area);
        if(l.kind == Tracer::LightRecord::Sky) skyLight = uint32_t(i);
        lights.push_back(g);
    }
    std::vector<uint32_t> lightLists(c.suns);
    lightLists.insert(lightLists.end(), c.spheres.begin(), c.spheres.end());

    std::vector<float> envTables(c.sky.marginalCdf());
    envTables.insert(envTables.end(), c.sky.conditionalCdf().begin(), c.sky.conditionalCdf().end());
    std::vector<float> energy;
    const size_t tableSize = size_t(Tracer::EnergyTableSize) * Tracer::EnergyTableSize;
    for(const float* table : {Tracer::ggxAlbedoTable(), Tracer::ggxSchlickATable(), Tracer::ggxSchlickBTable()})
        energy.insert(energy.end(), table, table + tableSize);

    //-- parametri --------------------------------------------------------------------------------------
    Params p{};
    const glm::mat4& toWorld = world.camera.cameraToWorld;
    p.cameraX = toWorld[0]; p.cameraY = toWorld[1]; p.cameraZ = toWorld[2]; p.cameraPos = toWorld[3];
    p.inverseX = c.cameraInverse[0]; p.inverseY = c.cameraInverse[1]; p.inverseZ = c.cameraInverse[2]; p.inversePos = c.cameraInverse[3];
    p.lens = glm::vec4(world.camera.focalPixels, world.camera.centre.x, world.camera.centre.y, world.camera.apertureRadius);
    p.lens2 = glm::vec4(world.camera.focusDistance, float(size[0]), float(size[1]), c.sceneRadius);
    p.counts = glm::uvec4(uint32_t(lights.size()), uint32_t(c.suns.size()), uint32_t(c.spheres.size()), settings.maxBounces);
    p.env = glm::uvec4(envTextured ? 1u : 0u, envTexture, c.sky.mapWidth(), c.sky.mapHeight());
    p.envColor = glm::vec4(world.environment.color, world.environment.intensity);
    p.toMap0 = glm::vec4(c.sky.worldToMap()[0], 0.0f); p.toMap1 = glm::vec4(c.sky.worldToMap()[1], 0.0f); p.toMap2 = glm::vec4(c.sky.worldToMap()[2], 0.0f);
    p.fromMap0 = glm::vec4(c.sky.mapToWorld()[0], 0.0f); p.fromMap1 = glm::vec4(c.sky.mapToWorld()[1], 0.0f); p.fromMap2 = glm::vec4(c.sky.mapToWorld()[2], 0.0f);
    p.flags = glm::uvec4(c.sky.active() ? 1u : 0u, world.environment.cameraVisible ? 1u : 0u, backplateTexture, skyLight);
    p.values = glm::vec4(settings.indirectClamp, world.camera.distorted() ? world.camera.k1 : 0.0f,
                         world.camera.distorted() ? world.camera.k2 : 0.0f, settings.adaptiveThreshold);
    p.distortion = world.camera.distorted() ? world.camera.lens : glm::vec4(0.0f);
    //Hardverske zrake: slotovi bez zastavica (neprozirni) pa sa zastavicama - broj prvih u holdout.w
    //Hardverski motion blur postoji samo kao NVIDIA ekstenzija: scena s pomakom ide kroz vlastiti BVH
    const bool rayQuery = allowRayQuery && pipelines.state->traceRayQuery.has_value() && !prepared.empty() && !world.motion.active();
    //-- motion blur: kamera po kljucu (4 stupca), trokuti po kljucu redom BVH-a, normale po kljucu --
    const std::vector<std::vector<Tracer::Bvh::Prepared>>& keyed = c.tree.preparedKeys();
    const uint32_t cameraKeys = world.motion.cameras.size() >= 2 ? uint32_t(world.motion.cameras.size()) : 0u;
    const uint32_t geometryKeys = keyed.size() >= 2 ? uint32_t(keyed.size()) : 0u;
    std::vector<glm::vec4> motionData;
    for(uint32_t k = 0; k < cameraKeys; ++k) for(int column = 0; column < 4; ++column) motionData.push_back(world.motion.cameras[k][column]);
    for(uint32_t k = 0; k < geometryKeys; ++k) for(const Tracer::Bvh::Prepared& t : keyed[k]){
        motionData.push_back(glm::vec4(t.v0, 0.0f));
        motionData.push_back(glm::vec4(t.e1, 0.0f));
        motionData.push_back(glm::vec4(t.e2, 0.0f));
    }
    std::vector<glm::vec4> motionNormals;
    if(geometryKeys) for(uint32_t k = 0; k < geometryKeys; ++k){
        const std::vector<glm::vec3>& normals = world.motion.normals.size() == geometryKeys ? world.motion.normals[k] : world.normals;
        for(const glm::vec3& n : normals) motionNormals.push_back(glm::vec4(n, 0.0f));
    }
    if(motionData.empty()) motionData.push_back(glm::vec4(0.0f));
    if(motionNormals.empty()) motionNormals.push_back(glm::vec4(0.0f));
    p.motion = glm::uvec4(geometryKeys, cameraKeys, uint32_t(prepared.size()), uint32_t(world.positions.size()));
    std::vector<uint32_t> geometrySlots;
    uint32_t opaqueCount = 0;
    if(rayQuery){
        for(uint32_t slot = 0; slot < prepared.size(); ++slot) if(c.triangleFlags[order[slot]] == 0) geometrySlots.push_back(slot);
        opaqueCount = uint32_t(geometrySlots.size());
        for(uint32_t slot = 0; slot < prepared.size(); ++slot) if(c.triangleFlags[order[slot]] != 0) geometrySlots.push_back(slot);
    }
    p.holdout = glm::vec4(bitsToFloat(holdoutTexture), world.holdoutBias, bitsToFloat(uint32_t(world.volumes.size())), bitsToFloat(opaqueCount));
    //Magla: 3 retka svijet -> kutija, (albedo, gustoca), (g)
    std::vector<glm::vec4> volumes;
    for(size_t i = 0; i < world.volumes.size(); ++i){
        const glm::mat4 m = glm::transpose(c.volumeInverse[i]);
        volumes.push_back(m[0]); volumes.push_back(m[1]); volumes.push_back(m[2]);
        volumes.push_back(glm::vec4(world.volumes[i].albedo, world.volumes[i].density));
        volumes.push_back(glm::vec4(world.volumes[i].anisotropy, 0.0f, 0.0f, 0.0f));
    }
    if(volumes.empty()) volumes.push_back(glm::vec4(0.0f));
    p.extra = glm::uvec4(settings.seed, uint32_t(c.sky.marginalCdf().size()), (settings.glassShadows ? 1u : 0u) | (settings.mipmaps ? 0u : 2u),
                         settings.adaptiveMinSamples);

    //-- na karticu -------------------------------------------------------------------------------------
    const VulkanDevice& device = loom.device;
    const auto usage = vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferDst | vk::BufferUsageFlagBits::eTransferSrc;
    //cacheable: sadrzaj koji se u sekvenci obicno ne mijenja (teksture, nebo, tablice energije) -
    //isti hash, isti spremnik s kartice
    auto onCard = [&](const void* data, size_t count, bool cacheable = false){
        const uint32_t binding = uint32_t(buffers->owned.size());
        uint64_t hash = 0;
        if(cacheable && cache){
            hash = UploadCache::hashBytes(data, count);
            auto found = cache->entries.find(binding);
            if(found != cache->entries.end() && found->second.hash == hash && found->second.bytes == count){
                cache->reusedBytes += count;
                buffers->owned.push_back(found->second.buffer);
                return;
            }
        }
        const vk::DeviceSize size = std::max<vk::DeviceSize>(count, 16);
        auto card = std::make_shared<VulkanBuffer>(device, size, usage, MemoryUsage::GPU_ONLY);
        if(count > 0){
            VulkanBuffer staging(device, count, vk::BufferUsageFlagBits::eTransferSrc, MemoryUsage::CPU_TO_GPU);
            staging.upload(data, count);
            loom.command.copyBuffer(staging.getBuffer(), card->getBuffer(), count);
        }
        bytes += size;
        if(cacheable && cache){
            cache->entries[binding] = UploadCache::Entry{hash, count, card};
            cache->uploadedBytes += count;
        }
        buffers->owned.push_back(std::move(card));
    };
    //Nule jednom kopirane u svaki zbroj (VulkanCommand ne izlaze fillBuffer izvan kadra)
    const vk::DeviceSize pixelCount = vk::DeviceSize(size[0]) * size[1];
    std::optional<VulkanBuffer> zeros;
    auto zeroed = [&](vk::DeviceSize size){
        auto card = std::make_shared<VulkanBuffer>(device, size, usage, MemoryUsage::GPU_ONLY);
        if(!zeros){
            zeros.emplace(device, pixelCount * 16, vk::BufferUsageFlagBits::eTransferSrc, MemoryUsage::CPU_TO_GPU);
            std::vector<uint8_t> blank(size_t(pixelCount * 16), 0);
            zeros->upload(blank.data(), blank.size());
        }
        loom.command.copyBuffer(zeros->getBuffer(), card->getBuffer(), size);
        bytes += size;
        buffers->owned.push_back(std::move(card));
    };
    buffers->owned.reserve(TraceBindings);
    onCard(&p, sizeof(p));                                                  //0
    onCard(nodes.data(), nodes.size() * sizeof(glm::vec4), true);                 //1
    onCard(triangles.data(), triangles.size() * sizeof(glm::vec4), true);         //2
    onCard(triangleInfo.data(), triangleInfo.size() * sizeof(glm::uvec4), true);  //3
    onCard(vertices.data(), vertices.size() * sizeof(glm::vec4), true);           //4
    onCard(materials.data(), materials.size() * sizeof(GpuMaterial), true);       //5
    onCard(textureInfo.data(), textureInfo.size() * sizeof(glm::uvec4), true);         //6
    onCard(texelsScene.data(), texelsScene.size() * sizeof(uint32_t), true);          //7
    onCard(texelsFloatScene.data(), texelsFloatScene.size() * sizeof(glm::vec4), true); //8
    onCard(lights.data(), lights.size() * sizeof(GpuLight), true);                //9
    onCard(lightLists.data(), lightLists.size() * sizeof(uint32_t), true);        //10
    onCard(envTables.data(), envTables.size() * sizeof(float), true);       //11
    onCard(energy.data(), energy.size() * sizeof(float), true);             //12
    const vk::DeviceSize pixels = vk::DeviceSize(size[0]) * size[1];
    for(int k = 0; k < 6; ++k) zeroed(pixels * 16);                         //13..18
    zeroed(pixels * 4);                                                     //19
    onCard(volumes.data(), volumes.size() * sizeof(glm::vec4));             //20
    zeroed(pixels * 4);                                                     //21 prilagodljivo stanje
    onCard(motionData.data(), motionData.size() * sizeof(glm::vec4));       //22 motion blur
    onCard(motionNormals.data(), motionNormals.size() * sizeof(glm::vec4)); //23
    onCard(frameTexels.data(), frameTexels.size() * sizeof(uint32_t));      //24 snimka, holdout
    onCard(frameTexelsFloat.data(), frameTexelsFloat.size() * sizeof(glm::vec4)); //25
    buffers->display.emplace(device, std::max<vk::DeviceSize>(pixels * 4, 16), usage, MemoryUsage::GPU_ONLY);

    if(rayQuery){
        buildAccelerationStructures(prepared, geometrySlots, opaqueCount);
        buffers->trace.emplace(device, loom.getDescriptorPool(), *pipelines.state->traceRayQuery);
        buffers->trace->setAccelerationStructure(TraceBindings, **buffers->top);
        buffers->trace->setStorageBuffer(TraceBindings + 1, buffers->rayQuery.back());
    }else buffers->trace.emplace(device, loom.getDescriptorPool(), pipelines.state->trace);
    for(uint32_t b = 0; b < TraceBindings; ++b) buffers->trace->setStorageBuffer(b, *buffers->owned[b]);
    buffers->resolve.emplace(device, loom.getDescriptorPool(), pipelines.state->resolve);
    buffers->resolve->setStorageBuffer(0, *buffers->owned[13]);
    buffers->resolve->setStorageBuffer(1, *buffers->owned[14]);
    buffers->resolve->setStorageBuffer(2, *buffers->owned[15]);
    buffers->resolve->setStorageBuffer(3, *buffers->owned[16]);
    buffers->resolve->setStorageBuffer(4, *buffers->owned[6]);
    buffers->resolve->setStorageBuffer(5, *buffers->owned[24]);         //snimka je u spremniku kadra
    buffers->resolve->setStorageBuffer(6, *buffers->display);
}

//BLAS iz istih trokuta kao BVH (v0, v0 + e1, v0 + e2, bez indeksa: isti u, v), geometrija 0
//neprozirna, 1 prolazi kroz accept() u shaderu (svaki kandidat jednom). TLAS: jedna instanca
void GpuTracer::buildAccelerationStructures(const std::vector<Tracer::Bvh::Prepared>& prepared,
                                            const std::vector<uint32_t>& geometrySlots, uint32_t opaqueCount){
    const VulkanDevice& device = loom.device;
    const vk::raii::Device& vkDevice = device.getDevice();
    const vk::BufferUsageFlags input = vk::BufferUsageFlagBits::eAccelerationStructureBuildInputReadOnlyKHR |
                                       vk::BufferUsageFlagBits::eShaderDeviceAddress;
    auto address = [&](const VulkanBuffer& b){ return vkDevice.getBufferAddress(vk::BufferDeviceAddressInfo(*b.getBuffer())); };
    buffers->rayQuery.reserve(8);           //reference na elemente ne smiju odlutati

    std::vector<glm::vec3> positions;
    positions.reserve(geometrySlots.size() * 3);
    for(uint32_t slot : geometrySlots){
        const Tracer::Bvh::Prepared& t = prepared[slot];
        positions.push_back(t.v0);
        positions.push_back(t.v0 + t.e1);
        positions.push_back(t.v0 + t.e2);
    }
    VulkanBuffer& vertexBuffer = buffers->rayQuery.emplace_back(device, positions.size() * sizeof(glm::vec3), input, MemoryUsage::CPU_TO_GPU);
    vertexBuffer.upload(positions.data(), positions.size() * sizeof(glm::vec3));
    const vk::DeviceAddress vertices = address(vertexBuffer);
    const uint32_t specialCount = uint32_t(geometrySlots.size()) - opaqueCount;

    std::vector<vk::AccelerationStructureGeometryKHR> geometries;
    std::vector<vk::AccelerationStructureBuildRangeInfoKHR> ranges;
    std::vector<uint32_t> counts;
    auto addGeometry = [&](uint32_t first, uint32_t count, vk::GeometryFlagsKHR flags){
        if(count == 0) return;
        vk::AccelerationStructureGeometryTrianglesDataKHR triangles;
        triangles.vertexFormat = vk::Format::eR32G32B32Sfloat;
        triangles.vertexData.deviceAddress = vertices + vk::DeviceAddress(first) * 3 * sizeof(glm::vec3);
        triangles.vertexStride = sizeof(glm::vec3);
        triangles.maxVertex = count * 3 - 1;
        triangles.indexType = vk::IndexType::eNoneKHR;
        vk::AccelerationStructureGeometryKHR geometry;
        geometry.geometryType = vk::GeometryTypeKHR::eTriangles;
        geometry.geometry.triangles = triangles;
        geometry.flags = flags;
        geometries.push_back(geometry);
        ranges.push_back(vk::AccelerationStructureBuildRangeInfoKHR(count, 0, 0, 0));
        counts.push_back(count);
    };
    //Bez neprozirnih je geometrija 0 ona sa zastavicama: shader tada ne smije oduzeti pomak
    addGeometry(0, opaqueCount, vk::GeometryFlagBitsKHR::eOpaque);
    addGeometry(opaqueCount, specialCount, vk::GeometryFlagBitsKHR::eNoDuplicateAnyHitInvocation);

    const vk::PhysicalDeviceAccelerationStructurePropertiesKHR properties =
        device.getPhysicalDevice().getProperties2<vk::PhysicalDeviceProperties2, vk::PhysicalDeviceAccelerationStructurePropertiesKHR>()
            .get<vk::PhysicalDeviceAccelerationStructurePropertiesKHR>();
    const vk::DeviceSize scratchAlign = std::max<vk::DeviceSize>(1, properties.minAccelerationStructureScratchOffsetAlignment);

    auto build = [&](vk::AccelerationStructureTypeKHR type, const std::vector<vk::AccelerationStructureGeometryKHR>& geoms,
                     const std::vector<vk::AccelerationStructureBuildRangeInfoKHR>& buildRanges, const std::vector<uint32_t>& primitiveCounts,
                     std::optional<vk::raii::AccelerationStructureKHR>& out){
        vk::AccelerationStructureBuildGeometryInfoKHR info;
        info.type = type;
        info.flags = vk::BuildAccelerationStructureFlagBitsKHR::ePreferFastTrace;
        info.mode = vk::BuildAccelerationStructureModeKHR::eBuild;
        info.setGeometries(geoms);
        const vk::AccelerationStructureBuildSizesInfoKHR sizes =
            vkDevice.getAccelerationStructureBuildSizesKHR(vk::AccelerationStructureBuildTypeKHR::eDevice, info, primitiveCounts);
        VulkanBuffer& storage = buffers->rayQuery.emplace_back(device, sizes.accelerationStructureSize,
            vk::BufferUsageFlagBits::eAccelerationStructureStorageKHR | vk::BufferUsageFlagBits::eShaderDeviceAddress, MemoryUsage::GPU_ONLY);
        vk::AccelerationStructureCreateInfoKHR create;
        create.buffer = *storage.getBuffer();
        create.size = sizes.accelerationStructureSize;
        create.type = type;
        out.emplace(vkDevice, create);
        VulkanBuffer scratch(device, sizes.buildScratchSize + scratchAlign,
                             vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eShaderDeviceAddress, MemoryUsage::GPU_ONLY);
        const vk::DeviceAddress scratchAddress = (address(scratch) + scratchAlign - 1) / scratchAlign * scratchAlign;
        info.dstAccelerationStructure = **out;
        info.scratchData.deviceAddress = scratchAddress;
        loom.command.submitNow([&](const vk::raii::CommandBuffer& commands){
            commands.buildAccelerationStructuresKHR(info, buildRanges.data());
        });
    };
    build(vk::AccelerationStructureTypeKHR::eBottomLevel, geometries, ranges, counts, buffers->bottom);

    vk::AccelerationStructureInstanceKHR instance;
    const std::array<std::array<float, 4>, 3> identity{{{1.0f, 0.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f, 0.0f}}};
    instance.transform = vk::TransformMatrixKHR(identity);
    instance.mask = 0xFF;
    instance.flags = VkGeometryInstanceFlagsKHR(vk::GeometryInstanceFlagBitsKHR::eTriangleFacingCullDisable);
    instance.accelerationStructureReference = vkDevice.getAccelerationStructureAddressKHR(vk::AccelerationStructureDeviceAddressInfoKHR(**buffers->bottom));
    VulkanBuffer& instances = buffers->rayQuery.emplace_back(device, sizeof(instance), input, MemoryUsage::CPU_TO_GPU);
    instances.upload(&instance, sizeof(instance));
    vk::AccelerationStructureGeometryInstancesDataKHR instanceData;
    instanceData.data.deviceAddress = address(instances);
    vk::AccelerationStructureGeometryKHR topGeometry;
    topGeometry.geometryType = vk::GeometryTypeKHR::eInstances;
    topGeometry.geometry.instances = instanceData;
    build(vk::AccelerationStructureTypeKHR::eTopLevel, {topGeometry}, {vk::AccelerationStructureBuildRangeInfoKHR(1, 0, 0, 0)}, {1u}, buffers->top);

    //(geometrija, primitiv) -> slot; kad neprozirnih nema, shader i geometriju 0 cita s pomakom 0
    std::vector<uint32_t> slots = geometrySlots;
    VulkanBuffer& slotBuffer = buffers->rayQuery.emplace_back(device, std::max<size_t>(4, slots.size() * 4),
        vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferDst, MemoryUsage::CPU_TO_GPU);
    slotBuffer.upload(slots.data(), slots.size() * 4);
    usingRayQuery = true;
}

GpuTracer::~GpuTracer() = default;

float GpuTracer::progress() const{
    if(finished()) return 1.0f;
    return (float(sample) + float(row) / float(std::max(1u, size[1]))) / float(settings.samples);
}

uint32_t GpuTracer::record(uint32_t rows){
    uint32_t sent = 0;
    while(rows > 0 && !finished()){
        const uint32_t band = std::min(rows, size[1] - row);
        TracePush push{sample, row, band, 0};
        loom.renderer.dispatch(*buffers->trace, (size[0] + 7) / 8, (band + 7) / 8, 1, &push, sizeof(push));
        row += band;
        rows -= band;
        sent += band;
        if(row >= size[1]){ row = 0; ++sample; }
    }
    return sent;
}

void GpuTracer::recordDisplay(const DisplayOptions& options){
    if(options.denoise || options.post.active()){ recordFinish(options); return; }
    ResolvePush push{};
    push.width = size[0];
    push.height = size[1];
    push.backdrop = options.backdrop == Tracer::Backdrop::Environment ? 0u : options.backdrop == Tracer::Backdrop::Transparent ? 1u : 2u;
    push.view = options.view == Tracer::ViewTransform::AgX ? 1u : 0u;
    push.gain = std::exp2(options.exposure);
    push.plate = backplateTexture;
    push.checker = options.checker ? 1u : 0u;
    loom.renderer.dispatch(*buffers->resolve, (size[0] + 7) / 8, (size[1] + 7) / 8, 1, &push, sizeof(push));
}

void GpuTracer::recordFinish(const DisplayOptions& options){
    const VulkanDevice& device = loom.device;
    const uint32_t w = size[0], h = size[1];
    const vk::DeviceSize pixels = vk::DeviceSize(w) * h;
    if(!buffers->finish){
        //Piramida: razina 0 puna velicina, pa pola, pola... do 1x1
        uint32_t offset = 0, lw = w, lh = h;
        buffers->levels.push_back(glm::uvec4(0, w, h, 0));
        offset += lw * lh;
        while(lw > 1 || lh > 1){
            lw = std::max(1u, (lw + 1) / 2);
            lh = std::max(1u, (lh + 1) / 2);
            buffers->levels.push_back(glm::uvec4(offset, lw, lh, 0));
            offset += lw * lh;
        }
        const vk::BufferUsageFlags usage = vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferSrc |
                                           vk::BufferUsageFlagBits::eTransferDst;
        for(int k = 0; k < 5; ++k) buffers->finishing.emplace_back(device, std::max<vk::DeviceSize>(pixels * 16, 16), usage, MemoryUsage::GPU_ONLY);
        buffers->finishing.emplace_back(device, std::max<vk::DeviceSize>(vk::DeviceSize(offset) * 16, 16), usage, MemoryUsage::GPU_ONLY);
        buffers->finish.emplace(device, loom.getDescriptorPool(), pipelines.state->finish);
        const uint32_t accumulators[7] = {13, 14, 15, 16, 17, 18, 19};
        for(uint32_t b = 0; b < 7; ++b) buffers->finish->setStorageBuffer(b, *buffers->owned[accumulators[b]]);
        buffers->finish->setStorageBuffer(7, *buffers->owned[6]);
        buffers->finish->setStorageBuffer(8, *buffers->owned[24]);      //snimka je u spremniku kadra
        for(uint32_t b = 0; b < 6; ++b) buffers->finish->setStorageBuffer(9 + b, buffers->finishing[b]);
        buffers->finish->setStorageBuffer(15, *buffers->display);
    }
    const Tracer::PostSettings& s = options.post;
    const bool post = s.active();
    FinishPush push{};
    push.width = w;
    push.height = h;
    push.backdrop = options.backdrop == Tracer::Backdrop::Environment ? 0u : options.backdrop == Tracer::Backdrop::Transparent ? 1u : 2u;
    push.view = options.view == Tracer::ViewTransform::AgX ? 1u : 0u;
    push.plate = backplateTexture;
    push.flags = (options.checker ? 1u : 0u) | (options.denoise ? 2u : 0u) | (post ? 4u : 0u);
    push.grainSeed = options.grainSeed;
    push.gain = std::exp2(options.exposure);
    push.bloom = std::clamp(s.bloom, 0.0f, 1.0f);
    push.bloomThreshold = s.bloomThreshold;
    push.chromatic = s.chromaticAberration;
    push.vignette = s.vignette;
    push.contrast = s.contrast;
    push.saturation = s.saturation;
    push.grain = s.grain;
    //Balans bijele kao Post.cpp
    float white[3] = {1.0f, 1.0f, 1.0f}, neutral[3];
    Tracer::blackBody(s.temperature, white);
    Tracer::blackBody(6500.0f, neutral);
    glm::vec3 balance(white[0] / neutral[0], white[1] / neutral[1], white[2] / neutral[2]);
    balance.g *= 1.0f - 0.25f * std::clamp(s.tint, -1.0f, 1.0f);
    balance /= 0.2126f * balance.r + 0.7152f * balance.g + 0.0722f * balance.b;
    push.balanceR = balance.r; push.balanceG = balance.g; push.balanceB = balance.b;

    auto run = [&](uint32_t mode, uint32_t gx, uint32_t gy){
        push.mode = mode;
        loom.renderer.dispatch(*buffers->finish, (gx + 7) / 8, (gy + 7) / 8, 1, &push, sizeof(push));
    };
    //Filtar: priprema i pet prolaza A <-> B (kao Denoise.cpp); kompozit cita posljednji
    run(0, w, h);
    bool inB = false;
    if(options.denoise){
        for(int k = 0; k < 5; ++k){
            push.step = 1u << k;
            push.flags = (push.flags & ~16u) | (inB ? 16u : 0u);
            run(1, w, h);
            inB = !inB;
        }
    }
    push.flags = (push.flags & ~16u) | (inB ? 16u : 0u);
    run(2, w, h);
    if(post && s.bloom > 0.0f){
        const float radius = std::max(2.0f, s.bloomRadius * float(w));
        const int wanted = std::clamp(int(std::ceil(std::log2(radius))), 1, 12);
        const int top = std::min(wanted, int(buffers->levels.size()) - 1);
        if(top >= 1){
            run(3, w, h);
            for(int k = 1; k <= top; ++k){
                const glm::uvec4 from = buffers->levels[size_t(k - 1)], to = buffers->levels[size_t(k)];
                push.srcOffset = from.x; push.srcWidth = from.y; push.srcHeight = from.z;
                push.dstOffset = to.x; push.dstWidth = to.y; push.dstHeight = to.z;
                run(4, to.y, to.z);
            }
            for(int k = top - 1; k >= 1; --k){
                const glm::uvec4 from = buffers->levels[size_t(k + 1)], to = buffers->levels[size_t(k)];
                push.srcOffset = from.x; push.srcWidth = from.y; push.srcHeight = from.z;
                push.dstOffset = to.x; push.dstWidth = to.y; push.dstHeight = to.z;
                run(5, to.y, to.z);
            }
            const glm::uvec4 first = buffers->levels[1];
            push.srcOffset = first.x; push.srcWidth = first.y; push.srcHeight = first.z;
            push.bloomScale = 1.0f / float(top);
            run(6, w, h);
        }
    }
    run(7, w, h);
}

std::vector<uint8_t> GpuTracer::readDisplay(){
    loom.waitIdle();
    const vk::DeviceSize count = vk::DeviceSize(size[0]) * size[1] * 4;
    std::vector<uint8_t> out(count);
    VulkanBuffer staging(loom.device, count, vk::BufferUsageFlagBits::eTransferDst, MemoryUsage::GPU_TO_CPU);
    loom.command.copyBuffer(buffers->display->getBuffer(), staging.getBuffer(), count);
    staging.download(out.data(), count);
    return out;
}

Tracer::Frame GpuTracer::readFrame(bool denoise){
    loom.waitIdle();
    const size_t n = size_t(size[0]) * size[1];
    auto read = [&](uint32_t binding, size_t floatsPerPixel){
        std::vector<float> values(n * floatsPerPixel);
        const vk::DeviceSize count = values.size() * sizeof(float);
        VulkanBuffer staging(loom.device, std::max<vk::DeviceSize>(count, 16), vk::BufferUsageFlagBits::eTransferDst, MemoryUsage::GPU_TO_CPU);
        loom.command.copyBuffer(buffers->owned[binding]->getBuffer(), staging.getBuffer(), count);
        staging.download(values.data(), count);
        return values;
    };
    const std::vector<float> cg = read(13, 4), background = read(14, 4), lit = read(15, 4), shadowed = read(16, 4),
                             albedo = read(17, 4), normal = read(18, 4), depth = read(19, 1);

    //Isto sto Renderer::frame radi nad procesorskim zbrojevima
    Tracer::Frame out;
    out.width = size[0];
    out.height = size[1];
    out.samples = sample;
    out.cg.assign(n * 4, 0.0f);
    out.background.assign(n * 3, 0.0f);
    out.shadow.assign(n * 3, 1.0f);
    out.albedo.assign(n * 3, 0.0f);
    out.normal.assign(n * 3, 0.0f);
    out.depth = depth;
    out.variance.assign(n, 0.0f);
    for(size_t i = 0; i < n; ++i){
        const float samples = shadowed[i * 4 + 3];
        if(samples <= 0.0f){ out.depth[i] = Tracer::NoDepth; continue; }
        const float inv = 1.0f / samples;
        for(int k = 0; k < 3; ++k){
            out.cg[i * 4 + size_t(k)] = cg[i * 4 + size_t(k)] * inv;
            out.background[i * 3 + size_t(k)] = background[i * 4 + size_t(k)] * inv;
            out.albedo[i * 3 + size_t(k)] = albedo[i * 4 + size_t(k)] * inv;
        }
        out.cg[i * 4 + 3] = cg[i * 4 + 3] * inv;
        const glm::vec3 nrm(normal[i * 4], normal[i * 4 + 1], normal[i * 4 + 2]);
        const glm::vec3 unit = glm::dot(nrm, nrm) > 0.0f ? glm::normalize(nrm) : glm::vec3(0.0f);
        for(int k = 0; k < 3; ++k) out.normal[i * 3 + size_t(k)] = unit[k];
        const float missSamples = background[i * 4 + 3], catcherSamples = lit[i * 4 + 3];
        if(catcherSamples > 0.0f){
            for(int k = 0; k < 3; ++k){
                const float l = lit[i * 4 + size_t(k)];
                const float ratio = l > 0.0f ? std::clamp(shadowed[i * 4 + size_t(k)] / l, 0.0f, 1.0f) : 1.0f;
                out.shadow[i * 3 + size_t(k)] = (missSamples + catcherSamples * ratio) / (missSamples + catcherSamples);
            }
        }
        const double mean = double(albedo[i * 4 + 3]) / samples;
        const double var = std::max(0.0, double(normal[i * 4 + 3]) / samples - mean * mean);
        out.variance[i] = float(var / samples);
    }
    if(denoise) Tracer::denoiseFrame(out);
    return out;
}

void GpuTracer::renderAll(uint32_t rowsPerFrame, const std::function<bool(uint32_t)>& onSample){
    //Bez zadane velicine: pola sekunde posla po predaji, izmjereno na prvim kadrovima. Dispatch
    //dulji od ~2 s Windows proglasi zaglavljenim (TDR), a kraci od par ms trosi vrijeme na predaju
    uint32_t rows = rowsPerFrame ? rowsPerFrame : std::max(1u, size[1] / 4);
    const bool adaptive = rowsPerFrame == 0;
    while(!finished()){
        const auto start = std::chrono::steady_clock::now();
        const uint32_t before = sample;
        if(!loom.renderer.beginFrame()) continue;
        record(rows);
        loom.renderer.endFrame();
        if(adaptive){
            loom.waitIdle();
            const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
            const double scale = std::clamp(0.4 / std::max(seconds, 1e-4), 0.25, 4.0);
            rows = uint32_t(std::clamp(double(rows) * scale, 1.0, double(size[1]) * 64.0));
        }
        if(onSample && sample != before && !onSample(sample)) break;
    }
    loom.waitIdle();
}

}
