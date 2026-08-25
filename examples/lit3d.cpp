// STEPENICA 1 - osvijetljena 3D scena.
//
// Ovo je specifikacija, ne implementacija: napisano kako zelimo da se cita, prije nego je
// biblioteka koja to podrzava napisana. Ako se ovo ne cita dobro, kriv je API, ne primjer.
//
// Jedini include je Loom/Loom.h. Nijedan Vulkan simbol ne smije ovdje biti vidljiv - to
// cuva test "tier1_header_is_clean".
#include <Loom/Loom.h>

int main(){
    //Preset odlucuje sve sto nismo rekli: dubinu, sjene, ambijent, sunce, kameru
    Loom::Scene scene(Loom::Preset::Lit3D);
    scene.setTitle("Loom");
    scene.setSize(1280, 720);

    //Datoteka -> gotova tekstura. Spool je iza ovoga i korisnik ne mora znati da postoji
    const Loom::TextureHandle brick = scene.loadTexture("assets/brick.png");
    const Loom::TextureHandle metal = scene.loadTexture("assets/metal.png");

    //Kamera, svjetlo i ambijent su vec bez Vulkana, pa se daju takvi kakvi jesu.
    //Ovo NIJE bijeg na nizu stepenicu - ovo je stepenica 1
    scene.sun().setDirection({-0.45f, -1.0f, -0.35f});
    scene.environment().setAmbient({0.06f, 0.07f, 0.10f});

    while(scene.isRunning()){
        const float time = scene.time();

        scene.camera().setPosition({6.5f * std::sin(time * 0.25f), 3.0f, 6.5f * std::cos(time * 0.25f)});
        scene.camera().lookAt({0.0f, 0.6f, 0.0f});

        //Petlja je i dalje korisnikova. Preset drzi postavljanje, ne tok
        scene.startRendering();

            scene.drawPlane(brick, Loom::Transform().scaled(16.0f));

            scene.drawCube(metal, Loom::Transform()
                .at(-1.8f, 0.5f, 0.0f)
                .spun(time * 0.8f, {0.3f, 1.0f, 0.15f}));

            scene.drawSphere(metal, Loom::Transform()
                .at(0.0f, 0.75f + 0.25f * std::sin(time * 1.6f), 0.0f)
                .scaled(1.3f));

            scene.drawPyramid(brick, Loom::Transform()
                .at(1.8f, 0.5f, 0.0f)
                .spun(-time * 0.6f, {0.0f, 1.0f, 0.0f}));

        scene.endRendering();
    }
}
