// Dotjerivanje PREMA REFERENTNOM KADRU: ostaju li opazanja jednog traga medjusobno dosljedna.
//
// ZASTO OVO POSTOJI, KAD VEC POSTOJI test_refine_corner. Ondje se brani da dotjerivanje pogodi vrh
// ugla u JEDNOJ slici. Izmjereno je da to lancu steti: graf poklapa na cetiri puta smanjenoj
// slici, a unutar te cetiri piksela na 4K stoje jos dva ili tri ugla - pa se dva opazanja istog
// traga dotjeraju na RAZLICITE. Trag koji je bio kvantiziran ali dosljedan postane tocan ali
// nedosljedan, a triangulaciji treba ovo drugo: ona iz dva pogleda trazi JEDNU tocku.
//
// Zato se ovdje ne trazi vrh nego SLAGANJE. Scena je cista translacija, pa je istina poznata bez
// ijedne poze: svako opazanje vraceno u koordinate prvog kadra mora pasti na isto mjesto.
//
// Sto se brani:
//
//   pogadja pomak      razlika dvaju opazanja mora biti stvarni pomak, ispod pola piksela
//   trag se slaze      tri kadra vracena u prvi moraju pasti na isto mjesto, i to bitno blize
//                      nego sto su bile kvantizirane procjene
//   susjedni ugao      s dva ugla blizu jedan drugome dotjerivanje SVAKOG ZA SEBE ih pomijesa;
//                      dotjerivanje prema referenci ne smije
//   granica pomaka     procjena promasena preko granice vraca se nedirnuta
#include "TestHarness.h"

#include <Engine/Track.h>

#include <algorithm>
#include <cmath>
#include <vector>

namespace{

const uint32_t width = 200, height = 200;

//Dva ugla blizu jedan drugome, oba pomaknuta za zadani pomak. Rub je zagladjen preko jednog
//piksela, jer savrseno ostar rub ne postoji ni u jednoj snimci - a dotjerivanje racuna gradijente
std::vector<uint8_t> renderPair(glm::vec2 first, glm::vec2 second, glm::vec2 shift){
    std::vector<uint8_t> pixels(size_t(width) * height, 230);

    auto draw = [&](glm::vec2 at, float depth){
        for(uint32_t y = 0; y < height; ++y){
            for(uint32_t x = 0; x < width; ++x){
                const float dx = float(x) - (at.x + shift.x);
                const float dy = float(y) - (at.y + shift.y);
                const float alongX = std::max(0.0f, std::min(1.0f, dx + 0.5f));
                const float alongY = std::max(0.0f, std::min(1.0f, dy + 0.5f));
                const float dark = alongX * alongY * depth;

                uint8_t& value = pixels[size_t(y) * width + x];
                value = uint8_t(std::max(0.0f, float(value) - 200.0f * dark));
            }
        }
    };

    draw(first, 1.0f);
    draw(second, 0.6f);   //drugi je slabiji, da se dade razlikovati koji je koji
    return pixels;
}

Engine::GrayImage view(const std::vector<uint8_t>& pixels){
    return Engine::GrayImage{pixels.data(), width, height, width};
}

//Kvantizacija kakvu daje rad na cetiri puta smanjenoj slici: srediste bloka od cetiri piksela
glm::vec2 quantize(glm::vec2 truth){
    return glm::vec2(std::floor(truth.x / 4.0f) * 4.0f + 1.5f,
                     std::floor(truth.y / 4.0f) * 4.0f + 1.5f);
}

}

