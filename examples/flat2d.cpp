// STEPENICA 1 - 2D, bez dubine i bez svjetla.
//
// Isti Scene, drugi preset. Razlika prema Lit3D je samo u tome sto preset ispuni, ne u
// tome kako se scena crta - inace bi to bile dvije biblioteke, ne dvije postavke.
#include <Loom/Loom.h>
#include <cmath>

int main(){
    Loom::Scene scene(Loom::Preset::Flat2D);
    scene.setTitle("Loom 2D");
    scene.setSize(960, 540);
    scene.setClearColor({0.05f, 0.05f, 0.08f, 1.0f});

    const Loom::TextureHandle sprite = scene.loadTexture("assets/sprite.png");

    while(scene.isRunning()){
        const float time = scene.time();

        scene.startRendering();

            //Bez svjetla i bez dubine, pa je redoslijed crtanja jedino sto odlucuje
            //sto je iznad cega - kao i u svakom 2D sustavu
            scene.drawSprite(sprite, Loom::Transform().at(0.0f, 0.0f, 0.0f).scaled(2.0f));
            scene.drawSprite(sprite, Loom::Transform()
                .at(0.6f * std::sin(time), 0.4f * std::cos(time), 0.0f)
                .scaled(0.5f));

        scene.endRendering();
    }
}
