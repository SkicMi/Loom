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

    //SUBPIKSELNI POLOZAJ UGLA: Foerstner na istoj slici (vidi detectCorners). Zadano iskljuceno,
    //pa je dosadasnje pracenje bit po bit isto; ukljucuje ga VideoSolve za graf poklapanja, gdje
    //se uglovi traze na smanjenoj slici i bez ovoga su na 4K tocni na cetiri piksela
    bool subpixel = false;

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

    //Na koliko se celija po strani dijeli slika kad se dopunjavaju tragovi. Trazi se samo u
    //celijama koje su ostale prazne - vidi komentar uz dopunu u addFrame
    uint32_t detectGrid = 8;

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

    //=========================================================================================
    // NAJMANJA JACINA UGLA, IZRAZENA U SUMU SLIKE. Nula iskljucuje.
    //
    // Prag quality iznad je RELATIVAN prema najboljem uglu u slici, i to je dobro protiv razlike
    // u osvjetljenju - ali ne kaze nista o tome je li ono sto je naslo uopce struktura. Na kadru
    // s jednim jakim kutom je jedan posto od njega vrlo malo, pa prolazi i sum na praznom zidu.
    //
    // Izmjereno posljedicom: nas oblak tocaka ima tocke rasute po praznom bijelom zidu, gdje
    // strukture nema; COLMAP-ove sjede na fugama kamena i rubovima. Takve tocke kvare i geometriju
    // i splat, jer trener splatova od njih krece.
    //
    // KAKO SE IZVODI. Bodovanje je Shi-Tomasi: manja svojstvena vrijednost tezinjene strukturne
    // matrice. Za cisti sum odstupanja s od centralne razlike ima varijancu s^2 / 2, pa obje
    // svojstvene vrijednosti ocekivano iznose (s^2 / 2) * W^2, gdje je W zbroj tezina po osi.
    // Ovaj broj je koliko puta jaci od toga ugao mora biti da bi se uopce racunao.
    //
    // Sum se MJERI po slici (estimateNoise), ne pretpostavlja - ista postavka time vrijedi i za
    // svijetlu i za tamnu snimku.
    //
    // ZADANO ISKLJUCENO, i to je najzanimljiviji nalaz od svih. Prag POPRAVLJA poze i tragove:
    //
    //   jacina   kamere    tragovi   polozaj   rotacija
    //     0     101/101      5.16      1.6 %    6.60 st
    //     1      93/101      5.95      1.4 %    4.32 st
    //     2     101/101      5.95      1.5 %    5.06 st
    //     3     101/101      6.01      1.3 %    6.37 st
    //     4     101/101      6.09      2.2 %    9.12 st
    //     6     101/101      6.31     32.1 %  132.53 st
    //
    // A KVARI SPLAT, koji je jedino sto se isporucuje:
    //
    //   bez praga    PSNR 26.81 dB, najgori 24.69, SSIM 0.861
    //   uz prag 2x   PSNR 26.34 dB, najgori 22.35, SSIM 0.853
    //
    // Objasnjenje koje se slaze s oboje: slabe tocke na praznom zidu jesu geometrijski slabe - i
    // zato su izgledale kao smece kad su se nacrtale preko kadra - ali treneru splatova daju
    // POKRIVENOST. Bez njih zid nema od cega poceti, pa MCMC gaussiane mora dovlaciti izdaleka.
    //
    // Poanta je sira od ovog polja: slaganje poza s COLMAP-om NIJE pouzdan pokazatelj kvalitete
    // splata, i sve sto se ovdje mijenja mora se na kraju izmjeriti u decibelima
    //=========================================================================================
    float minStrengthOverNoise = 0.0f;
};

//Uglovi koje se isplati pratiti, najjaci prvi
std::vector<glm::vec2> detectCorners(const GrayImage& image, const TrackConfig& config = {});

