// spool export: pixels become a PNG on disk, that PNG reads back as exactly the same
//               pixels, and a headless render turns into a numbered sequence of frames.
//
// This is the far end of the split. A renderer with no window, driven by a frame number
// rather than a clock, only becomes a sequence export once the frames reach the disk - and
// only counts as one if the frame that comes back off the disk is the frame that was drawn.
#include "TestHarness.h"
#include "TestScene.h"
#include "Core/LoomConfig.h"
#include "Core/Camera.h"
#include "Core/LoomShapes.h"
#include "Vulkan/RenderTarget.h"
#include "Spool/ImageFile.h"
#include "Spool/Sequence.h"
#include <filesystem>
#include <glm/gtc/matrix_transform.hpp>

//A pattern with a different value in every channel of every pixel, so a swapped channel or
//a flipped row cannot survive the round trip unnoticed
static Spool::Image makePattern(uint32_t width, uint32_t height){
    Spool::Image image;
    image.width = width;
    image.height = height;
    image.sourceChannels = 4;
    image.pixels.resize(size_t(width) * height * 4);

    for(uint32_t y = 0; y < height; ++y){
        for(uint32_t x = 0; x < width; ++x){
            const size_t i = (size_t(y) * width + x) * 4;
            image.pixels[i+0] = static_cast<uint8_t>(x * 7 + 3);
            image.pixels[i+1] = static_cast<uint8_t>(y * 11 + 17);
            image.pixels[i+2] = static_cast<uint8_t>((x ^ y) * 5 + 1);
            image.pixels[i+3] = static_cast<uint8_t>(255 - ((x + y) % 64));
        }
    }
    return image;
}

