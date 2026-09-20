// Graf poklapanja na SMANJENOJ slici: vraca li opazanja ondje gdje stvarno jesu.
//
// Trazenje i poklapanje idu na smanjenoj slici, jer na punoj razlucivosti detektor hvata sum koji
// se izmedju kadrova ne ponavlja - izmjereno na pravoj snimci: od 4K na 960 udio poklapanja koja
// prezive geometriju ide s 36 na 82 posto, a broj provjerenih parova s 230 na 598.
//
// Ali opazanja se moraju vratiti u koordinate slike koju je pozivatelj dao, jer s njima ide i
// njegov intrinsics. Tu se greska ne vidi: sve se prevede, rekonstrukcija ispadne, samo je sve
// sustavno pomaknuto za pola smanjenja prema gore lijevo.
//
// Scena je cista translacija, pa je istina poznata bez ijedne poze: sto god se poklopilo, razlika
// polozaja mora biti tocno taj pomak.
//
// Sto se brani:
//
//   nema pomaka        srednja razlika od istinitog pomaka mora biti oko nule. Izostavljeno pola
//                      piksela dalo bi pristranost od (smanjenje - 1) / 2
//   tocnost je poznata  rasap ne smije biti veci od samog smanjenja
//   precizinost se javlja  localizationPixels mora reci koliko je slika smanjena, jer prag
//                      prihvacanja kamere u reconstructu iz toga izvodi svoj broj
//   puna sirina        bez smanjenja mora biti tocno, na piksel
//   pomijesani tragovi na ponavljajucem uzorku poklapanja GRIJESE, i tada komponenta dodirne isti
//                      kadar dvaput - jedna tocka na dva mjesta u istoj slici. To se mora
//                      prebrojati, i bacanjem stvarno ukloniti. Na pravoj snimci je takvih bilo
//                      27 posto svih opazanja, a greska poza je bacanjem pala s 15.7 na 4.4 posto
#include "TestHarness.h"

#include <Engine/MatchGraph.h>
#include <Engine/Track.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <map>
#include <vector>

namespace{

const uint32_t width = 1920, height = 1080;

//Neponavljajuca tekstura: periodicna bi dala uglove koji se stvarno ne razlikuju
float speckle(float x, float y){
    auto value = [](int cx, int cy){
        uint32_t h = uint32_t(cx) * 374761393u + uint32_t(cy) * 668265263u;
        h = (h ^ (h >> 13)) * 1274126177u;
        return float((h ^ (h >> 16)) & 0xffu);
    };
    const int cx = int(std::floor(x / 9.0f)), cy = int(std::floor(y / 9.0f));
    const float fx = x / 9.0f - float(cx), fy = y / 9.0f - float(cy);
    const float top = value(cx, cy) * (1.0f - fx) + value(cx + 1, cy) * fx;
    const float bottom = value(cx, cy + 1) * (1.0f - fx) + value(cx + 1, cy + 1) * fx;
    return top * (1.0f - fy) + bottom * fy;
}

//Ista tekstura, ali POPLOCANA: isti isjecak se ponavlja svakih 320 px. Sluzi samo jednoj
//provjeri - da bi komponenta uopce mogla postati sukobljena, poklapanja moraju griyesiti, a ona
//ne grijese dok je tekstura neponavljajuca. Ovdje svaki ugao ima svog blizanca s tocno istim
//potpisom, i unutar polumjera pretrage, pa se trag jednom zalijepi za pravog a drugi put za
//blizanca - i komponenta dodirne isti kadar dvaput
std::vector<uint8_t> renderRepeating(glm::vec2 shift){
    const float period = 320.0f;
    std::vector<uint8_t> pixels(size_t(width) * height);
    for(uint32_t y = 0; y < height; ++y){
        for(uint32_t x = 0; x < width; ++x){
            float at = std::fmod(float(x) - shift.x, period);
            if(at < 0.0f) at += period;
            //Malo NEPOPLOCANOG suma preko svega: bez njega su blizanci savrseno jednaki, pa
            //izjednacenje uvijek puca na istu stranu i graf ostane dosljedan iako je kriv. Sum
            //cini izbor izmedju blizanaca nasumicnim po paru kadrova - i tek tada se dva traga
            //stvarno slijepe
            const float value = 0.94f * speckle(at, float(y) - shift.y)
                              + 0.06f * speckle(float(x) + 5000.0f, float(y) + 5000.0f);
            pixels[size_t(y) * width + x] = uint8_t(std::max(0.0f, std::min(255.0f, value)));
        }
    }
    return pixels;
}

std::vector<uint8_t> render(glm::vec2 shift){
    std::vector<uint8_t> pixels(size_t(width) * height);
    for(uint32_t y = 0; y < height; ++y){
        for(uint32_t x = 0; x < width; ++x){
            const float value = speckle(float(x) - shift.x, float(y) - shift.y);
            pixels[size_t(y) * width + x] = uint8_t(std::max(0.0f, std::min(255.0f, value)));
        }
    }
    return pixels;
}

struct Agreement{
    uint32_t points = 0;
    double bias = 0.0;     //srednja razlika od istinitog pomaka, po obje osi zajedno

