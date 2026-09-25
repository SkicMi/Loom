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
constexpr uint32_t TraceBindings = 20;
constexpr uint32_t ResolveBindings = 7;

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
};
static_assert(sizeof(Params) == 22 * 16, "Params mora odgovarati shaders/tracer.slang");

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

struct Pipelines::State{
    VulkanComputePipeline trace, resolve;
    explicit State(const VulkanDevice& device)
    : trace(device, configFor("tracer.comp.spv", TraceBindings, sizeof(TracePush))),
      resolve(device, configFor("tracer_resolve.comp.spv", ResolveBindings, sizeof(ResolvePush))){}
};

Pipelines::Pipelines(LoomInitializer& loom) : state(std::make_unique<State>(loom.device)){}
Pipelines::~Pipelines() = default;

struct GpuTracer::Buffers{
    std::vector<VulkanBuffer> owned;        //sve, redom vezanja 0..19 shadera tracera
    std::optional<VulkanBuffer> display;
    std::optional<ComputeMaterial> trace, resolve;
};

GpuTracer::GpuTracer(LoomInitializer& loom_, Pipelines& pipelines_, std::shared_ptr<const Tracer::CompiledScene> scene,
                     const Tracer::RenderSettings& settings_)
: loom(loom_), pipelines(pipelines_), compiled(std::move(scene)), settings(settings_), buffers(std::make_unique<Buffers>()){
    const Tracer::CompiledScene& c = *compiled;
    const Tracer::Scene& world = c.world;
    size[0] = world.camera.width;
    size[1] = world.camera.height;
    settings.samples = std::max(1u, settings.samples);

    //-- teksture: scena, pa nebo, pa snimka. Osam bita u jedno polje, float u drugo ----------------
    std::vector<glm::uvec4> textureInfo;
    std::vector<uint32_t> texels;
    std::vector<glm::vec4> texelsFloat;
    auto addTexture = [&](const Tracer::Texture& t){
        if(!t.valid()){ textureInfo.push_back(glm::uvec4(0)); return uint32_t(textureInfo.size() - 1); }
        uint32_t flags = (t.srgb ? 1u : 0u) | (t.repeat ? 4u : 0u);
        uint32_t start = 0;
        const size_t count = size_t(t.width) * t.height;
        if(!t.floats.empty()){
            flags |= 2u;
            start = uint32_t(texelsFloat.size());
            for(size_t i = 0; i < count; ++i) texelsFloat.push_back(glm::make_vec4(t.floats.data() + i * 4));
        }else{
            start = uint32_t(texels.size());
            texels.resize(texels.size() + count);
            std::memcpy(texels.data() + start, t.bytes.data(), count * 4);    //RGBA8 = r | g<<8 | b<<16 | a<<24
        }
        textureInfo.push_back(glm::uvec4(start, t.width, t.height, flags));
        return uint32_t(textureInfo.size() - 1);
    };
    for(const Tracer::Texture& t : world.textures) addTexture(t);
    const bool envTextured = c.sky.isTextured();
    const uint32_t envTexture = envTextured ? addTexture(world.environment.map) : None;
    if(world.backplate.valid()) backplateTexture = addTexture(world.backplate);

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
        g.mode = glm::uvec4(m.alphaMode == Tracer::Material::Alpha::Mask ? 1u : m.alphaMode == Tracer::Material::Alpha::Blend ? 2u : 0u, 0, 0, 0);
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
    p.values = glm::vec4(settings.indirectClamp, 0.0f, 0.0f, 0.0f);
    p.extra = glm::uvec4(settings.seed, uint32_t(c.sky.marginalCdf().size()), 0u, 0u);

    //-- na karticu -------------------------------------------------------------------------------------
    const VulkanDevice& device = loom.device;
    const auto usage = vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferDst | vk::BufferUsageFlagBits::eTransferSrc;
    auto onCard = [&](const void* data, size_t count){
        const vk::DeviceSize size = std::max<vk::DeviceSize>(count, 16);
        VulkanBuffer card(device, size, usage, MemoryUsage::GPU_ONLY);
        if(count > 0){
            VulkanBuffer staging(device, count, vk::BufferUsageFlagBits::eTransferSrc, MemoryUsage::CPU_TO_GPU);
            staging.upload(data, count);
            loom.command.copyBuffer(staging.getBuffer(), card.getBuffer(), count);
        }
        bytes += size;
        buffers->owned.push_back(std::move(card));
    };
    //Nule jednom kopirane u svaki zbroj (VulkanCommand ne izlaze fillBuffer izvan kadra)
    const vk::DeviceSize pixelCount = vk::DeviceSize(size[0]) * size[1];
    std::optional<VulkanBuffer> zeros;
    auto zeroed = [&](vk::DeviceSize size){
        VulkanBuffer card(device, size, usage, MemoryUsage::GPU_ONLY);
        if(!zeros){
            zeros.emplace(device, pixelCount * 16, vk::BufferUsageFlagBits::eTransferSrc, MemoryUsage::CPU_TO_GPU);
            std::vector<uint8_t> blank(size_t(pixelCount * 16), 0);
            zeros->upload(blank.data(), blank.size());
        }
        loom.command.copyBuffer(zeros->getBuffer(), card.getBuffer(), size);
        bytes += size;
        buffers->owned.push_back(std::move(card));
    };
    buffers->owned.reserve(TraceBindings);
    onCard(&p, sizeof(p));                                                  //0
    onCard(nodes.data(), nodes.size() * sizeof(glm::vec4));                 //1
    onCard(triangles.data(), triangles.size() * sizeof(glm::vec4));         //2
    onCard(triangleInfo.data(), triangleInfo.size() * sizeof(glm::uvec4));  //3
    onCard(vertices.data(), vertices.size() * sizeof(glm::vec4));           //4
    onCard(materials.data(), materials.size() * sizeof(GpuMaterial));       //5
    onCard(textureInfo.data(), textureInfo.size() * sizeof(glm::uvec4));    //6
    onCard(texels.data(), texels.size() * sizeof(uint32_t));                //7
    onCard(texelsFloat.data(), texelsFloat.size() * sizeof(glm::vec4));    //8
    onCard(lights.data(), lights.size() * sizeof(GpuLight));                //9
    onCard(lightLists.data(), lightLists.size() * sizeof(uint32_t));        //10
    onCard(envTables.data(), envTables.size() * sizeof(float));             //11
    onCard(energy.data(), energy.size() * sizeof(float));                   //12
    const vk::DeviceSize pixels = vk::DeviceSize(size[0]) * size[1];
    for(int k = 0; k < 6; ++k) zeroed(pixels * 16);                         //13..18
    zeroed(pixels * 4);                                                     //19
    buffers->display.emplace(device, std::max<vk::DeviceSize>(pixels * 4, 16), usage, MemoryUsage::GPU_ONLY);

    buffers->trace.emplace(device, loom.getDescriptorPool(), pipelines.state->trace);
    for(uint32_t b = 0; b < TraceBindings; ++b) buffers->trace->setStorageBuffer(b, buffers->owned[b]);
    buffers->resolve.emplace(device, loom.getDescriptorPool(), pipelines.state->resolve);
    buffers->resolve->setStorageBuffer(0, buffers->owned[13]);
    buffers->resolve->setStorageBuffer(1, buffers->owned[14]);
    buffers->resolve->setStorageBuffer(2, buffers->owned[15]);
    buffers->resolve->setStorageBuffer(3, buffers->owned[16]);
    buffers->resolve->setStorageBuffer(4, buffers->owned[6]);
    buffers->resolve->setStorageBuffer(5, buffers->owned[7]);
    buffers->resolve->setStorageBuffer(6, *buffers->display);
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
        loom.command.copyBuffer(buffers->owned[binding].getBuffer(), staging.getBuffer(), count);
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
