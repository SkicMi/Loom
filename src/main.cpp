// Loom, na tri stepenice odjednom.
//
// Ovo je ista scena kao prije - cetiri oblika na podu, sunce sa sjenama, pokretna zarulja -
// ali napisana onako kako se od sada pise. Vecina je stepenica 1. Ono sto preset ne pokriva
// je svjesni silazak, i on je zapisan jednim includeom na vrhu, vidljivo svakome tko file
// otvori:
//
//   stepenica 1  <Loom/Loom.h>            preset, oblici, teksture, petlja
//   stepenica 2  <Loom/Preset_Advanced.h> config prije upotrebe, i zivi objekti dok rade
//   stepenica 3  ovdje ne treba
//
// Bez tog drugog includea ovaj file ne bi vidio nijedan Vulkan simbol.
#include <Loom/Loom.h>
#include <Loom/Preset_Advanced.h>

#include <cmath>
#include <cstdint>
#include <vector>

static std::vector<uint8_t> makeCheckerboard(uint32_t size, uint32_t cell,
                                             uint8_t light = 235, uint8_t dark = 40){
    std::vector<uint8_t> pixels(size_t(size) * size * 4);
    for(uint32_t y = 0; y < size; ++y){
        for(uint32_t x = 0; x < size; ++x){
            const uint8_t value = (((x / cell) + (y / cell)) % 2) == 0 ? light : dark;
            const size_t i = (size_t(y) * size + x) * 4;
            pixels[i+0] = value; pixels[i+1] = value; pixels[i+2] = value; pixels[i+3] = 255;
        }
    }
    return pixels;
}

int main(){
    //Deklarirana prije scene, pa scena umire prva. Renderer drzi pokazivac na svjetlo, i taj
    //pokazivac ne smije nadzivjeti ono na sto pokazuje
    LightConfig bulbConfig;
    bulbConfig.type = LightType::Point;
    bulbConfig.position = {2.6f, 2.0f, 0.0f};
    bulbConfig.color = {1.0f, 0.35f, 0.15f};
    bulbConfig.intensity = 14.0f;
    bulbConfig.range = 12.0f;
    Light bulb(bulbConfig);

    // -- stepenica 1, s jednim spustom na stepenicu 2 --------------------------------------

    //Preset ispuni config; ovo je trenutak izmedu toga i njegove upotrebe. Nacin
    //prikazivanja nije na stepenici 1 i ne treba biti - a i dalje je dohvatljiv
    Loom::Scene scene(Loom::Preset::Lit3D, [](LoomConfig& config){
        config.swapchainConfig.preferredPresentMode = vk::PresentModeKHR::eMailbox;
    });

    scene.setTitle("Loom");
    scene.setSize(1280, 720);

    const std::vector<uint8_t> floorPixels = makeCheckerboard(128, 16, 210, 60);
    const std::vector<uint8_t> shapePixels = makeCheckerboard(64, 8, 245, 90);

    const Loom::TextureHandle floorTexture = scene.createTexture(floorPixels.data(), 128, 128);
    const Loom::TextureHandle shapeTexture = scene.createTexture(shapePixels.data(), 64, 64);

    //Preset daje jedno usmjereno svjetlo sa sjenom. Drugo svjetlo nije na stepenici 1, pa se
    //dodaje kroz vrata - zivom rendereru, dok scena vec postoji
    scene.loom().renderer.addLight(bulb);

    scene.environment().setAmbient({0.06f, 0.07f, 0.10f});

    // -- petlja je i dalje nasa ------------------------------------------------------------

    while(scene.isRunning()){
        const float time = scene.time();

        //Kamera kruzi, pa se sjenina kutija svaki frame ponovno pripasuje frustumu i snapa na
        //cijele teksele. Da toga nema, ovdje bi rubovi sjena puzali
        scene.camera().setPosition({6.5f * std::sin(time * 0.25f), 3.0f, 6.5f * std::cos(time * 0.25f)});
        scene.camera().lookAt({0.0f, 0.6f, 0.0f});

        bulb.setPosition({2.6f * std::cos(time * 0.7f), 2.0f, 2.6f * std::sin(time * 0.7f)});

        scene.startRendering();

            scene.drawPlane(floorTexture, Loom::Transform().scaled(16.0f));

            scene.drawCube(shapeTexture, Loom::Transform()
                .at(-1.8f, 0.5f, 0.0f)
                .spun(time * 0.8f, {0.3f, 1.0f, 0.15f}));

            scene.drawSphere(shapeTexture, Loom::Transform()
                .at(0.0f, 0.75f + 0.25f * std::sin(time * 1.6f), 0.0f)
                .scaled(1.3f));

            scene.drawPyramid(shapeTexture, Loom::Transform()
                .at(1.8f, 0.5f, 0.0f)
                .spun(-time * 0.6f, {0.0f, 1.0f, 0.0f}));

        scene.endRendering();
    }
}
