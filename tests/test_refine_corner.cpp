// Kut dotjeran ispod piksela: nalazi li pravi polozaj iz kvantizirane procjene.
//
// ZASTO OVO POSTOJI. Graf poklapanja radi na smanjenoj slici, jer na punoj detektor hvata sum koji
// se izmedju kadrova ne ponavlja. Cijena je kvantizacija: pri smanjenju cetiri puta svaka je
// znacajka tocna na cetiri piksela, a COLMAP-ove su subpikselne - i ta razlika ide ravno u tocnost
// poza, dakle u ostrinu splata.
//
// Istina je ovdje poznata do zadnje znamenke, jer se kut CRTA na zadano mjesto.
//
// Sto se brani:
//
//   pogadja kut        iz procjene promasene par piksela vraca pravi polozaj, ispod pola piksela
//   ne mice tocno      kad je procjena vec tocna, ne smije je pokvariti
//   rub ne dotjeruje   na rubu je sustav slabo uvjetovan; mora vratiti pocetak, a ne odletjeti
//   ravna ploha        ondje gradijenta nema uopce - isto pravilo
//   granica pomaka     procjena daleko od svakog kuta vraca se nedirnuta, ne odvlaci se
#include "TestHarness.h"

#include <Engine/Track.h>

#include <cmath>
#include <vector>

namespace{

const uint32_t width = 160, height = 160;

//Kut dvaju bridova: tamni kvadrant dolje desno od zadane tocke. Rub je zagladjen preko jednog
//piksela, jer savrseno ostar rub ne postoji ni u jednoj snimci - a dotjerivanje racuna gradijente.
//
//PIKSEL (x, y) LEZI NA (x, y), bez pola piksela. To je konvencija koju Engine koristi posvuda -
//detectCorners vraca cijele brojeve, a at() cita piksel na tom indeksu. Prva verzija ovog testa
//crtala je u srediste piksela i onda mjerila sustavni promasaj od 0.6 px koji je bio njezin
std::vector<uint8_t> renderCorner(glm::vec2 at){
    std::vector<uint8_t> pixels(size_t(width) * height, 0);
    for(uint32_t y = 0; y < height; ++y){
        for(uint32_t x = 0; x < width; ++x){
            const float dx = float(x) - at.x;
            const float dy = float(y) - at.y;

            const float alongX = std::max(0.0f, std::min(1.0f, dx + 0.5f));
            const float alongY = std::max(0.0f, std::min(1.0f, dy + 0.5f));
            const float dark = alongX * alongY;

            pixels[size_t(y) * width + x] = uint8_t(230.0f - 200.0f * dark);
        }
    }
    return pixels;
}

Engine::GrayImage view(const std::vector<uint8_t>& pixels){
    return Engine::GrayImage{pixels.data(), width, height, width};
}

}

int main(){
    TestReport report("dotjerivanje kuta ispod piksela");

    //-- pogadja kut ---------------------------------------------------------------------------
    {
        //Mjesta izabrana tako da kut NIJE na cijelom pikselu - inace bi i kvantizirana procjena
        //vec bila tocna i provjera ne bi mjerila nista
        const glm::vec2 places[] = {{80.30f, 80.70f}, {64.25f, 96.60f}, {100.80f, 50.10f}};

        double worst = 0.0, worstStart = 0.0;
        for(const glm::vec2& truth : places){
            const std::vector<uint8_t> pixels = renderCorner(truth);

            //Procjena kakvu daje rad na cetiri puta smanjenoj slici: zaokruzena na mrezu od 4 px
            const glm::vec2 guess(std::floor(truth.x / 4.0f) * 4.0f + 1.5f,
                                  std::floor(truth.y / 4.0f) * 4.0f + 1.5f);

            const glm::vec2 found = Engine::refineCorner(view(pixels), guess);
            worst = std::max(worst, double(glm::length(found - truth)));
            worstStart = std::max(worstStart, double(glm::length(guess - truth)));
        }

        report.check("pogadja kut", worst < 0.5,
            fmt("procjena promasi do %.2f px, dotjerana do %.2f px", worstStart, worst));
    }

    //-- ne kvari tocnu procjenu ---------------------------------------------------------------
    {
        const glm::vec2 truth(80.30f, 80.70f);
        const std::vector<uint8_t> pixels = renderCorner(truth);
        const glm::vec2 found = Engine::refineCorner(view(pixels), truth);

        report.check("ne mice tocno", glm::length(found - truth) < 0.3f,
            fmt("pomak %.3f px", double(glm::length(found - truth))));
    }

    //-- rub i ravna ploha ---------------------------------------------------------------------
    {
        //Okomiti rub: gradijent postoji samo u jednom smjeru, pa je sustav singularan
        std::vector<uint8_t> edge(size_t(width) * height, 0);
        for(uint32_t y = 0; y < height; ++y){
            for(uint32_t x = 0; x < width; ++x){
                edge[size_t(y) * width + x] = x < 80 ? uint8_t(230) : uint8_t(30);
            }
        }
        const glm::vec2 onEdge(80.0f, 80.0f);
        const glm::vec2 fromEdge = Engine::refineCorner(view(edge), onEdge);

        const std::vector<uint8_t> flat(size_t(width) * height, 128);
        const glm::vec2 fromFlat = Engine::refineCorner(view(flat), onEdge);

        report.check("rub se ne dotjeruje", fromEdge == onEdge,
            fmt("vraceno (%.2f, %.2f)", double(fromEdge.x), double(fromEdge.y)));
        report.check("ravna ploha se ne dotjeruje", fromFlat == onEdge,
            fmt("vraceno (%.2f, %.2f)", double(fromFlat.x), double(fromFlat.y)));
    }

    //-- granica pomaka ------------------------------------------------------------------------
    {
        //Procjena daleko od kuta: rjesenje bi je odvuklo preko pola slike. Granica to mora
        //zaustaviti, jer je kvantiziran polozaj bolji od pogresnog
        const std::vector<uint8_t> pixels = renderCorner(glm::vec2(80.3f, 80.7f));
        const glm::vec2 faraway(30.0f, 130.0f);
        const glm::vec2 found = Engine::refineCorner(view(pixels), faraway);

        report.check("daleka procjena ostaje netaknuta", found == faraway,
            fmt("vraceno (%.2f, %.2f)", double(found.x), double(found.y)));
    }

    return report.result();
}
