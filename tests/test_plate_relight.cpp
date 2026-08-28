// 2c: svjetlo ubaceno u sliku koja je dosla iz filea.
//
// Sve komade smo dokazali odvojeno: odprojekciju (1a), normale (1b), svjetlo nad G-bufferom
// (1c), kompoziciju (1d), dubinu iz filea (2a) i kalibraciju (2b). Ovo je prvi put da lanac
// ide s KRAJA na kraj - slika i karta dubine se ucitaju s diska, kalibriraju u metre, i
// svjetlo se stvarno racuna nad geometrijom koja iz njih ispadne.
//
// Scena je namjerno takva da se istina da izracunati rukom: ravna ploha na tocno pet metara.
// Tada su normale (0,0,1) posvuda, pozicija svakog piksela je poznata iz intrinsika, i cijeli
// Blinn-Phong se da napisati na CPU-u. Nista se ne usporeduje s drugom slikom - usporeduje se
// s ARITMETIKOM.
#include "TestHarness.h"
#include "TestScene.h"
#include "Core/LoomConfig.h"
#include "Core/Camera.h"
#include "Core/CameraIntrinsics.h"
#include "Core/Light.h"
#include "Vulkan/NormalMap.h"
#include "Vulkan/PositionMap.h"
#include "Vulkan/Relight.h"
#include "Vulkan/RenderTarget.h"
#include "Vulkan/Texture.h"

#include <Spool/DepthFile.h>
#include <Spool/ImageFile.h>

#include <cmath>
#include <filesystem>
#include <glm/gtc/matrix_transform.hpp>

namespace{

const uint32_t sceneWidth = 256;
const uint32_t sceneHeight = 192;
const vk::Format sceneFormat = vk::Format::eR32G32B32A32Sfloat;

const float wallDistance = 5.0f;
const float nearDistance = 1.5f;
const float farDistance = 20.0f;

const float shininess = 24.0f;
const float specularStrength = 0.35f;

const glm::vec4& at(const std::vector<uint8_t>& pixels, size_t index){
    return reinterpret_cast<const glm::vec4*>(pixels.data())[index];
}

//Isti racun koji radi shadeSurface, napisan ovdje neovisno. Ovo je istina protiv koje se
//mjeri, pa mora doci iz aritmetike a ne iz druge slike
glm::vec3 expectedLight(const glm::vec3& plateColor, const glm::vec3& worldPos,
                        const glm::vec3& normal, const glm::vec3& cameraPos,
                        const glm::vec3& lightPos, const glm::vec3& lightColor, float range){
    const glm::vec3 V = glm::normalize(cameraPos - worldPos);
    const glm::vec3 toLight = lightPos - worldPos;
    const float distance = std::max(glm::length(toLight), 1e-4f);
    const glm::vec3 L = toLight / distance;

    const float window = glm::clamp(1.0f - std::pow(distance / std::max(range, 1e-4f), 4.0f), 0.0f, 1.0f);
    const float attenuation = window * window / (distance * distance);

    const glm::vec3 H = glm::normalize(L + V);
    const float diffuse = std::max(glm::dot(normal, L), 0.0f);
    const float specular = std::pow(std::max(glm::dot(normal, H), 0.0f), shininess) * specularStrength;

    //Isti redoslijed zbrajanja kao u shaderu: krece se OD snimke
    glm::vec3 lit = plateColor;
    lit += plateColor * lightColor * diffuse * attenuation;
    lit += lightColor * specular * attenuation;
    return lit;
}

}