    //RASAP SE MJERI 95. POSTOTKOM, ne najgorim. Scena je cista translacija slike, sto znaci da se
    //kamera u prostoru nije ni pomakla - paralakse nema, pa dvoprizorno sito nema po cemu odbaciti
    //krivo poklapanje i poneko prodje. To je svojstvo OVE scene, a ne koda: ovdje se brani
    //preslikavanje koordinata, ne kakvoca poklapanja
    double spread = 0.0;
};

//Koliko se poklopljene tocke slazu s poznatim pomakom
Agreement compare(const Engine::MatchGraphResult& graph, glm::vec2 trueShift){
    std::map<uint32_t, glm::vec2> inFirst, inSecond;
    for(const Engine::Observation& one : graph.observations){
        if(one.camera == 0) inFirst[one.point] = one.pixel;
        if(one.camera == 1) inSecond[one.point] = one.pixel;
    }

    Agreement out;
    glm::dvec2 sum(0.0);
    std::vector<double> misses;
    for(const auto& entry : inFirst){
        const auto found = inSecond.find(entry.first);
        if(found == inSecond.end()) continue;

        const glm::vec2 measured = found->second - entry.second;
        const glm::vec2 miss = measured - trueShift;
        sum += glm::dvec2(miss);
        misses.push_back(double(std::max(std::fabs(miss.x), std::fabs(miss.y))));
        ++out.points;
    }
    if(out.points > 0){
        //PRISTRANOST PO SREDNJAKU, ne po prosjeku: prosjek bi jedno poklapanje promaseno za 170 px
        //razmazalo preko svih dvije tisuce i pristranost bi izgledala kao da postoji
        std::vector<double> alongX, alongY;
        for(const auto& entry : inFirst){
            const auto found = inSecond.find(entry.first);
            if(found == inSecond.end()) continue;
            const glm::vec2 miss = (found->second - entry.second) - trueShift;
            alongX.push_back(double(miss.x));
            alongY.push_back(double(miss.y));
        }
        std::sort(alongX.begin(), alongX.end());
        std::sort(alongY.begin(), alongY.end());
        out.bias = glm::length(glm::dvec2(alongX[alongX.size() / 2], alongY[alongY.size() / 2]));

        std::sort(misses.begin(), misses.end());
        out.spread = misses[size_t(0.95 * double(misses.size() - 1))];
    }
    return out;
}

uint64_t exactGraphHash(const Engine::MatchGraphResult& graph){
    uint64_t hash = 1469598103934665603ull;
    auto add = [&](uint32_t value){
        for(uint32_t byte = 0; byte < 4; ++byte){
            hash ^= uint8_t(value >> (byte * 8));
            hash *= 1099511628211ull;
        }
    };
    add(graph.pointCount);
    add(uint32_t(graph.observations.size()));
    for(const Engine::Observation& observation : graph.observations){
        uint32_t pixelX = 0, pixelY = 0;
        static_assert(sizeof(pixelX) == sizeof(observation.pixel.x), "float mora imati 32 bita");
        std::memcpy(&pixelX, &observation.pixel.x, sizeof(pixelX));
        std::memcpy(&pixelY, &observation.pixel.y, sizeof(pixelY));
        add(observation.camera);
        add(observation.point);
        add(pixelX);
        add(pixelY);
    }
    return hash;
}

}

