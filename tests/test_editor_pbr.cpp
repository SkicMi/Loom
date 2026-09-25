// PBR meshevi u pogledu editora: projekcija, obris, materijal, mapa, prozirnost.
//
// ZASTO SE OVO TESTIRA. Meshevi se crtaju Loomovom kamerom (vidno polje + glavna tocka), a crte
// i snimka pogledovom (pinhole u pikselima). Ako se te dvije ne slazu, kocka se kroz rijesenu
// kameru odlijepi od snimke za nekoliko piksela - bas ono sto matchmove provjerava - a u
// orbiti se nista ne primijeti. Zato:
//
//   1. PROJEKCIJA: tocke kroz Loomovu kameru i kroz pogled padaju na isti piksel, i s glavnom
//      tockom izvan sredine (rijesena kamera je rijetko tocno u sredini)
//   2. OBRIS: rub nacrtane kocke = obuhvat pogledove projekcije njezinih vrhova
//   3. POZADINA prozirna (slaze se preko snimke), materijal crven gdje je crven, mapa zelena
//      gdje je zelena, prozirni materijal pola prozirnosti
#include "TestHarness.h"

#include "Core/LoomConfig.h"
#include "Core/LoomInitializer.h"

#include "../src/LoomPbr.h"
#include "../src/LoomModel.h"

#include <Spool/ImageFile.h>

#include <cmath>
#include <chrono>
#include <fstream>
#include <thread>
#include <cstring>
#include <filesystem>

namespace{

float half(uint16_t h){
    const uint32_t sign = (h >> 15) & 1, exponent = (h >> 10) & 31, mantissa = h & 1023;
    float value;
    if(exponent == 0) value = std::ldexp(float(mantissa), -24);
    else if(exponent == 31) value = mantissa ? NAN : INFINITY;
    else value = std::ldexp(float(mantissa | 1024), int(exponent) - 25);
    return sign ? -value : value;
}

struct Pixels{
    std::vector<float> rgba;
    uint32_t width = 0, height = 0;
    glm::vec4 at(uint32_t x, uint32_t y) const{
        const size_t i = (size_t(y) * width + x) * 4;
        return glm::vec4(rgba[i], rgba[i + 1], rgba[i + 2], rgba[i + 3]);
    }
};

Pixels decode(const ImageData& image){
    Pixels out;
    out.width = image.extent.width;
    out.height = image.extent.height;
    out.rgba.resize(size_t(out.width) * out.height * 4);
    const uint16_t* h = reinterpret_cast<const uint16_t*>(image.pixels.data());
    for(size_t i = 0; i < out.rgba.size(); ++i) out.rgba[i] = half(h[i]);
    return out;
}

}

