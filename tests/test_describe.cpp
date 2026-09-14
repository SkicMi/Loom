// Potpis okoline ugla: prepoznaje li isto mjesto iz drugog kadra.
//
// ZASTO OVO TREBA. Pracenje zna samo gdje je ugao BIO i umire cim ugao izadje iz kadra - izmjereno
// na pravoj snimci, 1366 od 1508 smrti tragova. Potpis to rjesava tako da se ugao dade prepoznati
// iz bilo kojeg kadra, pa i nesusjednog. Ali potpis koji se ne prepozna je beskoristan, a onaj
// koji prepoznaje sve je gori od beskorisnog: triangulacija dobije dva imena za istu tocku, ili
// jedno ime za dvije.
//
// Slike su ZADANE FORMULOM i warp je poznat, pa se ne procjenjuje nego zna koje se tocke moraju
// poklopiti.
//
// Sto se brani:
//
//   isti ugao          ista tocka u pomaknutoj slici se prepozna
//   zaokret            i kad je slika zaokrenuta - zbog toga se smjer uopce racuna
//   razliciti uglovi   dva razlicita mjesta se NE poklope
//   tudja slika        u nepovezanoj slici se ne nalazi gotovo nista
//   ponavljanje        sahovnica ima mnogo jednakih uglova; prag omjera ih mora odbiti
#include "TestHarness.h"

#include <Engine/Describe.h>

#include <algorithm>
#include <cmath>
#include <vector>

namespace{

const uint32_t width = 480, height = 360;

float pattern(float x, float y){
    return 128.0f
         + 60.0f * std::sin(0.21f * x) * std::cos(0.17f * y)
         + 45.0f * std::sin(0.07f * x + 0.11f * y)
         + 20.0f * std::sin(0.53f * x - 0.31f * y);
}

float other(float x, float y){
    return 128.0f + 55.0f * std::sin(0.13f * x + 0.9f) * std::sin(0.19f * y - 0.4f);
}

//NEPERIODICNA tekstura. pattern je zbroj sinusoida i time se PONAVLJA - dva razlicita ugla ondje
//stvarno imaju istu okolinu, pa se od potpisa ne smije traziti da ih razlikuje. Ovdje vrijednost
//ovisi o cjelobrojnoj celiji kroz hash, pa se dvije celije ne ponavljaju
float speckle(float x, float y){
    auto value = [](int cx, int cy){
        uint32_t h = uint32_t(cx) * 374761393u + uint32_t(cy) * 668265263u;
        h = (h ^ (h >> 13)) * 1274126177u;
        return float((h ^ (h >> 16)) & 0xffu);
    };
    const int cx = int(std::floor(x / 7.0f)), cy = int(std::floor(y / 7.0f));
    const float fx = x / 7.0f - float(cx), fy = y / 7.0f - float(cy);
    const float top = value(cx, cy) * (1.0f - fx) + value(cx + 1, cy) * fx;
    const float bottom = value(cx, cy + 1) * (1.0f - fx) + value(cx + 1, cy + 1) * fx;
    return top * (1.0f - fy) + bottom * fy;
}

float checkerboard(float x, float y){
    const int cx = int(std::floor(x / 40.0f));
    const int cy = int(std::floor(y / 40.0f));
    return ((cx + cy) % 2 == 0) ? 40.0f : 215.0f;
}

//Slika pod poznatim zaokretom i pomakom oko sredista
std::vector<uint8_t> render(float (*source)(float, float), float turn, glm::vec2 shift){
    const glm::vec2 centre{float(width) / 2.0f, float(height) / 2.0f};
    const float c = std::cos(-turn), s = std::sin(-turn);
    std::vector<uint8_t> pixels(size_t(width) * height);
    for(uint32_t y = 0; y < height; ++y){
        for(uint32_t x = 0; x < width; ++x){
            const glm::vec2 here{float(x), float(y)};
            const glm::vec2 local = here - centre - shift;
            const glm::vec2 there = centre + glm::vec2(c * local.x - s * local.y, s * local.x + c * local.y);
            const float value = source(there.x, there.y);
            pixels[size_t(y) * width + x] = uint8_t(std::max(0.0f, std::min(255.0f, value)));
        }
    }
    return pixels;
}

glm::vec2 warped(float turn, glm::vec2 shift, glm::vec2 point){
    const glm::vec2 centre{float(width) / 2.0f, float(height) / 2.0f};
    const float c = std::cos(turn), s = std::sin(turn);
    const glm::vec2 local = point - centre;
    return centre + glm::vec2(c * local.x - s * local.y, s * local.x + c * local.y) + shift;
}

Engine::GrayImage view(const std::vector<uint8_t>& pixels){
    return Engine::GrayImage{pixels.data(), width, height, width};
}

}

