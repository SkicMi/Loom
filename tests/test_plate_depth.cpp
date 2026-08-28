// 2a: dubina ulazi izvana.
//
// Ovo je spoj dviju grana. Do sad je odprojekcija citala Loomov vlastiti depth buffer; sad
// cita kartu iz filea - onakvu kakvu daje model za procjenu dubine nad tudom snimkom.
//
// Dokaz je zatvoren krug, i to je ovdje moguce jer JEDAN od ta dva puta vec znamo da je
// tocan. Loom nacrta scenu, iz njene dubine napravi tocke (put dokazan u 1a), pa se ta ista
// dubina izveze u file kao disparitet, ucita natrag i pusti kroz DRUGI put. Ako se tocke
// poklope, novi put je tocan koliko i stari - i to bez ijednog modela.
//
// Disparitet a ne udaljenost, jer je to ono sto modeli daju: broj proporcionalan RECIPROCNOJ
// udaljenosti, blize je vece. Da se interpolira udaljenost umjesto njene reciprocne
// vrijednosti, scena bi ispala rastegnuta u daljini i zbijena blizu - i nista u njoj ne bi
// izgledalo kao greska.
#include "TestHarness.h"
#include "TestScene.h"
#include "Core/LoomConfig.h"
#include "Core/Camera.h"
#include "Core/CameraIntrinsics.h"
#include "Vulkan/Mesh.h"
#include "Vulkan/PositionMap.h"
#include "Vulkan/RenderTarget.h"
#include "Vulkan/Texture.h"

#include <Spool/DepthFile.h>

#include <cmath>
#include <filesystem>
#include <glm/gtc/matrix_transform.hpp>

namespace{

const uint32_t sceneWidth = 320;
const uint32_t sceneHeight = 240;

std::vector<Vertex> quad(float half){
    return {
        {{-half, -half, 0.0f}, {1,1,1}, {0,0}, {0,0,1}},
        {{ half, -half, 0.0f}, {1,1,1}, {1,0}, {0,0,1}},
        {{ half,  half, 0.0f}, {1,1,1}, {1,1}, {0,0,1}},
        {{-half,  half, 0.0f}, {1,1,1}, {0,1}, {0,0,1}},
    };
}

//Plohe na raznim dubinama i pod raznim kutovima, da raspon dubina bude sirok
std::vector<glm::mat4> panels(){
    return {
        glm::translate(glm::mat4(1.0f), {0.0f, -0.9f, -4.0f})
            * glm::rotate(glm::mat4(1.0f), glm::radians(-70.0f), {1.0f, 0.0f, 0.0f})
            * glm::scale(glm::mat4(1.0f), glm::vec3(9.0f)),

        glm::translate(glm::mat4(1.0f), {-1.0f, 0.3f, -3.0f})
            * glm::rotate(glm::mat4(1.0f), glm::radians(22.0f), {0.0f, 1.0f, 0.0f})
            * glm::scale(glm::mat4(1.0f), glm::vec3(1.6f)),

        glm::translate(glm::mat4(1.0f), {1.4f, 0.4f, -6.5f})
            * glm::scale(glm::mat4(1.0f), glm::vec3(2.2f)),
    };
}

struct Point{ float x, y, z, valid; };

const Point& at(const std::vector<uint8_t>& pixels, size_t index){
    return reinterpret_cast<const Point*>(pixels.data())[index];
}

}

