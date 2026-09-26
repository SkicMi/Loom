#pragma once
//=============================================================================================
// LOOMTRACER - path tracer.
//
// Za svaki piksel i svaki uzorak jedna putanja svjetla, od kamere unatrag:
//
//   1. zraka iz kamere (piksel s filtrom sirine 1.5 px, tocka na leci za dubinsku ostrinu)
//   2. na svakoj plohi DVA nacina da se nadje svjetlo, spojena visestrukim uzorkovanjem po
//      vaznosti (MIS, Veach 1995, potencija 2):
//        - izravno: izabere se svjetlo (sunce, kugla, reflektor, svijetleci trokut, nebo po
//          vaznosti) i zraka sjene prema njemu
//        - kroz BSDF: novi smjer odbijanja; ako pogodi svjetlo, i to se broji
//      Svaki nacin je dobar ondje gdje je drugi los (malo svjetlo i hrapava ploha / veliko
//      svjetlo i zrcalo), a MIS uzme bolji bez ijednog praga
//   3. dalje po putanji do najvise maxBounces odbijanja; od treceg ruski rulet prekida putanje
//      koje malo nose, bez pristranosti (preostale se pojacaju)
//
// Sto se namjerno NE radi, jer bi bilo lijepo a ne istinito: nema ambijentalnog svjetla, nema
// "fill" svjetla, nema zatamnjivanja kutova. Sve sto se vidi dolazi od svjetala scene i neba.
// Jedini pristrani zahvat je ogranicenje neizravnih doprinosa (indirectClamp) - krijesnice od
// rijetkih kaustika. Iskljucuje se s 0.
//
// PROGRESIVNO. Uzorci se racunaju u prolazima (1, 1, 2, 4, ... 32 po pikselu), a izmedju
// prolaza se slika moze procitati - editor tako pokazuje render koji se cisti. Radi na svim
// jezgrama, po plocicama 32x32; prekid se provjerava po plocici.
//
// DETERMINIZAM. Isti broj uzoraka i ista scena daju isti rezultat bit po bit, bez obzira na
// broj dretvi: svaki piksel ima svoj Sobolov niz iz svoje adrese, a plocice su disjunktne.
//=============================================================================================
#include "Tracer/Bvh.h"
#include "Tracer/Compiled.h"
#include "Tracer/Environment.h"
#include "Tracer/Film.h"
#include "Tracer/Scene.h"

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

namespace Tracer{

struct RenderSettings{
    uint32_t samples = 128;                 //po pikselu
    uint32_t maxBounces = 12;
    float indirectClamp = 16.0f;            //najveca luminancija jednog neizravnog doprinosa; 0 = bez
    uint32_t threads = 0;                   //0 = sve jezgre
    uint32_t seed = 0;                      //drugi seed, drugi (jednako dobar) sum

    //PRILAGODLJIVO UZORKOVANJE. Piksel je "gotov" kad mu je standardna greska procjene, mjerena
    //kao na zaslonu (sqrt(var/n) / sqrt(srednja luminancija)), ispod praga. Provjera svakih 8
    //uzoraka od adaptiveMinSamples. Piksel STANE tek kad je gotov sad i na prosloj provjeri, i
    //kad je svih 8 susjeda bilo gotovo na prosloj provjeri: sam piksel ne razlikuje crno od
    //rijetkog dogadjaja (magla u kojoj 32 uzorka nista ne pogode ima varijancu 0), susjedi da.
    //Susjedi se citaju s PROSLE provjere, pa je odluka ista na procesoru i kartici i ne ovisi
    //o redoslijedu ni broju dretvi. 0 = iskljuceno
    float adaptiveThreshold = 0.0f;
    uint32_t adaptiveMinSamples = 32;

    //STAKLENE SJENE. Zraka sjene prolazi kroz prozirne materijale (transmission) oslabljena
    //bojom i Fresnelom na svakoj plohi, pa staklo baca svijetlu obojenu sjenu umjesto crne.
    //Kaustike (putanja plohe -> kroz staklo -> svjetlo) se tada NE broje - svjetlo kroz staklo
    //je vec doslo zrakom sjene; bez toga bi se brojalo dvaput. Pristrano (nema fokusiranja
    //svjetla iza lece), ali bez suma - kao "caustics off" u produkcijskim rendererima
    bool glassShadows = false;

