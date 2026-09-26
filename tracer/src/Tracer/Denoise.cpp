#include "Tracer/Denoise.h"

#include <glm/glm.hpp>

#include <algorithm>
#include <atomic>
#include <cstdlib>
#include <dlfcn.h>
#include <filesystem>
#include <mutex>
#include <thread>
#include <cmath>
#include <vector>

#ifndef TRACER_OIDN_DIR
#define TRACER_OIDN_DIR ""
#endif

namespace Tracer{

namespace{
float luminance(const glm::vec3& c){ return 0.2126f * c.r + 0.7152f * c.g + 0.0722f * c.b; }
const float Kernel[3] = {3.0f / 8.0f, 1.0f / 4.0f, 1.0f / 16.0f};

//-- OIDN kroz dlopen: C sucelje oidn.h (2.x), samo ono sto treba -----------------------------
using OidnHandle = void*;
constexpr int OidnDeviceDefault = 0;        //OIDN_DEVICE_TYPE_DEFAULT: najbrzi koji postoji (kartica)
constexpr int OidnDeviceCpu = 1;            //OIDN_DEVICE_TYPE_CPU
constexpr int OidnFloat3 = 3;               //OIDN_FORMAT_FLOAT3
const char* const OidnDeviceNames[] = {"default", "CPU", "SYCL", "CUDA", "HIP", "Metal"};

struct Oidn{
    void* library = nullptr;
    OidnHandle (*newDevice)(int) = nullptr;
    void (*commitDevice)(OidnHandle) = nullptr;
    int (*deviceError)(OidnHandle, const char**) = nullptr;
    OidnHandle (*newFilter)(OidnHandle, const char*) = nullptr;
    int (*deviceInt)(OidnHandle, const char*) = nullptr;
    OidnHandle (*newBuffer)(OidnHandle, size_t) = nullptr;
    void (*releaseBuffer)(OidnHandle) = nullptr;
    void (*writeBuffer)(OidnHandle, size_t, size_t, const void*) = nullptr;
    void (*readBuffer)(OidnHandle, size_t, size_t, void*) = nullptr;
    void (*setImage)(OidnHandle, const char*, OidnHandle, int, size_t, size_t, size_t, size_t, size_t) = nullptr;
    void (*setBool)(OidnHandle, const char*, bool) = nullptr;
    void (*commitFilter)(OidnHandle) = nullptr;
    void (*executeFilter)(OidnHandle) = nullptr;
    OidnHandle device = nullptr, filter = nullptr;
    //Spremnici NA UREDJAJU (kartica ne vidi memoriju procesora): boja, albedo, normala, izlaz
    OidnHandle buffers[4] = {nullptr, nullptr, nullptr, nullptr};
    size_t bufferBytes = 0;
    std::string where, deviceName = "none";
    std::mutex lock;                        //jedan uredjaj i filtar, jedan posao u isto vrijeme
    bool ready = false;

