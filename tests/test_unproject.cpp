// 1a: dubina natrag u tocke.
//
// Prvi kamen svjetla koje se ubacuje u snimku. Depth buffer zna koliko je sto daleko, ali ne
// i GDJE je - a svjetlo koje pada po inverznom kvadratu treba tocku, ne udaljenost.
//
// Ovdje ne sudjeluje nijedan model i nijedna kamera: Loom nacrta scenu koju je sam postavio,
// procita svoju pravu dubinu, i mora iz nje rekonstruirati tocke koje smo mi upisali. Istina
// je poznata jer smo je sami odabrali - a to je jedini nacin da se odprojekcija dokaze prije
// nego joj se pod ruku podmetne tuda snimka.
//
// Kamera stoji u ishodistu i gleda niz -Z, pa je view matrica jedinicna i svjetske koordinate
// SU view koordinate. Nista se ne mora prevoditi da bi se brojevi u testu citali.
#include "TestHarness.h"
#include "TestScene.h"
#include "Core/LoomConfig.h"
#include "Core/Camera.h"
#include "Core/CameraIntrinsics.h"
#include "Vulkan/Mesh.h"
#include "Vulkan/PositionMap.h"
#include "Vulkan/RenderTarget.h"

#include <cmath>
#include <glm/gtc/matrix_transform.hpp>

namespace{

const uint32_t sceneWidth = 640;
const uint32_t sceneHeight = 480;

//Kvadrat u ravnini XY, na zadanoj dubini, sa zadanim rubovima. Ovo su brojevi koje test
//poslije trazi natrag
std::vector<Vertex> quad(float minX, float maxX, float minY, float maxY, float z){
    return {
        {{minX, minY, z}, {1,1,1}, {0,0}, {0,0,1}},
        {{maxX, minY, z}, {1,1,1}, {1,0}, {0,0,1}},
        {{maxX, maxY, z}, {1,1,1}, {1,1}, {0,0,1}},
        {{minX, maxY, z}, {1,1,1}, {0,1}, {0,0,1}},
    };
}

struct Point{
    float x, y, z, valid;
};

const Point& at(const std::vector<uint8_t>& pixels, uint32_t x, uint32_t y){
    return reinterpret_cast<const Point*>(pixels.data())[size_t(y) * sceneWidth + x];
}

//Raspon rekonstruiranih tocaka po svim pikselima na kojima ploha postoji
struct Bounds{
    float minX = 1e30f, maxX = -1e30f;
    float minY = 1e30f, maxY = -1e30f;
    float minZ = 1e30f, maxZ = -1e30f;
    size_t count = 0;
};

Bounds boundsOf(const std::vector<uint8_t>& pixels){
    Bounds out;
    for(uint32_t y = 0; y < sceneHeight; ++y){
        for(uint32_t x = 0; x < sceneWidth; ++x){
            const Point& p = at(pixels, x, y);
            if(p.valid < 0.5f) continue;
            out.minX = std::min(out.minX, p.x); out.maxX = std::max(out.maxX, p.x);
            out.minY = std::min(out.minY, p.y); out.maxY = std::max(out.maxY, p.y);
            out.minZ = std::min(out.minZ, p.z); out.maxZ = std::max(out.maxZ, p.z);
            ++out.count;
        }
    }
    return out;
}

}