int main(){
    TestReport report("2a dubina izvana");

    const std::filesystem::path work = std::filesystem::temp_directory_path() / "loom_plate_depth";
    std::filesystem::remove_all(work);
    std::filesystem::create_directories(work);

    CameraConfig cameraConfig;
    cameraConfig.position = {0.8f, 0.7f, 2.0f};
    cameraConfig.target = {0.0f, -0.1f, -3.0f};
    cameraConfig.fovY = glm::radians(48.0f);
    cameraConfig.nearPlane = 0.1f;
    cameraConfig.farPlane = 80.0f;
    Camera camera(cameraConfig);

    LoomConfig config;
    config.width = sceneWidth; config.height = sceneHeight;
    config.appName = "plate depth"; config.engineName = "Loom tests";
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

    const Mesh panel(loom.device, loom.command, quad(0.5f), {0,1,2, 2,3,0});
    const std::vector<glm::mat4> models = panels();

    const CameraIntrinsics intrinsics =
        CameraIntrinsics::fromProjection(camera.getProjection(sceneWidth, sceneHeight), sceneWidth, sceneHeight);

    if(loom.renderer.beginFrame()){
        loom.renderer.beginPass(target);
        for(const glm::mat4& model : models) loom.renderer.draw(panel, model);
        loom.renderer.endPass();
        loom.renderer.endFrame();
    }
    loom.waitIdle();

    auto unproject = [&](PositionMap& map){
        if(loom.renderer.beginFrame()){
            const UnprojectPush push = map.makePush();
            loom.renderer.dispatch(map.getComputeMaterial(), map.groupsX(), map.groupsY(), 1,
                                   &push, sizeof(push));
            loom.renderer.endFrame();
        }
        loom.waitIdle();
        return map.readPixels(loom.command).pixels;
    };

    // -------------------------------------------------------------------------------
    // Put koji vec znamo da je tocan
    // -------------------------------------------------------------------------------

    PositionMap fromBuffer(loom.device, vk::Extent2D{sceneWidth, sceneHeight},
                           PositionMapConfig{vk::ImageLayout::eTransferSrcOptimal});
    fromBuffer.setDepthSource(loom.getDescriptorPool(), target);
    fromBuffer.setIntrinsics(intrinsics);

    const std::vector<uint8_t> truth = unproject(fromBuffer);

    // -------------------------------------------------------------------------------
    // Ista dubina, ali kroz file - kao da je dosla iz modela
    // -------------------------------------------------------------------------------

    const ImageData rawDepth = target.readDepthPixels(loom.command);
    const float* stored = reinterpret_cast<const float*>(rawDepth.pixels.data());

    //Raspon se bira tako da obuhvati scenu. Na pravoj snimci to je ono sto covjek procijeni
    //gledajuci je - "najblize je metar i pol, najdalje dvadeset"
    const float nearDistance = 1.0f;
    const float farDistance = 40.0f;

    Spool::DepthImage disparity;
    disparity.width = sceneWidth;
    disparity.height = sceneHeight;
    disparity.sourceBits = 32;
    disparity.values.resize(size_t(sceneWidth) * sceneHeight);

    size_t withSurface = 0;
    for(size_t i = 0; i < disparity.values.size(); ++i){
        const float ndc = stored[i];

        //Nebo: dubina je ostala na vrijednosti brisanja. U dispariteta to je nula - najdalje
        //sto postoji - jer model nad snimkom nikad ne vrati "ovdje nema nicega"
        if(ndc >= 1.0f){
            disparity.values[i] = 0.0f;
            continue;
        }
        ++withSurface;

        //Ista linearizacija koju radi i shader, pa iz udaljenosti u disparitet
        const float n = intrinsics.nearPlane;
        const float f = intrinsics.farPlane;
        const float viewDepth = (f * n) / (f - ndc * (f - n));

        disparity.values[i] = (1.0f / viewDepth - 1.0f / farDistance) /
                              (1.0f / nearDistance - 1.0f / farDistance);
    }

    const std::string path = (work / "depth.pfm").string();
    Spool::saveDepthImage(path, disparity);

    const Spool::DepthImage loaded = Spool::loadDepthImage(path);

    report.check("karta je prosla kroz file",
        loaded.width == sceneWidth && loaded.height == sceneHeight && loaded.sourceBits == 32,
        fmt("%ux%u, %u bita, vrijednosti od %.4f do %.4f",
            loaded.width, loaded.height, loaded.sourceBits, loaded.minValue, loaded.maxValue));

    //Jedan kanal, float, bez mip lanca i bez interpolacije: dubina se ne prosjecuje
    TextureConfig depthTextureConfig;
    depthTextureConfig.format = vk::Format::eR32Sfloat;
    depthTextureConfig.filter = vk::Filter::eNearest;
    depthTextureConfig.addressMode = vk::SamplerAddressMode::eClampToEdge;
    depthTextureConfig.generateMipmaps = false;

    Texture depthTexture(loom.device, loom.command, loaded.values.data(),
                         vk::Extent2D{sceneWidth, sceneHeight}, depthTextureConfig);

    DepthMapping mapping;
    mapping.encoding = DepthEncoding::Disparity;
    mapping.nearDistance = nearDistance;
    mapping.farDistance = farDistance;

    PositionMap fromFile(loom.device, vk::Extent2D{sceneWidth, sceneHeight},
                         PositionMapConfig{vk::ImageLayout::eTransferSrcOptimal});
    fromFile.setPlateDepth(loom.getDescriptorPool(), depthTexture.getSampled(),
                           vk::Extent2D{sceneWidth, sceneHeight}, mapping);
    fromFile.setIntrinsics(intrinsics);

    const std::vector<uint8_t> viaFile = unproject(fromFile);

    // -------------------------------------------------------------------------------
    // Poklapaju li se
    // -------------------------------------------------------------------------------

    float worst = 0.0f, deepest = 0.0f;
    double sum = 0.0;
    size_t compared = 0;

    for(size_t i = 0; i < size_t(sceneWidth) * sceneHeight; ++i){
        const Point& a = at(truth, i);
        if(a.valid < 0.5f) continue;   //nebo: dva puta ga opisuju razlicito, i to je u redu

        const Point& b = at(viaFile, i);
        const float delta = std::max(std::abs(a.x - b.x),
                             std::max(std::abs(a.y - b.y), std::abs(a.z - b.z)));
        worst = std::max(worst, delta);
        deepest = std::max(deepest, -a.z);
        sum += delta;
        ++compared;
    }

    //Prag se izvodi, i izvod mora uracunati da disparitet POJACAVA gresku. Iz njega se
    //udaljenost dobiva kao 1/(a*d + b), pa se apsolutna greska u d mnozi sa Z^2 * a, gdje je
    //a = 1/near - 1/far. Na deset metara i rasponu od jednog metra to je faktor od stotinjak.
    //
    //Greska u samom d nije d*eps nego eps na skali najveceg clana racuna, a to je 1/near.
    //Osam zaokruzivanja je rezerva za sest koje ovaj krug stvarno napravi
    const float span = 1.0f / nearDistance - 1.0f / farDistance;
    const float disparityStep = 1.1920929e-7f / nearDistance;
    const float tolerance = 8.0f * deepest * deepest * span * disparityStep;

    report.check("dubina iz filea daje iste tocke",
        compared > 0 && worst < tolerance,
        fmt("%zu tocaka, najveca razlika %.9f uz izvedeni prag %.9f (najdublja tocka %.2f), prosjek %.11f",
            compared, worst, tolerance, deepest, sum / double(compared)));

    // -------------------------------------------------------------------------------
    // I ono sto se MORA razlikovati
    // -------------------------------------------------------------------------------

    //Kriv raspon je jedini nacin na koji se u ovom koraku moze pogrijesiti tako da slika i
    //dalje izgleda uvjerljivo - scena samo bude dublja ili plica nego sto jest
    {
        DepthMapping wrong = mapping;
        wrong.farDistance = 12.0f;

        fromFile.setMapping(wrong);
        const std::vector<uint8_t> misjudged = unproject(fromFile);

        float worstWrong = 0.0f;
        for(size_t i = 0; i < size_t(sceneWidth) * sceneHeight; ++i){
            const Point& a = at(truth, i);
            if(a.valid < 0.5f) continue;
            worstWrong = std::max(worstWrong, std::abs(a.z - at(misjudged, i).z));
        }

        report.check("kriv raspon daje krivu scenu", worstWrong > 100.0f * tolerance,
            fmt("s rasponom do %.0f m dubina promasi za %.4f, s pravim za %.9f",
                wrong.farDistance, worstWrong, worst));

        fromFile.setMapping(mapping);
    }

    //Disparitet NIJE udaljenost, i to je najlakse krivo napisati: interpolirati udaljenost
    //izmedu blizu i daleko umjesto njene reciprocne vrijednosti. Takva scena i dalje izgleda
    //kao scena - samo je rastegnuta u daljini i zbijena blizu.
    //
    //Zato se ovdje ne trazi samo da smo blizu istine, nego i da smo DALEKO od tog krivog
    //modela. Da su ta dva odgovora slucajno bliska, gornja provjera ne bi razlikovala tocan
    //racun od tog
    {
        float worstAgainstLinear = 0.0f;
        size_t middle = 0;

        for(size_t i = 0; i < size_t(sceneWidth) * sceneHeight; ++i){
            const Point& a = at(truth, i);
            if(a.valid < 0.5f) continue;

            const float distance = -a.z;
            if(distance < 3.0f || distance > 9.0f) continue;
            ++middle;

            //Sto bi linearna interpolacija dala za taj isti disparitet
            const float d = disparity.values[i];
            const float linear = nearDistance + (1.0f - d) * (farDistance - nearDistance);
            worstAgainstLinear = std::max(worstAgainstLinear, std::abs(linear - distance));
        }

        report.check("racuna se reciprocno, ne linearno",
            middle > 0 && worstAgainstLinear > 1.0f && worst < tolerance,
            fmt("na %zu tocaka izmedu 3 i 9 metara linearni bi model promasio za %.2f m, "
                "a nas promasuje za %.9f", middle, worstAgainstLinear, worst));
    }

    report.checkNoValidationMessages();
    std::filesystem::remove_all(work);
    return report.result();
}
