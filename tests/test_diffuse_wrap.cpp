// Omotani difuz.
//
// Lambert pada u nulu tocno na dot(N,L) = 0. Taj rub je ostar, i svaka greska u normali se na
// njemu vidi kao mrlja - a normale iz PROCIJENJENE dubine su pune gresaka. Omatanje pomice tu
// nulu unatrag i preslikava raspon natrag u 0..1, pa svjetlo malo zade iza ruba.
//
// Scena je kugla iz karte dubine, osvijetljena gotovo bocno: terminator tada lezi preko nje i
// ima se sto mjeriti. Mjeri se pojas oko njega, a ne cijela kugla - jer omatanje mora smeksati
// RUB, a ne samo sve posvijetliti.
#include "TestHarness.h"
#include "Core/LoomConfig.h"
#include "Core/LoomInitializer.h"
#include "Core/Camera.h"
#include "Core/CameraIntrinsics.h"
#include "Core/Light.h"
#include "Vulkan/NormalMap.h"
#include "Vulkan/PositionMap.h"
#include "Vulkan/Relight.h"
#include "Vulkan/RenderTarget.h"
#include "Vulkan/Texture.h"

#include <cmath>
#include <glm/gtc/matrix_transform.hpp>
#include <vector>

namespace{

const uint32_t sceneWidth = 320;
const uint32_t sceneHeight = 320;
const vk::Format sceneFormat = vk::Format::eR32G32B32A32Sfloat;

const float nearDistance = 1.0f;
const float farDistance = 40.0f;

const glm::vec3 sphereCentre{0.0f, 0.0f, -5.0f};
const float sphereRadius = 2.0f;
const float backdrop = 20.0f;

//Gotovo bocno: terminator tada padne preko sredine kugle
const glm::vec3 lightTravel{-0.98f, 0.0f, -0.20f};

const glm::vec4& at(const std::vector<uint8_t>& pixels, size_t index){
    return reinterpret_cast<const glm::vec4*>(pixels.data())[index];
}

}

