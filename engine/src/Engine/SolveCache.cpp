#include <Engine/SolveCache.h>

#include <cstring>
#include <fstream>
#include <limits>

namespace Engine{
namespace{

constexpr char magic[8] = {'L', 'O', 'O', 'M', 'G', 'R', 'F', '1'};
constexpr uint32_t version = 1;
constexpr uint64_t maximumPayload = 512ull * 1024ull * 1024ull;
constexpr uint64_t maximumElements = 100000000ull;

void fail(std::string* error, const char* message){
    if(error) *error = message;
}

void append32(std::vector<uint8_t>& bytes, uint32_t value){
    for(unsigned shift = 0; shift < 32; shift += 8) bytes.push_back(uint8_t(value >> shift));
}

void append64(std::vector<uint8_t>& bytes, uint64_t value){
    for(unsigned shift = 0; shift < 64; shift += 8) bytes.push_back(uint8_t(value >> shift));
}

void appendFloat(std::vector<uint8_t>& bytes, float value){
    uint32_t bits = 0;
    static_assert(sizeof(bits) == sizeof(value), "neocekivana velicina floata");
    std::memcpy(&bits, &value, sizeof(bits));
    append32(bytes, bits);
}

uint64_t checksum(const uint8_t* data, size_t size){
    uint64_t hash = 1469598103934665603ull;
    for(size_t i = 0; i < size; ++i){
        hash ^= data[i];
        hash *= 1099511628211ull;
    }
    return hash;
}

struct Reader{
    const std::vector<uint8_t>& bytes;
    size_t at = 0;

    bool u32(uint32_t& value){
        if(bytes.size() - at < 4) return false;
        value = 0;
        for(unsigned shift = 0; shift < 32; shift += 8) value |= uint32_t(bytes[at++]) << shift;
        return true;
    }

    bool u64(uint64_t& value){
        if(bytes.size() - at < 8) return false;
        value = 0;
        for(unsigned shift = 0; shift < 64; shift += 8) value |= uint64_t(bytes[at++]) << shift;
        return true;
    }

