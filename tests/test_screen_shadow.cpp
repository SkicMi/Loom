// 3a: sjena trazena kroz samu sliku.
//
// Karte sjena ovdje nema i ne moze je biti. Dubina daje LJUSKU, ne geometriju - postoji samo
// prednja ploha onoga sto se vidi, i nista iza nje. Zato se zaklon trazi hodanjem: od tocke
// prema svjetlu, u koracima, i na svakom se koraku pogleda sto u slici stoji na tom mjestu.
//
// Granica je posteno reci i ovdje se i mjeri: sjenu baca samo ono sto je U KADRU.
//
// Scena je postavljena tako da se sjena da izracunati RUKOM. Zid na sest metara, ravna plocica
// na tri, i USMJERENO svjetlo - tada je sjena samo pomak plocice, i njena cetiri ugla su
// zbrajanje. Nista se ne usporeduje s drugom slikom nego s geometrijom.
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

#include <cmath>
#include <glm/gtc/matrix_transform.hpp>
#include <vector>

namespace{

const uint32_t sceneWidth = 400;
const uint32_t sceneHeight = 300;
const vk::Format sceneFormat = vk::Format::eR32G32B32A32Sfloat;

const float wallZ = -6.0f;
const float panelZ = -3.0f;
const float panelHalf = 0.5f;

//Smjer u kojem svjetlo PUTUJE. Odabran tako da sjena padne pokraj plocice, na zid koji se
//vidi - iza same plocice zid ionako nije vidljiv, pa se tamo nema sto mjeriti
const glm::vec3 lightTravel{-0.6f, -0.2f, -1.0f};

const float nearDistance = 1.0f;
const float farDistance = 40.0f;

const glm::vec4& at(const std::vector<uint8_t>& pixels, size_t index){
    return reinterpret_cast<const glm::vec4*>(pixels.data())[index];
}

}

