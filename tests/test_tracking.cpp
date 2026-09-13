// Pracenje uglova kroz kadrove: od piksela do opazanja.
//
// Ovo je zadnji komad koji fali da prava snimka udje u lanac. Testira se na slikama koje su
// ZADANE FORMULOM, pa se pomak zna na proizvoljan broj decimala: kadar B je ista funkcija
// racunata u pomaknutoj tocki, bez presemplavanja. Tako se mjeri tocnost pracenja, a ne kvaliteta
// moje pripreme slike.
//
// Sto se brani:
//
//   uglovi           sahovnica ima uglove na poznatim mjestima; detektor ih mora naci tamo
//   pomak            poznat pomak od 3.4 i -2.1 piksela mora se izmjeriti na desetinu piksela
//   piramida         pomak od 25 piksela je veci od prozora. S piramidom se prati, bez nje ne -
//                    i to je razlog zasto piramida postoji, pa se mjeri a ne tvrdi
//   odbijanje        pracenje u nepovezanu sliku mora reci da nije uspjelo
//   tragovi          kroz niz kadrova isti trag mora zadrzati svoj broj, a opazanja pratiti gibanje
//   afino            prozor koji je i zaokrenut i rastegnut: pomak sam ga promasi, afini ga vrati
//   sidro            kroz niz kadrova ulancano pracenje ZBRAJA gresku, sidreno je mjeri iznova
//   izrodjenje       warp preko dopustenog rastezanja mora reci da je trag izgubljen
#include "TestHarness.h"

#include <Engine/Track.h>

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

namespace{

const uint32_t width = 480, height = 360;

//Uzorak s gradijentom u oba smjera: zbroj dviju sinusoida pod razlicitim kutovima. Jedna sama bi
//dala pruge, a pruga se ne da pratiti uzduz sebe (problem otvora)
float pattern(float x, float y){
    return 128.0f
         + 60.0f * std::sin(0.21f * x) * std::cos(0.17f * y)
         + 45.0f * std::sin(0.07f * x + 0.11f * y)
         + 20.0f * std::sin(0.53f * x - 0.31f * y);
}

float checkerboard(float x, float y){
    const int cell = 40;
    const int cx = int(std::floor(x / float(cell)));
    const int cy = int(std::floor(y / float(cell)));
    return ((cx + cy) % 2 == 0) ? 40.0f : 215.0f;
}

std::vector<uint8_t> render(float (*source)(float, float), float shiftX, float shiftY){
    std::vector<uint8_t> pixels(size_t(width) * height);
    for(uint32_t y = 0; y < height; ++y){
        for(uint32_t x = 0; x < width; ++x){
            const float value = source(float(x) - shiftX, float(y) - shiftY);
            pixels[size_t(y) * width + x] = uint8_t(std::max(0.0f, std::min(255.0f, value)));
        }
    }
    return pixels;
}

//Slika pod poznatim afinim warpom. Tocka p iz polazne slike se nalazi na  c + linear*(p-c) + shift,
//pa je istina zadana formulom i ne ovisi ni o kakvom presemplavanju s moje strane
std::vector<uint8_t> renderWarped(float (*source)(float, float), const glm::mat2& linear,
                                  const glm::vec2& shift, const glm::vec2& centre){
    const glm::mat2 back = glm::inverse(linear);
    std::vector<uint8_t> pixels(size_t(width) * height);
    for(uint32_t y = 0; y < height; ++y){
        for(uint32_t x = 0; x < width; ++x){
            const glm::vec2 here{float(x), float(y)};
            const glm::vec2 there = centre + back * (here - centre - shift);
            const float value = source(there.x, there.y);
            pixels[size_t(y) * width + x] = uint8_t(std::max(0.0f, std::min(255.0f, value)));
        }
    }
    return pixels;
}

//Gdje poznati warp odnese tocku
glm::vec2 warpedPlace(const glm::mat2& linear, const glm::vec2& shift,
                      const glm::vec2& centre, const glm::vec2& point){
    return centre + linear * (point - centre) + shift;
}

Engine::GrayImage view(const std::vector<uint8_t>& pixels){
    return Engine::GrayImage{pixels.data(), width, height, width};
}

double medianOf(std::vector<double> values){
    if(values.empty()) return 0.0;
    std::sort(values.begin(), values.end());
    return values[values.size() / 2];
}

}