int main(){
    TestReport report("dotjerivanje prema referentnom kadru");

    const glm::vec2 corner(100.30f, 100.70f);
    const glm::vec2 neighbour(106.30f, 100.70f);   //sest piksela dalje: unutar iste kvantizacije

    Engine::TrackConfig config;
    config.window = 8;

    //-- pogadja pomak -------------------------------------------------------------------------
    {
        const glm::vec2 shift(2.40f, -1.60f);
        const std::vector<uint8_t> before = renderPair(corner, neighbour, glm::vec2(0.0f));
        const std::vector<uint8_t> after = renderPair(corner, neighbour, shift);

        const glm::vec2 reference = quantize(corner);
        glm::vec2 position = reference + glm::vec2(std::round(shift.x), std::round(shift.y));
        const glm::vec2 guess = position;

        const bool ok = Engine::refineToward(view(before), view(after), reference, position, 4.0f, config);
        const double miss = double(glm::length((position - reference) - shift));
        const double before_ = double(glm::length((guess - reference) - shift));

        report.check("pogadja pomak", ok && miss < 0.5,
            fmt("promasaj %.3f px (procjena je promasila %.3f)", miss, before_));
    }

    //-- trag se slaze kroz tri kadra ----------------------------------------------------------
    {
        const glm::vec2 shifts[] = {glm::vec2(0.0f), glm::vec2(3.30f, 1.80f), glm::vec2(-2.70f, 4.20f)};
        std::vector<std::vector<uint8_t>> frames;
        for(const glm::vec2& shift : shifts) frames.push_back(renderPair(corner, neighbour, shift));

        const glm::vec2 reference = quantize(corner);

        std::vector<glm::vec2> refined, raw;
        for(size_t k = 0; k < 3; ++k){
            //Procjena kakvu daje graf: kvantizirani polozaj u tom kadru
            glm::vec2 position = quantize(corner + shifts[k]);
            raw.push_back(position - shifts[k]);

            if(k > 0) Engine::refineToward(view(frames[0]), view(frames[k]), reference, position, 4.0f, config);
            refined.push_back(position - shifts[k]);
        }

        auto spreadOf = [](const std::vector<glm::vec2>& list){
            double widest = 0.0;
            for(size_t a = 0; a < list.size(); ++a){
                for(size_t b = a + 1; b < list.size(); ++b){
                    widest = std::max(widest, double(glm::length(list[a] - list[b])));
                }
            }
            return widest;
        };

        const double after = spreadOf(refined);
        const double before = spreadOf(raw);

        report.check("trag se slaze", after < 0.5 && after < 0.5 * before,
            fmt("rasap %.3f px, a kvantizirane procjene su se razilazile %.3f px", after, before));
    }

    //-- susjedni ugao ne smije preoteti --------------------------------------------------------
    //
    //Ovo je tocna slika kvara zbog kojeg refineAtFullResolution nije prosao. Dotjerivanje svakog
    //opazanja ZA SEBE trazi najblizi vrh, a unutar cetiri piksela kvantizacije ih ima vise - pa
    //dva kadra zavrse na dva RAZLICITA ugla i trag se raspadne.
    //
    //Mjeri se NEDOSLJEDNOST, ne promasaj vrha: svako opazanje se vrati u koordinate prvog kadra i
    //gleda se koliko se razilaze. Tko pogodi vrh, a u drugom kadru drugi vrh, ovdje pada - i tako
    //i treba, jer triangulacija trazi jednu tocku a ne dva tocna vrha
    {
        //Tri piksela razmaka: unutar iste kvantizacije od cetiri, dakle stvarno zamjenjiva
        const glm::vec2 close(103.30f, 100.70f);
        const glm::vec2 shifts[] = {glm::vec2(0.0f), glm::vec2(2.40f, 2.20f),
                                    glm::vec2(-3.10f, 1.40f), glm::vec2(1.70f, -2.90f)};

        std::vector<std::vector<uint8_t>> frames;
        for(const glm::vec2& shift : shifts) frames.push_back(renderPair(corner, close, shift));

        const glm::vec2 reference = quantize(corner);

        double towardWorst = 0.0, aloneWorst = 0.0;
        for(size_t k = 1; k < 4; ++k){
            glm::vec2 position = quantize(corner + shifts[k]);

            const glm::vec2 alone = Engine::refineCorner(view(frames[k]), position);
            const glm::vec2 aloneFirst = Engine::refineCorner(view(frames[0]), reference);
            aloneWorst = std::max(aloneWorst,
                double(glm::length((alone - shifts[k]) - aloneFirst)));

            Engine::refineToward(view(frames[0]), view(frames[k]), reference, position, 4.0f, config);
            towardWorst = std::max(towardWorst,
                double(glm::length((position - shifts[k]) - reference)));
        }

        report.check("susjedni ugao ne preotima",
            towardWorst < 0.5 && towardWorst < 0.5 * aloneWorst,
            fmt("prema referenci se razilazi %.3f px, svaki za sebe %.3f px",
                towardWorst, aloneWorst));
    }

    //-- granica pomaka ------------------------------------------------------------------------
    {
        const std::vector<uint8_t> before = renderPair(corner, neighbour, glm::vec2(0.0f));
        const std::vector<uint8_t> after = renderPair(corner, neighbour, glm::vec2(2.4f, -1.6f));

        const glm::vec2 reference = quantize(corner);
        const glm::vec2 faraway(40.0f, 160.0f);
        glm::vec2 position = faraway;

        const bool ok = Engine::refineToward(view(before), view(after), reference, position, 2.0f, config);

        report.check("daleka procjena ostaje netaknuta", !ok && position == faraway,
            fmt("vraceno %s, (%.2f, %.2f)", ok ? "true" : "false",
                double(position.x), double(position.y)));
    }

    return report.result();
}
