// Ambijentna okluzija iz same karte dubine.
//
// Ono sto ubaceno svjetlo cini SJEDECIM: bez nje predmet lebdi nad pozadinom, jer nista ne
// potamni tamo gdje se dodiruju.
//
// Scena je zid na sest metara i plocica na tri, i U NJOJ NEMA NIJEDNOG SVJETLA. To je namjerno:
// okluzija se racuna iz geometrije i ne treba joj svjetlo, pa je svaka promjena u slici njena i
// samo njena. Nema se sto pomijesati sa sjenom.
#include "TestHarness.h"
#include "Core/LoomConfig.h"
#include "Core/LoomInitializer.h"
#include "Core/Camera.h"
#include "Core/CameraIntrinsics.h"
#include "Vulkan/NormalMap.h"
#include "Vulkan/PositionMap.h"
#include "Vulkan/Relight.h"
#include "Vulkan/RenderTarget.h"
#include "Vulkan/Texture.h"

#include <cmath>
#include <glm/gtc/matrix_transform.hpp>
#include <cstring>
#include <vector>

namespace{

const uint32_t sceneWidth = 400;
const uint32_t sceneHeight = 300;
const vk::Format sceneFormat = vk::Format::eR32G32B32A32Sfloat;

const float nearDistance = 1.0f;
const float farDistance = 40.0f;
const float wallZ = -6.0f;
const float panelZ = -3.0f;
const float panelHalf = 0.5f;

const glm::vec4& at(const std::vector<uint8_t>& pixels, size_t index){
    return reinterpret_cast<const glm::vec4*>(pixels.data())[index];
}

}

