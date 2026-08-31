// 5a i 5b: boja svjetla mijenja ton, a ne svjetlinu.
//
// Svjetlo je i dosad imalo boju - tri broja koja mnoze svaki kanal. Ali svaki ton koji nije
// bijel time ujedno oduzima svjetla, pa se dvije boje ne daju usporediti: razlika u tonu nosi
// i razliku u svjetlini. To je ista zamka zbog koje se zrnatost polusjene morala mjeriti na
// faktoru vidljivosti umjesto na zatamnjenju - metrika ne smije nositi dvije stvari odjednom.
//
// Ovdje se mjere dvije stvari:
//
//   5a  boja iz Kelvina nosi tocno jedinicnu luminanciju, po konstrukciji a ne slucajno, pa
//       --kelvin mijenja iskljucivo ton
//   5b  isto vrijedi i za ton postavljen rukom, ako se normalizira - i NE vrijedi bez toga
//
// Uz svaku tvrdnju "isto je" stoji i jedna koja trazi da se nesto MORA razlikovati: na sivoj
// plohi je dodano svjetlo isto pod svakom temperaturom, ali na PLAVOJ ne smije biti - inace
// bi provjera prolazila i nad slikom u kojoj boja svjetla ne radi nista.
#include "TestHarness.h"
#include "TestScene.h"
#include "Core/LoomConfig.h"
#include "Core/Camera.h"
#include "Core/CameraIntrinsics.h"
#include "Core/ColorTemperature.h"
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

const glm::vec4& at(const std::vector<uint8_t>& pixels, size_t index){
    return reinterpret_cast<const glm::vec4*>(pixels.data())[index];
}

glm::vec3 rgb(const glm::vec4& v){ return glm::vec3(v.r, v.g, v.b); }

//Koliko je svjetla dodano nekoj skupini piksela, u luminanciji i po kanalu. Mjeri se razlika
//prema snimci, jer je snimka pocetna vrijednost - ono sto je vec bilo tamo nije nase svjetlo
struct Added{
    glm::dvec3 perChannel{0.0};
    double luma = 0.0;
    size_t pixels = 0;
};

Added addedOver(const std::vector<uint8_t>& before, const std::vector<uint8_t>& after,
                const std::vector<bool>& mask, float minimum = 0.0f){
    Added out;
    for(size_t i = 0; i < mask.size(); ++i){
        if(!mask[i]) continue;
        const glm::vec3 delta = rgb(at(after, i)) - rgb(at(before, i));

        //Prag postoji za zrcalni clan: on je na vecini plohe nula, pa bi prosjek preko cijele
        //plohe mjerio uglavnom prazno mjesto umjesto samog odsjaja
        if(delta.r + delta.g + delta.b <= minimum) continue;
        out.perChannel += glm::dvec3(delta);
        out.luma += double(luminance(delta));
        ++out.pixels;
    }
    if(out.pixels > 0){
        out.perChannel /= double(out.pixels);
        out.luma /= double(out.pixels);
    }
    return out;
}

double spread(const glm::vec3& c){
    return double(std::max(c.r, std::max(c.g, c.b))) / double(std::max(std::min(c.r, std::min(c.g, c.b)), 1e-9f));
}

}

