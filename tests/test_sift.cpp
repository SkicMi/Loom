// Potpis okoline preko histograma gradijenata.
//
// Dvije stvari cine potpis upotrebljivim, i obje se daju provjeriti bez ijedne snimke: isti detalj
// mora dati ISTI potpis kad se malo pomakne ili promijeni svjetlina, a razliciti detalji moraju
// dati RAZLICITE. Bez prve nema poklapanja, bez druge se poklapa sve sa svime.
//
// Sto se brani:
//
//   isti detalj        okolina pomaknuta za piksel mora ostati bliza sebi nego bilo cemu drugom
//   svjetlina          mnozenje i pomak intenziteta ne smiju promijeniti potpis - potpis se
//                      normira, pa je to svojstvo a ne slucajnost
//   razliciti detalji  dva razlicita mjesta u teksturi moraju biti dalje nego isto mjesto pomaknuto
//   rub slike          okolina koja ne stane vraca false, a ne cita izvan polja
//   zbroj je jedan     potpis je jedinicne duljine skaliran na 512, pa mu je norma poznata
#include "TestHarness.h"

#include <Engine/Sift.h>

#include <algorithm>
#include <cmath>
#include <vector>

namespace{

const uint32_t width = 256, height = 256;

//Neponavljajuca tekstura: periodicna bi dala okoline koje se stvarno ne razlikuju, pa bi provjera
//razlicitosti pala s pravom
float speckle(float x, float y){
    auto value = [](int cx, int cy){
        uint32_t h = uint32_t(cx) * 374761393u + uint32_t(cy) * 668265263u;
        h = (h ^ (h >> 13)) * 1274126177u;
        return float((h ^ (h >> 16)) & 0xffu);
    };
    const int cx = int(std::floor(x / 5.0f)), cy = int(std::floor(y / 5.0f));
    const float fx = x / 5.0f - float(cx), fy = y / 5.0f - float(cy);
    const float top = value(cx, cy) * (1.0f - fx) + value(cx + 1, cy) * fx;
    const float bottom = value(cx, cy + 1) * (1.0f - fx) + value(cx + 1, cy + 1) * fx;
    return top * (1.0f - fy) + bottom * fy;
}

std::vector<uint8_t> render(glm::vec2 shift, float gain = 1.0f, float lift = 0.0f){
    std::vector<uint8_t> pixels(size_t(width) * height);
    for(uint32_t y = 0; y < height; ++y){
        for(uint32_t x = 0; x < width; ++x){
            const float value = speckle(float(x) - shift.x, float(y) - shift.y) * gain + lift;
            pixels[size_t(y) * width + x] = uint8_t(std::max(0.0f, std::min(255.0f, value)));
        }
    }
    return pixels;
}

Engine::GrayImage view(const std::vector<uint8_t>& pixels){
    return Engine::GrayImage{pixels.data(), width, height, width};
}

}

