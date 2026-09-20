#include "TestHarness.h"

#include <Engine/SolveCache.h>

#include <cstring>
#include <filesystem>
#include <fstream>

namespace{

uint32_t bits(float value){
    uint32_t result = 0;
    std::memcpy(&result, &value, sizeof(result));
    return result;
}

}

int main(){
    TestReport report("cache view-grapha");
    const std::filesystem::path path = std::filesystem::temp_directory_path() / "loom_solve_cache_test.bin";
    const std::filesystem::path broken = std::filesystem::temp_directory_path() / "loom_solve_cache_broken.bin";

    Engine::SolveCache original;
    original.width = 3840;
    original.height = 2160;
    original.step = 2;
    original.requestedFrames = 80;
    original.usedFrames = 73;
    original.pointCount = 9;
    original.localizationPixels = 3.75f;
    original.sourceBytes = 9876543210ull;
    original.sourceWriteTime = -123456789;
    original.buildSignature = 0x123456789abcdef0ull;
    original.keyframes = {0, 7, 29, 72};
    original.observations = {
        {0, 0, {11.25f, 22.5f}},
        {1, 0, {33.75f, 44.125f}},
        {2, 8, {-0.0f, 2160.0f}},
        {3, 8, {3839.5f, 0.03125f}}
    };

    std::string error;
    const bool wrote = Engine::writeSolveCache(path, original, &error);
    report.check("zapis", wrote, error.empty() ? path.string() : error);

    Engine::SolveCache loaded;
    error.clear();
    const bool read = Engine::readSolveCache(path, loaded, &error);
    report.check("citanje", read, error.empty() ? "format prihvacen" : error);

    bool same = read && loaded.width == original.width && loaded.height == original.height &&
        loaded.step == original.step && loaded.requestedFrames == original.requestedFrames &&
        loaded.usedFrames == original.usedFrames && loaded.pointCount == original.pointCount &&
        bits(loaded.localizationPixels) == bits(original.localizationPixels) &&
        loaded.sourceBytes == original.sourceBytes &&
        loaded.sourceWriteTime == original.sourceWriteTime &&
        loaded.buildSignature == original.buildSignature &&
        loaded.keyframes == original.keyframes &&
        loaded.observations.size() == original.observations.size();
    if(same){
        for(size_t i = 0; i < loaded.observations.size(); ++i){
            const auto& a = loaded.observations[i];
            const auto& b = original.observations[i];
            same = same && a.camera == b.camera && a.point == b.point &&
                bits(a.pixel.x) == bits(b.pixel.x) && bits(a.pixel.y) == bits(b.pixel.y);
        }
    }
    report.check("bit-identican round-trip", same,
                 fmt("%zu kljucna kadra, %zu opazanja", loaded.keyframes.size(), loaded.observations.size()));

    //Ne testira se samo magic: promjena jednog bita usred valjanog payloada mora pasti na checksumu.
    {
        std::ifstream input(path, std::ios::binary);
        std::vector<char> bytes((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
        if(bytes.size() > 40) bytes[40] ^= 1;
        std::ofstream output(broken, std::ios::binary | std::ios::trunc);
        output.write(bytes.data(), std::streamsize(bytes.size()));
    }
    error.clear();
    report.check("ostecenje se odbija", !Engine::readSolveCache(broken, loaded, &error), error);

    //Isto vrijedi za djelomicno zapisan cache nakon prekida procesa ili punog diska.
    {
        std::ifstream input(path, std::ios::binary);
        std::vector<char> bytes((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
        bytes.resize(bytes.size() / 2);
        std::ofstream output(broken, std::ios::binary | std::ios::trunc);
        output.write(bytes.data(), std::streamsize(bytes.size()));
    }
    error.clear();
    report.check("skrati se odbija", !Engine::readSolveCache(broken, loaded, &error), error);

    std::filesystem::remove(path);
    std::filesystem::remove(broken);
    report.checkNoValidationMessages();
    return report.result();
}
