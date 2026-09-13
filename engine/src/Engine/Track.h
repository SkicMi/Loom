#pragma once
#include "Engine/SyntheticScene.h"

#include <cstdint>
#include <vector>

namespace Engine{

//=============================================================================================
// Pracenje uglova kroz kadrove: od piksela do opazanja.
//
// Solver zna sto s opazanjima, ali ih netko mora napraviti - a na videu to znaci naci mjesta koja
// se daju prepoznati i slijediti ih iz kadra u kadar.
//
// ZASTO UGLOVI. Tocka na ravnoj plohi se ne da pratiti: pomakni je u bilo kojem smjeru i slika
// izgleda isto. Rub se da pratiti samo okomito na sebe (problem otvora). Jedino mjesto gdje se
// slika mijenja u OBA smjera je ugao, i to mjeri Shi-Tomasi: manja svojstvena vrijednost
// strukturne matrice kaze koliko je slabiji od dva smjera.
//
// ZASTO PIRAMIDA. Lucas-Kanade pretpostavlja da je pomak malen u odnosu na prozor - inace linearna
// aproksimacija ne vrijedi. Na smanjenoj slici je pomak od dvadeset piksela zapravo pomak od dva,
// pa se krene s najgrubljeg nivoa i rezultat spusta na sljedeci. Bez toga se gubi svaki pomak veci
// od prozora, a to je na snimci iz ruke pravilo.
//
// ENGINE NE ZNA ZA SPOOL, pa ovdje ulaze samo sivi pikseli. Tko ih je dekodirao - video, niz slika
// ili renderer - nije stvar ovog fajla.
//=============================================================================================

//Pogled na sive piksele, 8 bita po pikselu. Ne posjeduje nista
struct GrayImage{
    const uint8_t* pixels = nullptr;
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t stride = 0;   //bajtova po retku; nula znaci width
};

struct TrackConfig{
    //Koliko uglova najvise, i koliko daleko moraju biti jedan od drugoga. Bez razmaka se svi
    //skupe na isti kontrastni detalj, a rekonstrukciji trebaju razasuti po slici
    uint32_t maxCorners = 600;
    float minDistance = 12.0f;

    //Prag je RELATIVAN prema najboljem uglu u slici: apsolutni bi na tamnoj snimci nasao nista,
    //a na kontrastnoj sve
    float quality = 0.01f;

    uint32_t window = 10;       //poluprozor za strukturnu matricu i za Lucas-Kanade
    uint32_t levels = 3;        //nivoa piramide
    uint32_t iterations = 20;
    float maxResidual = 18.0f;  //prosjecna razlika u intenzitetu nakon pracenja

    //Kad aktivnih tragova padne ispod ovoga, trazi se nove. Tragovi se gube - izadju iz kadra,
    //zaklone se, promijene izgled
    uint32_t minTracks = 250;
};

//Uglovi koje se isplati pratiti, najjaci prvi
std::vector<glm::vec2> detectCorners(const GrayImage& image, const TrackConfig& config = {});

//Piramida jedne slike. Postoji kao vlastiti tip iz jednog razloga: gradi se JEDNOM PO KADRU, a ne
//po tragu. Prva verzija ju je gradila unutar trackPoint, pa se za 800 tragova ista slika smanjivala
//1600 puta u svakom kadru - izmjereno 934 ms po kadru, od cega je dekodiranje bilo 4 ms
class Pyramid{
    public:
    Pyramid() = default;
    Pyramid(const GrayImage& image, uint32_t levels);

    uint32_t levels() const;
    GrayImage level(uint32_t index) const;
    bool empty() const;

    private:
    struct Level{
        std::vector<uint8_t> pixels;
        uint32_t width = 0;
        uint32_t height = 0;
    };
    std::vector<Level> steps;
};

//Isti ugao u sljedecem kadru. False kad ga nije nasao: izasao je iz slike, prozor je bez teksture
//ili se okolina previse promijenila.
//
//Dva oblika, i drugi mora davati BIT-IDENTICAN rezultat prvome - to test i provjerava. Prvi je
//zgodan kad se prati jedna tocka, drugi je onaj koji se koristi u nizu kadrova
bool trackPoint(const GrayImage& from, const GrayImage& to,
                const glm::vec2& start, glm::vec2& end, const TrackConfig& config = {});

bool trackPoint(const Pyramid& from, const Pyramid& to,
                const glm::vec2& start, glm::vec2& end, const TrackConfig& config = {});

//Tragovi kroz niz kadrova. Izlaz je tocno ono sto reconstruct trazi: opazanje nosi redni broj
//kadra kao kameru i redni broj TRAGA kao tocku
class Tracker{
    public:
    explicit Tracker(const TrackConfig& config = {});

    //Kadar po kadar, redom. Prvi pokrece tragove, svaki sljedeci ih pomice i po potrebi dopunjava
    void addFrame(const GrayImage& image);

    const std::vector<Observation>& observations() const {return collected;}
    uint32_t frameCount() const {return frames;}
    uint32_t trackCount() const {return nextTrack;}       //ukupno pokrenutih
    uint32_t activeTracks() const {return uint32_t(active.size());}

    private:
    struct Active{
        glm::vec2 position{0.0f};
        uint32_t track = 0;
    };

    TrackConfig config;
    std::vector<uint8_t> previous;   //vlastita kopija: pozivateljev buffer ne mora zivjeti dalje
    uint32_t previousWidth = 0;
    uint32_t previousHeight = 0;
    Pyramid previousPyramid;         //gradjena jednom, kad je kadar stigao - ne po tragu

    std::vector<Active> active;
    std::vector<Observation> collected;
    uint32_t frames = 0;
    uint32_t nextTrack = 0;
};

}
