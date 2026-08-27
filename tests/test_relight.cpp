// 1c: svjetlo nad G-bufferom.
//
// Ovdje se ubacivanje svjetla konacno dogada. Fragment ne zna ni za jedan trokut - zna gdje
// je tocka i kamo gleda, a to je sve sto osvjetljenje ikad treba.
//
// Dokaz je usporedba s crtezom preko mesha: ista scena, ista svjetla, dva puta. Jednom kroz
// trokute, jednom kroz sliku dubine. Ako se te dvije slike poklope, relight radi - jer je
// crtez preko mesha ono sto vec znamo da je tocno.
//
// Crta se u float metu, ne u osmobitnu: usporedujemo svjetlo, a ne njegov zapis. U 8 bita bi
// se razlika od pola koraka izgubila i test bi prolazio i kad ne bi trebao.
//
// Kamera NAMJERNO nije u ishodistu i ne gleda niz os. Da jest, view matrica bi bila jedinicna,
// pretvorba view -> svijet bi bila prazan posao, i greska u njoj bi prosla neprimjeceno.
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

std::vector<Vertex> quad(float half){
    return {
        {{-half, -half, 0.0f}, {1,1,1}, {0,0}, {0,0,1}},
        {{ half, -half, 0.0f}, {1,1,1}, {1,0}, {0,0,1}},
        {{ half,  half, 0.0f}, {1,1,1}, {1,1}, {0,0,1}},
        {{-half,  half, 0.0f}, {1,1,1}, {0,1}, {0,0,1}},
    };
}

//Ravne plohe pod raznim kutovima. Ravne zato sto na njima normala vrha i geometrijska
//normala moraju biti ISTA - na zaobljenoj bi se razlikovale i mjerili bismo tesselaciju
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

struct Difference{
    float worst = 0.0f;
    double sum = 0.0;
    size_t count = 0;
    float mean() const {return count ? float(sum / double(count)) : 0.0f;}
};

//Najveca i prosjecna razlika po kanalu, preko cijele slike
Difference compare(const std::vector<uint8_t>& a, const std::vector<uint8_t>& b){
    Difference out;
    const size_t pixels = size_t(sceneWidth) * sceneHeight;
    for(size_t i = 0; i < pixels; ++i){
        const glm::vec4& left = at(a, i);
        const glm::vec4& right = at(b, i);
        for(int c = 0; c < 3; ++c){
            const float delta = std::abs(left[c] - right[c]);
            out.worst = std::max(out.worst, delta);
            out.sum += delta;
            ++out.count;
        }
    }
    return out;
}

//Koliko svjetla uopce ima u slici - da se zna prema cemu je razlika mala
float brightest(const std::vector<uint8_t>& pixels){
    float out = 0.0f;
    const size_t count = size_t(sceneWidth) * sceneHeight;
    for(size_t i = 0; i < count; ++i){
        const glm::vec4& p = at(pixels, i);
        out = std::max(out, std::max(p.r, std::max(p.g, p.b)));
    }
    return out;
}

}

