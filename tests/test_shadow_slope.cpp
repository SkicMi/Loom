// Odmak koji prati plohu.
//
// Jedan odmak za cijelu sliku ne moze biti dobar. Trag koji putuje tik uz plohu vidi kako mu
// ona bjezi pod uzorkom, inFront raste sam od sebe, i ploha zasjeni samu sebe. Odmak koji bi
// to zaustavio pojeo bi kontaktnu sjenu drugdje.
//
// UVJET ZA AKNE JE IZVEDEN, ne pogoden - prve tri verzije ove scene nisu ih uspjele izazvati.
// inFront je pozitivan samo ako trag ide DUBLJE od plohe na kojoj stoji, a to znaci da svjetlo
// mora biti dalje od kamere nego sama ploha (toLight.z < 0). Ako je uz to ploha jos i
// osvijetljena, dakle N.L > 0, dobiva se kosa ploha sa svjetlom IZA i sa strane - rim light.
//
// Iz istog izvoda slijedi i sto NE radi: svjetlo ispred plohe (toLight.z > 0) ne moze dati
// akne ni na kakvom nagibu, a konveksno tijelo poput kugle trag napusta prema van, ispred
// vlastite povrsine. Zato je scena kosa ravnina sa svjetlom iza nje, a ne ni jedno ni drugo.
//
// Rjesenje je da se odmak ne bira nego IZMJERI, jednom probom unatrag po smjeru svjetla.
// Izvod: naprijed na putu t uzorak je na udaljenosti D0 - t*toLight.z, a zapisana ploha na
// D0 + t*r; njihova razlika, koja i jest inFront, iznosi -t*(toLight.z + r). Ista velicina se
// unatrag na -b vidi kao b*(toLight.z + r), pa jedan uzorak daje stopu.
//
// Scena je kosa ravnina - dubina od 2.6 m lijevo do 9 m desno - i plocica pred njom. Svjetlo
// je usmjereno i putuje UZ kosinu, sto je najgori slucaj za akne.
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

const uint32_t sceneWidth = 400;
const uint32_t sceneHeight = 300;
const vk::Format sceneFormat = vk::Format::eR32G32B32A32Sfloat;

const float nearDistance = 1.0f;
const float farDistance = 40.0f;

//Kosa ravnina kroz (0,0,-4), nagnuta oko okomite osi. Normala gleda prema kameri i udesno
const float slantDegrees = 62.0f;
const glm::vec3 planePoint{0.0f, 0.0f, -4.0f};

//Svjetlo putuje PREMA kameri i ulijevo, dakle stoji iza plohe i desno od nje. Time je
//toLight.z negativan (trag ide dublje, pa inFront raste) a N.L pozitivan (ploha je
//osvijetljena) - jedina kombinacija u kojoj se akne uopce pojave
const glm::vec3 lightTravel{-0.95f, -0.10f, 0.28f};

const float panelZ = -2.2f;
const float panelHalf = 0.35f;

const glm::vec4& at(const std::vector<uint8_t>& pixels, size_t index){
    return reinterpret_cast<const glm::vec4*>(pixels.data())[index];
}

}

