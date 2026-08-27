// 1b: normale iz tocaka.
//
// Svjetlo treba znati kamo je ploha okrenuta - bez toga je Blinn-Phong slijep i sve ispadne
// jednako svijetlo. Iz slike tocaka se normala dobiva vektorskim mnozenjem dviju tangenti, i
// racun je trivijalan. Sav posao je u tome kojeg susjeda uzeti.
//
// Zato se ovdje mjere dvije razlicite stvari:
//   1. je li normala TOCNA na plohi poznatog nagiba - protiv kuta koji smo sami zadali
//   2. sto se dogada na SILUETI, gdje naivna razlika uzme susjeda s druge plohe
//
// Druga je ona zbog koje je shader slozeniji nego sto bi morao biti, pa se i mjeri protiv
// naivne varijante izracunate ovdje na CPU-u. Ista aritmetika nad istim tockama - razlikuje
// se samo izbor susjeda, sto je tocno ono sto se tvrdi.
#include "TestHarness.h"
#include "TestScene.h"
#include "Core/LoomConfig.h"
#include "Core/Camera.h"
#include "Core/CameraIntrinsics.h"
#include "Vulkan/Mesh.h"
#include "Vulkan/NormalMap.h"
#include "Vulkan/PositionMap.h"
#include "Vulkan/RenderTarget.h"

#include <cmath>
#include <glm/gtc/matrix_transform.hpp>

namespace{

const uint32_t sceneWidth = 640;
const uint32_t sceneHeight = 480;

std::vector<Vertex> quad(float half){
    return {
        {{-half, -half, 0.0f}, {1,1,1}, {0,0}, {0,0,1}},
        {{ half, -half, 0.0f}, {1,1,1}, {1,0}, {0,0,1}},
        {{ half,  half, 0.0f}, {1,1,1}, {1,1}, {0,0,1}},
        {{-half,  half, 0.0f}, {1,1,1}, {0,1}, {0,0,1}},
    };
}

const glm::vec4& at(const std::vector<uint8_t>& pixels, uint32_t x, uint32_t y){
    return reinterpret_cast<const glm::vec4*>(pixels.data())[size_t(y) * sceneWidth + x];
}

float angleBetween(const glm::vec3& a, const glm::vec3& b){
    return glm::degrees(std::acos(glm::clamp(glm::dot(a, b), -1.0f, 1.0f)));
}

struct AngleError{
    float worst = 0.0f;
    double sum = 0.0;
    size_t count = 0;
    float mean() const {return count ? float(sum / double(count)) : 0.0f;}
};

//Odstupanje od poznate normale, po svim pikselima na kojima ploha postoji
AngleError errorAgainst(const std::vector<uint8_t>& normals, const glm::vec3& truth){
    AngleError out;
    for(uint32_t y = 0; y < sceneHeight; ++y){
        for(uint32_t x = 0; x < sceneWidth; ++x){
            const glm::vec4& n = at(normals, x, y);
            if(n.w < 0.5f) continue;
            const float angle = angleBetween(glm::vec3(n), truth);
            out.worst = std::max(out.worst, angle);
            out.sum += angle;
            ++out.count;
        }
    }
    return out;
}

}

