// 1d: svjetlo ubaceno u snimku.
//
// Ovo je ono zbog cega cijela grana postoji. Snimka je vec osvijetljena necim, mi joj dodamo
// jos jedno svjetlo, i rezultat mora izgledati kao da je to svjetlo bilo tamo kad se snimalo.
//
// Snimka je pritom DVIJE stvari odjednom: pocetna vrijednost i albedo. To drugo je
// aproksimacija - piksel snimke nije albedo nego albedo pomnozen starim osvjetljenjem - i
// zato je scena ovdje namjerno postavljena tako da ta aproksimacija bude TOCNA: snima se pod
// ravnim bijelim ambijentom i bez ijednog svjetla, pa je svaki piksel doslovno albedo.
//
// Time se dobiva istina protiv koje se ima sto usporediti. Na pravoj snimci ta pretpostavka
// ne stoji, i to ovaj test ne moze dokazati - moze dokazati da je racun tocan onda kad
// pretpostavka stoji, sto je sve sto se prije albedo passa i da dokazati.
#include "TestHarness.h"
#include "TestScene.h"
#include "Core/LoomConfig.h"
#include "Core/Camera.h"
#include "Core/CameraIntrinsics.h"
#include "Core/Light.h"
#include "Vulkan/Mesh.h"
#include "Vulkan/NormalMap.h"
#include "Vulkan/PositionMap.h"
#include "Vulkan/Relight.h"
#include "Vulkan/RenderTarget.h"

#include <cmath>
#include <glm/gtc/matrix_transform.hpp>

namespace{

const uint32_t sceneWidth = 480;
const uint32_t sceneHeight = 360;
const vk::Format sceneFormat = vk::Format::eR32G32B32A32Sfloat;

std::vector<Vertex> quad(float half, const glm::vec3& color){
    return {
        {{-half, -half, 0.0f}, color, {0,0}, {0,0,1}},
        {{ half, -half, 0.0f}, color, {1,0}, {0,0,1}},
        {{ half,  half, 0.0f}, color, {1,1}, {0,0,1}},
        {{-half,  half, 0.0f}, color, {0,1}, {0,0,1}},
    };
}

std::vector<glm::mat4> panels(){
    return {
        glm::translate(glm::mat4(1.0f), {0.0f, -1.2f, -2.0f})
            * glm::rotate(glm::mat4(1.0f), glm::radians(-72.0f), {1.0f, 0.0f, 0.0f})
            * glm::scale(glm::mat4(1.0f), glm::vec3(6.0f)),

        glm::translate(glm::mat4(1.0f), {-1.1f, 0.2f, -2.6f})
            * glm::rotate(glm::mat4(1.0f), glm::radians(28.0f), {0.0f, 1.0f, 0.0f})
            * glm::scale(glm::mat4(1.0f), glm::vec3(1.8f)),

        glm::translate(glm::mat4(1.0f), {1.3f, 0.5f, -3.4f})
            * glm::rotate(glm::mat4(1.0f), glm::radians(-35.0f), {0.0f, 1.0f, 0.0f})
            * glm::rotate(glm::mat4(1.0f), glm::radians(15.0f), {1.0f, 0.0f, 0.0f})
            * glm::scale(glm::mat4(1.0f), glm::vec3(1.5f)),
    };
}

const glm::vec4& at(const std::vector<uint8_t>& pixels, size_t index){
    return reinterpret_cast<const glm::vec4*>(pixels.data())[index];
}

}