int main(){
    TestReport report("E3 PBR u pogledu");

    LoomConfig config;
    config.width = 640; config.height = 480;
    config.appName = "pbr"; config.engineName = "Loom tests";
    config.headless = true;
    config.maxDescriptorSets = 256;
    LoomInitializer loom(config);

    //Pogled koji nije u kutu prozora, i kamera kroz koju gleda s glavnom tockom izvan sredine
    const Treadle::Rect area{30.0f, 20.0f, 320.0f, 240.0f};
    Warp::Stage stage;
    const Warp::Id camera = stage.create("Kamera");
    Warp::Camera lens;
    lens.width = 1600; lens.height = 1200;
    lens.focalPixels = 1500.0f;
    lens.centreX = 870.0f; lens.centreY = 540.0f;
    stage.get(camera)->camera = lens;
    stage.get(camera)->local.translation = glm::vec3(0.6f, 1.2f, 3.5f);
    stage.get(camera)->local.rotation = glm::normalize(glm::quatLookAt(glm::normalize(glm::vec3(-0.6f, -1.0f, -3.5f)), glm::vec3(0, 1, 0)));
    Loom::ViewportState state;
    state.lookThrough = camera;
    const Loom::ViewCamera view = Loom::viewCameraFor(stage, 1.0, area, state);

    //-- 1. projekcija: Loomova kamera = pogled ------------------------------------------------
    {
        const Camera loomCamera = Loom::loomCameraFor(view, view.rect, 1.0f, 0.01f, 100.0f);
        const glm::mat4 clipFromWorld = loomCamera.getProjection(uint32_t(view.rect.width), uint32_t(view.rect.height)) * loomCamera.getView();
        float worst = 0.0f;
        for(int i = 0; i < 40; ++i){
            const glm::vec3 p(std::sin(i * 1.7f) * 0.8f, std::cos(i * 0.9f) * 0.6f, std::sin(i * 0.4f) * 0.7f);
            glm::vec2 expected;
            if(!Loom::project(view, p, expected)) continue;
            const glm::vec4 clip = clipFromWorld * glm::vec4(p, 1.0f);
            const glm::vec2 ndc = glm::vec2(clip) / clip.w;
            const glm::vec2 pixel(view.rect.x + (ndc.x * 0.5f + 0.5f) * view.rect.width, view.rect.y + (ndc.y * 0.5f + 0.5f) * view.rect.height);
            worst = std::max(worst, glm::length(pixel - expected));
        }
        report.check("Loomova kamera projicira kao pogled, i s glavnom tockom izvan sredine", worst < 0.01f,
            fmt("najvece odstupanje %.4f px", worst));
    }

    //-- 2. kocka: obris, boja, pozadina ----------------------------------------------------------
    const std::filesystem::path green = std::filesystem::temp_directory_path() / "loom_pbr_zeleno.png";
    {
        const uint8_t pixels[16] = {20, 220, 40, 255, 20, 220, 40, 255, 20, 220, 40, 255, 20, 220, 40, 255};
        Spool::savePng(green.string(), Spool::imageFromPixels(pixels, 2, 2));
    }
    Warp::Material red;
    red.name = "Crvena";
    red.baseColor = glm::vec4(0.8f, 0.05f, 0.05f, 1.0f);
    red.roughness = 0.6f;
    const int redIndex = stage.addMaterial(red);
    Warp::Material textured;
    textured.name = "Zelena mapa";
    textured.baseColorMap = Warp::TextureSlot{green.string(), -1, 0, 1.0f};
    const int texturedIndex = stage.addMaterial(textured);
    Warp::Material glass;
    glass.name = "Staklo";
    glass.baseColor = glm::vec4(0.9f, 0.9f, 0.9f, 0.5f);
    glass.alphaMode = Warp::Material::Alpha::Blend;
    const int glassIndex = stage.addMaterial(glass);

    const Warp::Id cube = stage.create("Kocka");
    stage.get(cube)->mesh = Warp::Mesh{Warp::Shape::Cube, glm::vec3(1.0f), redIndex};

    Loom::ViewportMeshes meshes(loom, true);
    auto draw = [&](){
        const bool ready = meshes.prepare(stage, 1.0, view, 1.0f, 0.01f, 100.0f);
        if(ready){
            loom.renderer.beginFrame();
            meshes.render();
            loom.renderer.endFrame();
        }
        return ready ? decode(meshes.readPixels()) : Pixels{};
    };

    glm::vec2 centre;
    Loom::project(view, glm::vec3(0.0f), centre);
    const glm::ivec2 c(int(centre.x - view.rect.x), int(centre.y - view.rect.y));
    {
        const Pixels image = draw();
        const bool drawn = image.width == uint32_t(view.rect.width) && image.height == uint32_t(view.rect.height);
        //Obuhvat pogledove projekcije vrhova kocke, u pikselima slike
        glm::vec2 low(1e9f), high(-1e9f);
        for(int i = 0; i < 8; ++i){
            glm::vec2 p;
            Loom::project(view, glm::vec3(i & 1 ? 0.5f : -0.5f, i & 2 ? 0.5f : -0.5f, i & 4 ? 0.5f : -0.5f), p);
            low = glm::min(low, p - glm::vec2(view.rect.x, view.rect.y));
            high = glm::max(high, p - glm::vec2(view.rect.x, view.rect.y));
        }
        glm::vec2 drawnLow(1e9f), drawnHigh(-1e9f);
        for(uint32_t y = 0; drawn && y < image.height; ++y){
            for(uint32_t x = 0; x < image.width; ++x){
                if(image.at(x, y).a < 0.5f) continue;
                drawnLow = glm::min(drawnLow, glm::vec2(float(x), float(y)));
                drawnHigh = glm::max(drawnHigh, glm::vec2(float(x + 1), float(y + 1)));
            }
        }
        const float edge = std::max(glm::length(drawnLow - low), glm::length(drawnHigh - high));
        report.check("obris kocke = pogledova projekcija vrhova", drawn && edge < 1.5f,
            fmt("projekcija (%.1f,%.1f)-(%.1f,%.1f), nacrtano (%.0f,%.0f)-(%.0f,%.0f)", low.x, low.y, high.x, high.y,
                drawnLow.x, drawnLow.y, drawnHigh.x, drawnHigh.y));

        const glm::vec4 middle = drawn ? image.at(uint32_t(c.x), uint32_t(c.y)) : glm::vec4(0.0f);
        const glm::vec4 corner = drawn ? image.at(1, 1) : glm::vec4(1.0f);
        report.check("crveni materijal crven, pozadina prozirna", middle.a > 0.99f && middle.r > 3.0f * middle.g && middle.r > 0.1f &&
                     corner.a == 0.0f,
            fmt("sredina (%.2f %.2f %.2f %.2f), kut alfa %.2f", middle.r, middle.g, middle.b, middle.a, corner.a));
    }

    //-- 3. mapa boje ------------------------------------------------------------------------------
    {
        stage.get(cube)->mesh->material = texturedIndex;
        const Pixels image = draw();
        const glm::vec4 middle = image.width ? image.at(uint32_t(c.x), uint32_t(c.y)) : glm::vec4(0.0f);
        report.check("mapa osnovne boje se uzorkuje", middle.g > 2.0f * middle.r && middle.g > 2.0f * middle.b,
            fmt("sredina (%.2f %.2f %.2f)", middle.r, middle.g, middle.b));
    }

    //-- 4. prozirno --------------------------------------------------------------------------------
    {
        stage.get(cube)->mesh->material = glassIndex;
        const Pixels image = draw();
        const glm::vec4 middle = image.width ? image.at(uint32_t(c.x), uint32_t(c.y)) : glm::vec4(0.0f);
        //Dvije plohe po 0.5 jedna preko druge (prednja i straznja): 1 - 0.5 * 0.5
        report.check("prozirni materijal: dvije plohe po pola", std::fabs(middle.a - 0.75f) < 0.02f,
            fmt("alfa %.3f (ocekivano 0.75)", middle.a));
    }

    //-- 5. tijelo bez materijala: zadani materijal u boji tijela ------------------------------------
    {
        stage.get(cube)->mesh->material = -1;
        stage.get(cube)->mesh->colour = glm::vec3(0.95f, 0.55f, 0.15f);
        const Pixels image = draw();
        const glm::vec4 middle = image.width ? image.at(uint32_t(c.x), uint32_t(c.y)) : glm::vec4(0.0f);
        report.check("tijelo bez materijala se crta u svojoj boji", middle.a > 0.99f && middle.r > middle.b * 2.0f,
            fmt("sredina (%.2f %.2f %.2f %.2f), nacrtano %zu", middle.r, middle.g, middle.b, middle.a, meshes.drawnPrimitives));
    }

    //-- 6. animated GLTF skin: a bone key must move the skinned triangle ------------------------
    {
        const std::filesystem::path directory = std::filesystem::temp_directory_path() / "loom_pbr_skin_test";
        std::filesystem::create_directories(directory);
        const std::filesystem::path gltfPath = directory / "skin.gltf";
        const std::vector<float> positions{-0.2f,0.0f,0.0f, 0.2f,0.0f,0.0f, 0.0f,0.4f,0.0f};
        const uint8_t joints[12] = {1,0,0,0, 1,0,0,0, 1,0,0,0};
        const std::vector<float> weights{1,0,0,0, 1,0,0,0, 1,0,0,0};
        std::vector<float> inverseBind(32, 0.0f);
        for(size_t joint = 0; joint < 2; ++joint) for(size_t i = 0; i < 4; ++i) inverseBind[joint * 16 + i * 5] = 1.0f;
        inverseBind[29] = -0.2f; //inverse bind translates the child joint back to the mesh origin
        {
            std::ofstream binary(directory / "skin.bin", std::ios::binary);
            binary.write(reinterpret_cast<const char*>(positions.data()), std::streamsize(positions.size() * sizeof(float)));
            binary.write(reinterpret_cast<const char*>(joints), sizeof(joints));
            binary.write(reinterpret_cast<const char*>(weights.data()), std::streamsize(weights.size() * sizeof(float)));
            binary.write(reinterpret_cast<const char*>(inverseBind.data()), std::streamsize(inverseBind.size() * sizeof(float)));
        }
        {
            std::ofstream gltf(gltfPath);
            gltf << R"({"asset":{"version":"2.0"},"scene":0,"scenes":[{"nodes":[0]}],"nodes":[{"name":"Hips","children":[1,2]},{"name":"Chest","translation":[0,0.2,0]},{"name":"Body","mesh":0,"skin":0}],"skins":[{"skeleton":0,"joints":[0,1],"inverseBindMatrices":3}],"meshes":[{"primitives":[{"attributes":{"POSITION":0,"JOINTS_0":1,"WEIGHTS_0":2}}]}],"buffers":[{"uri":"skin.bin","byteLength":224}],"bufferViews":[{"buffer":0,"byteOffset":0,"byteLength":36},{"buffer":0,"byteOffset":36,"byteLength":12},{"buffer":0,"byteOffset":48,"byteLength":48},{"buffer":0,"byteOffset":96,"byteLength":128}],"accessors":[{"bufferView":0,"componentType":5126,"count":3,"type":"VEC3"},{"bufferView":1,"componentType":5121,"count":3,"type":"VEC4"},{"bufferView":2,"componentType":5126,"count":3,"type":"VEC4"},{"bufferView":3,"componentType":5126,"count":2,"type":"MAT4"}]})";
        }
        Spool::GltfScene skinScene;
        std::string skinError;
        const bool loaded = Spool::loadGltf(gltfPath.string(), skinScene, skinError);
        Warp::Stage skinStage;
        const Loom::ModelImportReport imported = loaded ? Loom::importGltf(skinStage, skinScene) : Loom::ModelImportReport{};
        const Warp::Id chest = skinStage.find("/skin/Hips/Chest");
        if(skinStage.get(chest)){
            skinStage.get(chest)->rotationKeys.set(1.0, glm::quat(1,0,0,0));
            skinStage.get(chest)->rotationKeys.set(2.0, glm::angleAxis(glm::radians(90.0f), glm::vec3(0,0,1)));
        }
        auto renderSkinFrame = [&](double frame){
            bool ready = false;
            for(int attempt = 0; attempt < 100 && !ready; ++attempt){
                ready = meshes.prepare(skinStage, frame, view, 1.0f, 0.01f, 100.0f);
                if(!ready) std::this_thread::sleep_for(std::chrono::milliseconds(2));
            }
            if(!ready) return Pixels{};
            loom.renderer.beginFrame();
            meshes.render();
            loom.renderer.endFrame();
            return decode(meshes.readPixels());
        };
        const Pixels restImage = renderSkinFrame(1.0);
        const Pixels bentImage = renderSkinFrame(2.0);
        double difference = 0.0;
        size_t visible = 0;
        if(restImage.width == bentImage.width && restImage.height == bentImage.height){
            for(uint32_t y = 0; y < restImage.height; ++y) for(uint32_t x = 0; x < restImage.width; ++x){
                const glm::vec4 a = restImage.at(x, y), b = bentImage.at(x, y);
                if(a.a > 0.05f || b.a > 0.05f) ++visible;
                difference += glm::length(glm::vec3(a) - glm::vec3(b));
            }
        }
        report.check("GLTF skin weights deform the imported mesh when a joint is animated",
            loaded && imported.problem.empty() && restImage.width > 0 && bentImage.width > 0 && visible > 10 && difference > 1.0,
            fmt("visible samples %zu, accumulated image change %.3f", visible, difference));
        std::filesystem::remove_all(directory);
    }

    std::filesystem::remove(green);
    report.checkNoValidationMessages();
    return report.result();
}