    Oidn(){
        std::vector<std::string> candidates;
        if(const char* env = std::getenv("LOOM_OIDN")){
            const std::filesystem::path p(env);
            candidates.push_back(std::filesystem::is_directory(p) ? (p / "libOpenImageDenoise.so.2").string() : p.string());
        }
        if(*TRACER_OIDN_DIR) candidates.push_back(std::string(TRACER_OIDN_DIR) + "/libOpenImageDenoise.so.2");
        candidates.push_back("libOpenImageDenoise.so.2");
        for(const std::string& c : candidates){
            library = dlopen(c.c_str(), RTLD_NOW | RTLD_LOCAL);
            if(library){ where = c; break; }
        }
        if(!library){ where = "libOpenImageDenoise.so.2 not found (tools/oidn/fetch.sh or LOOM_OIDN)"; return; }
        auto get = [&](auto& fn, const char* name){ fn = reinterpret_cast<std::remove_reference_t<decltype(fn)>>(dlsym(library, name)); return fn != nullptr; };
        if(!(get(newDevice, "oidnNewDevice") && get(commitDevice, "oidnCommitDevice") && get(deviceError, "oidnGetDeviceError") &&
             get(newFilter, "oidnNewFilter") && get(setImage, "oidnSetFilterImage") && get(setBool, "oidnSetFilterBool") &&
             get(commitFilter, "oidnCommitFilter") && get(executeFilter, "oidnExecuteFilter") && get(deviceInt, "oidnGetDeviceInt") &&
             get(newBuffer, "oidnNewBuffer") && get(releaseBuffer, "oidnReleaseBuffer") && get(writeBuffer, "oidnWriteBuffer") &&
             get(readBuffer, "oidnReadBuffer"))){
            where += ": missing functions (not OIDN 2?)";
            return;
        }
        //UREDJAJ: zadano najbrzi koji OIDN nadje (CUDA/HIP/SYCL kad su njihove biblioteke uz
        //libOpenImageDenoise i kartica postoji - tools/oidn/fetch.sh --gpu), inace procesor.
        //LOOM_OIDN_DEVICE=cpu|cuda|hip|sycl bira izrijekom
        int wanted = OidnDeviceDefault;
        if(const char* env = std::getenv("LOOM_OIDN_DEVICE")){
            const std::string name(env);
            wanted = name == "cpu" ? 1 : name == "sycl" ? 2 : name == "cuda" ? 3 : name == "hip" ? 4 : OidnDeviceDefault;
        }
        const char* message = nullptr;
        for(int attempt : {wanted, OidnDeviceCpu}){
            device = newDevice(attempt);
            if(!device) continue;
            commitDevice(device);
            if(deviceError(device, &message) == 0) break;
            device = nullptr;
        }
        if(!device){ where += std::string(": ") + (message ? message : "no device"); return; }
        const int type = deviceInt(device, "type");
        deviceName = type >= 0 && type <= 5 ? OidnDeviceNames[type] : "unknown";
        filter = newFilter(device, "RT");
        ready = filter != nullptr;
        if(!ready) where += ": no RT filter";
    }

