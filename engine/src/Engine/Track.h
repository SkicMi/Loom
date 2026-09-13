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

    //AFINO PRACENJE. Pomak sam pretpostavlja da okolina ugla izgleda isto iz kadra u kadar - a kad
    //se kamera pomakne u stranu, ta se okolina i rastegne i zakosi. LK to ne moze opisati, pa
    //razliku upise u jedino sto ima: u pomak. Vrh sustavno otklizne, i to u istom smjeru svaki
    //kadar, pa se greska ZBRAJA. S afinim warpom prozor smije promijeniti oblik, i pomak ostaje
    //pomak.
    //
    //Iskljucivo za usporedbu: s false se dobije stari, samo-pomak tracker
    bool affine = true;

    //Koliko se prozor smije rastegnuti prije nego se trag proglasi izgubljenim. Afini warp bez
    //ogranicenja rado pobjegne u degeneraciju - prozor se stanji u crtu i "savrseno" poklopi s
    //bilo cime.
    //
    //TIJESNO JE BOLJE, i to je bilo suprotno od ocekivanog. Labava ograda pusti izrodjene tragove
    //da zive, pa aktivnih nikad ne padne ispod minTracks i dopune nema; tijesna ih pobije rano i
    //zamijeni svjezima. Izmjereno, rotacija kao medijan:
    //
    //   maxStretch    fina 0.9 st/kadar   gruba 5.5 st/kadar   duga snimka 180 kadrova
    //     1.05            0.0178 st            0.0294 st            0.1747 st
    //     1.1             0.0196 st            0.3490 st            0.0537 st
    //     1.15            0.0314 st            0.7404 st            0.0651 st
    //     1.2             0.0347 st            0.8281 st            0.1054 st
    //     1.8             0.1145 st            2.1017 st            0.8287 st
    //
    //1.05 je prividno najbolji na finoj snimci, ali tamo gruba ispadne jednako dobra kao fina - a
    //to je bas ono na sto kontrola u test_end_to_end upozorava: ne znaci da je tracker bolji nego
    //da fina vise ne mjeri. Uz to na dugoj snimci daje najgori rezultat od svih. 1.1 je najbolji
    //na dugoj snimci, a duga snimka je ono za sto ovo postoji
    float maxStretch = 1.1f;
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

//=============================================================================================
// Afini warp prozora: gdje se i KAKO okolina ugla nasla u ovom kadru.
//
// Prozor iz kadra rodjenja se preslika u trenutni kadar kao  slika = rodjen + linear*x + shift,
// gdje je x pomak unutar prozora. Cisti pomak je poseban slucaj s linear = jedinicna matrica.
// Sam vrh je x = 0, dakle rodjen + shift - zato pracena tocka i dalje izlazi iz jednog zbroja.
//=============================================================================================
struct AffineWarp{
    glm::mat2 linear = glm::mat2(1.0f);
    glm::vec2 shift = glm::vec2(0.0f);
};

//=============================================================================================
// Sto se o tragu zapamti u kadru u kojem je rodjen.
//
// SIDRO, a ne lanac. Stari tracker je usporedjivao kadar N s kadrom N-1, pa je svaka mala greska
// ulazila u polaznu tocku sljedece usporedbe i ostajala tamo zauvijek. Ovdje se svaki kadar
// usporedjuje s kadrom RODJENJA traga: greska u kadru N vise ne truje kadar N+1, nego se svaki
// put mjeri iznova od istog sidra. Drift se time ne smanjuje nego nema odakle nastati.
//
// Cijena je da se izgled izmedju rodjenja i sada moze jako razlikovati - a bas to afini warp i
// opisuje. Zato ove dvije stvari idu zajedno: sidrenje trazi afino, afino omogucuje sidrenje.
//
// ZASTO SE PAMTE GRADIJENTI A NE GOTOVE SD SLIKE. Inverzno-kompozicijski LK racuna gradijente i
// Hessian JEDNOM, na predlosku, umjesto u svakoj iteraciji na slici. Sest SD slika bi bilo sest
// polja po nivou; iz gradijenta i poznatog (x,y) se dobiju s po sest mnozenja, pa se pamte dva
// polja umjesto sest - uz 600 tragova to je razlika izmedju 19 i 9 MB
//=============================================================================================
class TrackTemplate{
    public:
    TrackTemplate() = default;
    TrackTemplate(const Pyramid& pyramid, const glm::vec2& point, const TrackConfig& config);

    bool empty() const {return steps.empty();}
    const glm::vec2& origin() const {return birth;}

    private:
    friend bool trackAffine(const TrackTemplate&, const Pyramid&, AffineWarp&, const TrackConfig&);

    struct Level{
        std::vector<float> values;
        std::vector<float> gradientX;
        std::vector<float> gradientY;
        std::vector<double> hessian;   //6x6, po recima
        glm::vec2 centre{0.0f};
    };
    std::vector<Level> steps;
    glm::vec2 birth{0.0f};
};

//Gdje se predlozak nasao u ovom kadru. warp ulazi kao pretpostavka (obicno onaj iz proslog kadra)
//i izlazi popravljen. False kad je trag izgubljen: izasao je iz slike, prozor se izrodio ili se
//okolina previse promijenila
bool trackAffine(const TrackTemplate& templ, const Pyramid& to, AffineWarp& warp,
                 const TrackConfig& config = {});

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

        //Sidro i warp postoje samo kad je config.affine; inace se nosi samo position, kao prije
        TrackTemplate anchor;
        AffineWarp warp;
    };

    TrackConfig config;
    Pyramid previousPyramid;         //samo za stari, ulancani nacin; sidrenom pracenju ne treba

    std::vector<Active> active;
    std::vector<Observation> collected;
    uint32_t frames = 0;
    uint32_t nextTrack = 0;
};

}