//=============================================================================================
// KUT DOTJERAN ISPOD PIKSELA, iz priblizne pozicije.
//
// ZASTO OVO TREBA. Graf poklapanja trazi i poklapa na smanjenoj slici, jer na punoj razlucivosti
// detektor hvata sum senzora koji se izmedju kadrova ne ponavlja - udio ispravnih poklapanja ide
// s 36 na 82 posto. Ali polozaj se time kvantizira na onoliko piksela koliko je smanjenje: na 4K
// smanjenom cetiri puta, svaka je znacajka tocna na cetiri piksela. COLMAP-ove su subpikselne, i
// ta razlika ide ravno u tocnost poza.
//
// KAKO. Kod pravog kuta gradijent je okomit na smjer ruba, pa za svaki piksel p okoline vrijedi
// g(p) . (p - q) = 0, gdje je q kut. Vise takvih uvjeta daje sustav 2x2:
//
//     q = (suma g g') ^-1 * (suma g g' p)
//
// Isti racun koji stoji iza cornerSubPixa. Ponovi se nekoliko puta jer se okolina racuna oko
// trenutne procjene.
//
// GRANICA POMAKA JE NUZNA, ne oprez: ako u okolini nema pravog kuta nego rub ili sum, sustav je
// slabo uvjetovan i rjesenje odleti. Tko se pomakne dalje od maxShift, vraca se na pocetak -
// bolje kvantiziran polozaj nego pogresan
//=============================================================================================
glm::vec2 refineCorner(const GrayImage& image, const glm::vec2& start,
                       uint32_t window = 5, float maxShift = 4.0f, uint32_t iterations = 4);

//=============================================================================================
// ISTI DETALJ U DRUGOM KADRU, polazeci od vec poznatog polozaja.
//
// ZASTO OVO, KAD POSTOJI refineCorner. Onaj dotjeruje ugao SAM ZA SEBE, unutar jedne slike - i to
// je izmjereno stetilo: na 4K su unutar cetiri piksela kvantizacije jos dva ili tri ugla, pa se
// dva opazanja istog traga zalijepe na RAZLICITE. Trag koji je bio kvantiziran ali dosljedan
// postane tocan ali nedosljedan, a triangulaciji treba ovo drugo.
//
// Ovdje se zakrpa iz referentnog kadra dotjeruje PREMA DRUGOM KADRU, pa svi clanovi traga opisuju
// istu fizicku tocku - makar ona bila pola piksela pokraj vrha ugla. Za triangulaciju je vazna
// dosljednost, ne poklapanje s vrhom.
//
// BEZ PIRAMIDE, I TO NAMJERNO. Polozaj je vec poznat na nekoliko piksela tocno; trazi se ostatak.
// Grubi nivo piramide bi na toj skali otisao na susjedni ugao - isti kvar koji refineCorner ima.
//
// position ulazi kao procjena u drugoj slici i izlazi dotjeran. False znaci da se nije dalo:
// zakrpa bez teksture, izlazak iz slike, ili pomak veci od maxShift - i tada position ostaje
// nedirnut
//=============================================================================================
bool refineToward(const GrayImage& reference, const GrayImage& image,
                  const glm::vec2& at, glm::vec2& position,
                  float maxShift, const TrackConfig& config = {});

//=============================================================================================
// Koliko slika ima suma, u istim jedinicama u kojima su pikseli.
//
// ZASTO OVO TREBA PRACENJU. Afini warp ima sest parametara, a zakrpa ih ne odredjuje jednako
// dobro: pomak se vidi uvijek, a rastezanje samo ako u zakrpi ima strukture u oba smjera. Kad je
// nema, razliku popunjava sum - i linearni dio odluta iako se u slici nista nije izoblicilo.
// Izmjereno na pravoj snimci: 78 posto tragova je umiralo tako, nakon JEDNOG kadra, gdje stvarno
// izoblicenje ne moze biti ni postotak.
//
// Renderirana slika sum nema, pa se na njoj to nikad nije vidjelo. Zato mjera mora doci IZ SLIKE,
// a ne iz konstante: ista konstanta bi na sintetici bila prevelika, a na snimci premala.
//
// KAKO. Immerkaerov ocjenitelj: slika se prevuce jezgrom koja ponistava sve sto je ravno ili
// linearno, pa ostane samo ono sto se mijenja od piksela do piksela - a to je sum. Medijan
// apsolutne vrijednosti je otporan na rubove, koji bi prosjek napuhali
float estimateNoise(const GrayImage& image);

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
    friend bool trackAffine(const TrackTemplate&, const Pyramid&, AffineWarp&, const TrackConfig&, float);

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
//noise je procjena suma ciljne slike iz estimateNoise. Nula znaci "nema suma" i tada se racuna
//tocno ono sto bi se racunalo bez regularizacije
bool trackAffine(const TrackTemplate& templ, const Pyramid& to, AffineWarp& warp,
                 const TrackConfig& config = {}, float noise = 0.0f);

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
