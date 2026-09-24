// B1: frame budget - what may happen in a frame, and how much it may cost
//
// All 45 tests before this one measure PIXELS. A three times slower Loom passes them all
// without a word, because none asks how much work it took. This one asks.
//
// Time is a measure that is easy to blame. Three things were tried, and two were rejected
// by measurement before being written down as a claim:
//
//   milliseconds per frame   the same code is 0.81 ms on GTX 1650 and 6.5 ms on lavapipe.
//                            A threshold that passes there catches nothing here
//   ratio to the empty       the empty frame measures COMMAND SUBMISSION, driver work, not
//     frame                  GPU: the ratio is 5.4 here and 16 there
//   measuring rod            a compute chain of integer ops, as a unit of someone else's
//                            work that Loom cannot change. Scatter came out 30x (275 million
//                            chains per frame here, 9 million there) - because ALU and
//                            rasterization don't scale together between the card and the
//                            software rasterizer. Worse than what it was meant to replace,
//                            so it was dropped
//
// What remains is what IS portable, and that is the first two checks: that a frame does not
// allocate, and that the per-object price does not grow with the number of objects. The
// ceilings are numbers about the MACHINE, so the device class holds them and they were
// written that way - one for the whole frame, one for the pixel itself.
//
// And one thing the measurement fixed along the way: the first scene was 25 small spheres,
// and in it a three times more expensive fragment shader passed every check without a single
// number moving. That is why the scene now has a surface across the whole frame, and why the
// same scene is also measured at four times more pixels: that difference is the only place
// where shading shows up alone.
#include "TestHarness.h"
#include "TestScene.h"
#include "Core/LoomConfig.h"
#include "Core/Camera.h"
#include "Core/Light.h"
#include "Core/LoomShapes.h"
#include "Vulkan/RenderTarget.h"
#include "Vulkan/Material.h"
#include "Vulkan/VulkanAllocator.h"

#include <algorithm>
#include <chrono>
#include <glm/gtc/matrix_transform.hpp>

namespace{

const vk::Extent2D size{512, 512};

std::vector<glm::mat4> grid(){
    std::vector<glm::mat4> out;
    for(int y = -2; y <= 2; ++y){
        for(int x = -2; x <= 2; ++x){
            out.push_back(glm::translate(glm::mat4(1.0f), {0.55f * x, 0.55f * y, 0.0f})
                        * glm::scale(glm::mat4(1.0f), glm::vec3(0.42f)));
        }
    }
    return out;
}

double median(std::vector<double> values){
    std::sort(values.begin(), values.end());
    return values[values.size() / 2];
}

}