    //MIPMAPE po stoscu zrake: udaljena ili kosa tekstura se usrednji umjesto da titra (aliasing)
    //i sumi. false: uvijek osnovna razina (za usporedbu)
    bool mipmaps = true;

    //MNOGO SVJETALA: kandidata po izravnom svjetlu za RIS (jedna zraka sjene za najboljeg po
    //doprinosu bez sjene). 0 = sam: 8 kad scena ima 16 ili vise lokalnih svjetala, inace 1
    uint32_t lightCandidates = 0;
    //Stablo svjetala (false: izbor samo po snazi, kao prije - za usporedbu; kartica uvijek stablo)
    bool lightTree = true;
    //Ekviangularno uzorkovanje u magli prema lokalnim svjetlima, s MIS-om prema slobodnom putu
    //(Kulla & Fajardo 2012): stozac reflektora i sjaj oko lampe bez tockastog suma. false: samo
    //slobodni put (za usporedbu)
    bool equiangular = true;
};

//Koliko kandidata stvarno (RenderSettings::lightCandidates, 0 = prema broju lokalnih svjetala)
uint32_t lightCandidatesFor(const RenderSettings& settings, const CompiledScene& scene);

struct RenderProgress{
    uint32_t samplesDone = 0;
    uint32_t samplesTotal = 0;
    double seconds = 0.0;
    uint64_t rays = 0;
};

//Je li procjena piksela ispod praga suma (isto u shaders/tracer.slang, adaptiveConverged)
bool adaptiveConverged(double luminanceSum, double luminance2Sum, double samples, float threshold);

class Renderer{
public:
    //Gradi BVH, popis svjetala i tablice neba (compile). Scena se preuzima (move)
    explicit Renderer(Scene scene);
    //Vec prevedena scena - ista koju moze dobiti i GPU tracer
    explicit Renderer(std::shared_ptr<const CompiledScene> scene);
    ~Renderer();

    //Racuna do settings.samples uzoraka ili do prekida. onPass se zove poslije svakog prolaza, iz
    //iste dretve - smije pozvati frame(). Moze se pozvati ponovno s vecim brojem uzoraka: nastavlja
    void render(const RenderSettings& settings, const std::function<void(const RenderProgress&)>& onPass = {},
                const std::atomic<bool>* cancel = nullptr);

    //Trenutno stanje nakupljenog. denoise: A-trous filtar (vidi Denoise.h)
    Frame frame(bool denoise = false) const;

    uint32_t samplesDone() const {return done;}
    //Prosjecan broj uzoraka po pikselu (manji od samplesDone kad prilagodljivo uzorkovanje radi)
    double averageSamples() const;
    const Scene& scene() const {return compiled->world;}
    const Bvh& bvh() const {return compiled->tree;}
    double buildSeconds() const {return compiled->buildSeconds;}
    const std::shared_ptr<const CompiledScene>& compiledScene() const {return compiled;}

    //Radijancija jedne zrake iz kamere kroz piksel (za testove): isti put kao render, bez filma
    glm::vec3 tracePixel(glm::vec2 pixel, uint32_t sampleIndex) const;

private:
    struct Accumulator;
    struct PathResult;

    std::shared_ptr<const CompiledScene> compiled;
    uint32_t done = 0;
    std::vector<Accumulator> pixels;
    std::atomic<uint64_t> rayCount{0};
    bool glass = false, mipmaps = true;
    uint32_t candidates = 1;
    bool useLightTree = true;
    bool equiangularSampling = true;
    //Prilagodljivo: po pikselu bit 1 i 2 = gotov na parnoj / neparnoj provjeri, 4 = stao
    std::vector<uint8_t> adaptiveState;
    void adaptiveCheckpoint(uint32_t samples);
    float adaptiveThreshold = 0.0f;
    uint32_t adaptiveMinSamples = 32;

    void renderPixel(uint32_t x, uint32_t y, uint32_t firstSample, uint32_t lastSample, uint32_t seed,
                     float clamp, uint32_t maxBounces, uint64_t& rays);
    PathResult trace(glm::vec2 pixel, uint32_t sampleIndex, uint32_t pixelSeed, float clamp, uint32_t maxBounces,
                     uint64_t& rays) const;
};

}