int main(){
    TestReport report("spool export");

    const std::filesystem::path work = std::filesystem::temp_directory_path() / "loom_spool_export";
    std::filesystem::remove_all(work);

    // -------------------------------------------------------------------------------
    // Zapis pa citanje mora vratiti iste bajtove
    // -------------------------------------------------------------------------------

    const Spool::Image pattern = makePattern(37, 23);   //neither square nor a power of two
    const std::string patternPath = (work / "pattern.png").string();

    Spool::savePng(patternPath, pattern);

    report.check("mapa je napravljena", std::filesystem::exists(patternPath),
        fmt("%s postoji, %ju bajtova",
            patternPath.c_str(), std::filesystem::file_size(patternPath)));

    const Spool::Image readBack = Spool::loadImage(patternPath);
    const ByteDiff roundTrip = diffBytes(pattern.pixels, readBack.pixels);

    //PNG is lossless, so this is not a tolerance. Any difference at all is a bug
    report.check("puni krug", roundTrip.different == 0 &&
        readBack.width == pattern.width && readBack.height == pattern.height,
        fmt("%u x %u, %zu razlicitih od %zu bajtova",
            readBack.width, readBack.height, roundTrip.different, pattern.pixels.size()));

    //Compression only changes the file, never the pixels
    const std::string smallPath = (work / "pattern_max.png").string();
    Spool::SaveConfig hard; hard.pngCompression = 9;
    Spool::savePng(smallPath, pattern, hard);

    const ByteDiff compressed = diffBytes(pattern.pixels, Spool::loadImage(smallPath).pixels);
    report.check("kompresija ne mijenja piksele", compressed.different == 0,
        fmt("razina 6 -> %ju bajtova, razina 9 -> %ju bajtova, piksela razlicitih: %zu",
            std::filesystem::file_size(patternPath), std::filesystem::file_size(smallPath),
            compressed.different));

    // -------------------------------------------------------------------------------
    // BGRA u RGBA
    // -------------------------------------------------------------------------------

    const uint8_t bgra[] = {  10, 20, 30, 40,    50, 60, 70, 80 };
    const Spool::Image swapped = Spool::imageFromPixels(bgra, 2, 1, Spool::ChannelOrder::BGRA);
    const Spool::Image kept = Spool::imageFromPixels(bgra, 2, 1, Spool::ChannelOrder::RGBA);

    const bool swapCorrect =
        swapped.pixels[0] == 30 && swapped.pixels[1] == 20 && swapped.pixels[2] == 10 && swapped.pixels[3] == 40 &&
        swapped.pixels[4] == 70 && swapped.pixels[5] == 60 && swapped.pixels[6] == 50 && swapped.pixels[7] == 80;

    report.check("BGRA -> RGBA", swapCorrect && kept.pixels[0] == 10,
        fmt("(%u,%u,%u,%u) iz (%u,%u,%u,%u), a RGBA ostaje netaknut",
            swapped.pixels[0], swapped.pixels[1], swapped.pixels[2], swapped.pixels[3],
            bgra[0], bgra[1], bgra[2], bgra[3]));

    // -------------------------------------------------------------------------------
    // Numeriranje sekvence
    // -------------------------------------------------------------------------------

    Spool::SequenceConfig sequenceConfig;
    sequenceConfig.directory = (work / "seq").string();
    sequenceConfig.prefix = "shot_";
    sequenceConfig.digits = 4;

    Spool::SequenceWriter writer(sequenceConfig);

    report.check("imena su poredana",
        writer.pathFor(0).find("shot_0000.png") != std::string::npos &&
        writer.pathFor(9).find("shot_0009.png") != std::string::npos &&
        writer.pathFor(10).find("shot_0010.png") != std::string::npos,
        "shot_0000, shot_0009, shot_0010 - sortiraju se kako se i gledaju");

    // -------------------------------------------------------------------------------
    // I ono pravo: headless render postaje sekvenca na disku
    // -------------------------------------------------------------------------------

    const vk::Extent2D size{192,192};

    LoomConfig config;
    config.width = size.width; config.height = size.height;
    config.appName = "spool export"; config.engineName = "Loom tests";
    config.headless = true;
    config.enableDepth = true;
    config.pipelineConfig.colorFormat = vk::Format::eB8G8R8A8Srgb;
    config.pipelineConfig.depthTestEnable = true;
    config.pipelineConfig.depthWriteEnable = true;

    LoomInitializer loom(config);
    LoomShapes::Primitives shapes(loom);

    CameraConfig camConfig;
    camConfig.position = {1.6f, 1.3f, 2.1f};
    camConfig.target = {0.0f, 0.0f, 0.0f};
    Camera camera(camConfig);
    loom.renderer.setCamera(camera);

    EnvironmentConfig envConfig; envConfig.ambientColor = {0.2f,0.2f,0.2f};
    Environment environment(envConfig);
    loom.renderer.setEnvironment(environment);

    LightConfig lightConfig;
    lightConfig.type = LightType::Directional;
    lightConfig.direction = {-0.3f,-1.0f,-0.4f};
    Light light(lightConfig);
    loom.renderer.addLight(light);

    RenderTargetConfig readConfig;
    readConfig.finalLayout = vk::ImageLayout::eTransferSrcOptimal;
    readConfig.extraColorUsage = vk::ImageUsageFlagBits::eTransferSrc;
    RenderTarget out(loom.device, size, readConfig);

    const uint32_t frames = 5;
    const float framesPerSecond = 24.0f;

    std::vector<std::vector<uint8_t>> rendered;
    std::vector<std::string> paths;

    for(uint32_t frame = 0; frame < frames; ++frame){
        //Frame N at N over the frame rate. Not a clock - that is the whole difference
        //between a sequence that can be resumed and one that cannot
        const float time = float(frame) / framesPerSecond;

        const glm::mat4 model = glm::rotate(glm::mat4(1.0f), time * 3.0f, glm::vec3(0.2f,1.0f,0.1f));

        if(!loom.renderer.beginFrame()) continue;
        loom.renderer.beginPass(out);
        shapes.cube(model);
        loom.renderer.endPass();
        loom.renderer.endFrame();
        loom.waitIdle();

        const ImageData shot = out.readPixels(loom.command);
        rendered.push_back(shot.pixels);

        //Loom's target is B8G8R8A8Srgb, so the bytes are BGRA. Spool is told that rather
        //than left to guess, and the sRGB encoding is already what a PNG wants
        paths.push_back(writer.write(
            Spool::imageFromPixels(shot.pixels.data(), size.width, size.height, Spool::ChannelOrder::BGRA)));
    }

    report.check("pet frameova", writer.frameCount() == frames && rendered.size() == frames,
        fmt("%u zapisanih, zadnji je %s", writer.frameCount(),
            paths.empty() ? "-" : std::filesystem::path(paths.back()).filename().string().c_str()));

    size_t missing = 0;
    for(const std::string& path : paths){
        if(!std::filesystem::exists(path)) ++missing;
    }
    report.check("svi su na disku", missing == 0, fmt("%zu nedostaje od %zu", missing, paths.size()));

    //Each file has to hold the frame that was drawn, not a frame that looks like it
    size_t mismatched = 0;
    size_t worstDelta = 0;
    for(uint32_t frame = 0; frame < paths.size(); ++frame){
        const Spool::Image fromDisk = Spool::loadImage(paths[frame]);
        const Spool::Image expected = Spool::imageFromPixels(rendered[frame].data(),
            size.width, size.height, Spool::ChannelOrder::BGRA);

        const ByteDiff difference = diffBytes(expected.pixels, fromDisk.pixels);
        if(difference.different > 0) ++mismatched;
        worstDelta = std::max(worstDelta, difference.maxDelta);
    }
    report.check("disk = nacrtano", mismatched == 0 && worstDelta == 0,
        fmt("%zu od %zu frameova se razlikuje, najveca delta %zu",
            mismatched, paths.size(), worstDelta));

    //If every frame were the same picture, the check above would pass on a still image
    const ByteDiff moved = diffBytes(rendered.front(), rendered.back());
    report.check("sekvenca se mice", moved.different > 0,
        fmt("prvi i zadnji frame razlikuju se u %zu bajtova", moved.different));

    // -------------------------------------------------------------------------------
    // Granice
    // -------------------------------------------------------------------------------

    bool unknownFormatThrew = false;
    try{ Spool::saveImage((work / "nope.tiff").string(), pattern); }
    catch(const std::exception&){ unknownFormatThrew = true; }
    report.check("nepoznat format", unknownFormatThrew, "baca iznimku, i kaze koji");

    bool emptyThrew = false;
    try{ Spool::savePng((work / "empty.png").string(), Spool::Image{}); }
    catch(const std::exception&){ emptyThrew = true; }
    report.check("prazna slika", emptyThrew, "baca iznimku");

    bool badDigitsThrew = false;
    try{
        Spool::SequenceConfig bad; bad.digits = 0;
        Spool::SequenceWriter refused(bad);
    }
    catch(const std::exception&){ badDigitsThrew = true; }
    report.check("nula znamenki", badDigitsThrew, "baca iznimku");

    std::filesystem::remove_all(work);

    report.checkNoValidationMessages();
    return report.result();
}