int main(){
    TestReport report("3a sjena iz kadra");

    CameraConfig cameraConfig;
    cameraConfig.position = {0.0f, 0.0f, 0.0f};
    cameraConfig.target = {0.0f, 0.0f, -1.0f};
    cameraConfig.fovY = glm::radians(50.0f);
    cameraConfig.nearPlane = 0.05f;
    cameraConfig.farPlane = 200.0f;
    Camera camera(cameraConfig);

    LoomConfig config;
    config.width = sceneWidth; config.height = sceneHeight;
    config.appName = "screen shadow"; config.engineName = "Loom tests";
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

    const CameraIntrinsics intrinsics =
        CameraIntrinsics::fromProjection(camera.getProjection(sceneWidth, sceneHeight), sceneWidth, sceneHeight);

    // -------------------------------------------------------------------------------
    // Karta dubine: zid, i plocica pred njim
    // -------------------------------------------------------------------------------

    std::vector<float> disparity(size_t(sceneWidth) * sceneHeight);
    std::vector<uint8_t> plate(size_t(sceneWidth) * sceneHeight * 4, 128);
    for(size_t i = 3; i < plate.size(); i += 4) plate[i] = 255;

    auto toDisparity = [&](float distance){
        return (1.0f / distance - 1.0f / farDistance) / (1.0f / nearDistance - 1.0f / farDistance);
    };

    for(uint32_t y = 0; y < sceneHeight; ++y){
        for(uint32_t x = 0; x < sceneWidth; ++x){
            //Zraka kroz ovaj piksel, u view prostoru
            const float dx = (float(x) + 0.5f - intrinsics.cx) / intrinsics.fx;
            const float dy = (float(y) + 0.5f - intrinsics.cy) / intrinsics.fy;

            //Gdje ta zraka probija ravninu plocice
            const float panelX = dx * panelHalf * 0.0f + dx * (-panelZ);
            const float panelY = dy * (-panelZ);

            const bool onPanel = std::abs(panelX) <= panelHalf && std::abs(panelY) <= panelHalf;
            const float distance = onPanel ? -panelZ : -wallZ;

            disparity[size_t(y) * sceneWidth + x] = toDisparity(distance);
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
    relightConfig.surface.specularStrength = 0.0f;   //samo difuzno: sjena se mjeri, ne odsjaj

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

    const std::vector<uint8_t> unshadowed = render();

    ScreenShadowConfig shadowConfig;
    shadowConfig.enabled = true;
    shadowConfig.steps = 48;
    shadowConfig.maxDistance = 8.0f;
    shadowConfig.thickness = 1.5f;
    shadowConfig.bias = 0.03f;
    relight.setShadow(shadowConfig);

    const std::vector<uint8_t> shadowed = render();

    report.check("prolaz zna da baca sjenu", relight.castsShadows(),
        fmt("%u koraka, do %.1f m", shadowConfig.steps, shadowConfig.maxDistance));

    // -------------------------------------------------------------------------------
    // Gdje sjena MORA biti - izracunato, ne pogodeno
    // -------------------------------------------------------------------------------

    //Usmjereno svjetlo: tocka na plocici baca sjenu tamo gdje njena zraka probije zid. Kako
    //su obje ravnine okomite na pogled, sjena je samo POMAK plocice
    const float travel = (wallZ - panelZ) / lightTravel.z;   //koliko se ide da se stigne do zida
    const glm::vec2 offset{lightTravel.x * travel, lightTravel.y * travel};

    //Cetiri ugla sjene na zidu
    const float shadowMinX = -panelHalf + offset.x;
    const float shadowMaxX =  panelHalf + offset.x;
    const float shadowMinY = -panelHalf + offset.y;
    const float shadowMaxY =  panelHalf + offset.y;

    report.check("sjena je izracunata",
        shadowMaxX < -panelHalf * (wallZ / panelZ),
        fmt("na zidu od x %.3f do %.3f, y %.3f do %.3f; plocica sakriva zid do x %.3f",
            shadowMinX, shadowMaxX, shadowMinY, shadowMaxY, -panelHalf * (wallZ / panelZ)));

    //Rub ima jedan korak neodredenosti, pa se mjeri unutrasnjost i vanjstina, a ne rub
    const float margin = 0.25f;

    size_t insideTotal = 0, insideDark = 0;
    size_t outsideTotal = 0, outsideLit = 0;
    const float albedo = 128.0f / 255.0f;

    for(uint32_t y = 0; y < sceneHeight; ++y){
        for(uint32_t x = 0; x < sceneWidth; ++x){
            const size_t i = size_t(y) * sceneWidth + x;

            //Gdje ovaj piksel pada na zid, po istoj zraci kojom je i karta napravljena
            const float dx = (float(x) + 0.5f - intrinsics.cx) / intrinsics.fx;
            const float dy = (float(y) + 0.5f - intrinsics.cy) / intrinsics.fy;

            const float panelX = dx * (-panelZ), panelY = dy * (-panelZ);
            if(std::abs(panelX) <= panelHalf && std::abs(panelY) <= panelHalf) continue;   //sama plocica

            const float wallX = dx * (-wallZ), wallY = dy * (-wallZ);

            //Koliko se ovaj piksel na zidu razlikuje od gole snimke: nula znaci da svjetlo
            //nije doprlo
            const float added = at(shadowed, i).r - albedo;

            const bool inside =
                wallX > shadowMinX + margin && wallX < shadowMaxX - margin &&
                wallY > shadowMinY + margin && wallY < shadowMaxY - margin;

            const bool outside =
                wallX < shadowMinX - margin || wallX > shadowMaxX + margin ||
                wallY < shadowMinY - margin || wallY > shadowMaxY + margin;

            if(inside){
                ++insideTotal;
                if(added < 1e-5f) ++insideDark;
            }
            else if(outside){
                ++outsideTotal;
                if(added > 1e-3f) ++outsideLit;
            }
        }
    }

    report.check("unutar izracunate sjene svjetlo ne dopire",
        insideTotal > 500 && insideDark == insideTotal,
        fmt("%zu od %zu piksela unutar sjene je bez ijednog dodanog svjetla", insideDark, insideTotal));

    report.check("izvan nje dopire",
        outsideTotal > 500 && outsideLit * 100 > outsideTotal * 99,
        fmt("%zu od %zu piksela izvan sjene je osvijetljeno", outsideLit, outsideTotal));

    // -------------------------------------------------------------------------------
    // I da sjene bez traga NEMA
    // -------------------------------------------------------------------------------

    //Bez ove provjere bi gornja prosla i nad slikom koju svjetlo uopce nije obasjalo
    {
        size_t litWithout = 0;
        for(uint32_t y = 0; y < sceneHeight; ++y){
            for(uint32_t x = 0; x < sceneWidth; ++x){
                const size_t i = size_t(y) * sceneWidth + x;
                const float dx = (float(x) + 0.5f - intrinsics.cx) / intrinsics.fx;
                const float dy = (float(y) + 0.5f - intrinsics.cy) / intrinsics.fy;
                const float panelX = dx * (-panelZ), panelY = dy * (-panelZ);
                if(std::abs(panelX) <= panelHalf && std::abs(panelY) <= panelHalf) continue;

                const float wallX = dx * (-wallZ), wallY = dy * (-wallZ);
                const bool inside =
                    wallX > shadowMinX + margin && wallX < shadowMaxX - margin &&
                    wallY > shadowMinY + margin && wallY < shadowMaxY - margin;

                if(inside && at(unshadowed, i).r - albedo > 1e-3f) ++litWithout;
            }
        }

        report.check("bez traga to isto mjesto JEST osvijetljeno",
            litWithout == insideTotal,
            fmt("%zu od %zu piksela buduce sjene svijetli kad se zaklon ne trazi",
                litWithout, insideTotal));
    }

    // -------------------------------------------------------------------------------
    // Debljina: ploha se ne pretvara u beskonacan zid
    // -------------------------------------------------------------------------------

    //Bez gornje granice bi plocica zaklanjala sve iza sebe do kraja scene. S vrlo malom
    //debljinom prolazi se kroz nju kao da je nema - i to se mora vidjeti
    {
        ScreenShadowConfig thin = shadowConfig;
        thin.thickness = 0.02f;
        relight.setShadow(thin);

        const std::vector<uint8_t> throughIt = render();

        size_t stillDark = 0;
        for(uint32_t y = 0; y < sceneHeight; ++y){
            for(uint32_t x = 0; x < sceneWidth; ++x){
                const size_t i = size_t(y) * sceneWidth + x;
                const float dx = (float(x) + 0.5f - intrinsics.cx) / intrinsics.fx;
                const float dy = (float(y) + 0.5f - intrinsics.cy) / intrinsics.fy;
                const float panelX = dx * (-panelZ), panelY = dy * (-panelZ);
                if(std::abs(panelX) <= panelHalf && std::abs(panelY) <= panelHalf) continue;

                const float wallX = dx * (-wallZ), wallY = dy * (-wallZ);
                const bool inside =
                    wallX > shadowMinX + margin && wallX < shadowMaxX - margin &&
                    wallY > shadowMinY + margin && wallY < shadowMaxY - margin;

                if(inside && at(throughIt, i).r - albedo < 1e-5f) ++stillDark;
            }
        }

        report.check("debljina odlucuje koliko ploha zaklanja",
            stillDark * 10 < insideTotal,
            fmt("s debljinom 2 cm sjena pokriva %zu od %zu piksela, s debljinom 1.5 m %zu",
                stillDark, insideTotal, insideDark));

        relight.setShadow(shadowConfig);
    }

    // -------------------------------------------------------------------------------
    // Granica postupka, izmjerena a ne precutana
    // -------------------------------------------------------------------------------

    //Ovo je jedina provjera koja mjeri sto postupak NE moze, i zato je najvaznija.
    //
    //Zaklon koji nije u kadru ne postoji ni u jednoj slici. Ista scena, ali s plocicom
    //POKRAJ kadra: geometrijski bi njena sjena pala na zid koji se vidi, a trag je ne moze
    //naci jer nema gdje pogledati. To nije greska nego granica, i zato ovdje stoji izmjerena
    //umjesto da je samo napisana u komentaru.
    {
        const float offscreenX = 5.0f;   //daleko izvan kadra: na tri metra kadar seze do 1.87

        std::vector<float> awayDisparity(size_t(sceneWidth) * sceneHeight);
        for(uint32_t y = 0; y < sceneHeight; ++y){
            for(uint32_t x = 0; x < sceneWidth; ++x){
                const float dx = (float(x) + 0.5f - intrinsics.cx) / intrinsics.fx;
                const float dy = (float(y) + 0.5f - intrinsics.cy) / intrinsics.fy;

                const float panelX = dx * (-panelZ), panelY = dy * (-panelZ);
                const bool onPanel = std::abs(panelX - offscreenX) <= panelHalf &&
                                     std::abs(panelY) <= panelHalf;

                awayDisparity[size_t(y) * sceneWidth + x] = toDisparity(onPanel ? -panelZ : -wallZ);
            }
        }

        Texture awayDepth(loom.device, loom.command, awayDisparity.data(),
                          vk::Extent2D{sceneWidth, sceneHeight}, depthConfig);

        PositionMap awayPositions(loom.device, vk::Extent2D{sceneWidth, sceneHeight});
        awayPositions.setPlateDepth(loom.getDescriptorPool(), awayDepth.getSampled(),
                                    vk::Extent2D{sceneWidth, sceneHeight},
                                    DepthMapping::fromRange(nearDistance, farDistance));
        awayPositions.setIntrinsics(intrinsics);

        NormalMap awayNormals(loom.device, vk::Extent2D{sceneWidth, sceneHeight});
        awayNormals.setPositionSource(loom.getDescriptorPool(), awayPositions);

        Relight awayRelight(loom.device, loom.command, loom.getDescriptorPool(),
                            awayPositions, awayNormals, plateTexture.getSampled(), relightConfig);
        awayRelight.setCamera(camera);
        awayRelight.setIntrinsics(intrinsics, vk::Extent2D{sceneWidth, sceneHeight});
        awayRelight.setShadow(shadowConfig);

        std::vector<uint8_t> away;
        if(loom.renderer.beginFrame()){
            const UnprojectPush unproject = awayPositions.makePush();
            loom.renderer.dispatch(awayPositions.getComputeMaterial(),
                                   awayPositions.groupsX(), awayPositions.groupsY(), 1,
                                   &unproject, sizeof(unproject));
            const NormalPush normalPush = awayNormals.makePush();
            loom.renderer.dispatch(awayNormals.getComputeMaterial(),
                                   awayNormals.groupsX(), awayNormals.groupsY(), 1,
                                   &normalPush, sizeof(normalPush));

            loom.renderer.beginPass(out);
            loom.renderer.drawFullscreen(awayRelight.getMaterial());
            loom.renderer.endPass();
            loom.renderer.endFrame();
        }
        loom.waitIdle();
        away = out.readPixels(loom.command).pixels;

        //Gdje bi ta sjena pala da je postupak vidi
        const float awayMinX = offscreenX - panelHalf + offset.x;
        const float awayMaxX = offscreenX + panelHalf + offset.x;

        size_t wouldBe = 0, actuallyDark = 0;
        for(uint32_t y = 0; y < sceneHeight; ++y){
            for(uint32_t x = 0; x < sceneWidth; ++x){
                const size_t i = size_t(y) * sceneWidth + x;
                const float dx = (float(x) + 0.5f - intrinsics.cx) / intrinsics.fx;
                const float dy = (float(y) + 0.5f - intrinsics.cy) / intrinsics.fy;

                const float wallX = dx * (-wallZ), wallY = dy * (-wallZ);
                if(wallX < awayMinX + margin || wallX > awayMaxX - margin) continue;
                if(wallY < shadowMinY + margin || wallY > shadowMaxY - margin) continue;

                ++wouldBe;
                if(at(away, i).r - albedo < 1e-5f) ++actuallyDark;
            }
        }

        report.check("zaklon izvan kadra ne baca sjenu",
            wouldBe > 100 && actuallyDark == 0,
            fmt("plocica na x=%.1f je izvan kadra; njena bi sjena pala na %zu vidljivih "
                "piksela zida, a pala je na %zu", offscreenX, wouldBe, actuallyDark));
    }

    //I zapisano jer me je test na tome ispravio: doseg NIJE geometrijska udaljenost do
    //zaklona. Trag nalazi zaklon po njegovu EKRANSKOM tragu, pa i kratak trag moze sletjeti
    //na plocicu koja je metrima daleko - ono sto tada odlucuje je debljina, ne doseg
    {
        ScreenShadowConfig shortReach = shadowConfig;
        shortReach.maxDistance = 1.0f;
        relight.setShadow(shortReach);

        const std::vector<uint8_t> shorter = render();

        size_t stillDark = 0;
        for(uint32_t y = 0; y < sceneHeight; ++y){
            for(uint32_t x = 0; x < sceneWidth; ++x){
                const size_t i = size_t(y) * sceneWidth + x;
                const float dx = (float(x) + 0.5f - intrinsics.cx) / intrinsics.fx;
                const float dy = (float(y) + 0.5f - intrinsics.cy) / intrinsics.fy;
                const float panelX = dx * (-panelZ), panelY = dy * (-panelZ);
                if(std::abs(panelX) <= panelHalf && std::abs(panelY) <= panelHalf) continue;

                const float wallX = dx * (-wallZ), wallY = dy * (-wallZ);
                const bool inside =
                    wallX > shadowMinX + margin && wallX < shadowMaxX - margin &&
                    wallY > shadowMinY + margin && wallY < shadowMaxY - margin;

                if(inside && at(shorter, i).r - albedo < 1e-5f) ++stillDark;
            }
        }

        report.check("kraci doseg smanjuje sjenu",
            stillDark < insideDark,
            fmt("s dosegom 1 m sjena pokriva %zu od %zu piksela, s dosegom %.0f m njih %zu",
                stillDark, insideTotal, shadowConfig.maxDistance, insideDark));

        relight.setShadow(shadowConfig);
    }

    report.checkNoValidationMessages();
    return report.result();
}