int main(){
    TestReport report("2c svjetlo u slici");

    const std::filesystem::path work = std::filesystem::temp_directory_path() / "loom_plate_relight";
    std::filesystem::remove_all(work);
    std::filesystem::create_directories(work);

    // -------------------------------------------------------------------------------
    // Materijal na disku: ravna ploha na pet metara, i siva slika preko nje
    // -------------------------------------------------------------------------------

    const uint8_t grey = 128;

    {
        std::vector<uint8_t> pixels(size_t(sceneWidth) * sceneHeight * 4);
        for(size_t i = 0; i + 3 < pixels.size(); i += 4){
            pixels[i+0] = grey; pixels[i+1] = grey; pixels[i+2] = grey; pixels[i+3] = 255;
        }
        Spool::savePng((work / "plate.png").string(),
                       Spool::imageFromPixels(pixels.data(), sceneWidth, sceneHeight));

        //Disparitet koji uz fromRange(1.5, 20) daje tocno pet metara, na svakom pikselu
        Spool::DepthImage depth;
        depth.width = sceneWidth;
        depth.height = sceneHeight;
        depth.sourceBits = 32;
        depth.values.assign(size_t(sceneWidth) * sceneHeight,
            (1.0f / wallDistance - 1.0f / farDistance) / (1.0f / nearDistance - 1.0f / farDistance));

        Spool::saveDepthImage((work / "depth.pfm").string(), depth);
    }

    const Spool::Image plateImage = Spool::loadImage((work / "plate.png").string());
    const Spool::DepthImage depthImage = Spool::loadDepthImage((work / "depth.pfm").string());

    report.check("slika i dubina su ucitane s diska",
        plateImage.width == sceneWidth && depthImage.width == sceneWidth &&
        plateImage.height == sceneHeight && depthImage.height == sceneHeight,
        fmt("slika %ux%u, dubina %ux%u u %u bita",
            plateImage.width, plateImage.height, depthImage.width, depthImage.height,
            depthImage.sourceBits));

    //Kalibracija mora vratiti tocno onu udaljenost koju smo upisali
    const DepthMapping mapping = DepthMapping::fromRange(nearDistance, farDistance);
    report.check("kalibracija vraca pet metara",
        std::abs(mapping.distanceAt(depthImage.values[0]) - wallDistance) < 1e-3f,
        fmt("disparitet %.6f -> %.5f m", depthImage.values[0],
            mapping.distanceAt(depthImage.values[0])));

    // -------------------------------------------------------------------------------
    // Loom
    // -------------------------------------------------------------------------------

    CameraConfig cameraConfig;
    cameraConfig.position = {0.0f, 0.0f, 0.0f};
    cameraConfig.target = {0.0f, 0.0f, -1.0f};
    cameraConfig.fovY = glm::radians(50.0f);
    cameraConfig.nearPlane = 0.05f;
    cameraConfig.farPlane = 200.0f;
    Camera camera(cameraConfig);

    LoomConfig config;
    config.width = sceneWidth; config.height = sceneHeight;
    config.appName = "plate relight"; config.engineName = "Loom tests";
    config.headless = true;
    config.enableDepth = false;
    config.pipelineConfig.colorFormat = sceneFormat;
    config.headlessColorFormat = sceneFormat;

    LoomInitializer loom(config);
    loom.renderer.setCamera(camera);

    //Snimka vec nosi svoje osvjetljenje; ambijent bi ga zbrojio jos jednom
    EnvironmentConfig environmentConfig;
    environmentConfig.ambientColor = {0.0f, 0.0f, 0.0f};
    Environment environment(environmentConfig);
    loom.renderer.setEnvironment(environment);

    LightConfig bulbConfig;
    bulbConfig.type = LightType::Point;
    bulbConfig.position = {1.4f, 0.8f, -3.2f};
    bulbConfig.color = {1.0f, 0.82f, 0.55f};
    bulbConfig.intensity = 30.0f;
    bulbConfig.range = 20.0f;
    Light bulb(bulbConfig);
    loom.renderer.addLight(bulb);

    TextureConfig plateTextureConfig;
    plateTextureConfig.format = vk::Format::eR8G8B8A8Unorm;   //Unorm: usporeduje se linearno svjetlo
    plateTextureConfig.filter = vk::Filter::eNearest;
    plateTextureConfig.generateMipmaps = false;
    Texture plateTexture(loom.device, loom.command, plateImage.pixels.data(),
                         vk::Extent2D{sceneWidth, sceneHeight}, plateTextureConfig);

    TextureConfig depthTextureConfig;
    depthTextureConfig.format = vk::Format::eR32Sfloat;
    depthTextureConfig.filter = vk::Filter::eNearest;
    depthTextureConfig.addressMode = vk::SamplerAddressMode::eClampToEdge;
    depthTextureConfig.generateMipmaps = false;
    Texture depthTexture(loom.device, loom.command, depthImage.values.data(),
                         vk::Extent2D{sceneWidth, sceneHeight}, depthTextureConfig);

    const CameraIntrinsics intrinsics =
        CameraIntrinsics::fromProjection(camera.getProjection(sceneWidth, sceneHeight), sceneWidth, sceneHeight);

    PositionMap positions(loom.device, vk::Extent2D{sceneWidth, sceneHeight});
    positions.setPlateDepth(loom.getDescriptorPool(), depthTexture.getSampled(),
                            vk::Extent2D{sceneWidth, sceneHeight}, mapping);
    positions.setIntrinsics(intrinsics);

    NormalMap normals(loom.device, vk::Extent2D{sceneWidth, sceneHeight});
    normals.setPositionSource(loom.getDescriptorPool(), positions);

    RelightConfig relightConfig;
    relightConfig.colorFormat = sceneFormat;
    relightConfig.surface.baseColor = {1.0f, 1.0f, 1.0f, 1.0f};
    relightConfig.surface.shininess = shininess;
    relightConfig.surface.specularStrength = specularStrength;

    Relight relight(loom.device, loom.command, loom.getDescriptorPool(),
                    positions, normals, plateTexture.getSampled(), relightConfig);
    relight.setCamera(camera);

    RenderTargetConfig targetConfig;
    targetConfig.colorFormat = sceneFormat;
    targetConfig.finalLayout = vk::ImageLayout::eTransferSrcOptimal;
    targetConfig.extraColorUsage = vk::ImageUsageFlagBits::eTransferSrc;
    targetConfig.enableDepth = false;
    RenderTarget out(loom.device, vk::Extent2D{sceneWidth, sceneHeight}, targetConfig);

    auto render = [&](){
        if(loom.renderer.beginFrame()){
            const UnprojectPush unproject = positions.makePush();
            loom.renderer.dispatch(positions.getComputeMaterial(),
                                   positions.groupsX(), positions.groupsY(), 1,
                                   &unproject, sizeof(unproject));

            const NormalPush normalPush = normals.makePush();
            loom.renderer.dispatch(normals.getComputeMaterial(),
                                   normals.groupsX(), normals.groupsY(), 1,
                                   &normalPush, sizeof(normalPush));

            loom.renderer.beginPass(out);
            loom.renderer.drawFullscreen(relight.getMaterial());
            loom.renderer.endPass();
            loom.renderer.endFrame();
        }
        loom.waitIdle();
        return out.readPixels(loom.command).pixels;
    };

    const std::vector<uint8_t> lit = render();

    // -------------------------------------------------------------------------------
    // Protiv aritmetike
    // -------------------------------------------------------------------------------

    const float albedo = float(grey) / 255.0f;
    const glm::vec3 plateColor{albedo, albedo, albedo};
    const glm::vec3 lightColor = bulb.getColor();

    float worst = 0.0f;
    double sum = 0.0;
    size_t compared = 0;

    //Rubni piksel ima susjeda samo s jedne strane, pa mu normala dolazi iz jednostrane
    //razlike. Na ravnoj plohi je i to tocno, ali se ne mjesa u brojku
    for(uint32_t y = 1; y + 1 < sceneHeight; ++y){
        for(uint32_t x = 1; x + 1 < sceneWidth; ++x){
            const size_t i = size_t(y) * sceneWidth + x;

            //Pozicija koju intrinsike daju za ovaj piksel na pet metara - poznata rukom
            const glm::vec3 worldPos{
                (float(x) + 0.5f - intrinsics.cx) * wallDistance / intrinsics.fx,
                (float(y) + 0.5f - intrinsics.cy) * wallDistance / intrinsics.fy,
                -wallDistance
            };

            const glm::vec3 expected = expectedLight(plateColor, worldPos, {0.0f, 0.0f, 1.0f},
                                                     camera.getPosition(), bulb.getPosition(),
                                                     lightColor, bulbConfig.range);

            for(int c = 0; c < 3; ++c){
                const float delta = std::abs(at(lit, i)[c] - expected[c]);
                worst = std::max(worst, delta);
                sum += delta;
                ++compared;
            }
        }
    }

    //Prag: jedan korak osmobitne slike. Sve ispod toga se ni u kojem isporucivom formatu ne
    //moze ni zapisati
    const float step = 1.0f / 255.0f;

    report.check("osvjetljenje je ono koje aritmetika kaze",
        worst < step,
        fmt("najveca razlika %.7f, prosjek %.9f (korak 8-bitne slike %.6f) na %zu usporedbi",
            worst, sum / double(compared), step, compared));

    // -------------------------------------------------------------------------------
    // Svjetlo je stvarno U prostoru
    // -------------------------------------------------------------------------------

    //Da je nalijepljeno na sliku, pomak ga ne bi premjestio. Trazi se da se najsvjetlije
    //mjesto POMAKNE na drugu stranu kad se svjetlo premjesti na drugu stranu
    auto brightestColumn = [](const std::vector<uint8_t>& pixels){
        size_t best = 0;
        float bestValue = -1.0f;
        const uint32_t row = sceneHeight / 2;
        for(uint32_t x = 0; x < sceneWidth; ++x){
            const glm::vec4& p = at(pixels, size_t(row) * sceneWidth + x);
            const float value = p.r + p.g + p.b;
            if(value > bestValue){ bestValue = value; best = x; }
        }
        return best;
    };

    const size_t rightSide = brightestColumn(lit);

    bulb.setPosition({-1.4f, 0.8f, -3.2f});
    const std::vector<uint8_t> mirrored = render();
    const size_t leftSide = brightestColumn(mirrored);

    report.check("svjetlo se pomice kroz scenu, ne po slici",
        rightSide > sceneWidth / 2 && leftSide < sceneWidth / 2,
        fmt("najsvjetlije je u stupcu %zu kad je svjetlo desno, %zu kad je lijevo (od %u)",
            rightSide, leftSide, sceneWidth));

    //I da je zrcaljenje stvarno zrcaljenje - ista scena, samo obrnuto
    report.check("i to simetricno",
        std::abs(int(rightSide) + int(leftSide) - int(sceneWidth)) < 4,
        fmt("%zu + %zu = %zu, sirina je %u", rightSide, leftSide, rightSide + leftSide, sceneWidth));

    // -------------------------------------------------------------------------------
    // Inverzni kvadrat
    // -------------------------------------------------------------------------------

    //Svjetlo dvaput dalje od plohe mora dati cetiri puta manje svjetla. To je jedina
    //provjera koja stvarno trazi da su udaljenosti u METRIMA, a ne u necemu proporcionalnom
    {
        auto centreBrightness = [&](float z){
            bulb.setPosition({0.0f, 0.0f, z});
            const std::vector<uint8_t> frame = render();
            const glm::vec4& p = at(frame, size_t(sceneHeight/2) * sceneWidth + sceneWidth/2);
            return (p.r + p.g + p.b) / 3.0f - albedo;   //samo doprinos svjetla
        };

        //Ploha je na -5; svjetlo na -4 je metar od nje, na -3 dva metra
        const float atOne = centreBrightness(-4.0f);
        const float atTwo = centreBrightness(-3.0f);

        //Prozorsko gusenje uz range 20 m je na metar i dva metra prakticki 1, pa je odnos
        //cist inverzni kvadrat. Deset posto rezerve za taj prozor i za zrcalni clan
        report.check("dvaput dalje je cetiri puta slabije",
            atOne > 0.0f && std::abs(atOne / atTwo - 4.0f) < 0.4f,
            fmt("na 1 m doprinos %.5f, na 2 m %.5f, odnos %.3f (inverzni kvadrat trazi 4)",
                atOne, atTwo, atOne / atTwo));
    }

    report.checkNoValidationMessages();
    std::filesystem::remove_all(work);
    return report.result();
}