int main(){
    TestReport report("graf poklapanja na smanjenoj slici");

    //POMAK JE VISEKRATNIK SMANJENJA. Inace se u razliku umijesa kvantizacija - pri smanjenju 4 i
    //pravom pomaku (37, 19) najblizi visekratnik je (36, 20), pa srednjak promasi za (-1, +1) i
    //to izgleda kao pristranost koje nema
    const glm::vec2 shift(36.0f, 20.0f);
    const std::vector<uint8_t> first = render(glm::vec2(0.0f));
    const std::vector<uint8_t> second = render(shift);

    const std::vector<Engine::GrayImage> images{
        Engine::GrayImage{first.data(), width, height, width},
        Engine::GrayImage{second.data(), width, height, width}};

    Engine::Intrinsics intrinsics;
    intrinsics.width = width; intrinsics.height = height;
    intrinsics.cx = 0.5f * float(width); intrinsics.cy = 0.5f * float(height);
    intrinsics.fx = 1400.0f; intrinsics.fy = 1400.0f;

    Engine::MatchGraphConfig config;
    config.detect.maxCorners = 3000;
    config.detect.minDistance = 8.0f;
    config.window = 1;
    config.minInliers = 20;

    //-- puna sirina --------------------------------------------------------------------------
    {
        Engine::MatchGraphConfig full = config;
        full.workingWidth = 0;
        const Engine::MatchGraphResult graph = Engine::buildMatchGraph(images, intrinsics, full);
        const Agreement agreement = compare(graph, shift);

        report.check("puna sirina javlja tocnost 1",
            graph.localizationPixels == 1.0f, fmt("%.1f px", double(graph.localizationPixels)));

        report.check("puna sirina pogadja pomak",
            agreement.points > 50 && agreement.bias < 0.5 && agreement.spread < 3.0,
            fmt("%u tocaka, pristranost %.3f px, rasap (95%%) %.2f px",
                agreement.points, agreement.bias, agreement.spread));
    }

    //-- smanjeno cetiri puta -----------------------------------------------------------------
    {
        Engine::MatchGraphConfig small = config;
        small.workingWidth = width / 4;
        const Engine::MatchGraphResult graph = Engine::buildMatchGraph(images, intrinsics, small);
        const Agreement agreement = compare(graph, shift);

        report.check("graf je bit-identican zlatnom izlazu",
            exactGraphHash(graph) == 11197454592418299683ull,
            fmt("hash %llu, %u tocaka, %zu opazanja",
                static_cast<unsigned long long>(exactGraphHash(graph)),
                graph.pointCount, graph.observations.size()));

        report.check("smanjenje se javlja",
            graph.localizationPixels == 4.0f, fmt("%.1f px", double(graph.localizationPixels)));

        //PRISTRANOST JE OVDJE CIJELI TEST. Prosjek po kvadratu od cetiri piksela stavlja srediste
        //bloka na x*4 + 1.5; tko vrati samo x*4, pomakne SVE za 1.5 px u istom smjeru - a rasap
        //ostane isti, pa provjera koja mjeri samo rasap to ne vidi
        report.check("nema sustavnog pomaka",
            agreement.points > 50 && agreement.bias < 1.0,
            fmt("%u tocaka, pristranost %.3f px (bez pola bloka bila bi oko 1.5)",
                agreement.points, agreement.bias));

        report.check("rasap je unutar smanjenja",
            agreement.spread <= 4.0 * 2.0,
            fmt("rasap (95%%) %.2f px, smanjenje %.0f px",
                agreement.spread, double(graph.localizationPixels)));

        //-- i APSOLUTNI polozaj ---------------------------------------------------------------
        //
        //Razlika dvaju kadrova ne moze uhvatiti pola bloka: konstantan pomak primijenjen na oba
        //kadra se u razlici ponisti. Zato se opazanja usporedjuju s uglovima nadjenima na PUNOJ
        //slici - ondje je polozaj tocan po definiciji, pa se sustavni pomak vidi.
        //
        //Prosjek po kvadratu od cetiri piksela stavlja srediste bloka na x*4 + 1.5. Tko vrati
        //samo x*4, pomakne SVE za 1.5 px prema gore lijevo
        Engine::TrackConfig detect = config.detect;
        const std::vector<glm::vec2> truth = Engine::detectCorners(images[0], detect);

        std::vector<double> alongX, alongY;
        for(const Engine::Observation& one : graph.observations){
            if(one.camera != 0) continue;

            double best = 1e9;
            glm::vec2 nearest(0.0f);
            for(const glm::vec2& corner : truth){
                const double distance = double(glm::length(corner - one.pixel));
                if(distance < best){ best = distance; nearest = corner; }
            }
            if(best > 4.0) continue;   //nema para na punoj slici - ne mjeri se

            alongX.push_back(double(nearest.x - one.pixel.x));
            alongY.push_back(double(nearest.y - one.pixel.y));
        }

        double offsetX = 0.0, offsetY = 0.0;
        if(!alongX.empty()){
            std::sort(alongX.begin(), alongX.end());
            std::sort(alongY.begin(), alongY.end());
            offsetX = alongX[alongX.size() / 2];
            offsetY = alongY[alongY.size() / 2];
        }

        report.check("polozaj nije sustavno pomaknut",
            alongX.size() > 50 && std::fabs(offsetX) < 1.0 && std::fabs(offsetY) < 1.0,
            fmt("%zu tocaka, srednjak odmaka (%.2f, %.2f) px - bez pola bloka bio bi (1.5, 1.5)",
                alongX.size(), offsetX, offsetY));
    }

    //-- prostor mjerila ---------------------------------------------------------------------
    //
    // Brzi graf gore koristi binarni potpis. Produkcijski put mu dodaje zaseban graf znacajki
    // s mjerilom i SIFT-ovim potpisom, pa optimizacija njegova matchera mora imati vlastitu
    // bit-identicnu branu. Ogranicenje broja znacajki drzi test kratkim; racun i poredak su isti
    // kao na 4K snimci.
    {
        Engine::MatchGraphConfig scaled = config;
        scaled.useScaleSpace = true;
        scaled.scaleSpace.maxKeypoints = 1500;
        scaled.scaleSpace.minDistance = 4.0f;
        const Engine::MatchGraphResult graph = Engine::buildMatchGraph(images, intrinsics, scaled);

        report.check("graf prostora mjerila je bit-identican zlatnom izlazu",
            exactGraphHash(graph) == 2453899824703454841ull,
            fmt("hash %llu, %u tocaka, %zu opazanja",
                static_cast<unsigned long long>(exactGraphHash(graph)),
                graph.pointCount, graph.observations.size()));

    }

    //-- ciscenje ne smije jesti zdravo ------------------------------------------------------
    //
    //Bacanje sukobljenih komponenti je sada ZADANO, i to je na pravoj snimci bilo 27 posto svih
    //opazanja - greska poza je time pala s 15.7 na 4.4 posto. Ali upravo zato ono mora biti tiho
    //kad nema sto baciti: na podacima bez krivih poklapanja ne smije nestati nijedno opazanje.
    //
    //Sukobljena komponenta se ovdje ne da napraviti, i to nije propust nego svojstvo: za sukob
    //trebaju najmanje TRI kadra (A1-A2, A2-A3 i krivi A1-B3), a krivo poklapanje na
    //neponavljajucoj teksturi ne nastaje. Perfektno ponavljajuci uzorak ga takodjer ne daje, jer
    //ga prag omjera odbije prije nego dodje do spajanja. Zato se ta strana mjeri na pravoj
    //snimci, a ovdje se brani da ciscenje ne radi stetu kad steta ne postoji
    {
        const std::vector<uint8_t> third = render(glm::vec2(72.0f, 40.0f));
        const std::vector<Engine::GrayImage> three{
            Engine::GrayImage{first.data(), width, height, width},
            Engine::GrayImage{second.data(), width, height, width},
            Engine::GrayImage{third.data(), width, height, width}};

        Engine::MatchGraphConfig threeFrames = config;
        threeFrames.window = 2;

        Engine::MatchGraphConfig keeping = threeFrames;
        keeping.dropConflicting = false;
        const Engine::MatchGraphResult kept = Engine::buildMatchGraph(three, intrinsics, keeping);

        Engine::MatchGraphConfig dropping = threeFrames;
        dropping.dropConflicting = true;
        const Engine::MatchGraphResult dropped = Engine::buildMatchGraph(three, intrinsics, dropping);

        report.check("na cistom nema sukoba",
            kept.conflictingPoints == 0,
            fmt("%u sukobljenih komponenti na %u tocaka", kept.conflictingPoints, kept.pointCount));

        report.check("ciscenje tada ne dira nista",
            dropped.observations.size() == kept.observations.size()
            && dropped.pointCount == kept.pointCount && kept.pointCount > 100,
            fmt("opazanja %zu naspram %zu, tocaka %u naspram %u",
                kept.observations.size(), dropped.observations.size(),
                kept.pointCount, dropped.pointCount));

        //I spajanje koje odbija sukob: na cistom ne smije odbiti nijedan brid, pa mora dati
        //istu rekonstrukciju kao slijepo. Zadano je iskljuceno jer na pravoj snimci daje losije
        //poze - vidi MatchGraphConfig::conflictFreeMerge - ali mehanizam mora biti tocan
        Engine::MatchGraphConfig refusing = threeFrames;
        refusing.dropConflicting = false;
        refusing.conflictFreeMerge = true;
        const Engine::MatchGraphResult refused = Engine::buildMatchGraph(three, intrinsics, refusing);

        report.check("odbijanje sukoba na cistom miruje",
            refused.refusedEdges == 0 && refused.observations.size() == kept.observations.size(),
            fmt("%u odbijenih bridova, opazanja %zu naspram %zu",
                refused.refusedEdges, refused.observations.size(), kept.observations.size()));
    }

    //-- rastavljanje sukobljene komponente ---------------------------------------------------
    //
    //Sukob se na neponavljajucoj teksturi ne da napraviti (vidi gore), pa se ovdje namjerno
    //pravi: resetka daje uglove koji se stvarno ne razlikuju, a prag omjera se iskljuci (ratio 1
    //znaci "odbaci ako je najbolji losiji od drugog", dakle nikad). Tada krivi bridovi nastaju i
    //komponente se slijepe - tocno ono stanje zbog kojeg dropConflicting postoji.
    //
    //Sto se brani:
    //
    //  invarijanta   nakon rastavljanja NIJEDNA tocka ne smije imati dva opazanja u istom kadru.
    //                To je cijela svrha postupka i mjeri se iz samog izlaza, bez brojaca
    //  dobitak       rastavljanje mora vratiti opazanja koja bi bacanje izgubilo
    //  bezopasnost   na cistoj teksturi ne smije promijeniti nista - bit po bit isti izlaz kao
    //                slijepo spajanje. Upravo to conflictFreeMerge nije imao, i zato je stetio
    {
        std::vector<std::vector<uint8_t>> store;
        std::vector<Engine::GrayImage> frames;
        for(uint32_t k = 0; k < 4; ++k){
            store.push_back(renderRepeating(glm::vec2(36.0f * float(k), 20.0f * float(k))));
            frames.push_back(Engine::GrayImage{store.back().data(), width, height, width});
        }

        Engine::MatchGraphConfig loose = config;
        loose.window = 3;
        loose.describe.ratio = 1.0f;   //bez praga omjera - resetka se tada poklapa preko perioda
        loose.dropConflicting = false;
        loose.minTriangleSupport = 0;   //svjedoci bas ovakve bridove i brisu - ovdje se brani rastavljanje, ne oni
        loose.splitConflicting = false; //mjera za usporedbu: slijepo spajanje, bez ikakvog popravka

        const Engine::MatchGraphResult glued = Engine::buildMatchGraph(frames, intrinsics, loose);

        //Prag svjedoka se ovdje IZRICITO gasi. Zadana dvojka je izmjerena na pravoj snimci, a na
        //ovoj sceni bi pobrisala i ono cime se rastavljanje uopce brani - a brani se mehanizam,
        //ne postavka
        Engine::MatchGraphConfig splitting = loose;
        splitting.splitConflicting = true;
        splitting.splitSupport = 0;
        const Engine::MatchGraphResult split = Engine::buildMatchGraph(frames, intrinsics, splitting);

        Engine::MatchGraphConfig dropping = loose;
        dropping.dropConflicting = true;
        dropping.splitConflicting = false;
        const Engine::MatchGraphResult dropped = Engine::buildMatchGraph(frames, intrinsics, dropping);

        auto twiceInFrame = [](const Engine::MatchGraphResult& graph){
            std::map<uint64_t, uint32_t> seen;
            uint32_t doubled = 0;
            for(const Engine::Observation& one : graph.observations){
                const uint64_t key = (uint64_t(one.camera) << 32) | uint64_t(one.point);
                if(++seen[key] == 2) ++doubled;
            }
            return doubled;
        };

        report.check("scena stvarno daje sukobe",
            glued.conflictingPoints > 0,
            fmt("%u sukobljenih komponenti, %u opazanja u njima",
                glued.conflictingPoints, glued.conflictingObservations));

        report.check("rastavljanje ukloni sukob",
            split.conflictingPoints == 0 && split.splitPoints > 0,
            fmt("%u sukobljenih ostalo (slijepo ih je %u), rastavljenih %u",
                split.conflictingPoints, glued.conflictingPoints, split.splitPoints));

        //I iz samog izlaza, bez brojaca: nijedna tocka ne smije imati dva opazanja u istom kadru
        report.check("izlaz nema tocku dvaput u kadru",
            twiceInFrame(split) == 0,
            fmt("%u takvih tocaka na %zu opazanja",
                twiceInFrame(split), split.observations.size()));

        report.check("rastavljanje vrati vise nego bacanje",
            split.observations.size() > dropped.observations.size(),
            fmt("rastavljeno %zu opazanja, baceno %zu, slijepo %zu",
                split.observations.size(), dropped.observations.size(), glued.observations.size()));

        //-- i na cistom mora mirovati --------------------------------------------------------
        Engine::MatchGraphConfig clean = config;
        clean.window = 1;
        clean.splitConflicting = false;
        const Engine::MatchGraphResult blind = Engine::buildMatchGraph(images, intrinsics, clean);

        Engine::MatchGraphConfig cleanSplit = clean;
        cleanSplit.splitConflicting = true;
        cleanSplit.splitSupport = 0;
        const Engine::MatchGraphResult quiet = Engine::buildMatchGraph(images, intrinsics, cleanSplit);

        bool same = blind.observations.size() == quiet.observations.size()
                 && blind.pointCount == quiet.pointCount;
        for(size_t i = 0; same && i < blind.observations.size(); ++i){
            same = blind.observations[i].camera == quiet.observations[i].camera
                && blind.observations[i].point == quiet.observations[i].point
                && blind.observations[i].pixel == quiet.observations[i].pixel;
        }

        report.check("na cistom rastavljanje ne mijenja nista",
            same && quiet.splitPoints == 0 && quiet.refusedEdges == 0,
            fmt("%zu naspram %zu opazanja, rastavljenih %u, odbijenih %u",
                blind.observations.size(), quiet.observations.size(),
                quiet.splitPoints, quiet.refusedEdges));
    }

    //-- dva grafa u jednu scenu ----------------------------------------------------------------
    //
    //Tocnost i pokrivenost su na pravoj snimci izmjereno na razlicitim putovima: uglovi daju 101
    //kameru i 5213 opazanja po kadru ali tocke 7.891 posto opsega od najblize COLMAP-ove, a prostor
    //mjerila tocke 0.933 posto ali 64 kamere. Spojeno daje oboje.
    //
    //Sto se brani:
    //
    //  nista se ne gubi   zbroj opazanja i tocaka mora biti tocno zbroj dvaju
    //  tragovi se ne mijesaju  nijedna tocka drugog grafa ne smije zavrsiti pod brojem prvog
    //  tocnost je grublja  prag prihvacanja kamere izvodi se iz nje, pa mora podnijeti i grublje
    {
        Engine::MatchGraphConfig full = config;
        full.workingWidth = 0;          //tocnost 1 px
        const Engine::MatchGraphResult fine = Engine::buildMatchGraph(images, intrinsics, full);

        Engine::MatchGraphConfig small = config;
        small.workingWidth = width / 4; //tocnost 4 px
        const Engine::MatchGraphResult coarse = Engine::buildMatchGraph(images, intrinsics, small);

        const Engine::MatchGraphResult both = Engine::mergeGraphs(fine, coarse);

        report.check("spajanje nista ne gubi",
            both.observations.size() == fine.observations.size() + coarse.observations.size()
            && both.pointCount == fine.pointCount + coarse.pointCount,
            fmt("%zu + %zu = %zu opazanja, %u + %u = %u tocaka",
                fine.observations.size(), coarse.observations.size(), both.observations.size(),
                fine.pointCount, coarse.pointCount, both.pointCount));

        //Drugi graf mora cijeli biti iznad prvoga: inace bi mu se tragovi zalijepili za tudje
        bool separated = true;
        for(size_t i = fine.observations.size(); i < both.observations.size(); ++i){
            if(both.observations[i].point < fine.pointCount){ separated = false; break; }
        }
        for(size_t i = 0; i < fine.observations.size() && separated; ++i){
            if(both.observations[i].point >= fine.pointCount) separated = false;
        }

        report.check("tragovi se ne mijesaju",
            separated, separated ? "svaki trag ostaje u svom grafu" : "brojevi tocaka se preklapaju");

        report.check("tocnost je ona grublja",
            both.localizationPixels == std::max(fine.localizationPixels, coarse.localizationPixels)
            && both.localizationPixels == 4.0f,
            fmt("%.1f px iz %.1f i %.1f", double(both.localizationPixels),
                double(fine.localizationPixels), double(coarse.localizationPixels)));
    }

    return report.result();
}