int main(){
    TestReport report("1b normale");

    CameraConfig cameraConfig;
    cameraConfig.position = {0.0f, 0.0f, 0.0f};
    cameraConfig.target = {0.0f, 0.0f, -1.0f};
    cameraConfig.fovY = glm::radians(45.0f);
    cameraConfig.nearPlane = 0.1f;
    cameraConfig.farPlane = 100.0f;
    Camera camera(cameraConfig);

    LoomConfig config;
    config.width = sceneWidth; config.height = sceneHeight;
    config.appName = "normals"; config.engineName = "Loom tests";
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

    RenderTargetConfig targetConfig;
    targetConfig.keepDepth = true;
    targetConfig.depthCompare = false;
    RenderTarget target(loom.device, vk::Extent2D{sceneWidth, sceneHeight}, targetConfig);

    const CameraIntrinsics intrinsics =
        CameraIntrinsics::fromProjection(camera.getProjection(sceneWidth, sceneHeight), sceneWidth, sceneHeight);

    //Tocke ostaju gdje ih normale mogu semplirati
    PositionMap positions(loom.device, vk::Extent2D{sceneWidth, sceneHeight});
    positions.setDepthSource(loom.getDescriptorPool(), target);
    positions.setIntrinsics(intrinsics);

    NormalMapConfig normalConfig;
    normalConfig.finalLayout = vk::ImageLayout::eTransferSrcOptimal;
    NormalMap normals(loom.device, vk::Extent2D{sceneWidth, sceneHeight}, normalConfig);
    normals.setPositionSource(loom.getDescriptorPool(), positions);

    const Mesh plane(loom.device, loom.command, quad(20.0f), {0,1,2, 2,3,0});

    //Nacrta zadane plohe, pa kroz tocke izvede normale. Sve u jednom frameu, jer prolaz
    //ostavlja dubinu tocno tamo gdje je compute ocekuje
    auto renderNormals = [&](const std::vector<glm::mat4>& models){
        if(loom.renderer.beginFrame()){
            loom.renderer.beginPass(target);
            for(const glm::mat4& model : models){
                loom.renderer.draw(plane, model);
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
            loom.renderer.endFrame();
        }
        loom.waitIdle();
        return normals.readPixels(loom.command).pixels;
    };

    // -------------------------------------------------------------------------------
    // Ploha okrenuta prema kameri
    // -------------------------------------------------------------------------------

    {
        const std::vector<uint8_t> facing =
            renderNormals({glm::translate(glm::mat4(1.0f), {0.0f, 0.0f, -5.0f})});

        const AngleError error = errorAgainst(facing, {0.0f, 0.0f, 1.0f});

        const float limit = depthPrecisionAngle(5.0f, intrinsics);

        report.check("ploha prema kameri daje (0,0,1)",
            error.count == size_t(sceneWidth) * sceneHeight && error.worst < limit,
            fmt("%zu od %zu piksela, najgore odstupanje %.4f stupnjeva uz izvedeni prag %.4f, prosjek %.4f",
                error.count, size_t(sceneWidth) * sceneHeight, error.worst, limit, error.mean()));
    }

    // -------------------------------------------------------------------------------
    // Nagnuta ploha - kut koji smo sami zadali
    // -------------------------------------------------------------------------------

    //Rotacija oko Y za 30 stupnjeva okrece normalu (0,0,1) u (sin30, 0, cos30). Nista se ne
    //procjenjuje: znamo tocno sto mora ispasti
    {
        const float tilt = glm::radians(30.0f);
        const glm::mat4 model = glm::translate(glm::mat4(1.0f), {0.0f, 0.0f, -6.0f})
                              * glm::rotate(glm::mat4(1.0f), tilt, {0.0f, 1.0f, 0.0f});

        const std::vector<uint8_t> tilted = renderNormals({model});
        const glm::vec3 truth = {std::sin(tilt), 0.0f, std::cos(tilt)};
        const AngleError error = errorAgainst(tilted, truth);

        const float limit = depthPrecisionAngle(6.0f, intrinsics);

        report.check("nagib oko Y",
            error.worst < limit,
            fmt("trazeno (%.3f, %.3f, %.3f), najgore odstupanje %.4f stupnjeva uz izvedeni prag %.4f, prosjek %.4f",
                truth.x, truth.y, truth.z, error.worst, limit, error.mean()));
    }

    //I oko obje osi odjednom, jer bi zamijenjene osi prosle prvi test i pali ovaj
    {
        const float tiltY = glm::radians(25.0f);
        const float tiltX = glm::radians(-18.0f);
        const glm::mat4 rotation = glm::rotate(glm::mat4(1.0f), tiltY, {0.0f, 1.0f, 0.0f})
                                 * glm::rotate(glm::mat4(1.0f), tiltX, {1.0f, 0.0f, 0.0f});
        const glm::mat4 model = glm::translate(glm::mat4(1.0f), {0.0f, 0.0f, -7.0f}) * rotation;

        const std::vector<uint8_t> tilted = renderNormals({model});
        const glm::vec3 truth = glm::normalize(glm::vec3(rotation * glm::vec4(0,0,1,0)));
        const AngleError error = errorAgainst(tilted, truth);

        const float limit = depthPrecisionAngle(7.0f, intrinsics);

        report.check("nagib oko obje osi",
            error.worst < limit,
            fmt("trazeno (%.3f, %.3f, %.3f), najgore odstupanje %.4f stupnjeva uz izvedeni prag %.4f",
                truth.x, truth.y, truth.z, error.worst, limit));
    }

    // -------------------------------------------------------------------------------
    // Silueta: dvije plohe, ista normala, ostar rub izmedu njih
    // -------------------------------------------------------------------------------

    //Obje su okrenute prema kameri, pa je tocna normala na SVAKOM pikselu ista - (0,0,1).
    //Svako odstupanje dolazi iskljucivo od skoka u dubini, i nema se na sto drugo svaliti
    const glm::mat4 nearHalf = glm::translate(glm::mat4(1.0f), {-3.0f, 0.0f, -4.0f});
    const glm::mat4 farHalf  = glm::translate(glm::mat4(1.0f), { 6.0f, 0.0f, -8.0f});

    const std::vector<uint8_t> stepped = renderNormals({
        nearHalf * glm::scale(glm::mat4(1.0f), glm::vec3(0.15f)),
        farHalf  * glm::scale(glm::mat4(1.0f), glm::vec3(0.30f))});

    //Iste tocke, ali procitane natrag - da naivna varijanta racuna iz istog materijala
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
    const std::vector<uint8_t> points = readable.readPixels(loom.command).pixels;

    //Naivno: uvijek susjed naprijed, bez obzira je li on na istoj plohi
    auto naiveNormalAt = [&](uint32_t x, uint32_t y, glm::vec3& out){
        const glm::vec4& center = at(points, x, y);
        const glm::vec4& right = at(points, x + 1, y);
        const glm::vec4& below = at(points, x, y + 1);
        if(center.w < 0.5f || right.w < 0.5f || below.w < 0.5f) return false;

        const glm::vec3 alongX = glm::vec3(right) - glm::vec3(center);
        const glm::vec3 alongY = glm::vec3(below) - glm::vec3(center);
        const glm::vec3 normal = glm::cross(alongY, alongX);
        if(glm::length(normal) < 1e-12f) return false;

        out = glm::normalize(normal);
        return true;
    };

    const glm::vec3 truth{0.0f, 0.0f, 1.0f};
    const float limit = 5.0f;

    size_t naiveBad = 0, ourBad = 0, compared = 0;
    for(uint32_t y = 0; y + 1 < sceneHeight; ++y){
        for(uint32_t x = 0; x + 1 < sceneWidth; ++x){
            glm::vec3 naive;
            if(!naiveNormalAt(x, y, naive)) continue;

            const glm::vec4& ours = at(stepped, x, y);
            if(ours.w < 0.5f) continue;

            ++compared;
            if(angleBetween(naive, truth) > limit) ++naiveBad;
            if(angleBetween(glm::vec3(ours), truth) > limit) ++ourBad;
        }
    }

    //Prvo kontrola: naivna varijanta MORA pasti, inace scena nema siluetu i nista se ne
    //mjeri. Bez ovoga bi test prosao i nad praznom slikom
    report.check("naivna razlika se lomi na rubu", naiveBad > 0,
        fmt("%zu od %zu piksela promasi vise od %.0f stupnjeva kad se uvijek gleda naprijed",
            naiveBad, compared, limit));

    report.check("izbor susjeda drzi rub", ourBad * 50 < naiveBad,
        fmt("nas izbor promasi %zu, naivni %zu - %.1fx manje",
            ourBad, naiveBad, naiveBad ? double(naiveBad) / double(std::max<size_t>(ourBad, 1)) : 0.0));

    //I da rub nije "popravljen" tako da je pola slike proglaseno praznim
    report.check("ploha nije nestala", compared > size_t(sceneWidth) * sceneHeight / 20,
        fmt("%zu piksela ima plohu i sa svim susjedima", compared));

    report.checkNoValidationMessages();
    return report.result();
}
