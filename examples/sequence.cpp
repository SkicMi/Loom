// STEPENICA 1 - offscreen render u niz slika.
//
// Nema prozora, nema sata. Frame N je na N/fps, pa isti program dvaput da iste fileove -
// to je ono zbog cega je renderer odvojen od swapchaina.
#include <Loom/Loom.h>

int main(){
    Loom::Scene scene(Loom::Preset::Offscreen);
    scene.setSize(1920, 1080);

    const Loom::TextureHandle brick = scene.loadTexture("assets/brick.png");

    //Numeriranje, mapa i zapis su Spoolovi, ali korisnik to ne mora znati
    Loom::Sequence sequence;
    sequence.setDirectory("out/render");
    sequence.setPrefix("shot_");

    const uint32_t frames = 120;
    const float framesPerSecond = 24.0f;

    for(uint32_t frame = 0; frame < frames; ++frame){
        //Vrijeme je parametar, ne sat. Bez ovoga sekvenca se ne moze nastaviti ni usporediti
        scene.setFrame(frame, framesPerSecond);
        const float time = scene.time();

        scene.camera().setPosition({4.0f * std::sin(time), 2.0f, 4.0f * std::cos(time)});
        scene.camera().lookAt({0.0f, 0.0f, 0.0f});

        scene.startRendering();
            scene.drawPlane(brick, Loom::Transform().scaled(12.0f));
            scene.drawCube(brick, Loom::Transform().at(0.0f, 0.5f, 0.0f).spun(time, {0,1,0}));
        scene.endRendering();

        sequence.write(scene);
    }
}