int main(){
    TestReport report("5a/5b boja svjetla");

    // ---------------------------------------------------------------------------------
    // 5a: sama pretvorba, prije nego se ista nacrta
    // ---------------------------------------------------------------------------------

    //Luminancija je zadana kao 1 pri pretvorbi (Y = 1 u XYZ), pa ovo nije mjerenje nego
    //provjera da matrica XYZ -> sRGB to zaista cuva. Ako se raziđu, temperatura tiho mijenja
    //ekspoziciju i sve ostalo ispod je bezvrijedno
    double worstLuma = 0.0;
    float worstAt = 0.0f;
    for(float kelvin = 2000.0f; kelvin <= 20000.0f; kelvin += 100.0f){
        const double error = std::abs(double(luminance(colorFromKelvin(kelvin))) - 1.0);
        if(error > worstLuma){ worstLuma = error; worstAt = kelvin; }
    }

    report.check("Kelvin nosi jedinicnu luminanciju",
        worstLuma < 1e-3,
        fmt("najvece odstupanje %.2e na %.0f K, kroz 2000-20000 K", worstLuma, worstAt));

    //A da ton ipak radi: omjer crvenog i plavog mora padati sa svakom stotinom Kelvina.
    //Bez ove provjere bi gornja prolazila i da pretvorba vraca bijelo na svakoj temperaturi
    bool monotone = true;
    double previous = 1e30;
    for(float kelvin = 2000.0f; kelvin <= 20000.0f; kelvin += 100.0f){
        const glm::vec3 c = colorFromKelvin(kelvin);
        const double ratio = double(c.r) / double(std::max(c.b, 1e-9f));
        if(ratio >= previous) monotone = false;
        previous = ratio;
    }

    const glm::vec3 tungsten = colorFromKelvin(3200.0f);
    const glm::vec3 daylight = colorFromKelvin(10000.0f);

    report.check("a ton se mijenja monotono",
        monotone && tungsten.r / tungsten.b > 2.0f,
        fmt("R/B pada kroz cijeli raspon; 3200 K ima %.2f, 10000 K %.2f",
            double(tungsten.r / tungsten.b), double(daylight.r / daylight.b)));

    //POSTENO O 6500 K. Blizu je neutralnog, ali nije bijelo, i to nije greska aproksimacije:
    //D65 je dnevno svjetlo, a dnevno svjetlo nije crno tijelo - Planckov lokus kroz bijelu
    //tocku sRGB-a ne prolazi. Dvije provjere zajedno, jer svaka sama dopusta krivi zakljucak
    const double spread6500 = spread(colorFromKelvin(6500.0f));
    report.check("6500 K je blizu neutralnog",
        spread6500 < 1.10,
        fmt("najjaci kanal je %.1f%% iznad najslabijeg", 100.0 * (spread6500 - 1.0)));

    report.check("ali nije sRGB bijelo",
        spread6500 > 1.03,
        fmt("razmak %.4f; kanali se izjednace tek oko 6530 K, i ni tamo nisu jednaki", spread6500));

    // ---------------------------------------------------------------------------------
    // Scena: dvije plohe, jedna siva i jedna plava, snimljene pod ravnim ambijentom
    // ---------------------------------------------------------------------------------

    CameraConfig cameraConfig;
    cameraConfig.position = {0.0f, 0.0f, 3.0f};
    cameraConfig.target = {0.0f, 0.0f, 0.0f};
    cameraConfig.fovY = glm::radians(50.0f);
    cameraConfig.nearPlane = 0.1f;
    cameraConfig.farPlane = 60.0f;
    Camera camera(cameraConfig);

    LoomConfig config;
    config.width = sceneWidth; config.height = sceneHeight;
    config.appName = "light tint"; config.engineName = "Loom tests";
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

    //Ravan bijeli ambijent i nijedno svjetlo: piksel snimke je tocno albedo. Samo u toj
    //postavci se dodano svjetlo da usporediti s racunom umjesto s dojmom
    EnvironmentConfig flat;
    flat.ambientColor = {1.0f, 1.0f, 1.0f};
    Environment environment(flat);
    loom.renderer.setEnvironment(environment);

    //Siva ploha ne moze razlikovati tonove iste luminancije - plava moze. Zato ih ima dvije
    const glm::vec3 greyColor = {0.50f, 0.50f, 0.50f};
    const glm::vec3 blueColor = {0.10f, 0.35f, 0.90f};

    Mesh greyQuad(loom.device, loom.command, quad(0.6f, greyColor), std::vector<uint16_t>{0,1,2, 2,3,0});
    Mesh blueQuad(loom.device, loom.command, quad(0.6f, blueColor), std::vector<uint16_t>{0,1,2, 2,3,0});

    //Treca ploha je CRNA i stoji 12 cm ispred plave. Radi dvije stvari odjednom: ploha koja
    //ne odbija nista je jedini nacin da se vidi mnozi li snimka ili se samo zbraja, a stepenica
    //u dubini je jedino sto ambijentnoj okluziji uopce daje sto raditi - dvije koplanarne
    //plohe joj ne daju nikakvu razliku u dubini
    Mesh blackQuad(loom.device, loom.command, quad(0.25f, glm::vec3(0.0f)), std::vector<uint16_t>{0,1,2, 2,3,0});

    const glm::mat4 leftModel = glm::translate(glm::mat4(1.0f), {-0.7f, 0.0f, 0.0f});
    const glm::mat4 rightModel = glm::translate(glm::mat4(1.0f), { 0.7f, 0.0f, 0.0f});
    const glm::mat4 frontModel = glm::translate(glm::mat4(1.0f), { 0.7f, -0.35f, 0.12f});

    auto drawScene = [&](){
        loom.renderer.draw(greyQuad, leftModel);
        loom.renderer.draw(blueQuad, rightModel);
        loom.renderer.draw(blackQuad, frontModel);
    };

    RenderTargetConfig readable;
    readable.colorFormat = sceneFormat;
    readable.finalLayout = vk::ImageLayout::eTransferSrcOptimal;
    readable.extraColorUsage = vk::ImageUsageFlagBits::eTransferSrc | vk::ImageUsageFlagBits::eSampled;
    readable.keepDepth = true;
    readable.depthCompare = false;

    RenderTarget plateTarget(loom.device, vk::Extent2D{sceneWidth, sceneHeight}, readable);
    RenderTarget outTarget(loom.device, vk::Extent2D{sceneWidth, sceneHeight}, readable);

    if(loom.renderer.beginFrame()){
        loom.renderer.beginPass(plateTarget);
        drawScene();
        loom.renderer.endPass();
        loom.renderer.endFrame();
    }
    loom.waitIdle();
    const std::vector<uint8_t> plate = plateTarget.readPixels(loom.command).pixels;
    loom.command.transitionImageLayout(plateTarget.getColorImage(), vk::ImageLayout::eShaderReadOnlyOptimal);

    // ---------------------------------------------------------------------------------
    // Kompozicija: svjetlo ubaceno u tu snimku
    // ---------------------------------------------------------------------------------

    const CameraIntrinsics intrinsics =
        CameraIntrinsics::fromProjection(camera.getProjection(sceneWidth, sceneHeight), sceneWidth, sceneHeight);

    PositionMap positions(loom.device, vk::Extent2D{sceneWidth, sceneHeight});
    positions.setDepthSource(loom.getDescriptorPool(), plateTarget);
    positions.setIntrinsics(intrinsics);

    NormalMap normals(loom.device, vk::Extent2D{sceneWidth, sceneHeight});
    normals.setPositionSource(loom.getDescriptorPool(), positions);

    //Crna ploha i prazan kadar su u snimci isti piksel - oboje je nula. Razlikuje ih jedino
    //dubina, pa se tocke citaju iz iste karte kojom se i sjenca
    const std::vector<uint8_t> points = [&]{
        PositionMap out(loom.device, vk::Extent2D{sceneWidth, sceneHeight},
                        PositionMapConfig{vk::ImageLayout::eTransferSrcOptimal});
        out.setDepthSource(loom.getDescriptorPool(), plateTarget);
        out.setIntrinsics(intrinsics);
        if(loom.renderer.beginFrame()){
            const UnprojectPush push = out.makePush();
            loom.renderer.dispatch(out.getComputeMaterial(), out.groupsX(), out.groupsY(), 1,
                                   &push, sizeof(push));
            loom.renderer.endFrame();
        }
        loom.waitIdle();
        return out.readPixels(loom.command).pixels;
    }();

    RelightConfig relightConfig;
    relightConfig.colorFormat = sceneFormat;
    relightConfig.depthFormat = outTarget.getDepthFormat();
    relightConfig.surface.baseColor = {1.0f, 1.0f, 1.0f, 1.0f};
    relightConfig.surface.shininess = 32.0f;

    //Zrcalni clan se NAMJERNO gasi: on se ne mnozi bojom plohe, pa bi na plavoj plohi vratio
    //dio one razlike koju ovaj test trazi. Tvrdnja je ovdje o difuznom odgovoru
    relightConfig.surface.specularStrength = 0.0f;

    Relight relight(loom.device, loom.command, loom.getDescriptorPool(),
                    positions, normals, plateTarget.getSampled(), relightConfig);
    relight.setCamera(camera);

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

    //Svjetlo se postavlja PRIJE beginFrame - inace kadar nosi jucerasnje svjetlo, sto je
    //vlastiti test
    LightConfig base;
    base.type = LightType::Point;
    base.position = {1.6f, 1.2f, 1.6f};
    base.intensity = 8.0f;
    base.range = 20.0f;

    auto litWith = [&](const glm::vec3& color, bool normalize){
        LightConfig cfg = base;
        cfg.color = color;
        cfg.normalizeColor = normalize;
        Light light(cfg);
        loom.renderer.clearLights();
        loom.renderer.addLight(light);
        const std::vector<uint8_t> pixels = composite();
        loom.renderer.clearLights();
        return pixels;
    };

    //Plohe se razdvajaju po samoj snimci, ne po geometriji: siva je ona kojoj su kanali
    //jednaki, plava ona kojoj plavi vodi
    std::vector<bool> greyMask(size_t(sceneWidth) * sceneHeight, false);
    std::vector<bool> blueMask(greyMask.size(), false);
    std::vector<bool> blackMask(greyMask.size(), false);
    for(size_t i = 0; i < greyMask.size(); ++i){
        const glm::vec3 c = rgb(at(plate, i));
        const bool surface = at(points, i).w > 0.5f;
        if(std::abs(c.r - c.g) < 1e-4f && std::abs(c.g - c.b) < 1e-4f && c.r > 0.1f) greyMask[i] = true;
        else if(c.b > c.r + 0.2f) blueMask[i] = true;
        else if(surface && c.r == 0.0f && c.g == 0.0f && c.b == 0.0f) blackMask[i] = true;
    }

    size_t greyPixels = 0, bluePixels = 0, blackPixels = 0;
    for(size_t i = 0; i < greyMask.size(); ++i){
        greyPixels += greyMask[i]; bluePixels += blueMask[i]; blackPixels += blackMask[i];
    }

    report.check("sve tri plohe su u kadru",
        greyPixels > 5000 && bluePixels > 5000 && blackPixels > 2000,
        fmt("%zu sivih, %zu plavih, %zu crnih piksela", greyPixels, bluePixels, blackPixels));

    // ---------------------------------------------------------------------------------
    // 5b: ista jacina, tri temperature
    // ---------------------------------------------------------------------------------

    const std::vector<uint8_t> warm = litWith(colorFromKelvin(2700.0f), true);
    const std::vector<uint8_t> neutral = litWith(colorFromKelvin(6500.0f), true);
    const std::vector<uint8_t> cool = litWith(colorFromKelvin(10000.0f), true);

    const Added greyWarm = addedOver(plate, warm, greyMask);
    const Added greyNeutral = addedOver(plate, neutral, greyMask);
    const Added greyCool = addedOver(plate, cool, greyMask);

    const double greyLow = std::min(greyWarm.luma, std::min(greyNeutral.luma, greyCool.luma));
    const double greyHigh = std::max(greyWarm.luma, std::max(greyNeutral.luma, greyCool.luma));

    report.check("temperatura ne mijenja svjetlinu",
        greyLow > 1e-4 && (greyHigh - greyLow) / greyHigh < 0.005,
        fmt("dodana luminancija 2700 K %.6f, 6500 K %.6f, 10000 K %.6f - razmak %.3f %%",
            greyWarm.luma, greyNeutral.luma, greyCool.luma, 100.0 * (greyHigh - greyLow) / greyHigh));

    //A da to nije zato sto boja ne radi nista: na istoj toj sivoj plohi se omjer kanala mora
    //razlikovati koliko i same boje
    const double warmRatio = greyWarm.perChannel.r / std::max(greyWarm.perChannel.b, 1e-9);
    const double coolRatio = greyCool.perChannel.r / std::max(greyCool.perChannel.b, 1e-9);

    report.check("a ton jest drugi",
        warmRatio / coolRatio > 5.0,
        fmt("dodani R/B je %.2f na 2700 K i %.2f na 10000 K - %.1f puta",
            warmRatio, coolRatio, warmRatio / coolRatio));

    //I plava ploha, koja to MORA osjetiti: pod toplim svjetlom odbija manje, jer joj crvenog
    //nema. Bez ove provjere bi gornja "svjetlina se ne mijenja" prolazila i nad slikom u kojoj
    //boja svjetla nigdje ne stize do racuna
    const Added blueWarm = addedOver(plate, warm, blueMask);
    const Added blueCool = addedOver(plate, cool, blueMask);
    const double blueChange = blueCool.luma / std::max(blueWarm.luma, 1e-9);

    report.check("ali boja plohe to osjeti",
        blueChange > 1.15,
        fmt("plava ploha dobiva %.6f pod 2700 K i %.6f pod 10000 K - %.2f puta vise",
            blueWarm.luma, blueCool.luma, blueChange));

    // ---------------------------------------------------------------------------------
    // Ton rukom: bez normalizacije mijenja i svjetlinu, s njom ne
    // ---------------------------------------------------------------------------------

    //Trojac koji je do danas stajao zakucan u LoomAppu
    const glm::vec3 bulb = {1.0f, 0.82f, 0.55f};
    const double bulbLuma = double(luminance(bulb));

    const std::vector<uint8_t> white = litWith({1.0f, 1.0f, 1.0f}, false);
    const std::vector<uint8_t> rawTint = litWith(bulb, false);
    const std::vector<uint8_t> evenTint = litWith(bulb, true);

    const double whiteLuma = addedOver(plate, white, greyMask).luma;
    const double rawRatio = addedOver(plate, rawTint, greyMask).luma / whiteLuma;
    const double evenRatio = addedOver(plate, evenTint, greyMask).luma / whiteLuma;

    report.check("ton rukom inace zatamni kadar",
        std::abs(rawRatio - bulbLuma) < 0.01,
        fmt("ista jacina daje %.4f puta manje svjetla, a luminancija tona je %.4f",
            rawRatio, bulbLuma));

    report.check("normalizacija to mice",
        std::abs(evenRatio - 1.0) < 0.005,
        fmt("isti ton normaliziran daje %.4f puta svjetla bijelog", evenRatio));

    // ---------------------------------------------------------------------------------
    // 5c: kamo boja svjetla smije, a kamo ne
    // ---------------------------------------------------------------------------------

    //Snimka MNOZI novo svjetlo, pa ploha koja u njoj ne odbija nista ne moze zasvijetliti - i
    //to ne smije ovisiti o tome koje je svjetlo boje. Tri tona, ista crna ploha, ista nula
    size_t blackChanged = 0;
    for(size_t i = 0; i < blackMask.size(); ++i){
        if(!blackMask[i]) continue;
        for(const std::vector<uint8_t>* image : {&warm, &neutral, &cool}){
            if(rgb(at(*image, i)) != rgb(at(plate, i))){ ++blackChanged; break; }
        }
    }

    report.check("crna ostaje crna pod svakim tonom",
        blackPixels > 0 && blackChanged == 0,
        fmt("%zu crnih piksela kroz 2700, 6500 i 10000 K, promijenilo se %zu",
            blackPixels, blackChanged));

    //...ali samo dok ploha ne odsjaji, i to je posteno reci naglas. Zrcalni clan se NE mnozi
    //bojom plohe, jer odsjaj dielektrika nije obojen bojom plohe nego bojom svjetla. Zato
    //crna ploha pod odsjajem dobiva TOCNO ton svjetla - sto je ujedno par koji trazi da se
    //gornja provjera ne moze proci tako da boja svjetla nigdje ne stigne
    MaterialData glossy;
    glossy.baseColor = {1.0f, 1.0f, 1.0f, 1.0f};
    glossy.shininess = 32.0f;
    glossy.specularStrength = 1.0f;
    relight.setSurface(glossy);

    const glm::vec3 warmColor = colorFromKelvin(2700.0f);
    const double lightRatio = double(warmColor.r) / double(warmColor.b);
    const std::vector<uint8_t> warmGloss = litWith(warmColor, true);

    const Added blackGloss = addedOver(plate, warmGloss, blackMask, 0.002f);
    const double blackRatio = blackGloss.perChannel.r / std::max(blackGloss.perChannel.b, 1e-9);

    report.check("a odsjaj nosi ton svjetla",
        blackGloss.pixels > 500 && std::abs(blackRatio / lightRatio - 1.0) < 0.02,
        fmt("crna ploha pod odsjajem ima R/B %.2f, svjetlo %.2f - na %zu piksela",
            blackRatio, lightRatio, blackGloss.pixels));

    //Ista razlika, ali na plohi koja ima boju: difuz nosi i boju plohe, odsjaj samo boju
    //svjetla. Mjeri se na istim pikselima, pa je jedina razlika u tome sto se cime mnozi
    const Added blueDiffuse = addedOver(plate, warm, blueMask);
    const Added blueSpecular = addedOver(warm, warmGloss, blueMask, 0.002f);

    const double diffuseRatio = blueDiffuse.perChannel.r / std::max(blueDiffuse.perChannel.b, 1e-9);
    const double specularRatio = blueSpecular.perChannel.r / std::max(blueSpecular.perChannel.b, 1e-9);

    //Difuz mora nositi omjer plohe (0.10 / 0.90) puta omjer svjetla, odsjaj samo omjer svjetla
    const double expectedDiffuse = lightRatio * double(blueColor.r) / double(blueColor.b);

    report.check("difuz nosi i boju plohe",
        std::abs(diffuseRatio / expectedDiffuse - 1.0) < 0.02,
        fmt("plava ploha difuzno ima R/B %.3f, a ploha puta svjetlo daje %.3f",
            diffuseRatio, expectedDiffuse));

    report.check("odsjaj na istim pikselima ne",
        blueSpecular.pixels > 500 && std::abs(specularRatio / lightRatio - 1.0) < 0.02,
        fmt("odsjaj na toj istoj plohi ima R/B %.2f, dakle ton svjetla %.2f - %.0f puta dalje "
            "od difuznog", specularRatio, lightRatio, specularRatio / diffuseRatio));

    //I zadnje mjesto na kojem bi se boja mogla uvuci a nema sto tamo traziti: ambijentna
    //okluzija mnozi SAME piksele snimke. Ako bi ta jedna vrijednost ikako ovisila o kanalu,
    //snimka bi se obojila - a okluzija ne dolazi od svjetla nego od geometrije
    MaterialData matte = glossy;
    matte.specularStrength = 0.0f;
    relight.setSurface(matte);

    ScreenOcclusionConfig occlusion;
    occlusion.strength = 0.6f;
    occlusion.scale = 0.05f;
    relight.setOcclusion(occlusion);

    //Bez ijednog svjetla: sve sto se od snimke razlikuje je onda okluzija i nista drugo
    loom.renderer.clearLights();
    const std::vector<uint8_t> shaded = composite();

    double worstTilt = 0.0;
    size_t darkened = 0, checked = 0;
    for(size_t i = 0; i < greyMask.size(); ++i){
        if(!greyMask[i] && !blueMask[i]) continue;

        const glm::dvec3 before = glm::dvec3(rgb(at(plate, i)));
        const glm::dvec3 after = glm::dvec3(rgb(at(shaded, i)));
        const glm::dvec3 ratio = after / before;

        const double tilt = std::max(ratio.r, std::max(ratio.g, ratio.b))
                          - std::min(ratio.r, std::min(ratio.g, ratio.b));
        worstTilt = std::max(worstTilt, tilt);
        if(ratio.g < 0.999) ++darkened;
        ++checked;
    }

    report.check("okluzija mnozi, ali ne boji",
        checked > 0 && worstTilt < 1e-5,
        fmt("najveci razmak medju kanalima istog piksela %.2e, na %zu piksela", worstTilt, checked));

    report.check("a stvarno je potamnila",
        darkened > 1000,
        fmt("%zu od %zu piksela je okluzija spustila", darkened, checked));

    report.checkNoValidationMessages();
    return report.result();
}