int main(){
    TestReport report("1a odprojekcija");

    // -------------------------------------------------------------------------------
    // Intrinsike iz matrice, bez ijednog Vulkan poziva
    // -------------------------------------------------------------------------------

    CameraConfig cameraConfig;
    cameraConfig.position = {0.0f, 0.0f, 0.0f};
    cameraConfig.target = {0.0f, 0.0f, -1.0f};
    cameraConfig.up = {0.0f, 1.0f, 0.0f};
    cameraConfig.fovY = glm::radians(45.0f);
    cameraConfig.nearPlane = 0.1f;
    cameraConfig.farPlane = 100.0f;
    Camera camera(cameraConfig);

    //Cijeli test stoji na tome da su svjetske koordinate ujedno i view koordinate. Ako se to
    //ikad promijeni, bolje da padne ovdje nego da tiho mjeri nesto drugo
    {
        const glm::mat4 view = camera.getView();
        float worst = 0.0f;
        for(int c = 0; c < 4; ++c){
            for(int r = 0; r < 4; ++r){
                worst = std::max(worst, std::abs(view[c][r] - (c == r ? 1.0f : 0.0f)));
            }
        }
        report.check("view matrica je jedinicna", worst < 1e-6f,
            fmt("najveci otklon od jedinicne %.2e - svjetske koordinate su view koordinate", worst));
    }

    const CameraIntrinsics intrinsics =
        CameraIntrinsics::fromProjection(camera.getProjection(sceneWidth, sceneHeight), sceneWidth, sceneHeight);

    //Rukom: fy = (visina/2) / tan(fovY/2) = 240 / tan(22.5) = 579.41
    const float expectedFocal = (float(sceneHeight) * 0.5f) / std::tan(glm::radians(22.5f));

    report.check("zariste u pikselima",
        std::abs(std::abs(intrinsics.fy) - expectedFocal) < 0.01f &&
        std::abs(intrinsics.fx - expectedFocal) < 0.01f,
        fmt("fx %.2f, |fy| %.2f, rukom izvedeno 240/tan(22.5) = %.2f",
            intrinsics.fx, std::abs(intrinsics.fy), expectedFocal));

    //fy je negativan jer Vulkanova projekcija okrece Y. To nije greska nego zapis smjera, i
    //odprojekcija ga sama ponisti - ali ako ikad postane pozitivan, slika se prevrne
    report.check("fy nosi okret Y osi", intrinsics.fy < 0.0f,
        fmt("fy = %.2f", intrinsics.fy));

    report.check("glavna tocka je u sredini",
        std::abs(intrinsics.cx - 320.0f) < 0.01f && std::abs(intrinsics.cy - 240.0f) < 0.01f,
        fmt("cx %.2f, cy %.2f za sliku %ux%u", intrinsics.cx, intrinsics.cy, sceneWidth, sceneHeight));

    report.check("ravnine se vracaju iz matrice",
        std::abs(intrinsics.nearPlane - 0.1f) < 1e-4f && std::abs(intrinsics.farPlane - 100.0f) < 1e-2f,
        fmt("near %.4f, far %.2f", intrinsics.nearPlane, intrinsics.farPlane));

    //Vertikalno vidno polje mora ispasti tocno ono koje smo trazili - to zatvara krug
    report.check("vidno polje se poklapa",
        std::abs(glm::degrees(intrinsics.verticalFov(sceneHeight)) - 45.0f) < 0.01f,
        fmt("okomito %.2f stupnjeva (trazeno 45), vodoravno %.2f",
            glm::degrees(intrinsics.verticalFov(sceneHeight)),
            glm::degrees(intrinsics.horizontalFov(sceneWidth))));

    //Pomaknuta glavna tocka mora pomaknuti cx za tocno toliko piksela. Bez ove provjere bi
    //prosla i implementacija koja principal point uopce ne cita
    {
        CameraConfig shiftedConfig = cameraConfig;
        shiftedConfig.principalPointX = 40.0f;
        shiftedConfig.principalPointY = -25.0f;
        Camera shifted(shiftedConfig);
        const CameraIntrinsics off =
            CameraIntrinsics::fromProjection(shifted.getProjection(sceneWidth, sceneHeight), sceneWidth, sceneHeight);

        report.check("glavna tocka se pomice",
            std::abs((off.cx - intrinsics.cx) - 40.0f) < 0.05f &&
            std::abs((off.cy - intrinsics.cy) - (-25.0f)) < 0.05f,
            fmt("pomak od %+.2f, %+.2f piksela za trazeno +40, -25",
                off.cx - intrinsics.cx, off.cy - intrinsics.cy));
    }

    // -------------------------------------------------------------------------------
    // Scena
    // -------------------------------------------------------------------------------

    LoomConfig config;
    config.width = sceneWidth; config.height = sceneHeight;
    config.appName = "unproject"; config.engineName = "Loom tests";
    config.headless = true;
    config.enableDepth = true;
    config.pipelineConfig.depthTestEnable = true;
    config.pipelineConfig.depthWriteEnable = true;
    config.pipelineConfig.cullMode = vk::CullModeFlagBits::eNone;
    config.pipelineConfig.colorFormat = vk::Format::eB8G8R8A8Srgb;
    config.headlessColorFormat = vk::Format::eB8G8R8A8Srgb;

    LoomInitializer loom(config);
    loom.renderer.setCamera(camera);

    EnvironmentConfig environmentConfig;
    environmentConfig.ambientColor = {1.0f, 1.0f, 1.0f};
    Environment environment(environmentConfig);
    loom.renderer.setEnvironment(environment);

    //Meta cuva dubinu da je compute ima odakle procitati, i ne semplira je usporedbom
    RenderTargetConfig targetConfig;
    targetConfig.keepDepth = true;
    targetConfig.depthCompare = false;
    RenderTarget target(loom.device, vk::Extent2D{sceneWidth, sceneHeight}, targetConfig);

    PositionMapConfig positionConfig;
    positionConfig.finalLayout = vk::ImageLayout::eTransferSrcOptimal;
    PositionMap positions(loom.device, vk::Extent2D{sceneWidth, sceneHeight}, positionConfig);
    positions.setDepthSource(loom.getDescriptorPool(), target);

    //Nacrta zadanu geometriju jednom, pa je odprojicira kroz zadane intrinsike. Dubina se
    //crta samo jednom po sceni - odprojekcija se poslije vrti nad istom dubinom, sto je
    //jedini nacin da se promjena intrinsika izolira od svega drugog
    auto renderDepth = [&](const std::vector<Vertex>& vertices){
        Mesh mesh(loom.device, loom.command, vertices, {0,1,2, 2,3,0});
        if(loom.renderer.beginFrame()){
            loom.renderer.beginPass(target);
            loom.renderer.draw(mesh, glm::mat4(1.0f));
            loom.renderer.endPass();
            loom.renderer.endFrame();
        }
        loom.waitIdle();
    };

    auto unprojectWith = [&](const CameraIntrinsics& value){
        positions.setIntrinsics(value);
        const UnprojectPush push = positions.makePush();

        if(loom.renderer.beginFrame()){
            loom.renderer.dispatch(positions.getComputeMaterial(),
                                   positions.groupsX(), positions.groupsY(), 1,
                                   &push, sizeof(push));
            loom.renderer.endFrame();
        }
        loom.waitIdle();
        return positions.readPixels(loom.command).pixels;
    };

    // -------------------------------------------------------------------------------
    // Ravnina okomita na pogled, na poznatoj udaljenosti
    // -------------------------------------------------------------------------------

    //Dovoljno velika da ispuni kadar do kutova. Kutovi su ono sto centar ne provjerava
    renderDepth(quad(-20.0f, 20.0f, -20.0f, 20.0f, -5.0f));
    const std::vector<uint8_t> flat = unprojectWith(intrinsics);

    {
        float worst = 0.0f;
        size_t valid = 0;
        for(uint32_t y = 0; y < sceneHeight; ++y){
            for(uint32_t x = 0; x < sceneWidth; ++x){
                const Point& p = at(flat, x, y);
                if(p.valid < 0.5f) continue;
                worst = std::max(worst, std::abs(p.z + 5.0f));
                ++valid;
            }
        }

        report.check("ravnina je cijela na svojoj dubini",
            valid == size_t(sceneWidth) * sceneHeight && worst < 1e-3f,
            fmt("%zu od %zu piksela ima plohu, najveci otklon od z = -5 je %.2e",
                valid, size_t(sceneWidth) * sceneHeight, worst));
    }

    // -------------------------------------------------------------------------------
    // Poznati rubovi se moraju vratiti kao ti rubovi
    // -------------------------------------------------------------------------------

    //Ovo je test koji dokazuje fx, fy, cx i cy odjednom, i to protiv brojeva koje smo sami
    //upisali - a ne protiv iste formule napisane drugi put
    const float quadZ = -4.0f;
    renderDepth(quad(-1.0f, 2.0f, -0.5f, 1.5f, quadZ));
    const std::vector<uint8_t> known = unprojectWith(intrinsics);

    //Prag se izvodi, ne pogada: rekonstruirana tocka je sredina piksela, pa rub kvadrata
    //moze promasiti najvise za sirinu jednog piksela na toj dubini. Dva za sigurnost
    const float pixelSize = 4.0f / intrinsics.fx;
    const float tolerance = 2.0f * pixelSize;

    const Bounds bounds = boundsOf(known);

    report.check("rubovi po X",
        std::abs(bounds.minX + 1.0f) < tolerance && std::abs(bounds.maxX - 2.0f) < tolerance,
        fmt("rekonstruirano [%.4f, %.4f] za upisano [-1, 2], prag %.4f (dva piksela)",
            bounds.minX, bounds.maxX, tolerance));

    report.check("rubovi po Y",
        std::abs(bounds.minY + 0.5f) < tolerance && std::abs(bounds.maxY - 1.5f) < tolerance,
        fmt("rekonstruirano [%.4f, %.4f] za upisano [-0.5, 1.5]", bounds.minY, bounds.maxY));

    report.check("dubina je ravna",
        std::abs(bounds.minZ + 4.0f) < 1e-3f && std::abs(bounds.maxZ + 4.0f) < 1e-3f,
        fmt("z u [%.5f, %.5f] za upisano -4", bounds.minZ, bounds.maxZ));

    //Pozadina mora ostati pozadina. Kvadrat pokriva dio kadra, ostalo nije ploha - i svjetlo
    //to ne smije obasjati
    report.check("pozadina nije ploha",
        bounds.count > 0 && bounds.count < size_t(sceneWidth) * sceneHeight,
        fmt("%zu od %zu piksela ima plohu, ostatak je oznacen kao prazan",
            bounds.count, size_t(sceneWidth) * sceneHeight));

    // -------------------------------------------------------------------------------
    // I onaj koji MORA pasti: kriva zarisna daje krive tocke
    // -------------------------------------------------------------------------------

    //Ista dubina, druge intrinsike. Bez ove provjere bi prosla i implementacija koja
    //intrinsike primi pa ih ignorira - tocno bug koji nas je vec jednom prevario
    CameraConfig narrowConfig = cameraConfig;
    narrowConfig.fovY = glm::radians(20.0f);
    Camera narrow(narrowConfig);
    const CameraIntrinsics wrong =
        CameraIntrinsics::fromProjection(narrow.getProjection(sceneWidth, sceneHeight), sceneWidth, sceneHeight);

    const std::vector<uint8_t> withWrong = unprojectWith(wrong);
    const Bounds wrongBounds = boundsOf(withWrong);

    const float trueWidth = bounds.maxX - bounds.minX;
    const float wrongWidth = wrongBounds.maxX - wrongBounds.minX;

    report.check("kriva zarisna daje krivu sirinu",
        std::abs(wrongWidth - trueWidth) > 20.0f * tolerance,
        fmt("kroz 45 stupnjeva kvadrat je sirok %.4f, kroz 20 stupnjeva %.4f - upisano je 3",
            trueWidth, wrongWidth));

    //A dubina se NE smije promijeniti: linearizacija ne zna za zarisnu. Dvije polovice
    //racuna su neovisne, i to se ovdje i vidi
    report.check("a dubina se ne mice",
        std::abs(wrongBounds.minZ - bounds.minZ) < 1e-5f &&
        std::abs(wrongBounds.maxZ - bounds.maxZ) < 1e-5f,
        fmt("z ostaje [%.5f, %.5f] iako je zarisna druga", wrongBounds.minZ, wrongBounds.maxZ));

    report.checkNoValidationMessages();
    return report.result();
}