int main(){
    TestReport report("potpis okoline ugla");

    Engine::TrackConfig trackConfig;
    trackConfig.minDistance = 24.0f;
    trackConfig.maxCorners = 120;

    // -------------------------------------------------------------------------------
    // Ista tocka u pomaknutoj slici
    // -------------------------------------------------------------------------------

    {
        const std::vector<uint8_t> before = render(pattern, 0.0f, {0.0f, 0.0f});
        const std::vector<uint8_t> after  = render(pattern, 0.0f, {7.0f, -4.0f});

        const std::vector<glm::vec2> corners = Engine::detectCorners(view(before), trackConfig);
        std::vector<glm::vec2> moved;
        for(const glm::vec2& corner : corners) moved.push_back(warped(0.0f, {7.0f, -4.0f}, corner));

        const auto a = Engine::describeAll(view(before), corners);
        const auto b = Engine::describeAll(view(after), moved);

        size_t compared = 0, correct = 0;
        for(const Engine::Match& match : Engine::matchDescriptors(a, b)){
            ++compared;
            if(match.from == match.to) ++correct;
        }
        report.check("isti ugao u pomaknutoj slici se prepozna",
            compared > 20 && correct * 10 >= compared * 9,
            fmt("%zu poklapanja, %zu tocnih (%.0f%%)", compared, correct,
                compared ? 100.0 * double(correct) / double(compared) : 0.0));
    }

    // -------------------------------------------------------------------------------
    // I kad je slika zaokrenuta - zbog toga se smjer racuna
    // -------------------------------------------------------------------------------

    {
        const float turn = glm::radians(20.0f);
        const std::vector<uint8_t> before = render(pattern, 0.0f, {0.0f, 0.0f});
        const std::vector<uint8_t> after  = render(pattern, turn, {0.0f, 0.0f});

        //Samo uglovi blizu sredista: daleki pri zaokretu izadju iz slike
        std::vector<glm::vec2> corners, moved;
        const glm::vec2 centre{float(width) / 2.0f, float(height) / 2.0f};
        for(const glm::vec2& corner : Engine::detectCorners(view(before), trackConfig)){
            if(glm::length(corner - centre) > 110.0f) continue;
            corners.push_back(corner);
            moved.push_back(warped(turn, {0.0f, 0.0f}, corner));
        }

        const auto a = Engine::describeAll(view(before), corners);
        const auto b = Engine::describeAll(view(after), moved);

        size_t compared = 0, correct = 0;
        for(const Engine::Match& match : Engine::matchDescriptors(a, b)){
            ++compared;
            if(match.from == match.to) ++correct;
        }
        report.check("zaokrenuta slika se i dalje prepozna",
            compared > 10 && correct * 4 >= compared * 3,
            fmt("zaokret 20 st: %zu poklapanja, %zu tocnih (%.0f%%)", compared, correct,
                compared ? 100.0 * double(correct) / double(compared) : 0.0));
    }

    // -------------------------------------------------------------------------------
    // Razliciti uglovi se ne smiju poklopiti
    // -------------------------------------------------------------------------------

    {
        //NEPERIODICNA slika. Prvi pokusaj je koristio pattern - zbroj sinusoida, dakle uzorak koji
        //se PONAVLJA - i test je pao jer su dva razlicita ugla imala identican potpis. To nije bila
        //greska potpisa nego moja: od njega se trazilo da razlikuje ono sto se ne razlikuje.
        //
        //Ni drugi pokusaj nije mjerio pravu stvar. Trazio je da najblizi TUDJI potpis bude barem
        //70 bitova dalek, a to je bio broj iz glave. Poklapanje ne ovisi o tome koliko je krivi
        //daleko nego je li TOCAN par osjetno blizi od najboljeg krivog - bas to i koristi prag
        //omjera. Zato se mjeri razmak izmedju to dvoje
        const glm::vec2 shift{6.0f, -3.0f};
        const std::vector<uint8_t> before = render(speckle, 0.0f, {0.0f, 0.0f});
        const std::vector<uint8_t> after  = render(speckle, 0.0f, shift);

        const std::vector<glm::vec2> corners = Engine::detectCorners(view(before), trackConfig);
        std::vector<glm::vec2> moved;
        for(const glm::vec2& corner : corners) moved.push_back(corner + shift);

        const auto a = Engine::describeAll(view(before), corners);
        const auto b = Engine::describeAll(view(after), moved);

        std::vector<double> right, wrong;
        for(size_t i = 0; i < a.size(); ++i){
            if(!a[i].valid || !b[i].valid) continue;
            right.push_back(double(Engine::distance(a[i], b[i])));

            uint32_t closest = 256;
            for(size_t j = 0; j < b.size(); ++j){
                if(i == j || !b[j].valid) continue;
                closest = std::min(closest, Engine::distance(a[i], b[j]));
            }
            wrong.push_back(double(closest));
        }
        std::sort(right.begin(), right.end());
        std::sort(wrong.begin(), wrong.end());
        const double rightMedian = right.empty() ? 0.0 : right[right.size() / 2];
        const double wrongMedian = wrong.empty() ? 0.0 : wrong[wrong.size() / 2];

        report.check("tocan par je osjetno blizi od najboljeg krivog",
            !right.empty() && rightMedian * 2.0 < wrongMedian,
            fmt("tocan %.0f bitova, najbolji krivi %.0f bitova (medijani od 256)",
                rightMedian, wrongMedian));
    }

    // -------------------------------------------------------------------------------
    // Nepovezana slika ne smije dati poklapanja
    // -------------------------------------------------------------------------------

    {
        const std::vector<uint8_t> one = render(pattern, 0.0f, {0.0f, 0.0f});
        const std::vector<uint8_t> two = render(other, 0.0f, {0.0f, 0.0f});

        const std::vector<glm::vec2> here = Engine::detectCorners(view(one), trackConfig);
        const std::vector<glm::vec2> there = Engine::detectCorners(view(two), trackConfig);

        const auto a = Engine::describeAll(view(one), here);
        const auto b = Engine::describeAll(view(two), there);
        const size_t found = Engine::matchDescriptors(a, b).size();

        report.check("u nepovezanoj slici se ne nalazi gotovo nista",
            found * 10 < here.size(),
            fmt("%zu poklapanja na %zu uglova", found, here.size()));
    }

    // -------------------------------------------------------------------------------
    // Ponavljajuci uzorak: sahovnica
    // -------------------------------------------------------------------------------
    //
    // Svi uglovi sahovnice izgledaju jednako, pa svaki ima mnogo jednako dobrih pogodaka. Prag
    // omjera mora odbiti takve - krivo poklapanje je gore od nijednog, jer triangulacija iz njega
    // dobije tocku koja ne postoji

    {
        //Sahovnica pomaknuta za tocno jednu celiju: svaki ugao IMA tocan par, ali i mnogo jednako
        //dobrih krivih. Usporedjuje se s neperiodicnom slikom pod istim pomakom - ondje potpis
        //smije i mora naci parove. Prvi pokusaj je skup usporedjivao sam sa sobom, sto je izrodjen
        //slucaj: svakome je najblizi on sam, na nula bitova
        const glm::vec2 shift{40.0f, 0.0f};

        auto found = [&](float (*source)(float, float)){
            const std::vector<uint8_t> before = render(source, 0.0f, {0.0f, 0.0f});
            const std::vector<uint8_t> after  = render(source, 0.0f, shift);
            const std::vector<glm::vec2> corners = Engine::detectCorners(view(before), trackConfig);
            std::vector<glm::vec2> moved;
            for(const glm::vec2& corner : corners) moved.push_back(corner + shift);
            const auto a = Engine::describeAll(view(before), corners);
            const auto b = Engine::describeAll(view(after), moved);
            return std::make_pair(Engine::matchDescriptors(a, b).size(), corners.size());
        };

        const auto board = found(checkerboard);
        const auto noise = found(speckle);

        //Na sahovnici se poklapa bitno manji udio nego na neponavljajucoj slici. Krivo poklapanje
        //je gore od nijednog: iz njega triangulacija dobije tocku koje nema
        report.check("ponavljajuci uzorak daje bitno manje poklapanja",
            board.second > 20 && noise.second > 20 &&
            double(board.first) / double(board.second) < 0.5 * double(noise.first) / double(noise.second),
            fmt("sahovnica %zu/%zu, neponavljajuca slika %zu/%zu",
                board.first, board.second, noise.first, noise.second));
    }

    return report.result();
}