int main(){
    TestReport report("odmak koji prati plohu");

    CameraConfig cameraConfig;
    cameraConfig.position = {0.0f, 0.0f, 0.0f};
    cameraConfig.target = {0.0f, 0.0f, -1.0f};
    cameraConfig.fovY = glm::radians(50.0f);
    cameraConfig.nearPlane = 0.05f;
    cameraConfig.farPlane = 200.0f;
    Camera camera(cameraConfig);

    LoomConfig config;
    config.width = sceneWidth; config.height = sceneHeight;
    config.appName = "shadow slope"; config.engineName = "Loom tests";
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

    // -------------------------------------------------------------------------------
    // Kosa ravnina, i plocica pred njom
    // -------------------------------------------------------------------------------

    std::vector<float> disparity(size_t(sceneWidth) * sceneHeight);
    std::vector<uint8_t> plate(size_t(sceneWidth) * sceneHeight * 4, 128);
    for(size_t i = 3; i < plate.size(); i += 4) plate[i] = 255;

    auto toDisparity = [&](float distance){
        return (1.0f / distance - 1.0f / farDistance) / (1.0f / nearDistance - 1.0f / farDistance);
    };

    //Kosa ravnina, analiticki
    const float slant = glm::radians(slantDegrees);
    const glm::vec3 planeNormal{std::sin(slant), 0.0f, std::cos(slant)};
    const float planeOffset = glm::dot(planeNormal, planePoint);

    float nearestPlane = 1e9f, farthestPlane = 0.0f;
    for(uint32_t y = 0; y < sceneHeight; ++y){
        for(uint32_t x = 0; x < sceneWidth; ++x){
            const float dx = (float(x) + 0.5f - intrinsics.cx) / intrinsics.fx;
            const float dy = (float(y) + 0.5f - intrinsics.cy) / intrinsics.fy;
            const glm::vec3 ray{dx, dy, -1.0f};

            const float denominator = glm::dot(planeNormal, ray);
            float distance = 30.0f;
            if(std::abs(denominator) > 1e-6f){
                const float t = planeOffset / denominator;
                if(t > 0.2f) distance = std::min(t, 30.0f);
            }

            const float panelX = dx * (-panelZ), panelY = dy * (-panelZ);
            const bool onPanel = std::abs(panelX) <= panelHalf && std::abs(panelY) <= panelHalf;
            if(!onPanel && distance < 29.9f){
                nearestPlane = std::min(nearestPlane, distance);
                farthestPlane = std::max(farthestPlane, distance);
            }

            disparity[size_t(y) * sceneWidth + x] = toDisparity(onPanel ? -panelZ : distance);
        }
    }

    report.check("kosina je stvarno kosa",
        farthestPlane > 2.0f * nearestPlane,
        fmt("ravnina ide od %.2f m do %.2f m preko kadra", nearestPlane, farthestPlane));

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

    ScreenShadowConfig plain;
    plain.enabled = true;
    plain.steps = 48;
    plain.maxDistance = 6.0f;
    plain.thickness = 1.0f;
    plain.bias = 0.03f;

    relight.setShadow(plain);
    const std::vector<uint8_t> withoutFollow = render();

    ScreenShadowConfig growing = plain;
    growing.slopeBias = 3.0f;

    relight.setShadow(growing);
    const std::vector<uint8_t> withFollow = render();

    ScreenShadowConfig none = plain;
    none.enabled = false;
    relight.setShadow(none);
    const std::vector<uint8_t> unshadowed = render();

    // -------------------------------------------------------------------------------
    // Gdje akne MORAJU biti, i gdje sjena mora ostati
    // -------------------------------------------------------------------------------

    //Plocicina sjena pada u smjeru svjetla, dakle UDESNO. Akne se broje na strani na kojoj
    //njena sjena ne moze biti - lijevo od nje - pa se to dvoje ne moze pomijesati
    const float albedo = 128.0f / 255.0f;

    size_t acneWithout = 0, acneWith = 0, litArea = 0;
    size_t shadowWithout = 0, shadowWith = 0;

    for(uint32_t y = 0; y < sceneHeight; ++y){
        for(uint32_t x = 0; x < sceneWidth; ++x){
            const size_t i = size_t(y) * sceneWidth + x;
            const float dx = (float(x) + 0.5f - intrinsics.cx) / intrinsics.fx;
            const float dy = (float(y) + 0.5f - intrinsics.cy) / intrinsics.fy;

            const float panelX = dx * (-panelZ), panelY = dy * (-panelZ);
            if(std::abs(panelX) <= panelHalf && std::abs(panelY) <= panelHalf) continue;

            const bool lit = at(unshadowed, i).r - albedo > 1e-3f;
            if(!lit) continue;

            //Lijevo od plocice: svjetlo putuje udesno, pa tamo njena sjena ne moze pasti -
            //svako zatamnjenje je akna
            if(dx < -panelHalf / (-panelZ) - 0.05f){
                ++litArea;
                if(at(withoutFollow, i).r - albedo < 1e-5f) ++acneWithout;
                if(at(withFollow, i).r - albedo < 1e-5f) ++acneWith;
            }

            //Desno, blizu plocice: tamo sjena JEST, i mora prezivjeti popravak
            if(dx > panelHalf / (-panelZ) + 0.05f && dx < 3.0f * panelHalf / (-panelZ)){
                if(at(withoutFollow, i).r - albedo < 1e-5f) ++shadowWithout;
                if(at(withFollow, i).r - albedo < 1e-5f) ++shadowWith;
            }
        }
    }

    report.check("s odmakom koji ne raste ploha zasjeni samu sebe",
        litArea > 1000 && acneWithout * 20 > litArea,
        fmt("%zu od %zu osvijetljenih piksela na strani bez sjene je tamno", acneWithout, litArea));

    report.check("odmak koji raste s putem to mice",
        acneWith * 20 < acneWithout,
        fmt("%zu naspram %zu piksela - %.1f%% od prijasnjeg",
            acneWith, acneWithout, 100.0 * double(acneWith) / double(acneWithout ? acneWithout : 1)));

    //I CIJENA, izmjerena umjesto precutana.
    //
    //Ovo nije provjera da je sve u redu nego da odmak NIJE besplatan: on ne razlikuje lazni
    //zaklon od pravog, oba rastu s putem. U ovoj sceni prava sjena umire vec na prvom koraku:
    //
    //   slopeBias  0.0   0.3   0.6   1.0   1.5   2.0   3.0
    //   akne     16556  8149  5500  3396  2059  1208   316
    //   sjena      300     0     0     0     0     0     0
    //
    //Koljena nema, jer plocicina sjena i akne ovdje leze na slicnoj udaljenosti po tragu.
    //Zato odmak koji raste jest alat za scene sa svjetlom iza plohe, ali se ne smije ukljuciti
    //"za svaki slucaj" - u LoomAppu stoji na nuli, jer tamo svjetlo kruzi ispred subjekta i
    //akni nema
    report.check("i to nije besplatno - prava sjena je platila",
        shadowWithout > 100 && shadowWith < shadowWithout / 2,
        fmt("plocicina sjena pala je s %zu na %zu piksela", shadowWithout, shadowWith));

    report.checkNoValidationMessages();
    return report.result();
}