int main(){
    TestReport report("1d kompozicija");

    CameraConfig cameraConfig;
    cameraConfig.position = {1.6f, 1.1f, 3.2f};
    cameraConfig.target = {0.0f, -0.1f, -2.4f};
    cameraConfig.fovY = glm::radians(50.0f);
    cameraConfig.nearPlane = 0.1f;
    cameraConfig.farPlane = 60.0f;
    Camera camera(cameraConfig);

    LoomConfig config;
    config.width = sceneWidth; config.height = sceneHeight;
    config.appName = "composite"; config.engineName = "Loom tests";
    config.headless = true;
    config.enableDepth = true;
    config.pipelineConfig.depthTestEnable = true;
    config.pipelineConfig.depthWriteEnable = true;
    config.pipelineConfig.cullMode = vk::CullModeFlagBits::eNone;
    config.pipelineConfig.colorFormat = sceneFormat;
    config.headlessColorFormat = sceneFormat;
    config.rendererConfig.clearColor = {0.03f, 0.03f, 0.05f, 1.0f};

    LoomInitializer loom(config);
    loom.renderer.setCamera(camera);

    //Ravan bijeli ambijent i nijedno svjetlo: piksel snimke je tocno albedo. Vidi komentar
    //na vrhu - to je jedina postavka u kojoj se kompozicija ima protiv cega mjeriti
    EnvironmentConfig flat;
    flat.ambientColor = {1.0f, 1.0f, 1.0f};
    Environment environment(flat);
    loom.renderer.setEnvironment(environment);

    //Svjetlo koje ubacujemo
    LightConfig bulbConfig;
    bulbConfig.type = LightType::Point;
    bulbConfig.position = {1.8f, 1.4f, -0.6f};
    bulbConfig.color = {0.25f, 0.55f, 1.0f};
    bulbConfig.intensity = 12.0f;
    bulbConfig.range = 14.0f;
    Light bulb(bulbConfig);

    //Jedna od ploha je CRNA - ona ne odbija nista, pa je ni najjace ubaceno svjetlo ne smije
    //podici. To je svojstvo zbog kojeg snimka mnozi, a ne samo zbraja
    const std::vector<glm::vec3> colors = {
        {0.75f, 0.72f, 0.68f},
        {0.00f, 0.00f, 0.00f},
        {0.20f, 0.60f, 0.90f},
    };

    std::vector<Mesh> meshes;
    for(const glm::vec3& color : colors){
        meshes.emplace_back(loom.device, loom.command, quad(0.5f, color), std::vector<uint16_t>{0,1,2, 2,3,0});
    }
    const std::vector<glm::mat4> models = panels();

    auto drawScene = [&](){
        for(size_t i = 0; i < meshes.size(); ++i){
            loom.renderer.draw(meshes[i], models[i]);
        }
    };

    RenderTargetConfig readable;
    readable.colorFormat = sceneFormat;
    readable.finalLayout = vk::ImageLayout::eTransferSrcOptimal;
    readable.extraColorUsage = vk::ImageUsageFlagBits::eTransferSrc | vk::ImageUsageFlagBits::eSampled;
    readable.keepDepth = true;
    readable.depthCompare = false;

    RenderTarget plateTarget(loom.device, vk::Extent2D{sceneWidth, sceneHeight}, readable);
    RenderTarget outTarget(loom.device, vk::Extent2D{sceneWidth, sceneHeight}, readable);

    // -------------------------------------------------------------------------------
    // Snimka: ravan ambijent, nijedno svjetlo
    // -------------------------------------------------------------------------------

    if(loom.renderer.beginFrame()){
        loom.renderer.beginPass(plateTarget);
        drawScene();
        loom.renderer.endPass();
        loom.renderer.endFrame();
    }
    loom.waitIdle();
    const std::vector<uint8_t> plate = plateTarget.readPixels(loom.command).pixels;

    //Procitana je, sad je treba i semplirati - a citanje ju je ostavilo tamo gdje kopiranje
    //hoce, ne tamo gdje shader hoce
    loom.command.transitionImageLayout(plateTarget.getColorImage(), vk::ImageLayout::eShaderReadOnlyOptimal);

    // -------------------------------------------------------------------------------
    // Istina: ista scena crtana ODJEDNOM, s ubacenim svjetlom
    // -------------------------------------------------------------------------------

    loom.renderer.addLight(bulb);

    if(loom.renderer.beginFrame()){
        loom.renderer.beginPass(outTarget);
        drawScene();
        loom.renderer.endPass();
        loom.renderer.endFrame();
    }
    loom.waitIdle();
    const std::vector<uint8_t> truth = outTarget.readPixels(loom.command).pixels;

    // -------------------------------------------------------------------------------
    // Kompozicija: iz dubine snimke, pa svjetlo preko nje
    // -------------------------------------------------------------------------------

    const CameraIntrinsics intrinsics =
        CameraIntrinsics::fromProjection(camera.getProjection(sceneWidth, sceneHeight), sceneWidth, sceneHeight);

    PositionMap positions(loom.device, vk::Extent2D{sceneWidth, sceneHeight});
    positions.setDepthSource(loom.getDescriptorPool(), plateTarget);
    positions.setIntrinsics(intrinsics);

    NormalMap normals(loom.device, vk::Extent2D{sceneWidth, sceneHeight});
    normals.setPositionSource(loom.getDescriptorPool(), positions);

    RelightConfig relightConfig;
    relightConfig.colorFormat = sceneFormat;
    relightConfig.depthFormat = outTarget.getDepthFormat();
    relightConfig.surface.baseColor = {1.0f, 1.0f, 1.0f, 1.0f};
    relightConfig.surface.shininess = 32.0f;
    relightConfig.surface.specularStrength = 1.0f;

    Relight relight(loom.device, loom.command, loom.getDescriptorPool(),
                    positions, normals, plateTarget.getSampled(), relightConfig);
    relight.setCamera(camera);

    report.check("prolaz zna da kompozitira", relight.compositesOverPlate(),
        "izgraden je sa snimkom, pa crta preko nje umjesto svoje pozadine");

    auto composite = [&](){
        if(loom.renderer.beginFrame()){
            const UnprojectPush unproject = positions.makePush();
            loom.renderer.dispatch(positions.getComputeMaterial(),
                                   positions.groupsX(), positions.groupsY(), 1,
                                   &unproject, sizeof(unproject));

            const NormalPush normalPush = normals.makePush();
            loom.renderer.dispatch(normals.getComputeMaterial(),
                                   normals.groupsX(), normals.groupsY(), 1,
                                   &normalPush, sizeof(normalPush));

            loom.renderer.beginPass(outTarget);
            loom.renderer.drawFullscreen(relight.getMaterial());
            loom.renderer.endPass();
            loom.renderer.endFrame();
        }
        loom.waitIdle();
        return outTarget.readPixels(loom.command).pixels;
    };

    const std::vector<uint8_t> composited = composite();

    // -------------------------------------------------------------------------------
    // Poklapa li se s crtezom odjednom
    // -------------------------------------------------------------------------------

    //Tocke se citaju iz iste dubine koju je snimka ostavila, za podjelu na unutrasnjost i
    //siluetu. Dubina snimke jos stoji: kompozicija je crtala u drugu metu
    const std::vector<uint8_t> points = [&]{
        PositionMap out(loom.device, vk::Extent2D{sceneWidth, sceneHeight},
                        PositionMapConfig{vk::ImageLayout::eTransferSrcOptimal});
        out.setDepthSource(loom.getDescriptorPool(), plateTarget);
        out.setIntrinsics(intrinsics);
        if(loom.renderer.beginFrame()){
            const UnprojectPush push = out.makePush();
            loom.renderer.dispatch(out.getComputeMaterial(), out.groupsX(), out.groupsY(), 1, &push, sizeof(push));
            loom.renderer.endFrame();
        }
        loom.waitIdle();
        return out.readPixels(loom.command).pixels;
    }();

    auto point = [&](int x, int y){
        x = glm::clamp(x, 0, int(sceneWidth) - 1);
        y = glm::clamp(y, 0, int(sceneHeight) - 1);
        return at(points, size_t(y) * sceneWidth + x);
    };

    //Sve plohe u sceni su ravne, pa srednja tocka lezi tocno na polovistu susjeda. Odstupanje
    //je rupa u dubini ili susjed s druge plohe - drugog izvora nema
    auto onSilhouette = [&](uint32_t x, uint32_t y){
        if(point(int(x), int(y)).w < 0.5f) return true;
        for(int axis = 0; axis < 2; ++axis){
            const glm::vec4 back = point(int(x) - (axis == 0), int(y) - (axis == 1));
            const glm::vec4 forward = point(int(x) + (axis == 0), int(y) + (axis == 1));
            if(back.w < 0.5f || forward.w < 0.5f) return true;
            if(std::abs(point(int(x), int(y)).z - 0.5f * (back.z + forward.z)) > 1e-3f) return true;
        }
        return false;
    };

    float deepest = 0.0f, interiorWorst = 0.0f;
    size_t surfacePixels = 0, backgroundPixels = 0, backgroundDiffer = 0;
    double interiorSum = 0.0; size_t interiorCount = 0;

    for(uint32_t y = 0; y < sceneHeight; ++y){
        for(uint32_t x = 0; x < sceneWidth; ++x){
            const size_t i = size_t(y) * sceneWidth + x;
            const glm::vec4 p = point(int(x), int(y));

            if(p.w > 0.5f){ ++surfacePixels; deepest = std::max(deepest, -p.z); }
            else{
                //Bez plohe snimka mora proci nedirnuta, i to bit za bit - to je puko
                //prepisivanje, a ne racun
                ++backgroundPixels;
                for(int c = 0; c < 4; ++c){
                    if(at(composited, i)[c] != at(plate, i)[c]){ ++backgroundDiffer; break; }
                }
                continue;
            }

            if(onSilhouette(x, y)) continue;

            for(int c = 0; c < 3; ++c){
                const float delta = std::abs(at(truth, i)[c] - at(composited, i)[c]);
                interiorWorst = std::max(interiorWorst, delta);
                interiorSum += delta;
                ++interiorCount;
            }
        }
    }

    const float tolerance = lightingTolerance(deepest, intrinsics, 32.0f);
    const float step = 1.0f / 255.0f;

    report.check("kompozicija je crtez odjednom",
        interiorWorst < tolerance,
        fmt("najveca razlika %.7f uz izvedeni prag %.7f na dubini %.2f; prosjek %.9f = %.2f%% koraka 8-bitne slike",
            interiorWorst, tolerance, deepest, interiorSum / double(interiorCount),
            100.0 * (interiorSum / double(interiorCount)) / step));

    report.check("gdje nema plohe, snimka prolazi nedirnuta",
        backgroundDiffer == 0 && backgroundPixels > 0,
        fmt("%zu od %zu piksela pozadine se razlikuje od snimke", backgroundDiffer, backgroundPixels));

    // -------------------------------------------------------------------------------
    // Crno ostaje crno
    // -------------------------------------------------------------------------------

    //Zrcalni clan se NE mnozi albedom - i to je namjerno, jer odsjaj dielektrika nije obojen
    //bojom plohe. Zato se ovdje gasi: tvrdnja je o difuznom odgovoru crne plohe
    MaterialData matte;
    matte.baseColor = {1.0f, 1.0f, 1.0f, 1.0f};
    matte.shininess = 32.0f;
    matte.specularStrength = 0.0f;
    relight.setSurface(matte);

    //I jace svjetlo, da se vidi da nije stvar u tome sto ga je premalo
    LightConfig blazing = bulbConfig;
    blazing.intensity = 400.0f;
    Light strong(blazing);
    loom.renderer.clearLights();
    loom.renderer.addLight(strong);

    const std::vector<uint8_t> blasted = composite();

    size_t blackPixels = 0, blackLit = 0, brightPixels = 0, brightLit = 0;
    for(size_t i = 0; i < size_t(sceneWidth) * sceneHeight; ++i){
        if(at(points, i).w < 0.5f) continue;

        const glm::vec4& before = at(plate, i);
        const glm::vec4& after = at(blasted, i);
        const bool changed = after.r != before.r || after.g != before.g || after.b != before.b;

        if(before.r == 0.0f && before.g == 0.0f && before.b == 0.0f){
            ++blackPixels;
            if(changed) ++blackLit;
        }
        else{
            ++brightPixels;
            if(changed) ++brightLit;
        }
    }

    report.check("crna ploha ostaje crna",
        blackPixels > 0 && blackLit == 0,
        fmt("%zu crnih piksela pod svjetlom jacine %.0f, promijenilo se %zu",
            blackPixels, blazing.intensity, blackLit));

    //Kontrola: da se pod istim svjetlom nista nije promijenilo, gornja provjera bi prosla
    //nad slikom koju nitko nije ni dotaknuo
    report.check("a ostalo se promijenilo",
        brightPixels > 0 && brightLit * 10 > brightPixels * 9,
        fmt("%zu od %zu ostalih piksela je poraslo", brightLit, brightPixels));

    // -------------------------------------------------------------------------------
    // baseColor gasi ubaceno svjetlo, ne snimku
    // -------------------------------------------------------------------------------

    MaterialData muted = matte;
    muted.baseColor = {0.0f, 0.0f, 0.0f, 1.0f};
    relight.setSurface(muted);

    const std::vector<uint8_t> unlit = composite();

    size_t differ = 0;
    for(size_t i = 0; i < size_t(sceneWidth) * sceneHeight; ++i){
        for(int c = 0; c < 4; ++c){
            if(at(unlit, i)[c] != at(plate, i)[c]){ ++differ; break; }
        }
    }

    report.check("baseColor 0 vraca tocno snimku", differ == 0,
        fmt("%zu od %zu piksela se razlikuje od snimke kad ploha ne odbija nista",
            differ, size_t(sceneWidth) * sceneHeight));

    report.checkNoValidationMessages();
    return report.result();
}