int main(){
    TestReport report("1c relight");

    //Kamera nije u ishodistu i ne gleda niz os - vidi komentar na vrhu
    CameraConfig cameraConfig;
    cameraConfig.position = {1.6f, 1.1f, 3.2f};
    cameraConfig.target = {0.0f, -0.1f, -2.4f};
    cameraConfig.fovY = glm::radians(50.0f);
    cameraConfig.nearPlane = 0.1f;
    cameraConfig.farPlane = 60.0f;
    Camera camera(cameraConfig);

    LoomConfig config;
    config.width = sceneWidth; config.height = sceneHeight;
    config.appName = "relight"; config.engineName = "Loom tests";
    config.headless = true;
    config.enableDepth = true;
    config.pipelineConfig.depthTestEnable = true;
    config.pipelineConfig.depthWriteEnable = true;
    config.pipelineConfig.cullMode = vk::CullModeFlagBits::eNone;
    config.pipelineConfig.colorFormat = sceneFormat;
    config.headlessColorFormat = sceneFormat;
    config.rendererConfig.clearColor = {0.0f, 0.0f, 0.0f, 1.0f};

    LoomInitializer loom(config);
    loom.renderer.setCamera(camera);

    EnvironmentConfig environmentConfig;
    environmentConfig.ambientColor = {0.06f, 0.06f, 0.08f};
    Environment environment(environmentConfig);
    loom.renderer.setEnvironment(environment);

    //Usmjereno svjetlo i tockasto. Tockasto je ovdje vazno: njegovo slabljenje ovisi o
    //UDALJENOSTI u svjetskom prostoru, pa kriva pretvorba view -> svijet odmah puca
    LightConfig sunConfig;
    sunConfig.type = LightType::Directional;
    sunConfig.direction = {-0.45f, -1.0f, -0.35f};
    sunConfig.color = {1.0f, 0.95f, 0.88f};
    sunConfig.intensity = 1.0f;
    Light sun(sunConfig);
    loom.renderer.addLight(sun);

    LightConfig bulbConfig;
    bulbConfig.type = LightType::Point;
    bulbConfig.position = {1.8f, 1.4f, -0.6f};
    bulbConfig.color = {0.25f, 0.55f, 1.0f};
    bulbConfig.intensity = 12.0f;
    bulbConfig.range = 14.0f;
    Light bulb(bulbConfig);
    loom.renderer.addLight(bulb);

    RenderTargetConfig targetConfig;
    targetConfig.colorFormat = sceneFormat;
    targetConfig.finalLayout = vk::ImageLayout::eTransferSrcOptimal;
    targetConfig.extraColorUsage = vk::ImageUsageFlagBits::eTransferSrc;
    targetConfig.keepDepth = true;
    targetConfig.depthCompare = false;
    RenderTarget target(loom.device, vk::Extent2D{sceneWidth, sceneHeight}, targetConfig);

    const Mesh panel(loom.device, loom.command, quad(0.5f), {0,1,2, 2,3,0});
    const std::vector<glm::mat4> models = panels();

    const CameraIntrinsics intrinsics =
        CameraIntrinsics::fromProjection(camera.getProjection(sceneWidth, sceneHeight), sceneWidth, sceneHeight);

    PositionMap positions(loom.device, vk::Extent2D{sceneWidth, sceneHeight});
    positions.setDepthSource(loom.getDescriptorPool(), target);
    positions.setIntrinsics(intrinsics);

    NormalMap normals(loom.device, vk::Extent2D{sceneWidth, sceneHeight});
    normals.setPositionSource(loom.getDescriptorPool(), positions);

    RelightConfig relightConfig;
    relightConfig.colorFormat = sceneFormat;
    relightConfig.depthFormat = target.getDepthFormat();
    //Ista ploha koju crtez preko mesha ima: bijeli vrhovi, i sjaj koji triangle.slang nosi
    relightConfig.surface.baseColor = {1.0f, 1.0f, 1.0f, 1.0f};
    relightConfig.surface.shininess = 32.0f;
    relightConfig.surface.specularStrength = 1.0f;

    Relight relight(loom.device, loom.command, loom.getDescriptorPool(), positions, normals, relightConfig);
    relight.setCamera(camera);

    // -------------------------------------------------------------------------------
    // Kroz trokute
    // -------------------------------------------------------------------------------

    if(loom.renderer.beginFrame()){
        loom.renderer.beginPass(target);
        for(const glm::mat4& model : models){
            loom.renderer.draw(panel, model);
        }
        loom.renderer.endPass();
        loom.renderer.endFrame();
    }
    loom.waitIdle();
    const std::vector<uint8_t> throughMesh = target.readPixels(loom.command).pixels;

    //Tocke se citaju SAD, dok dubina ove scene jos stoji u meti. Relight prolaz ispod crta u
    //istu metu i pritom joj obrise dubinu - a tada bi ovo procitalo praznu sliku i svaka
    //podjela na unutrasnjost i siluetu bila bi podjela nicega
    const std::vector<uint8_t> points = [&]{
        PositionMap readable(loom.device, vk::Extent2D{sceneWidth, sceneHeight},
                             PositionMapConfig{vk::ImageLayout::eTransferSrcOptimal});
        readable.setDepthSource(loom.getDescriptorPool(), target);
        readable.setIntrinsics(intrinsics);
        if(loom.renderer.beginFrame()){
            const UnprojectPush push = readable.makePush();
            loom.renderer.dispatch(readable.getComputeMaterial(),
                                   readable.groupsX(), readable.groupsY(), 1, &push, sizeof(push));
            loom.renderer.endFrame();
        }
        loom.waitIdle();
        return readable.readPixels(loom.command).pixels;
    }();


    // -------------------------------------------------------------------------------
    // Kroz sliku dubine
    // -------------------------------------------------------------------------------

    //Ista scena: dubina se napise, iz nje tocke, iz tocaka normale, i tek onda svjetlo -
    //nad metom koja je ocistena, pa nista od proslog crteza ne moze preostati
    auto relightPass = [&](){
        if(loom.renderer.beginFrame()){
            //Isti crtez kao gore. Boja koju napise nikoga ne zanima - treba nam dubina, a
            //nju drugi prolaz vise ne dira jer je do tada vec pretvorena u tocke
            loom.renderer.beginPass(target);
            for(const glm::mat4& model : models){
                loom.renderer.draw(panel, model);
            }
            loom.renderer.endPass();

            const UnprojectPush unproject = positions.makePush();
            loom.renderer.dispatch(positions.getComputeMaterial(),
                                   positions.groupsX(), positions.groupsY(), 1,
                                   &unproject, sizeof(unproject));

            const NormalPush normalPush = normals.makePush();
            loom.renderer.dispatch(normals.getComputeMaterial(),
                                   normals.groupsX(), normals.groupsY(), 1,
                                   &normalPush, sizeof(normalPush));

            loom.renderer.beginPass(target);
            loom.renderer.drawFullscreen(relight.getMaterial());
            loom.renderer.endPass();
            loom.renderer.endFrame();
        }
        loom.waitIdle();
        return target.readPixels(loom.command).pixels;
    };

    const std::vector<uint8_t> throughDepth = relightPass();

    // -------------------------------------------------------------------------------
    // Poklapaju li se
    // -------------------------------------------------------------------------------

    const float peak = brightest(throughMesh);

    report.check("u slici ima svjetla", peak > 0.2f,
        fmt("najsvjetliji kanal %.4f - da se zna prema cemu se razlika mjeri", peak));

    const Difference difference = compare(throughMesh, throughDepth);

    //Prag: jedan korak osmobitne slike. Sto god razlika bila u float-u, ispod ovoga se u
    //bilo kojem isporucivom formatu ne moze ni zapisati, a kamoli vidjeti
    const float step = 1.0f / 255.0f;

    // -------------------------------------------------------------------------------
    // Unutrasnjost i silueta se mjere odvojeno
    // -------------------------------------------------------------------------------

    //Na silueti G-buffer nema sto rekonstruirati: piksel na rubu plohe ima susjede na drugoj
    //plohi ili nigdje, pa mu normala dolazi iz jednostrane razlike. Mesh tamo zna tocan
    //trokut. To je STVARNA granica 2.5D relighta, ne greska - i zato se broji zasebno umjesto
    //da se sakrije u jedan maksimum preko cijele slike
    auto point = [&](int x, int y){
        x = glm::clamp(x, 0, int(sceneWidth) - 1);
        y = glm::clamp(y, 0, int(sceneHeight) - 1);
        return at(points, size_t(y) * sceneWidth + x);
    };

    //Sve plohe u ovoj sceni su RAVNE, pa srednja tocka mora lezati tocno na polovistu svojih
    //susjeda. Odstupanje od toga je ili rupa u dubini ili susjed s druge plohe - drugog
    //izvora nema, i prag zato treba samo nadvisiti sum float-a (oko 4e-5 na ovim dubinama)
    auto onSilhouette = [&](uint32_t x, uint32_t y){
        const glm::vec4 center = point(int(x), int(y));
        if(center.w < 0.5f) return true;

        for(int axis = 0; axis < 2; ++axis){
            const glm::vec4 back = point(int(x) - (axis == 0), int(y) - (axis == 1));
            const glm::vec4 forward = point(int(x) + (axis == 0), int(y) + (axis == 1));
            if(back.w < 0.5f || forward.w < 0.5f) return true;
            if(std::abs(center.z - 0.5f * (back.z + forward.z)) > 1e-3f) return true;
        }
        return false;
    };

    Difference interior;
    size_t surfacePixels = 0, edgeOverStep = 0, interiorOverStep = 0;

    for(uint32_t y = 0; y < sceneHeight; ++y){
        for(uint32_t x = 0; x < sceneWidth; ++x){
            const size_t i = size_t(y) * sceneWidth + x;
            if(point(int(x), int(y)).w > 0.5f) ++surfacePixels;

            float worst = 0.0f;
            for(int c = 0; c < 3; ++c){
                worst = std::max(worst, std::abs(at(throughMesh, i)[c] - at(throughDepth, i)[c]));
            }

            if(onSilhouette(x, y)){
                if(worst > step) ++edgeOverStep;
                continue;
            }

            if(worst > step) ++interiorOverStep;
            interior.worst = std::max(interior.worst, worst);
            interior.sum += worst;
            ++interior.count;
        }
    }

    //Prag se izvodi, ne bira. Normala iz zapisane dubine tocna je do kuta koji taj zapis
    //dopusta, a svjetlo se s tim kutom mijenja najvise 1:1 difuzno i specularSlope puta
    //zrcalno. Prvi pokusaj je stajao na "manje od koraka 8-bitne slike" i padao na 0.0043 -
    //broju koji nije bio greska nego zrcalni clan na najdubljoj plohi
    float deepest = 0.0f;
    for(uint32_t y = 0; y < sceneHeight; ++y){
        for(uint32_t x = 0; x < sceneWidth; ++x){
            const glm::vec4 p = point(int(x), int(y));
            if(p.w > 0.5f) deepest = std::max(deepest, -p.z);
        }
    }

    const float tolerance = lightingTolerance(deepest, intrinsics, 32.0f);

    //Prava tvrdnja: gdje god ploha stvarno postoji, dva puta racunato svjetlo je isto svjetlo
    report.check("u unutrasnjosti relight je crtez preko mesha",
        interior.worst < tolerance,
        fmt("najveca razlika %.7f uz izvedeni prag %.7f na dubini %.2f; prosjek %.9f = %.2f%% koraka 8-bitne slike",
            interior.worst, tolerance, deepest, interior.mean(), 100.0f * interior.mean() / step));

    //I koliko ih uopce dosegne jedan korak osmobitne slike - jer prag u linearnom svjetlu
    //je jedno, a "vidi li se to" je drugo
    report.check("gotovo nijedan piksel ne dosegne ni korak 8-bitne slike",
        interiorOverStep * 2000 < surfacePixels,
        fmt("%zu od %zu piksela unutrasnjosti dosegne %.6f - %.4f%%",
            interiorOverStep, interior.count, step,
            100.0 * double(interiorOverStep) / double(interior.count)));

    //A gdje se ne poklapa, poklapa se PRICA: sve razlike preko praga su na silueti i nigdje
    //drugdje. Da ih ima i u unutrasnjosti, gornja provjera bi pala - ali ovo imenuje i koliko
    //ih ukupno ima, pa se ne moze tiho popeti
    report.check("razlika je samo na silueti",
        edgeOverStep * 1000 < surfacePixels,
        fmt("%zu rubnih piksela odstupa od %zu na kojima ima plohe - %.3f%%; ukupni maksimum %.6f",
            edgeOverStep, surfacePixels, 100.0 * double(edgeOverStep) / double(surfacePixels),
            difference.worst));

    // -------------------------------------------------------------------------------
    // I ono sto se MORA razlikovati
    // -------------------------------------------------------------------------------

    //Kamera koja nije ta kroz koju su tocke nastale. Pozicije i normale ostaju iste, mijenja
    //se samo most izmedu view i svjetskog prostora - i to mora prolomiti sliku. Bez ove
    //provjere bi prosla i implementacija koja inverseView primi pa ga ne upotrijebi
    {
        CameraConfig elsewhereConfig = cameraConfig;
        elsewhereConfig.position = {-2.0f, 0.4f, 1.0f};
        Camera elsewhere(elsewhereConfig);

        relight.setCamera(elsewhere);
        const std::vector<uint8_t> wrong = relightPass();
        const Difference broken = compare(throughMesh, wrong);

        report.check("kriva kamera lomi sliku", broken.worst > 20.0f * step,
            fmt("najveca razlika %.6f, prosjek %.6f - kroz pravu kameru je bilo %.6f",
                broken.worst, broken.mean(), difference.worst));

        //I natrag: ista kamera mora vratiti istu sliku, pa se zna da je razlika bila zbog
        //kamere a ne zbog toga sto je scena crtana drugi put
        relight.setCamera(camera);
        const Difference again = compare(throughDepth, relightPass());

        report.check("ista kamera vraca istu sliku", again.worst == 0.0f,
            fmt("%.8f razlike nakon povratka na pravu kameru", again.worst));
    }

    report.checkNoValidationMessages();
    return report.result();
}
