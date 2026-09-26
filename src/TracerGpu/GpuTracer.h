#pragma once
//=============================================================================================
// LOOMTRACER NA KARTICI (Vulkan compute).
//
// Zaseban target, isto kao LoomPreset i TreadlePaint: jedini zna i za Loom (Vulkan) i za Tracer.
// Tracer ostaje bez ijednog vk:: - gradi scenu (compile: BVH, svjetla, tablice neba) i racuna je
// na procesoru kao referencu; ovo tu ISTU prevedenu scenu prepise u storage buffere i obilazi je
// shaderom (shaders/tracer.slang), koji je port procesorskog koda redak po redak.
//
// ZASTO COMPUTE NAD VLASTITIM BVH-om, A NE HARDVERSKE ZRAKE (ray query). Radi na svakoj Vulkan
// kartici i na llvmpipeu - pa se testira ovdje, bez kartice - a BVH je bit po bit onaj koji je
// procesorski tracer vec provjerio protiv grube sile. Hardverske zrake su sljedeci korak za RTX
// i mijenjaju samo obilazak; sve ostalo (BSDF, svjetla, film) ostaje.
//
// RASPORED POSLA. Jedan dispatch = jedan uzorak za pojas redaka. Tko zove odlucuje koliko redaka
// po kadru: editor malo (ostaje odziv, a Windows ne ubija dispatch dulji od ~2 s), loom-render
// puno. Zbrojevi po pikselu stoje na kartici; natrag se cita samo slika za prikaz (4 bajta po
// pikselu, resolve shader) i na kraju cijeli film.
//=============================================================================================
#include <Tracer/Compiled.h>
#include <Tracer/Film.h>
#include <Tracer/Post.h>
#include <Tracer/Renderer.h>

#include <cstdint>
#include <cstring>
#include <functional>
#include <map>
#include <memory>
#include <vector>

class LoomInitializer;
class VulkanBuffer;

namespace TracerGpu{

//Cjevovodi (prevodjenje velikog shadera traje) - jednom po aplikaciji
class Pipelines{
public:
    explicit Pipelines(LoomInitializer& loom);
    ~Pipelines();
    struct State;
    std::unique_ptr<State> state;
};

//SEKVENCA: spremnici koji se izmedju kadrova obicno ne mijenjaju (teksture s mipmapama, nebo,
//tablice energije, geometrija mirne scene) ostaju na kartici. Po vezanju pamti hash sadrzaja;
//isti hash, isti spremnik - bez prijenosa. Drzi ga onaj tko renderira vise kadrova (pogon)
struct UploadCache{
    struct Entry{
        uint64_t hash = 0;
        size_t bytes = 0;
        std::shared_ptr<VulkanBuffer> buffer;
    };
    std::map<uint32_t, Entry> entries;
    uint64_t uploadedBytes = 0, reusedBytes = 0;

    //Brz 64-bitni hash (po 8 bajta, mnozenje i posmak) - desetine GB/s, ne usporava prijenos
    static uint64_t hashBytes(const void* data, size_t count){
        const uint8_t* p = static_cast<const uint8_t*>(data);
        uint64_t h = 0x9E3779B97F4A7C15ull ^ (count * 0xC2B2AE3D27D4EB4Full);
        size_t i = 0;
        for(; i + 8 <= count; i += 8){
            uint64_t w;
            std::memcpy(&w, p + i, 8);
            h = (h ^ (w * 0x87C37B91114253D5ull)) * 0x4CF5AD432745937Full;
            h ^= h >> 31;
        }
        for(; i < count; ++i) h = (h ^ p[i]) * 0x100000001B3ull;
        return h ^ (h >> 29);
    }
};

struct DisplayOptions{
    Tracer::Backdrop backdrop = Tracer::Backdrop::Environment;
    Tracer::ViewTransform view = Tracer::ViewTransform::Standard;
    float exposure = 0.0f;
    bool checker = false;           //prozirno preko sahovnice (prozor editora)
    //Filtar suma (A-trous) i post na kartici: slika koja se cisti izgleda kao gotov kadar
    //(shaders/tracer_finish.slang). Bez njih ide jednostavni resolve
    bool denoise = false;
    Tracer::PostSettings post;      //exposure gore ima prednost
    uint32_t grainSeed = 0;
};

class GpuTracer{
public:
    //Prepisuje scenu na karticu (sinkrono, IZVAN kadra) i nulira zbrojeve
    //allowRayQuery: hardverske zrake kad ih kartica ima (false: uvijek vlastiti BVH, za usporedbu)
    GpuTracer(LoomInitializer& loom, Pipelines& pipelines, std::shared_ptr<const Tracer::CompiledScene> scene,
              const Tracer::RenderSettings& settings, bool allowRayQuery = true, UploadCache* cache = nullptr);
    ~GpuTracer();
    GpuTracer(const GpuTracer&) = delete;
    GpuTracer& operator=(const GpuTracer&) = delete;

    //UNUTAR kadra (poslije beginFrame, prije beginPass): najvise `rows` redaka-uzoraka posla.
    //Vraca koliko je redaka poslano. Svaki dispatch je jedan uzorak za pojas redaka
    uint32_t record(uint32_t rows);

    //Unutar kadra: slika za prikaz u buffer (poslije record istog kadra)
    void recordDisplay(const DisplayOptions& options);
    //Filtar i post (recordDisplay ga zove kad su ukljuceni)
    void recordFinish(const DisplayOptions& options);

    //Racuna li hardverskim zrakama (VK_KHR_ray_query) ili vlastitim BVH-om
    bool usesRayQuery() const {return usingRayQuery;}
    //Izvan kadra: ceka karticu i cita sliku za prikaz (RGBA8, sRGB)
    std::vector<uint8_t> readDisplay();

    //Izvan kadra: ceka karticu i cita cijeli film (isti oblik kao Renderer::frame)
    Tracer::Frame readFrame(bool denoise);

    //Cijeli render odjednom: kadrovi s po `rowsPerFrame` redaka dok ne zavrsi ili onPass kaze stop.
    //Za loom-render i testove - editor zove record() iz svoje petlje
    void renderAll(uint32_t rowsPerFrame = 0, const std::function<bool(uint32_t samplesDone)>& onSample = {});

    bool finished() const {return sample >= settings.samples;}
    uint32_t samplesDone() const {return sample;}
    uint32_t samplesTotal() const {return settings.samples;}
    float progress() const;
    uint32_t width() const {return size[0];}
    uint32_t height() const {return size[1];}
    const Tracer::CompiledScene& scene() const {return *compiled;}
    const std::shared_ptr<const Tracer::CompiledScene>& compiledScene() const {return compiled;}
    //Koliko je bajtova scene na kartici (geometrija, teksture, zbrojevi)
    uint64_t deviceBytes() const {return bytes;}

private:
    struct Buffers;
    LoomInitializer& loom;
    Pipelines& pipelines;
    std::shared_ptr<const Tracer::CompiledScene> compiled;
    Tracer::RenderSettings settings;
    std::unique_ptr<Buffers> buffers;
    uint32_t size[2] = {0, 0};
    uint32_t sample = 0, row = 0;
    uint32_t backplateTexture = ~0u;
    bool usingRayQuery = false;
    void buildAccelerationStructures(const std::vector<Tracer::Bvh::Prepared>& prepared,
                                     const std::vector<uint32_t>& geometrySlots, uint32_t opaqueCount);
    uint64_t bytes = 0;
};

}