int main(){
    TestReport report("omotani difuz");

    CameraConfig cameraConfig;
    cameraConfig.position = {0.0f, 0.0f, 0.0f};
    cameraConfig.target = {0.0f, 0.0f, -1.0f};
    cameraConfig.fovY = glm::radians(50.0f);
    cameraConfig.nearPlane = 0.05f;
    cameraConfig.farPlane = 200.0f;
    Camera camera(cameraConfig);

    LoomConfig config;
    config.width = sceneWidth; config.height = sceneHeight;
    config.appName = "diffuse wrap"; config.engineName = "Loom tests";
    config.headless = true;
    config.enableDepth = false;
    config.pipelineConfig.colorFormat = sceneFormat;
    config.headlessColorFormat = sceneFormat;

    LoomInitializer loom(config);
    loom.renderer.setCamera(camera);

    EnvironmentConfig environmentConfig;
    environmentConfig.ambientColor = {0.0f, 0.0f, 0.0f};
    Environment environment(environmentConfig);
    loom.renderer.setEnvironment(environment);

    LightConfig sunConfig;
    sunConfig.type = LightType::Directional;
    sunConfig.direction = lightTravel;
    sunConfig.color = {1.0f, 1.0f, 1.0f};
    sunConfig.intensity = 1.0f;
    Light sun(sunConfig);
    loom.renderer.addLight(sun);

    const CameraIntrinsics intrinsics = CameraIntrinsics::fromProjection(
        camera.getProjection(sceneWidth, sceneHeight), sceneWidth, sceneHeight);

    std::vector<float> disparity(size_t(sceneWidth) * sceneHeight);
    std::vector<uint8_t> plate(size_t(sceneWidth) * sceneHeight * 4, 200);
    for(size_t i = 3; i < plate.size(); i += 4) plate[i] = 255;

    //Gdje je na kugli, i kolika je tamo Lambertova vrijednost - da se pojas oko terminatora
    //moze odabrati po GEOMETRIJI, a ne po tome sto se u slici vidi
    std::vector<float> lambert(size_t(sceneWidth) * sceneHeight, -2.0f);

    const glm::vec3 toLight = glm::normalize(-lightTravel);

    for(uint32_t y = 0; y < sceneHeight; ++y){
        for(uint32_t x = 0; x < sceneWidth; ++x){
            const float dx = (float(x) + 0.5f - intrinsics.cx) / intrinsics.fx;
            const float dy = (float(y) + 0.5f - intrinsics.cy) / intrinsics.fy;
            const glm::vec3 ray = glm::normalize(glm::vec3{dx, dy, -1.0f});

            const float b = 2.0f * glm::dot(ray, -sphereCentre);
            const float c = glm::dot(sphereCentre, sphereCentre) - sphereRadius * sphereRadius;
            const float discriminant = b * b - 4.0f * c;

            float distance = backdrop;
            if(discriminant >= 0.0f){
                const float t = (-b - std::sqrt(discriminant)) * 0.5f;
                if(t > 0.2f){
                    const glm::vec3 hit = ray * t;
                    distance = -hit.z;

                    //Normala kugle je analiticka; view prostor ima Y prema gore, a redak
                    //raste prema dolje - ista pretvorba kao u odprojekciji
                    const glm::vec3 surface = glm::normalize(hit - sphereCentre);
                    lambert[size_t(y) * sceneWidth + x] = glm::dot(surface, toLight);
                }
            }

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
    relightConfig.surface.shininess = 32.0f;
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

    auto render = [&](float wrap){
        MaterialData surface = relightConfig.surface;
        surface.diffuseWrap = wrap;
        relight.setSurface(surface);

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

    const std::vector<uint8_t> lambertOnly = render(0.0f);
    const std::vector<uint8_t> wrapped = render(0.25f);

    // -------------------------------------------------------------------------------
    // Tri pojasa, odabrana po GEOMETRIJI
    // -------------------------------------------------------------------------------

    const float albedo = 200.0f / 255.0f;

    size_t darkTotal = 0, darkLifted = 0;      //iza terminatora: Lambert je tu tocno nula
    size_t brightTotal = 0, brightChanged = 0; //puno svjetlo: mjera protiv koje se rub usporeduje
    double darkGain = 0.0, brightGain = 0.0;

    for(size_t i = 0; i < lambert.size(); ++i){
        const float value = lambert[i];
        if(value < -1.5f) continue;             //nije na kugli

        const float before = at(lambertOnly, i).r - albedo;
        const float after = at(wrapped, i).r - albedo;

        if(value < -0.05f && value > -0.20f){
            ++darkTotal;
            if(after > before + 1e-4f) ++darkLifted;
            darkGain += double(after - before);
        }
        else if(value > 0.8f){
            ++brightTotal;
            if(std::abs(after - before) > 1e-4f) ++brightChanged;
            brightGain += double(after - before);
        }
    }

    report.check("iza terminatora Lambert ne daje nista",
        darkTotal > 300,
        fmt("%zu piksela ima dot(N,L) izmedu -0.20 i -0.05", darkTotal));

    report.check("omatanje ih podigne",
        darkTotal > 0 && darkLifted * 100 > darkTotal * 99,
        fmt("%zu od %zu piksela iza ruba je posvijetlilo, prosjecno za %.4f",
            darkLifted, darkTotal, darkGain / double(darkTotal ? darkTotal : 1)));

    //PARNA PROVJERA, i njena prva verzija je bila kriva pa se to ovdje pamti: tvrdio sam da
    //puno svjetlo mora POTAMNITI. Ne mora - (d + w) / (1 + w) je veci od d za svaki d manji
    //od 1, pa omatanje posvjetljuje posvuda i jednako je tocno na cijeloj kugli.
    //
    //Ono sto se stvarno da tvrditi je da RUB dobiva bitno vise od punog svjetla - jer to i
    //jest razlika izmedu "smeksao se terminator" i "slika je pojacana"
    const double darkAverage = darkGain / double(darkTotal ? darkTotal : 1);
    const double brightAverage = brightGain / double(brightTotal ? brightTotal : 1);

    report.check("rub dobiva bitno vise od punog svjetla",
        brightTotal > 300 && darkAverage > 3.0 * brightAverage && brightAverage > 0.0,
        fmt("iza ruba +%.4f, u punom svjetlu +%.4f - %.1f puta",
            darkAverage, brightAverage, darkAverage / (brightAverage > 0.0 ? brightAverage : 1.0)));

    report.checkNoValidationMessages();
    return report.result();
}