int main(){
    TestReport report("potpis preko histograma gradijenata");

    Engine::SiftConfig config;
    config.patch = 16;

    const std::vector<uint8_t> plain = render(glm::vec2(0.0f));
    const glm::vec2 place(128.0f, 128.0f);

    Engine::SiftDescriptor here;
    const bool made = Engine::describeSift(view(plain), place, here, config);

    report.check("potpis nastaje", made && here.valid, made ? "valjan" : "NIJE");
    if(!made) return report.result();

    //-- norma ---------------------------------------------------------------------------------
    {
        double sum = 0.0;
        for(uint8_t value : here.values) sum += double(value) * double(value);
        const double length = std::sqrt(sum);

        //Jedinicna duljina skalirana na 512; odsijecanje na 255 po clanu je jedino sto to pomakne
        report.check("potpis je normiran", length > 400.0 && length < 520.0,
            fmt("norma %.1f, ocekivano oko 512", length));
    }

    //-- isti detalj, pomaknut -----------------------------------------------------------------
    {
        //Slika pomaknuta za jedan piksel, pa se ista tocka scene gleda na pomaknutom mjestu
        const std::vector<uint8_t> moved = render(glm::vec2(1.0f, 0.0f));
        Engine::SiftDescriptor shifted;
        Engine::describeSift(view(moved), place + glm::vec2(1.0f, 0.0f), shifted, config);

        //I neko drugo mjesto u istoj teksturi
        Engine::SiftDescriptor elsewhere;
        Engine::describeSift(view(plain), glm::vec2(80.0f, 170.0f), elsewhere, config);

        const float toSelf = Engine::distance(here, shifted);
        const float toOther = Engine::distance(here, elsewhere);

        report.check("isti detalj ostaje blizu", shifted.valid && toSelf < 0.5f * toOther,
            fmt("sebi %.0f, drugom %.0f", double(toSelf), double(toOther)));

        report.check("razliciti detalji su daleko", toOther > 200.0f,
            fmt("%.0f od najvise oko 1024", double(toOther)));
    }

    //-- svjetlina -----------------------------------------------------------------------------
    {
        //Potpis se normira, pa mnozenje intenziteta ne smije promijeniti nista osim zaokruzivanja.
        //Pomak svjetline ne mijenja ni gradijente
        const std::vector<uint8_t> brighter = render(glm::vec2(0.0f), 0.7f, 40.0f);
        Engine::SiftDescriptor lit;
        Engine::describeSift(view(brighter), place, lit, config);

        const float apart = Engine::distance(here, lit);
        report.check("svjetlina ne mijenja potpis", lit.valid && apart < 60.0f,
            fmt("udaljenost %.0f uz pojacanje 0.7 i pomak 40", double(apart)));
    }

    //-- rub slike -----------------------------------------------------------------------------
    {
        Engine::SiftDescriptor edge;
        const bool atEdge = Engine::describeSift(view(plain), glm::vec2(3.0f, 3.0f), edge, config);
        report.check("rub vraca false", !atEdge && !edge.valid, atEdge ? "NAPRAVIO GA" : "odbio");
    }

    //-- vise odjednom daje isto ---------------------------------------------------------------
    {
        //describeSiftAll racuna gradijente jednom za cijelu sliku; mora dati isto sto i pojedinacni
        const std::vector<glm::vec2> places{place, glm::vec2(80.0f, 170.0f)};
        const std::vector<Engine::SiftDescriptor> many = Engine::describeSiftAll(view(plain), places, config);

        report.check("skupno i pojedinacno se slazu",
            many.size() == 2 && many[0].valid && Engine::distance(many[0], here) == 0.0f,
            many.size() == 2 ? fmt("razlika %.0f", double(Engine::distance(many[0], here))) : "kriv broj");
    }

    //-- pripremljena prostorna mreza ne mijenja poklapanje -----------------------------------
    {
        const std::vector<glm::vec2> places{
            glm::vec2(52.0f, 52.0f), glm::vec2(91.0f, 61.0f), glm::vec2(139.0f, 75.0f),
            glm::vec2(67.0f, 128.0f), glm::vec2(121.0f, 146.0f), glm::vec2(177.0f, 169.0f)
        };
        const std::vector<Engine::SiftDescriptor> descriptors =
            Engine::describeSiftAll(view(plain), places, config);
        const float radius = 80.0f;

        //Stari poziv namjerno ostaje referenca: on mreze gradi iznova unutar matchera.
        const std::vector<Engine::SiftMatch> reference =
            Engine::matchSiftNear(descriptors, places, descriptors, places, radius, config);
        const Engine::SiftMatchGrid grid = Engine::prepareSiftMatchGrid(descriptors, places, radius);
        const std::vector<Engine::SiftMatch> prepared =
            Engine::matchSiftNear(descriptors, places, grid, descriptors, places, grid, radius, config);

        bool exactlyEqual = reference.size() == prepared.size();
        for(size_t i = 0; exactlyEqual && i < reference.size(); ++i){
            exactlyEqual = reference[i].from == prepared[i].from &&
                           reference[i].to == prepared[i].to &&
                           reference[i].distance == prepared[i].distance;
        }
        report.check("pripremljena mreza daje bit-identican rezultat",
            !reference.empty() && exactlyEqual,
            fmt("referenca %zu, pripremljeno %zu", reference.size(), prepared.size()));

        //Mreza napravljena za drugi radijus ne smije se tiho upotrijebiti s krivim celijama.
        const std::vector<Engine::SiftMatch> mismatched =
            Engine::matchSiftNear(descriptors, places, grid, descriptors, places, grid,
                                  radius * 0.5f, config);
        report.check("mreza krivog radijusa se odbija", mismatched.empty(),
            mismatched.empty() ? "odbijena" : "PRIHVACENA");
    }

    return report.result();
}