int main(){
    TestReport report("okluzija iz dubine");

    CameraConfig cameraConfig;
    cameraConfig.position = {0.0f, 0.0f, 0.0f};
    cameraConfig.target = {0.0f, 0.0f, -1.0f};
    cameraConfig.fovY = glm::radians(50.0f);
    cameraConfig.nearPlane = 0.05f;
    cameraConfig.farPlane = 200.0f;
    Camera camera(cameraConfig);

    LoomConfig config;
    config.width = sceneWidth; config.height = sceneHeight;
    config.appName = "occlusion"; config.engineName = "Loom tests";
    config.headless = true;
    config.enableDepth = false;
    config.pipelineConfig.colorFormat = sceneFormat;
    config.headlessColorFormat = sceneFormat;

    LoomInitializer loom(config);
    loom.renderer.setCamera(camera);

    //Nijedno svjetlo, i ambijent nula: sve sto se promijeni promijenila je okluzija
    EnvironmentConfig environmentConfig;
    environmentConfig.ambientColor = {0.0f, 0.0f, 0.0f};
    Environment environment(environmentConfig);
    loom.renderer.setEnvironment(environment);

    const CameraIntrinsics intrinsics = CameraIntrinsics::fromProjection(
        camera.getProjection(sceneWidth, sceneHeight), sceneWidth, sceneHeight);

    std::vector<float> disparity(size_t(sceneWidth) * sceneHeight);
    std::vector<uint8_t> plate(size_t(sceneWidth) * sceneHeight * 4, 200);
    for(size_t i = 3; i < plate.size(); i += 4) plate[i] = 255;

    for(uint32_t y = 0; y < sceneHeight; ++y){
        for(uint32_t x = 0; x < sceneWidth; ++x){
            const float dx = (float(x) + 0.5f - intrinsics.cx) / intrinsics.fx;
            const float dy = (float(y) + 0.5f - intrinsics.cy) / intrinsics.fy;
            const float panelX = dx * (-panelZ), panelY = dy * (-panelZ);
            const bool onPanel = std::abs(panelX) <= panelHalf && std::abs(panelY) <= panelHalf;
            const float distance = onPanel ? -panelZ : -wallZ;
            disparity[size_t(y) * sceneWidth + x] =
                (1.0f / distance - 1.0f / farDistance) / (1.0f / nearDistance - 1.0f / farDistance);
        }
    }

    TextureConfig depthConfig;
    depthConfig.format = vk::Format::eR32Sfloat;
    depthConfig.filter = vk::Filter::eNearest;
    depthConfig.addressMode = vk::SamplerAddressMode::eClampToEdge;
    depthConfig.generateMipmaps = false;
    Texture depthTexture(loom.device, loom.command, disparity.data(),
                         vk::Extent2D{sceneWidth, sceneHeight}, depthConfig);

    TextureConfig plateConfig;
    plateConfig.format = vk::Format::eR8G8B8A8Unorm;
    plateConfig.filter = vk::Filter::eNearest;
    plateConfig.generateMipmaps = false;
    Texture plateTexture(loom.device, loom.command, plate.data(),
                         vk::Extent2D{sceneWidth, sceneHeight}, plateConfig);

    PositionMap positions(loom.device, vk::Extent2D{sceneWidth, sceneHeight});
    positions.setPlateDepth(loom.getDescriptorPool(), depthTexture.getSampled(),
                            vk::Extent2D{sceneWidth, sceneHeight},
                            DepthMapping::fromRange(nearDistance, farDistance));
    positions.setIntrinsics(intrinsics);

    NormalMap normals(loom.device, vk::Extent2D{sceneWidth, sceneHeight});
    normals.setPositionSource(loom.getDescriptorPool(), positions);

    RelightConfig relightConfig;
    relightConfig.colorFormat = sceneFormat;
    relightConfig.surface.baseColor = {1.0f, 1.0f, 1.0f, 1.0f};
    relightConfig.surface.specularStrength = 0.0f;

    Relight relight(loom.device, loom.command, loom.getDescriptorPool(),
                    positions, normals, plateTexture.getSampled(), relightConfig);
    relight.setCamera(camera);
    relight.setIntrinsics(intrinsics, vk::Extent2D{sceneWidth, sceneHeight});

    RenderTargetConfig targetConfig;
    targetConfig.colorFormat = sceneFormat;
    targetConfig.finalLayout = vk::ImageLayout::eTransferSrcOptimal;
    targetConfig.extraColorUsage = vk::ImageUsageFlagBits::eTransferSrc;
    targetConfig.enableDepth = false;
    RenderTarget out(loom.device, vk::Extent2D{sceneWidth, sceneHeight}, targetConfig);

    auto render = [&](const ScreenOcclusionConfig& occlusion){
        relight.setOcclusion(occlusion);
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

    ScreenOcclusionConfig off;
    const std::vector<uint8_t> plain = render(off);

    //Mjerilo mora odgovarati skoku koji scena ima, i to je prvo sto je ovaj test naucio.
    //Sa 0.5 se nije dogodilo NISTA: domet dodira je cetiri mjerila, dakle 2 m, a plocica je
    //od zida udaljena 3 m - clan `contact` ju je odbacio kao predaleku. To nije greska nego
    //bas ono zbog cega taj clan postoji: bez njega bi svaka daleka pozadina bila zaklon.
    //
    //Posljedica koju treba znati: ova okluzija vidi POSTUPNE promjene dubine, a tvrdu siluetu
    //s velikim skokom ne vidi. Na procijenjenoj dubini to je manje vidljivo, jer model rubove
    //ionako razmaze
    ScreenOcclusionConfig on;
    on.strength = 1.0f;
    on.scale = 1.5f;
    const std::vector<uint8_t> occluded = render(on);

    // -------------------------------------------------------------------------------
    // Gdje okluzija MORA biti: uz siluetu plocice, i nigdje drugdje
    // -------------------------------------------------------------------------------

    //Silueta plocice na zidu, u ekranskim jedinicama. Blizu nje je "uz rub", daleko je zid
    const float silhouette = panelHalf / (-panelZ);
    const float pixelsPerUnit = intrinsics.fx;

    double nearSum = 0.0, farSum = 0.0;
    size_t nearCount = 0, farCount = 0, changedOnPlate = 0;

    for(uint32_t y = 0; y < sceneHeight; ++y){
        for(uint32_t x = 0; x < sceneWidth; ++x){
            const size_t i = size_t(y) * sceneWidth + x;
            const float dx = (float(x) + 0.5f - intrinsics.cx) / intrinsics.fx;
            const float dy = (float(y) + 0.5f - intrinsics.cy) / intrinsics.fy;

            const float panelX = dx * (-panelZ), panelY = dy * (-panelZ);
            if(std::abs(panelX) <= panelHalf && std::abs(panelY) <= panelHalf) continue;

            const float change = at(occluded, i).r - at(plain, i).r;
            if(std::abs(change) > 1e-5f) ++changedOnPlate;

            //Koliko je piksela od siluete, po obje osi
            const float distanceX = (std::abs(dx) - silhouette) * pixelsPerUnit;
            const float distanceY = (std::abs(dy) - silhouette) * pixelsPerUnit;
            const float distance = std::max(distanceX, distanceY);

            if(distance > 1.0f && distance < 12.0f){ nearSum += change; ++nearCount; }
            else if(distance > 60.0f){ farSum += change; ++farCount; }
        }
    }

    const double nearAverage = nearSum / double(nearCount ? nearCount : 1);
    const double farAverage = farSum / double(farCount ? farCount : 1);

    report.check("okluzija uopce nesto radi",
        changedOnPlate > 1000,
        fmt("%zu piksela zida se promijenilo, bez ijednog svjetla u sceni", changedOnPlate));

    report.check("tamni UZ siluetu",
        nearCount > 300 && nearAverage < -0.002,
        fmt("%zu piksela do 12 px od ruba, prosjecno %+.4f", nearCount, nearAverage));

    //Bez ovoga bi gornja prosla i da je cijela slika samo potamnjena
    report.check("a ne posvuda",
        farCount > 300 && nearAverage < 5.0 * farAverage,
        fmt("daleko od ruba %+.4f, dakle %.1f puta manje", farAverage,
            farAverage < -1e-9 ? nearAverage / farAverage : 999.0));

    //I da nula stvarno znaci nula
    ScreenOcclusionConfig zero;
    zero.strength = 0.0f;
    const std::vector<uint8_t> again = render(zero);

    size_t identical = 0;
    for(size_t i = 0; i < plain.size(); i += 4 * sizeof(float)){
        if(std::memcmp(plain.data() + i, again.data() + i, 4 * sizeof(float)) == 0) ++identical;
    }

    report.check("jacina nula ne dira nista",
        identical == size_t(sceneWidth) * sceneHeight,
        fmt("%zu od %u piksela bit-identicno", identical, sceneWidth * sceneHeight));

    report.checkNoValidationMessages();
    return report.result();
}
