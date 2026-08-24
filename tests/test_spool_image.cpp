// spool image: a real PNG decodes to exactly the pixels that were put into it, and those
//              pixels go on to be a Loom texture without either library knowing about the
//              other.
//
// The two files below are genuine PNGs - header, zlib stream, CRCs and all - written out by
// hand and inlined as bytes. Decoding against a file this suite produced with its own
// encoder would only prove the encoder and the decoder agree with each other.
#include "TestHarness.h"
#include "TestScene.h"
#include "Core/LoomConfig.h"
#include "Core/Camera.h"
#include "Core/LoomShapes.h"
#include "Vulkan/RenderTarget.h"
#include "Vulkan/Texture.h"
#include "Spool/ImageFile.h"
#include <cstdio>
#include <fstream>
#include <set>

//4x4, three channels, a red and green checker - except the top left corner, which is a
//colour nothing else in the image shares. A mirrored or rotated decode cannot hide from it
static const uint8_t rgbPng[] = {
    0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a, 0x00, 0x00, 0x00, 0x0d,
    0x49, 0x48, 0x44, 0x52, 0x00, 0x00, 0x00, 0x04, 0x00, 0x00, 0x00, 0x04,
    0x08, 0x02, 0x00, 0x00, 0x00, 0x26, 0x93, 0x09, 0x29, 0x00, 0x00, 0x00,
    0x17, 0x49, 0x44, 0x41, 0x54, 0x78, 0xda, 0x63, 0xe0, 0x12, 0x91, 0x63,
    0xf8, 0x0f, 0x84, 0x0c, 0x50, 0x8c, 0x4c, 0x32, 0x60, 0x97, 0x01, 0x00,
    0x7f, 0x72, 0x0f, 0x2e, 0x72, 0xe7, 0xaa, 0x02, 0x00, 0x00, 0x00, 0x00,
    0x49, 0x45, 0x4e, 0x44, 0xae, 0x42, 0x60, 0x82
};

//2x2, four channels, four different alphas including a fully transparent one
static const uint8_t rgbaPng[] = {
    0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a, 0x00, 0x00, 0x00, 0x0d,
    0x49, 0x48, 0x44, 0x52, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x02,
    0x08, 0x06, 0x00, 0x00, 0x00, 0x72, 0xb6, 0x0d, 0x24, 0x00, 0x00, 0x00,
    0x18, 0x49, 0x44, 0x41, 0x54, 0x78, 0xda, 0x63, 0xf8, 0xcf, 0xc0, 0xf0,
    0x1f, 0x08, 0x57, 0x31, 0x00, 0xe9, 0xd0, 0xff, 0xff, 0xff, 0x33, 0x00,
    0x00, 0x42, 0x25, 0x07, 0xf9, 0x90, 0xe0, 0x8c, 0xce, 0x00, 0x00, 0x00,
    0x00, 0x49, 0x45, 0x4e, 0x44, 0xae, 0x42, 0x60, 0x82
};

struct Rgba{ uint8_t r,g,b,a; };

static Rgba pixelAt(const Spool::Image& image, uint32_t x, uint32_t y){
    const size_t i = (size_t(y) * image.width + x) * 4;
    return {image.pixels[i], image.pixels[i+1], image.pixels[i+2], image.pixels[i+3]};
}

static bool same(Rgba a, Rgba b){
    return a.r == b.r && a.g == b.g && a.b == b.b && a.a == b.a;
}

static std::string show(Rgba c){
    return fmt("(%u,%u,%u,%u)", c.r, c.g, c.b, c.a);
}