    bool f32(float& value){
        uint32_t bits = 0;
        if(!u32(bits)) return false;
        std::memcpy(&value, &bits, sizeof(value));
        return true;
    }
};

}

bool writeSolveCache(const std::filesystem::path& path,
                     const SolveCache& cache,
                     std::string* error){
    if(cache.keyframes.empty() || cache.observations.empty() || cache.pointCount == 0){
        fail(error, "cache nema graf");
        return false;
    }
    if(cache.keyframes.size() > std::numeric_limits<uint32_t>::max() ||
       cache.observations.size() > maximumElements){
        fail(error, "cache je prevelik");
        return false;
    }

    std::vector<uint8_t> payload;
    payload.reserve(76 + cache.keyframes.size() * 4 + cache.observations.size() * 16);
    append32(payload, cache.width);
    append32(payload, cache.height);
    append32(payload, cache.step);
    append32(payload, cache.requestedFrames);
    append32(payload, cache.usedFrames);
    append32(payload, cache.pointCount);
    appendFloat(payload, cache.localizationPixels);
    append64(payload, cache.sourceBytes);
    append64(payload, uint64_t(cache.sourceWriteTime));
    append64(payload, cache.buildSignature);
    append32(payload, uint32_t(cache.keyframes.size()));
    append64(payload, uint64_t(cache.observations.size()));
    for(uint32_t frame : cache.keyframes) append32(payload, frame);
    for(const Observation& observation : cache.observations){
        append32(payload, observation.camera);
        append32(payload, observation.point);
        appendFloat(payload, observation.pixel.x);
        appendFloat(payload, observation.pixel.y);
    }

    if(payload.size() > maximumPayload){
        fail(error, "cache je prevelik");
        return false;
    }

    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if(!file){
        fail(error, "ne mogu otvoriti cache za pisanje");
        return false;
    }
    file.write(magic, sizeof(magic));
    std::vector<uint8_t> header;
    append32(header, version);
    append64(header, uint64_t(payload.size()));
    append64(header, checksum(payload.data(), payload.size()));
    file.write(reinterpret_cast<const char*>(header.data()), std::streamsize(header.size()));
    file.write(reinterpret_cast<const char*>(payload.data()), std::streamsize(payload.size()));
    if(!file){
        fail(error, "ne mogu zapisati cijeli cache");
        return false;
    }
    return true;
}

bool readSolveCache(const std::filesystem::path& path,
                    SolveCache& cache,
                    std::string* error){
    std::ifstream file(path, std::ios::binary);
    if(!file){
        fail(error, "ne mogu otvoriti cache");
        return false;
    }

    char fileMagic[sizeof(magic)]{};
    file.read(fileMagic, sizeof(fileMagic));
    if(!file || std::memcmp(fileMagic, magic, sizeof(magic)) != 0){
        fail(error, "cache ima krivi potpis");
        return false;
    }

    std::vector<uint8_t> header(20);
    file.read(reinterpret_cast<char*>(header.data()), std::streamsize(header.size()));
    Reader headerReader{header};
    uint32_t fileVersion = 0;
    uint64_t payloadSize = 0, expectedChecksum = 0;
    if(!file || !headerReader.u32(fileVersion) || !headerReader.u64(payloadSize) ||
       !headerReader.u64(expectedChecksum)){
        fail(error, "cache ima nepotpuno zaglavlje");
        return false;
    }
    if(fileVersion != version){
        fail(error, "cache ima nepodrzanu verziju");
        return false;
    }
    if(payloadSize > maximumPayload){
        fail(error, "cache tvrdi da je prevelik");
        return false;
    }

    std::vector<uint8_t> payload(static_cast<size_t>(payloadSize), uint8_t(0));
    file.read(reinterpret_cast<char*>(payload.data()), std::streamsize(payload.size()));
    if(file.gcount() != std::streamsize(payload.size()) || checksum(payload.data(), payload.size()) != expectedChecksum){
        fail(error, "cache je skracen ili ostecen");
        return false;
    }
    char extra = 0;
    if(file.read(&extra, 1)){
        fail(error, "cache ima podatke iza payloada");
        return false;
    }

    SolveCache loaded;
    Reader in{payload};
    uint64_t sourceWrite = 0, observationCount = 0;
    uint32_t keyframeCount = 0;
    if(!in.u32(loaded.width) || !in.u32(loaded.height) || !in.u32(loaded.step) ||
       !in.u32(loaded.requestedFrames) || !in.u32(loaded.usedFrames) ||
       !in.u32(loaded.pointCount) || !in.f32(loaded.localizationPixels) ||
       !in.u64(loaded.sourceBytes) || !in.u64(sourceWrite) ||
       !in.u64(loaded.buildSignature) || !in.u32(keyframeCount) ||
       !in.u64(observationCount)){
        fail(error, "cache nema sva obavezna polja");
        return false;
    }
    loaded.sourceWriteTime = int64_t(sourceWrite);
    if(keyframeCount < 3 || keyframeCount > maximumElements ||
       observationCount == 0 || observationCount > maximumElements || loaded.pointCount == 0){
        fail(error, "cache ima nevjerojatne brojace");
        return false;
    }
    const uint64_t expectedBytes = uint64_t(keyframeCount) * 4ull + observationCount * 16ull;
    if(expectedBytes != uint64_t(payload.size() - in.at)){
        fail(error, "brojaci cachea ne odgovaraju duljini");
        return false;
    }

    loaded.keyframes.resize(keyframeCount);
    uint32_t previousFrame = 0;
    for(size_t index = 0; index < loaded.keyframes.size(); ++index){
        uint32_t& frame = loaded.keyframes[index];
        if(!in.u32(frame) || frame >= loaded.usedFrames || (index > 0 && frame <= previousFrame)){
            fail(error, "cache ima nevaljan kljucni kadar");
            return false;
        }
        previousFrame = frame;
    }
    loaded.observations.resize(size_t(observationCount));
    for(Observation& observation : loaded.observations){
        if(!in.u32(observation.camera) || !in.u32(observation.point) ||
           !in.f32(observation.pixel.x) || !in.f32(observation.pixel.y) ||
           observation.camera >= keyframeCount || observation.point >= loaded.pointCount){
            fail(error, "cache ima nevaljano opazanje");
            return false;
        }
    }
    cache = std::move(loaded);
    return true;
}

}