int main(){
    TestReport report("B1 budzet kadra");

    LoomConfig config;
    config.width = size.width; config.height = size.height;
    config.appName = "budget"; config.engineName = "Loom tests";
    config.headless = true;
    config.enableDepth = true;
    config.pipelineConfig.depthTestEnable = true;
    config.pipelineConfig.depthWriteEnable = true;

    LoomInitializer loom(config);

    CameraConfig cameraConfig;
    cameraConfig.position = {0.0f, 0.0f, 3.0f};
    Camera camera(cameraConfig);
    loom.renderer.setCamera(camera);

    EnvironmentConfig environmentConfig;
    environmentConfig.ambientColor = {0.15f, 0.15f, 0.18f};
    Environment environment(environmentConfig);
    loom.renderer.setEnvironment(environment);

    LightConfig lightConfig;
    lightConfig.type = LightType::Directional;
    lightConfig.direction = {-0.3f, -1.0f, -0.4f};
    lightConfig.shadowExtent = 2.0f;
    Light light(lightConfig);
    loom.renderer.addLight(light);

    LoomShapes::Primitives primitives(loom);

    RenderTarget shadowMap(loom.device, size, makeShadowMapConfig());

    RenderTargetConfig colorConfig;
    colorConfig.keepDepth = true;
    RenderTarget colorTarget(loom.device, size, colorConfig);

    //SAME SCENE, FOUR TIMES MORE PIXELS.
    //
    //A whole frame on a card of this order is stubborn: adding 220 sines to the fragment
    //shader per pixel lifts it 16 percent, because draw calls hold it, not fragments. The
    //difference between these two frames lacks that flaw - draw calls are the same, the
    //shadow pass is the same, only 786432 extra pixels differ. That is then the price of
    //SHADING and nothing else
    const vk::Extent2D bigSize{size.width * 2, size.height * 2};
    RenderTarget bigTarget(loom.device, bigSize, colorConfig);

    PipelineConfig shadowPipelineConfig;
    shadowPipelineConfig.vertShaderPath = std::string(LOOM_SHADER_DIR) + "/shadow.vert.spv";
    shadowPipelineConfig.fragShaderPath = "";
    shadowPipelineConfig.enableColor = false;
    shadowPipelineConfig.vertexAttributes = Vertex::getPositionAttribute();
    shadowPipelineConfig.cullMode = vk::CullModeFlagBits::eNone;
    shadowPipelineConfig.depthTestEnable = true;
    shadowPipelineConfig.depthWriteEnable = true;
    VulkanGraphicsPipeline shadowPipeline(loom.device, shadowPipelineConfig,
        loom.getColorFormat(), shadowMap.getDepthFormat());
    Material shadowMaterial(shadowPipeline);

    const std::vector<glm::mat4> models = grid();

    //The surface sits behind the spheres and fills the frame - fragment work the scene
    //would otherwise not have
    const glm::mat4 backdrop = glm::translate(glm::mat4(1.0f), {0.0f, 0.0f, -1.0f})
                             * glm::scale(glm::mat4(1.0f), glm::vec3(6.0f));

    //The frame is measured TOGETHER with waiting on the GPU. Without it only command-submission
    //time would be measured - and that is the part of the work a shading regression does not
    //touch at all
    auto frameInto = [&](size_t count, const RenderTarget& target){
        const auto started = std::chrono::steady_clock::now();

        if(loom.renderer.beginFrame()){
            loom.renderer.beginPass(shadowMap, light);
            for(size_t i = 0; i < count; ++i){
                loom.renderer.draw(primitives.cubeMesh(), models[i], shadowMaterial);
            }
            loom.renderer.endPass();

            loom.renderer.beginPass(target);

            //A surface across the whole frame, ALWAYS, even in the empty frame. Without it the
            //scene is not fragment-bound: 25 small spheres spend draw calls, so a three times
            //more expensive fragment shader passed through the measurement without a number
            //moving. Since it is in every frame, it falls out of the boundary per-object price
            //and does not break the second check
            primitives.plane(backdrop);

            for(size_t i = 0; i < count; ++i) primitives.sphere(models[i]);
            loom.renderer.endPass();
            loom.renderer.endFrame();
        }
        loom.waitIdle();

        return std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - started).count();
    };

    auto frame = [&](size_t count){ return frameInto(count, colorTarget); };

    //Warm-up: the first frames build meshes, materials and pipelines. If those were measured
    //too, building would be measured instead of drawing
    for(int i = 0; i < 5; ++i) frame(models.size());

    // -------------------------------------------------------------------------------
    // A frame must not allocate
    // -------------------------------------------------------------------------------

    const MemoryStats before = loom.device.getAllocator().getStats();
    VulkanAllocator::resetAllocationsMade();

    const int frames = 40;
    std::vector<double> full, few, empty;
    for(int i = 0; i < frames; ++i) full.push_back(frame(models.size()));
    for(int i = 0; i < frames; ++i) few.push_back(frame(5));
    for(int i = 0; i < frames; ++i) empty.push_back(frame(0));

    //INTERLEAVED, and the difference is taken per PAIR. Two separate arrays and then the
    //difference of their minima is too nervous: it is enough that a big frame catches someone
    //else's work on the machine and a small one does not, and the number jumps threefold
    //(seen 0.070 to 0.244 across four runs). A pair is recorded at the same moment, so load
    //shifts it whole
    for(int i = 0; i < 3; ++i) frameInto(models.size(), bigTarget);

    std::vector<double> pairs;
    for(int i = 0; i < frames; ++i){
        const double small = frameInto(models.size(), colorTarget);
        const double large = frameInto(models.size(), bigTarget);
        pairs.push_back(large - small);
    }

    const MemoryStats after = loom.device.getAllocator().getStats();
    const uint64_t made = VulkanAllocator::getAllocationsMade();

    //Two different claims, and both are needed: the standing count says nothing was left
    //behind, and the MADE count says nothing even happened. An allocation freed in the same
    //frame is invisible to the first, and costs the same as any other
    report.check("kadar ne alocira",
        made == 0 &&
        after.allocationCount == before.allocationCount && after.blockCount == before.blockCount,
        fmt("%llu novih alokacija kroz %d kadrova; stanje %u -> %u alokacija, %u -> %u blokova",
            (unsigned long long)made, 3 * frames,
            before.allocationCount, after.allocationCount,
            before.blockCount, after.blockCount));

    // -------------------------------------------------------------------------------
    // Per-object price must not grow with the number of objects
    // -------------------------------------------------------------------------------

    const double fullMedian = median(full);
    const double emptyMedian = median(empty);

    //Best frame, not median: the median also catches someone else's work on the machine, so
    //the ratio under it was known to jump from 0.9 to 0.55 without a single code change.
    //The best frame is what the code can do when let loose, and that is the only number
    //allowed for comparison
    const double fullBest = *std::min_element(full.begin(), full.end());
    const double fewBest = *std::min_element(few.begin(), few.end());
    const double emptyBest = *std::min_element(empty.begin(), empty.end());

    //The empty frame is subtracted because it is the fixed cost of the pass: only the drawing
    //remains. This is the only timing claim that does not depend on the card. Measured on
    //the best frames: 0.84 across four runs on GTX 1650, and 0.60 to 1.12 on lavapipe, where
    //the empty frame is small enough for someone else's work on the machine to rock it.
    //Threshold 2.0 is above all that, and a quadratic price would score a five here
    const double perObjectMany = (fullBest - emptyBest) / double(models.size());
    const double perObjectFew = (fewBest - emptyBest) / 5.0;
    const double growth = perObjectMany / perObjectFew;

    report.check("cijena je linearna u broju objekata",
        perObjectFew > 0.0 && growth < 2.0,
        fmt("%.4f ms po objektu na %zu, %.4f na 5 -> omjer %.2f (mjereno 0.60-1.12 na dvije kartice, 0.77-1.04 na trecoj)",
            perObjectMany, models.size(), perObjectFew, growth));

    // -------------------------------------------------------------------------------
    // And a ceiling, which is a number about THIS machine
    // -------------------------------------------------------------------------------

    const double best = fullBest;

    //Two numbers, because there are two classes of devices and both run this. Measured: 0.81 ms
    //on GTX 1650, 6.3 to 7.1 ms on lavapipe - the ceiling is about four times above that. It
    //only catches gross regression, and it is fair to say so: the frame itself is held by
    //draw calls, so a slower fragment shader would not even squeak here. The check above
    //catches it
    //Shading price: everything except pixels is the same in both frames, so the difference
    //belongs to them
    //Median of the pairs, not the best: the best grabs that pair where the big frame happened
    //to be fast and the small slow, so it measured zero (0.001 ns across five runs, next to
    //3.065 on the following one). The noise is symmetric around the true value, so the
    //median cuts it and the tail does not
    const double bestPair = median(pairs);
    const double extraPixels = double(bigSize.width) * bigSize.height - double(size.width) * size.height;
    const double perPixel = 1.0e6 * bestPair / extraPixels;   //nanoseconds per pixel

    //Measured across six runs: 0.100 to 0.109 ns per pixel on GTX 1650, and 6.15 to 6.61 on
    //lavapipe - so this ceiling is also per device class, two and a half times above the
    //worst. A mutation that adds 220 sines to the fragment lifts this number sevenfold and
    //fails this check, while the whole frame stays at 0.96 ms and the 3 ms ceiling passes
    //calmly. That is why there are two numbers
    //THREE CLASSES, not two. An integrated card is neither: it shares memory with the CPU
    //and draws the whole desktop on the side, so its scatter is bigger too. Five runs on
    //Intel UHD (CML GT2), twice - because it surfaced along the way that until then the
    //project was built without a single optimization:
    //
    //                      -O0 (worst of five)   -O2 (worst of five)
    //   frame                    4.33 ms                1.42 ms      3.0x faster
    //   shading                  1.67 ns/px             1.58 ns/px   unchanged
    //   price per object         0.116 ms               0.036 ms     3.2x faster
    //
    //THAT IS THE MEASUREMENT THAT JUSTIFIES TWO SEPARATE NUMBERS. Draw calls hold the frame,
    //and submitting commands is CPU work and the compiler speeds it up threefold. Shading is
    //the card's work and a compiler cannot help it - the same number up to noise. If those
    //two rates were one, this difference would not show anywhere.
    //
    //The ceilings are the same factors the other two classes carry: about 3.5x above the
    //worst frame and about 2.5x above the worst shading.
    //
    //SCOPE NOTE: the ceilings for the card and the software rasterizer (3.0 and 24.0 ms) were
    //measured BEFORE the build got -O2, so by the ratio above they are probably about three
    //times too loose. I won't touch them from memory - they need the same kind of measurement
    //on your own device
    const vk::PhysicalDeviceType deviceType = loom.device.getDeviceType();
    const bool software = deviceType == vk::PhysicalDeviceType::eCpu;
    const bool integrated = deviceType == vk::PhysicalDeviceType::eIntegratedGpu;

    const double ceiling = software ? 24.0 : integrated ? 5.0 : 3.0;
    const double shadingCeiling = software ? 16.0 : integrated ? 4.0 : 0.28;

    report.check("sjencanje ostaje unutar stropa",
        perPixel > 0.0 && perPixel < shadingCeiling,
        fmt("%.3f ns po pikselu iz %d parova (%ux%u naspram %ux%u, srednja razlika %.3f ms), strop %.2f",
            perPixel, frames, bigSize.width, bigSize.height, size.width, size.height, bestPair,
            shadingCeiling));

    report.check("kadar ostaje unutar stropa",
        best < ceiling,
        fmt("%s: najbolji %.3f ms, medijan %.3f, prazan %.3f, strop %.1f ms",
            loom.device.getDeviceName().c_str(), best, fullMedian, emptyMedian, ceiling));

    report.checkNoValidationMessages();
    return report.result();
}
