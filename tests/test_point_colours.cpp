// Boja tocke iz kadrova koji ju vide.
//
// ZASTO OVO POSTOJI. Trener splatova iz points3D.txt cita polozaj I BOJU, a boja postaje pocetna
// boja gaussiane. Nas je izvoz dugo pisao 200 200 200 za sve, pa je svaka scena kretala jednolicno
// siva i trener ju je morao cijelu prebojiti - a to se ne vidi ni u jednoj mjeri poza ni tocaka i
// tiho je ulazilo u svaku usporedbu u decibelima.
//
// Sto se brani:
//
//   uzima iz slike     tocka vidjena u kadru mora dobiti boju tog piksela
//   medijan, ne prosjek  jedan zaklonjen kadar ne smije pomaknuti boju. Prosjek bi ga razmazao
//   bez opazanja siva  tocka koju nitko ne vidi ostaje ona zadana
//   smanjenje se postuje  slike se citaju smanjene, pa se opazanje mora podijeliti istim brojem
#include "TestHarness.h"

#include <Engine/ColmapExport.h>

#include <vector>

namespace{

const uint32_t width = 64, height = 64;

//Slika jedne boje, osim jednog kvadrata koji nosi drugu - da se vidi da se cita bas ondje gdje
//opazanje kaze, a ne bilo gdje
std::vector<uint8_t> flat(glm::u8vec3 colour, glm::u8vec3 patch, uint32_t patchX, uint32_t patchY){
    std::vector<uint8_t> pixels(size_t(width) * height * 4, 0);
    for(uint32_t y = 0; y < height; ++y){
        for(uint32_t x = 0; x < width; ++x){
            const bool inside = x >= patchX && x < patchX + 4 && y >= patchY && y < patchY + 4;
            const glm::u8vec3 which = inside ? patch : colour;
            uint8_t* to = pixels.data() + (size_t(y) * width + x) * 4;
            to[0] = which.r; to[1] = which.g; to[2] = which.b; to[3] = 255;
        }
    }
    return pixels;
}

Engine::ColourImage view(const std::vector<uint8_t>& pixels){
    return Engine::ColourImage{pixels.data(), width, height, width};
}

}

int main(){
    TestReport report("boje tocaka iz kadrova");

    //Tri tocke: prvu vide tri kadra, drugu jedan, trecu nijedan
    Engine::Reconstruction state;
    state.points.assign(3, glm::vec3(0.0f));
    state.solved.assign(3, 1);
    state.poses.assign(3, Engine::Pose{});
    state.posed.assign(3, 1);

    //Prva tocka je u sva tri kadra na (20, 30); tamo je zakrpa u prvom i trecem, a u drugom je
    //kadar zaklonjen pa je ondje obicna pozadina
    const std::vector<uint8_t> first = flat(glm::u8vec3(10, 20, 30), glm::u8vec3(200, 100, 50), 20, 30);
    const std::vector<uint8_t> second = flat(glm::u8vec3(10, 20, 30), glm::u8vec3(0, 0, 0), 0, 0);
    const std::vector<uint8_t> third = flat(glm::u8vec3(10, 20, 30), glm::u8vec3(202, 102, 52), 20, 30);

    const std::vector<Engine::ColourImage> images{view(first), view(second), view(third)};

    std::vector<Engine::Observation> observations{
        Engine::Observation{0, 0, glm::vec2(20.0f, 30.0f)},
        Engine::Observation{1, 0, glm::vec2(20.0f, 30.0f)},
        Engine::Observation{2, 0, glm::vec2(20.0f, 30.0f)},
        Engine::Observation{0, 1, glm::vec2(20.0f, 30.0f)}};

    const std::vector<glm::u8vec3> colours = Engine::pointColours(state, observations, images);

    report.check("uzima boju iz slike",
        colours.size() == 3 && colours[1] == glm::u8vec3(200, 100, 50),
        fmt("tocka vidjena jednom dobila (%d, %d, %d), a u slici je (200, 100, 50)",
            int(colours[1].r), int(colours[1].g), int(colours[1].b)));

    //Kadar 1 ondje ima pozadinu (10, 20, 30), kadrovi 0 i 2 imaju zakrpu. Medijan mora biti zakrpa;
    //prosjek bi dao nesto izmedju i promasio oboje
    report.check("medijan preskace zaklonjen kadar",
        colours[0] == glm::u8vec3(200, 100, 50),
        fmt("dobiveno (%d, %d, %d); prosjek bi dao oko (137, 74, 44)",
            int(colours[0].r), int(colours[0].g), int(colours[0].b)));

    report.check("tocka bez opazanja ostaje zadana",
        colours[2] == glm::u8vec3(200, 200, 200),
        fmt("dobiveno (%d, %d, %d)", int(colours[2].r), int(colours[2].g), int(colours[2].b)));

    //-- smanjenje ------------------------------------------------------------------------------
    //
    //Slike se citaju smanjene jer boja ne treba punu razlucivost; opazanje je u koordinatama PUNE
    //slike, pa se mora podijeliti. Tko to zaboravi, cita cetiri puta predaleko - i dobije pozadinu
    {
        std::vector<Engine::Observation> far{Engine::Observation{0, 0, glm::vec2(80.0f, 120.0f)}};
        const std::vector<glm::u8vec3> scaled = Engine::pointColours(state, far, images, 4);

        report.check("smanjenje se postuje",
            scaled[0] == glm::u8vec3(200, 100, 50),
            fmt("dobiveno (%d, %d, %d); bez dijeljenja bi se citalo izvan slike",
                int(scaled[0].r), int(scaled[0].g), int(scaled[0].b)));
    }

    return report.result();
}