int main(){
    TestReport report("spool image");

    // -------------------------------------------------------------------------------
    // Tri kanala postaju cetiri
    // -------------------------------------------------------------------------------

    const Spool::Image rgb = Spool::decodeImage(rgbPng, sizeof(rgbPng));

    report.check("velicina", rgb.width == 4 && rgb.height == 4 && rgb.byteSize() == 4*4*4,
        fmt("%u x %u, %zu bajtova", rgb.width, rgb.height, rgb.byteSize()));

    //Four channels out of a three channel file, and the file's own count kept so the caller
    //can still tell the difference
    report.check("kanali", rgb.sourceChannels == 3,
        fmt("datoteka je imala %u kanala, izlaz ima 4", rgb.sourceChannels));

    //Every one of the sixteen pixels, against what was written into the file
    size_t wrong = 0;
    Rgba firstWrong{}; Rgba firstExpected{}; uint32_t wrongX = 0, wrongY = 0;
    for(uint32_t y = 0; y < 4; ++y){
        for(uint32_t x = 0; x < 4; ++x){
            Rgba expected = ((x + y) % 2 == 0) ? Rgba{255,0,0,255} : Rgba{0,255,0,255};
            if(x == 0 && y == 0) expected = Rgba{10,20,30,255};

            const Rgba got = pixelAt(rgb, x, y);
            if(!same(got, expected)){
                if(wrong == 0){ firstWrong = got; firstExpected = expected; wrongX = x; wrongY = y; }
                ++wrong;
            }
        }
    }
    report.check("svaki piksel", wrong == 0,
        wrong == 0 ? "16 od 16 tocno, alfa dopunjena na 255"
                   : fmt("%zu krivih, prvi na (%u,%u): %s umjesto %s",
                         wrong, wrongX, wrongY, show(firstWrong).c_str(), show(firstExpected).c_str()));

    //Orientation, said separately because it is the mistake that survives a checker pattern
    report.check("gornji lijevi kut", same(pixelAt(rgb,0,0), Rgba{10,20,30,255}),
        fmt("prvi piksel je %s", show(pixelAt(rgb,0,0)).c_str()));

    // -------------------------------------------------------------------------------
    // Alfa prezivi
    // -------------------------------------------------------------------------------

    const Spool::Image rgba = Spool::decodeImage(rgbaPng, sizeof(rgbaPng));

    const bool alphaHeld =
        same(pixelAt(rgba,0,0), Rgba{255,0,0,255}) &&
        same(pixelAt(rgba,1,0), Rgba{0,255,0,170}) &&
        same(pixelAt(rgba,0,1), Rgba{0,0,255,85}) &&
        same(pixelAt(rgba,1,1), Rgba{255,255,255,0});

    report.check("alfa", rgba.sourceChannels == 4 && alphaHeld,
        fmt("%s %s %s %s",
            show(pixelAt(rgba,0,0)).c_str(), show(pixelAt(rgba,1,0)).c_str(),
            show(pixelAt(rgba,0,1)).c_str(), show(pixelAt(rgba,1,1)).c_str()));

    // -------------------------------------------------------------------------------
    // Iz datoteke i iz memorije mora biti isto
    // -------------------------------------------------------------------------------

    const std::string path = "spool_test_image.png";
    {
        std::ofstream file(path, std::ios::binary);
        file.write(reinterpret_cast<const char*>(rgbPng), sizeof(rgbPng));
    }

    const Spool::Image fromFile = Spool::loadImage(path);
    const ByteDiff difference = diffBytes(fromFile.pixels, rgb.pixels);

    report.check("datoteka = memorija", difference.different == 0 &&
        fromFile.width == rgb.width && fromFile.sourceChannels == rgb.sourceChannels,
        fmt("%zu razlicitih bajtova", difference.different));

    std::remove(path.c_str());

    // -------------------------------------------------------------------------------
    // Kad ne ide, mora reci sto
    // -------------------------------------------------------------------------------

    std::string missingMessage;
    try{ Spool::loadImage("this_file_is_not_here.png"); }
    catch(const std::exception& e){ missingMessage = e.what(); }

    report.check("datoteke nema", missingMessage.find("this_file_is_not_here.png") != std::string::npos,
        missingMessage.empty() ? "nije bacilo" : missingMessage);

    bool garbageThrew = false;
    const uint8_t garbage[] = {0x00,0x01,0x02,0x03,0x04,0x05,0x06,0x07};
    try{ Spool::decodeImage(garbage, sizeof(garbage)); }
    catch(const std::exception&){ garbageThrew = true; }
    report.check("smece nije slika", garbageThrew, "baca iznimku");

    bool emptyThrew = false;
    try{ Spool::decodeImage(nullptr, 0); }
    catch(const std::exception&){ emptyThrew = true; }
    report.check("prazan buffer", emptyThrew, "baca iznimku");

    // -------------------------------------------------------------------------------
    // I ono zbog cega sve ovo postoji: pikseli postaju materijal
    //   Loom ne zna za Spool i Spool ne zna za Loom. Test zna za oba, i to je jedino
    //   mjesto gdje se sastaju
    // -------------------------------------------------------------------------------

    const vk::Extent2D size{256,256};

    LoomConfig config;
    config.width = size.width; config.height = size.height;
    config.appName = "spool"; config.engineName = "Loom tests";
    config.headless = true;   //nista ovdje ne treba prozor
    config.enableDepth = true;
    config.pipelineConfig.colorFormat = vk::Format::eB8G8R8A8Srgb;
    config.pipelineConfig.depthTestEnable = true;
    config.pipelineConfig.depthWriteEnable = true;

    LoomInitializer loom(config);
    LoomShapes::Primitives shapes(loom);

    //Straight from Spool's bytes into a Vulkan texture, with nothing in between
    TextureConfig textureConfig;
    textureConfig.filter = vk::Filter::eNearest;
    Texture decoded(loom.device, loom.command, rgb.pixels.data(),
        vk::Extent2D{rgb.width, rgb.height}, textureConfig);

    CameraConfig camConfig;
    camConfig.position = {0.0f, 2.2f, 0.01f};
    camConfig.target = {0.0f, 0.0f, 0.0f};
    Camera camera(camConfig);
    loom.renderer.setCamera(camera);

    EnvironmentConfig envConfig; envConfig.ambientColor = {1.0f,1.0f,1.0f};
    Environment environment(envConfig);
    loom.renderer.setEnvironment(environment);

    RenderTargetConfig readConfig;
    readConfig.finalLayout = vk::ImageLayout::eTransferSrcOptimal;
    readConfig.extraColorUsage = vk::ImageUsageFlagBits::eTransferSrc;
    RenderTarget out(loom.device, size, readConfig);

    int drawn = 0;
    while(drawn < 2 && !loom.shouldClose()){
        loom.pollEvents();
        if(!loom.renderer.beginFrame()) continue;

        loom.renderer.beginPass(out);
        shapes.plane(decoded, glm::scale(glm::mat4(1.0f), glm::vec3(2.0f,1.0f,2.0f)));
        loom.renderer.endPass();

        loom.renderer.endFrame();
        ++drawn;
    }
    loom.waitIdle();

    const std::vector<uint8_t> rendered = out.readPixels(loom.command).pixels;

    //Nearest filtering and a flat ambient light, so what lands on screen is the checker
    //itself: a handful of distinct colours, not a blur and not one flat surface
    std::set<uint32_t> colours;
    for(size_t i = 0; i + 3 < rendered.size(); i += 4){
        if(rendered[i] || rendered[i+1] || rendered[i+2]){
            colours.insert(uint32_t(rendered[i]) | (uint32_t(rendered[i+1]) << 8) | (uint32_t(rendered[i+2]) << 16));
        }
    }

    report.check("tekstura iz Spoola", countNonBlack(rendered) > 5000 && colours.size() >= 3,
        fmt("%zu ne-crnih piksela, %zu razlicitih boja", countNonBlack(rendered), colours.size()));

    report.checkNoValidationMessages();
    return report.result();
}