int main(){
    TestReport report("pracenje uglova");

    // -------------------------------------------------------------------------------
    // Uglovi sahovnice su na poznatim mjestima
    // -------------------------------------------------------------------------------

    {
        const std::vector<uint8_t> board = render(checkerboard, 0.0f, 0.0f);
        Engine::TrackConfig config;
        config.minDistance = 20.0f;
        const std::vector<glm::vec2> corners = Engine::detectCorners(view(board), config);

        double worst = 0.0;
        for(const glm::vec2& corner : corners){
            const double x = std::fabs(std::round(corner.x / 40.0) * 40.0 - double(corner.x));
            const double y = std::fabs(std::round(corner.y / 40.0) * 40.0 - double(corner.y));
            worst = std::max(worst, std::max(x, y));
        }

        report.check("uglovi su nadjeni tamo gdje jesu",
            corners.size() > 30 && worst < 2.0,
            fmt("%zu uglova, najdalji %.2f px od krizista", corners.size(), worst));
    }

    // -------------------------------------------------------------------------------
    // Poznat pomak
    // -------------------------------------------------------------------------------

    const std::vector<uint8_t> first = render(pattern, 0.0f, 0.0f);

    {
        const float shiftX = 3.4f, shiftY = -2.1f;
        const std::vector<uint8_t> second = render(pattern, shiftX, shiftY);

        const std::vector<glm::vec2> corners = Engine::detectCorners(view(first));
        std::vector<double> errors;
        size_t lost = 0;

        for(const glm::vec2& corner : corners){
            glm::vec2 moved;
            if(!Engine::trackPoint(view(first), view(second), corner, moved)){
                ++lost;
                continue;
            }
            errors.push_back(double(glm::length(moved - corner - glm::vec2(shiftX, shiftY))));
        }

        report.check("poznat pomak se izmjeri na desetinu piksela",
            errors.size() > 50 && medianOf(errors) < 0.1,
            fmt("medijan %.4f px kroz %zu uglova, izgubljeno %zu", medianOf(errors), errors.size(), lost));
    }

    // -------------------------------------------------------------------------------
    // Veliki pomak: s piramidom i bez nje
    // -------------------------------------------------------------------------------

    {
        const float shiftX = 25.0f, shiftY = 14.0f;
        const std::vector<uint8_t> far = render(pattern, shiftX, shiftY);
        const std::vector<glm::vec2> corners = Engine::detectCorners(view(first));

        Engine::TrackConfig flat;
        flat.levels = 1;

        std::vector<double> withPyramid, withoutPyramid;
        for(const glm::vec2& corner : corners){
            glm::vec2 moved;
            if(Engine::trackPoint(view(first), view(far), corner, moved)){
                withPyramid.push_back(double(glm::length(moved - corner - glm::vec2(shiftX, shiftY))));
            }
            if(Engine::trackPoint(view(first), view(far), corner, moved, flat)){
                withoutPyramid.push_back(double(glm::length(moved - corner - glm::vec2(shiftX, shiftY))));
            }
        }

        const double good = medianOf(withPyramid);
        const double flatMedian = medianOf(withoutPyramid);

        report.check("piramida hvata pomak veci od prozora",
            withPyramid.size() > 40 && good < 0.2,
            fmt("medijan %.4f px kroz %zu uglova", good, withPyramid.size()));

        report.check("bez piramide isti pomak ne ide",
            withoutPyramid.size() < withPyramid.size() / 2 || flatMedian > 2.0,
            fmt("bez piramide %zu pracenih (medijan %.3f px), s piramidom %zu",
                withoutPyramid.size(), flatMedian, withPyramid.size()));
    }

    // -------------------------------------------------------------------------------
    // Nepovezana slika mora biti odbijena
    // -------------------------------------------------------------------------------

    {
        const std::vector<uint8_t> other = render(checkerboard, 0.0f, 0.0f);
        const std::vector<glm::vec2> corners = Engine::detectCorners(view(first));

        size_t accepted = 0;
        for(const glm::vec2& corner : corners){
            glm::vec2 moved;
            if(Engine::trackPoint(view(first), view(other), corner, moved)) ++accepted;
        }

        report.check("pracenje u nepovezanu sliku se odbija",
            accepted < corners.size() / 10,
            fmt("prihvaceno %zu od %zu", accepted, corners.size()));
    }

    // -------------------------------------------------------------------------------
    // Niz kadrova: tragovi zadrzavaju svoj broj
    // -------------------------------------------------------------------------------

    {
        Engine::Tracker tracker;
        const uint32_t frames = 6;
        const float stepX = 2.5f, stepY = -1.5f;

        std::vector<std::vector<uint8_t>> sequence;
        for(uint32_t frame = 0; frame < frames; ++frame){
            sequence.push_back(render(pattern, stepX * float(frame), stepY * float(frame)));
            tracker.addFrame(view(sequence.back()));
        }

        //Za svaki trag: koliko se pomaknuo izmedju prvog i zadnjeg kadra u kojem se vidi
        std::vector<glm::vec2> firstSeen(tracker.trackCount(), glm::vec2(0.0f));
        std::vector<uint32_t> firstFrame(tracker.trackCount(), 0);
        std::vector<double> errors;

        for(const Engine::Observation& observation : tracker.observations()){
            if(firstSeen[observation.point] == glm::vec2(0.0f) && firstFrame[observation.point] == 0){
                firstSeen[observation.point] = observation.pixel;
                firstFrame[observation.point] = observation.camera;
                continue;
            }
            const float steps = float(observation.camera) - float(firstFrame[observation.point]);
            const glm::vec2 expected = firstSeen[observation.point] + glm::vec2(stepX, stepY) * steps;
            errors.push_back(double(glm::length(observation.pixel - expected)));
        }

        report.check("tragovi prate gibanje kroz niz kadrova",
            tracker.frameCount() == frames && tracker.activeTracks() > 50 && medianOf(errors) < 0.15,
            fmt("%u kadrova, %u tragova pokrenuto, %u jos zivo, medijan %.4f px kroz %zu opazanja",
                tracker.frameCount(), tracker.trackCount(), tracker.activeTracks(), medianOf(errors), errors.size()));
    }

    // -------------------------------------------------------------------------------
    // Piramida se gradi jednom po kadru, a ne po tragu
    //
    // Prva verzija ju je gradila unutar trackPoint, pa se za 800 tragova ista slika smanjivala
    // 1600 puta u svakom kadru: izmjereno 934 ms po kadru naspram 4 ms za dekodiranje. Na
    // trominutnoj snimci to je 70 minuta samo pracenja.
    //
    // Brzina se ovdje ne mjeri - stoperica u testu je nepouzdana i ovisi o stroju. Mjeri se ono
    // sto se MORA drzati: da novi oblik daje BIT-IDENTICAN rezultat starome. Ako se razlikuju,
    // ubrzanje nije ubrzanje nego promjena rezultata
    // -------------------------------------------------------------------------------

    {
        const std::vector<uint8_t> before = render(pattern, 0.0f, 0.0f);
        const std::vector<uint8_t> after = render(pattern, 1.7f, -1.1f);

        const Engine::TrackConfig config;
        const Engine::Pyramid fromPyramid(view(before), config.levels);
        const Engine::Pyramid toPyramid(view(after), config.levels);

        size_t compared = 0, different = 0, agreedOnFailure = 0;
        for(const glm::vec2& corner : Engine::detectCorners(view(before), config)){
            glm::vec2 fromImages, fromPyramids;
            const bool a = Engine::trackPoint(view(before), view(after), corner, fromImages, config);
            const bool b = Engine::trackPoint(fromPyramid, toPyramid, corner, fromPyramids, config);

            if(a != b){ ++different; continue; }
            if(!a){ ++agreedOnFailure; continue; }

            ++compared;
            //Bit po bit, ne "dovoljno blizu": isti racun mora dati isti broj
            if(fromImages.x != fromPyramids.x || fromImages.y != fromPyramids.y) ++different;
        }

        report.check("gotova piramida daje bit-identican rezultat",
            compared > 0 && different == 0,
            fmt("%zu uglova isto do zadnjeg bita, %zu odbijeno s obje strane, %zu razlika",
                compared, agreedOnFailure, different));
    }


    // -------------------------------------------------------------------------------
    // Prozor koji je i zaokrenut i rastegnut: ono zbog cega afino postoji
    // -------------------------------------------------------------------------------
    //
    // Ovo je tocno slucaj koji je S9 nasao mjerenjem: kad se izmedju dva kadra promijeni i
    // perspektiva, prozor se izoblici. Pomak sam nema cime to opisati, pa razliku upise u jedino
    // sto ima - u pomak - i vrh otklizne. Ovdje se to izoblicenje ZADA, pa se vidi koliko svaki
    // od dva nacina vrati

    {
        const glm::vec2 centre{float(width) / 2.0f, float(height) / 2.0f};
        const float angle = glm::radians(4.0f);
        //Zaokret pa rastezanje: 6 posto u jednom smjeru, 4 posto skupljanja u drugom
        const glm::mat2 turn{std::cos(angle), std::sin(angle), -std::sin(angle), std::cos(angle)};
        const glm::mat2 stretch{1.06f, 0.0f, 0.0f, 0.96f};
        const glm::mat2 linear = turn * stretch;
        const glm::vec2 shift{2.3f, -1.4f};

        const std::vector<uint8_t> before = render(pattern, 0.0f, 0.0f);
        const std::vector<uint8_t> after = renderWarped(pattern, linear, shift, centre);

        Engine::TrackConfig config;
        const Engine::Pyramid fromPyramid(view(before), config.levels);
        const Engine::Pyramid toPyramid(view(after), config.levels);

        std::vector<double> affineMiss, shiftOnlyMiss;
        for(const glm::vec2& corner : Engine::detectCorners(view(before), config)){
            const glm::vec2 truth = warpedPlace(linear, shift, centre, corner);

            Engine::TrackTemplate anchor(fromPyramid, corner, config);
            Engine::AffineWarp warp;
            if(!anchor.empty() && Engine::trackAffine(anchor, toPyramid, warp, config)){
                affineMiss.push_back(double(glm::length(corner + warp.shift - truth)));
            }

            glm::vec2 moved;
            if(Engine::trackPoint(fromPyramid, toPyramid, corner, moved, config)){
                shiftOnlyMiss.push_back(double(glm::length(moved - truth)));
            }
        }

        //Prag je apsolutan jer je istina egzaktna: desetina piksela je ono sto se od pracenja trazi
        report.check("afino vraca zaokrenut i rastegnut prozor",
            !affineMiss.empty() && medianOf(affineMiss) < 0.1,
            fmt("%zu uglova, medijan promasaja %.4f px", affineMiss.size(), medianOf(affineMiss)));

        report.check("pomak sam tu vidljivo promasi",
            !shiftOnlyMiss.empty() && medianOf(shiftOnlyMiss) > 3.0 * medianOf(affineMiss),
            fmt("samo pomak %.4f px, afino %.4f px",
                medianOf(shiftOnlyMiss), medianOf(affineMiss)));
    }

    // -------------------------------------------------------------------------------
    // Sidro naspram lanca: greska koja se zbraja i greska koja se ne zbraja
    // -------------------------------------------------------------------------------
    //
    // Ulancano pracenje usporedjuje kadar N s kadrom N-1, pa svaka mala greska udje u polaznu
    // tocku sljedece usporedbe i ostane tamo zauvijek. Sidreno svaki kadar mjeri od kadra
    // RODJENJA, pa greska u kadru N ne truje kadar N+1. Kroz dvanaest kadrova se ta razlika mora
    // vidjeti, i to na istim slikama za oba nacina

    {
        const glm::vec2 centre{float(width) / 2.0f, float(height) / 2.0f};
        const uint32_t steps = 12;

        auto warpOfFrame = [&](uint32_t frame){
            const float angle = glm::radians(0.5f * float(frame));
            const float grow = 1.0f + 0.004f * float(frame);
            const glm::mat2 turn{std::cos(angle), std::sin(angle), -std::sin(angle), std::cos(angle)};
            return turn * glm::mat2{grow, 0.0f, 0.0f, grow};
        };
        auto shiftOfFrame = [&](uint32_t frame){
            return glm::vec2{0.7f * float(frame), -0.4f * float(frame)};
        };

        auto run = [&](bool affine){
            Engine::TrackConfig config;
            config.affine = affine;
            Engine::Tracker tracker(config);

            std::vector<std::vector<uint8_t>> frames;
            for(uint32_t frame = 0; frame < steps; ++frame){
                frames.push_back(renderWarped(pattern, warpOfFrame(frame), shiftOfFrame(frame), centre));
                tracker.addFrame(view(frames.back()));
            }

            //Gdje je svaki trag ROĐEN - samo tragovi iz nultog kadra imaju poznatu istinu
            std::vector<glm::vec2> born(tracker.trackCount(), glm::vec2(-1.0f));
            for(const Engine::Observation& one : tracker.observations()){
                if(one.camera == 0) born[one.point] = one.pixel;
            }

            std::vector<double> miss;
            for(const Engine::Observation& one : tracker.observations()){
                if(one.camera != steps - 1) continue;
                if(born[one.point].x < 0.0f) continue;
                const glm::vec2 truth = warpedPlace(warpOfFrame(steps - 1), shiftOfFrame(steps - 1),
                                                    centre, born[one.point]);
                miss.push_back(double(glm::length(one.pixel - truth)));
            }
            return miss;
        };

        const std::vector<double> anchored = run(true);
        const std::vector<double> chained = run(false);

        report.check("sidreno pracenje drzi tocku kroz niz kadrova",
            !anchored.empty() && medianOf(anchored) < 0.2,
            fmt("%zu tragova, medijan promasaja %.4f px nakon %u kadrova",
                anchored.size(), medianOf(anchored), steps));

        report.check("ulancano pracenje zbraja gresku",
            !chained.empty() && medianOf(chained) > 2.0 * medianOf(anchored),
            fmt("ulancano %.4f px, sidreno %.4f px",
                medianOf(chained), medianOf(anchored)));
    }

    // -------------------------------------------------------------------------------
    // Izrodjen prozor se prijavi kao izgubljen, a ne kao nadjen
    // -------------------------------------------------------------------------------
    //
    // Bez ove ograde afini warp rado stanji prozor u crtu i onda se "savrseno" poklopi s bilo
    // cime. Trag koji je pobjegao mora reci da je pobjegao - krivi broj je gori od nijednog

    {
        const glm::vec2 centre{float(width) / 2.0f, float(height) / 2.0f};
        const glm::mat2 tooMuch{1.6f, 0.0f, 0.0f, 1.0f};   //preko maxStretch, koji je 1.1

        const std::vector<uint8_t> before = render(pattern, 0.0f, 0.0f);
        const std::vector<uint8_t> after = renderWarped(pattern, tooMuch, glm::vec2(0.0f), centre);

        Engine::TrackConfig config;
        const Engine::Pyramid fromPyramid(view(before), config.levels);
        const Engine::Pyramid toPyramid(view(after), config.levels);

        size_t accepted = 0, rejected = 0;
        for(const glm::vec2& corner : Engine::detectCorners(view(before), config)){
            Engine::TrackTemplate anchor(fromPyramid, corner, config);
            if(anchor.empty()) continue;
            Engine::AffineWarp warp;
            if(Engine::trackAffine(anchor, toPyramid, warp, config)) ++accepted;
            else ++rejected;
        }

        report.check("rastezanje preko granice se prijavi kao gubitak",
            rejected > 0 && accepted < rejected / 4,
            fmt("%zu odbijeno, %zu prihvaceno", rejected, accepted));
    }

    return report.result();
}