    //Boja (HDR, premnozena) uz albedo i normale; false kad OIDN javi gresku
    bool run(std::vector<float>& colour, std::vector<float>& albedo, std::vector<float>& normal,
             std::vector<float>& output, uint32_t w, uint32_t h){
        std::lock_guard<std::mutex> guard(lock);
        const size_t stride = 3 * sizeof(float);
        const size_t bytes = stride * size_t(w) * h;
        if(bytes != bufferBytes){
            for(OidnHandle& b : buffers){ if(b) releaseBuffer(b); b = newBuffer(device, bytes); }
            bufferBytes = bytes;
        }
        writeBuffer(buffers[0], 0, bytes, colour.data());
        writeBuffer(buffers[1], 0, bytes, albedo.data());
        writeBuffer(buffers[2], 0, bytes, normal.data());
        setImage(filter, "color", buffers[0], OidnFloat3, w, h, 0, stride, stride * w);
        setImage(filter, "albedo", buffers[1], OidnFloat3, w, h, 0, stride, stride * w);
        setImage(filter, "normal", buffers[2], OidnFloat3, w, h, 0, stride, stride * w);
        setImage(filter, "output", buffers[3], OidnFloat3, w, h, 0, stride, stride * w);
        setBool(filter, "hdr", true);
        commitFilter(filter);
        executeFilter(filter);
        readBuffer(buffers[3], 0, bytes, output.data());
        const char* message = nullptr;
        return deviceError(device, &message) == 0;
    }
};

Oidn& oidn(){
    static Oidn instance;               //jednom: ucitavanje tezina mreze traje
    return instance;
}

//OIDN na boji CG-a. false: nema ga ili je javio gresku (tada ide A-trous)
bool oidnColour(Frame& frame){
    Oidn& o = oidn();
    if(!o.ready) return false;
    const size_t n = frame.pixelCount();
    std::vector<float> colour(n * 3), albedo(frame.albedo), normal(frame.normal), output(n * 3);
    for(size_t i = 0; i < n; ++i) for(int k = 0; k < 3; ++k) colour[i * 3 + size_t(k)] = std::max(0.0f, frame.cg[i * 4 + size_t(k)]);
    if(albedo.size() != n * 3) albedo.assign(n * 3, 0.0f);
    if(normal.size() != n * 3) normal.assign(n * 3, 0.0f);
    //Albedo mora biti u [0,1] (OIDN), a CG-a bez pogotka nema: tamo je albedo neba 0 - mreza ga
    //tada cita kao sam izvor svjetla, sto je tocno za pozadinu
    for(float& a : albedo) a = std::clamp(a, 0.0f, 1.0f);
    if(!o.run(colour, albedo, normal, output, frame.width, frame.height)) return false;
    for(size_t i = 0; i < n; ++i) for(int k = 0; k < 3; ++k) frame.cg[i * 4 + size_t(k)] = std::max(0.0f, output[i * 3 + size_t(k)]);
    return true;
}

void aTrous(Frame& frame, bool colour);
}

bool oidnAvailable(std::string* where){
    Oidn& o = oidn();
    if(where) *where = o.where;
    return o.ready;
}

std::string oidnDevice(){
    Oidn& o = oidn();
    return o.ready ? o.deviceName : "none";
}

void denoiseFrame(Frame& frame, Denoiser which){
    if(frame.pixelCount() == 0) return;
    const bool viaOidn = which != Denoiser::ATrous && oidnColour(frame);
    aTrous(frame, !viaOidn);
    frame.denoised = true;
}

namespace{
//A-trous: boja CG-a (colour) i uvijek sjena catchera
void aTrous(Frame& frame, bool colour){
    const int w = int(frame.width), h = int(frame.height);
    const size_t n = frame.pixelCount();
    if(n == 0) return;

    std::vector<glm::vec3> modulation(n), light(n), next(n);
    std::vector<float> variance(frame.variance), nextVariance(n);
    auto normalAt = [&](size_t i){ return glm::vec3(frame.normal[i * 3], frame.normal[i * 3 + 1], frame.normal[i * 3 + 2]); };
    auto albedoAt = [&](size_t i){ return glm::vec3(frame.albedo[i * 3], frame.albedo[i * 3 + 1], frame.albedo[i * 3 + 2]); };
    for(size_t i = 0; i < n; ++i){
        modulation[i] = glm::max(albedoAt(i), glm::vec3(0.02f));
        light[i] = glm::vec3(frame.cg[i * 4], frame.cg[i * 4 + 1], frame.cg[i * 4 + 2]) / modulation[i];
        //Varijanca je bila za boju; svjetlo je boja / albedo
        const float m = std::max(0.02f, luminance(modulation[i]));
        variance[i] /= m * m;
    }

    //Vodici se procitaju jednom u gusta polja: u petlji od 25 susjeda po pikselu i pet prolaza
    //citanje iz Framea (tri floata na preskok) i pow/exp po susjedu bili su vise od pola vremena
    std::vector<glm::vec3> normals(n), albedos(n);
    std::vector<float> coverage(n), lum(n);
    std::vector<uint8_t> empty(n);
    for(size_t i = 0; i < n; ++i){
        normals[i] = normalAt(i);
        albedos[i] = albedoAt(i);
        coverage[i] = frame.cg[i * 4 + 3];
        empty[i] = glm::dot(normals[i], normals[i]) == 0.0f;
    }
    const std::vector<float>& depth = frame.depth;

    //Retci na sve jezgre: svaki piksel pise samo svoje mjesto, pa nema dijeljenja
    auto parallelRows = [&](const auto& body){
        const unsigned threads = std::max(1u, std::min(std::thread::hardware_concurrency(), unsigned(h)));
        std::atomic<int> next{0};
        auto work = [&]{ for(int y = next++; y < h; y = next++) body(y); };
        std::vector<std::thread> pool;
        for(unsigned t = 1; t < threads; ++t) pool.emplace_back(work);
        work();
        for(std::thread& t : pool) t.join();
    };

    auto pass = [&](int step, std::vector<glm::vec3>& source, std::vector<glm::vec3>& target,
                    const std::vector<float>* vIn, std::vector<float>* vOut, bool luminanceStop){
        if(luminanceStop) for(size_t i = 0; i < n; ++i) lum[i] = luminance(source[i]);
        parallelRows([&](int y){
            for(int x = 0; x < w; ++x){
                const size_t p = size_t(y) * size_t(w) + size_t(x);
                if(luminanceStop && coverage[p] <= 0.0f){ target[p] = source[p]; if(vOut) (*vOut)[p] = vIn ? (*vIn)[p] : 0.0f; continue; }
                const glm::vec3 np = normals[p], ap = albedos[p];
                const float zp = depth[p], lp = luminanceStop ? lum[p] : 0.0f, cp = coverage[p];
                const float inverseSigmaL = luminanceStop && vIn ? 1.0f / (4.0f * std::sqrt(std::max(0.0f, (*vIn)[p])) + 1e-4f) : 0.0f;
                glm::vec3 sum(0.0f);
                float weightSum = 0.0f, varianceSum = 0.0f;
                for(int dy = -2; dy <= 2; ++dy){
                    const int qy = y + dy * step;
                    if(qy < 0 || qy >= h) continue;
                    for(int dx = -2; dx <= 2; ++dx){
                        const int qx = x + dx * step;
                        if(qx < 0 || qx >= w) continue;
                        const size_t q = size_t(qy) * size_t(w) + size_t(qx);
                        float weight = Kernel[std::abs(dx)] * Kernel[std::abs(dy)];
                        //Ista ploha: normala (^64 kao sest kvadriranja), dubina, pokrivenost, albedo
                        if(!(empty[p] && empty[q])){
                            float nn = std::max(0.0f, glm::dot(np, normals[q]));
                            nn *= nn; nn *= nn; nn *= nn; nn *= nn; nn *= nn; nn *= nn;
                            weight *= nn;
                        }
                        weight *= std::max(0.0f, 1.0f - 4.0f * std::abs(cp - coverage[q]));
                        if(weight <= 0.0f) continue;
                        const float zq = depth[q];
                        const float scale = std::max(1e-6f, 0.02f * float(step) * std::min(zp, zq));
                        //Tri eksponencijalna faktora kao jedan exp zbroja
                        float exponent = std::abs(zp - zq) / scale + glm::length(ap - albedos[q]) * 8.0f;
                        if(luminanceStop) exponent += std::abs(lp - lum[q]) * inverseSigmaL;
                        weight *= std::exp(-exponent);
                        if(weight <= 0.0f) continue;
                        sum += source[q] * weight;
                        weightSum += weight;
                        if(vIn) varianceSum += weight * weight * (*vIn)[q];
                    }
                }
                target[p] = weightSum > 0.0f ? sum / weightSum : source[p];
                if(vOut) (*vOut)[p] = weightSum > 0.0f ? varianceSum / (weightSum * weightSum) : (vIn ? (*vIn)[p] : 0.0f);
            }
        });
    };

    if(colour){
        for(int i = 0; i < 5; ++i){
            pass(1 << i, light, next, &variance, &nextVariance, true);
            light.swap(next);
            variance.swap(nextVariance);
        }
        for(size_t i = 0; i < n; ++i){
            const glm::vec3 c = light[i] * modulation[i];
            frame.cg[i * 4] = c.r; frame.cg[i * 4 + 1] = c.g; frame.cg[i * 4 + 2] = c.b;
        }
    }

    //Sjena na catcheru: bez albeda i bez mjere suma, samo ista ploha. Tri prolaza
    std::vector<glm::vec3> shadow(n), shadowNext(n);
    bool anyShadow = false;
    for(size_t i = 0; i < n; ++i){
        shadow[i] = glm::vec3(frame.shadow[i * 3], frame.shadow[i * 3 + 1], frame.shadow[i * 3 + 2]);
        anyShadow = anyShadow || shadow[i] != glm::vec3(1.0f);
    }
    if(anyShadow){
        for(int i = 0; i < 3; ++i){
            pass(1 << i, shadow, shadowNext, nullptr, nullptr, false);
            shadow.swap(shadowNext);
        }
        for(size_t i = 0; i < n; ++i) for(int k = 0; k < 3; ++k) frame.shadow[i * 3 + size_t(k)] = shadow[i][k];
    }
}
}

}
