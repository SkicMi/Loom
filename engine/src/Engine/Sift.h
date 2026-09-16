#pragma once
#include "Engine/Track.h"

#include <array>
#include <cstdint>
#include <vector>

namespace Engine{

//=============================================================================================
// POTPIS OKOLINE PREKO HISTOGRAMA GRADIJENATA (SIFT-ova vrsta).
//
// ZASTO, KAD VEC IMAMO POTPIS. Postojeci je ORB-ove vrste: 256 usporedbi intenziteta dvaju
// piksela. Izmjereno na pravoj snimci sto time dobivamo:
//
//   par susjednih kadrova     27 posto znacajki nadje par
//   razmak od deset kadrova    5 posto
//   razmak 15 do 25            16 do 25 provjerenih parova, ondje gdje COLMAP ima 600 do 760
//
// A sirina baze koja odredjuje tocnost poza lezi bas na tim velikim razmacima - u COLMAP-ovom
// modelu je najjace preklapanje na razmaku 15 do 20 kadrova, jer se tada gleda isti zid s druge
// strane sobe. Prosirivanje prozora bez boljeg potpisa RUŠI rekonstrukciju, jer ondje nas potpis
// daje uglavnom sum.
//
// KAKO. Okolina se podijeli na 4x4 celije, a u svakoj se vodi histogram od 8 smjerova gradijenta.
// Svaki uzorak se rasporedi TROLINEARNO - izmedju dvije susjedne celije po obje osi i izmedju dva
// susjedna smjera - jer bi inace piksel tik uz granicu celije pri najmanjem pomaku skocio u drugu
// i potpis bi se promijenio skokovito. To je razlika u vrsti prema binarnom potpisu: ovaj mjeri
// RASPORED gradijenata, koji prezivi pomak, blagi zaokret i promjenu svjetline.
//
// Tezina uzorka pada Gaussovo od sredista, pa rub okoline - gdje se najprije pojavi nesto drugo -
// nosi manje.
//
// ZASTO uint8 A NE float. Sto dvadeset osam floatova je 512 bajtova po znacajki, a na 101 kadru s
// dvadeset tisuca znacajki to je gigabajt. Nakon normiranja su vrijednosti u 0..1, pa ih 256
// stupnjeva opisuje finije nego sto ih sam potpis razlikuje - isto sto radi i izvorni SIFT
//=============================================================================================

constexpr uint32_t siftCells = 4;       //4x4 prostornih celija
constexpr uint32_t siftBins = 8;        //8 smjerova po celiji
constexpr uint32_t siftLength = siftCells * siftCells * siftBins;   //128

struct SiftDescriptor{
    std::array<uint8_t, siftLength> values{};
    float angle = 0.0f;     //smjer okoline, radijani - okolina se po njemu zakrene
    bool valid = false;
};

struct SiftConfig{
    //Polumjer okoline u pikselima. Cijela okolina je 2r, a jedna celija r/2
    uint32_t patch = 16;

    //ZAGLADJIVANJE PRIJE GRADIJENATA, u pikselima. Nula znaci izvedeno iz zakrpe.
    //
    //Izvorni SIFT gradijente racuna na slici zamucenoj na MJERILO znacajke, ne na sirovim
    //pikselima. Bez toga potpis opisuje najfiniju teksturu, a ona se izmedju dva udaljena pogleda
    //ne ponavlja - ista greska koju smo vec jednom napravili s binarnim potpisom, gdje je zakrpa
    //narasla na 96 px a sigma ostala 1.5.
    //
    //Izvedeno je pola sirine celije: celija je 2r/4, pa sigma ispada r/4
    float smoothing = 0.0f;

    //Prag omjera: najbolji pogodak mora biti barem ovoliko puta blizi od drugog po redu
    float ratio = 0.8f;

    //=========================================================================================
    // KOLIKO DALEKO OD NAJBOLJEG MORA BITI DRUGI PO REDU, u pikselima.
    //
    // Prag omjera pretpostavlja da je drugi po redu KRIVO poklapanje - tada njegova blizina znaci
    // da se okolina ponavlja i da poklapanju ne treba vjerovati. Kod gustih uglova to vise ne
    // stoji: uglovi su na tri piksela razmaka, a zakrpa je 24, pa susjedni ugao gleda gotovo istu
    // okolinu i ima gotovo isti potpis. Drugi po redu je tada SUSJED PRAVOG, i prag omjera odbija
    // upravo ona poklapanja koja su tocna.
    //
    // Izmjereno na razmaku od deset kadrova: uz prag omjera prolazi 7 poklapanja, bez njega 1815.
    // Histogram gradijenata je na to osjetljiviji od binarnog potpisa jer skuplja gradijente u
    // 4x4 celije - pomak od tri piksela ondje mijenja malo, dok BRIEF uzorkuje druge parove
    // piksela i time se vise razlikuje.
    //
    // Nula iskljucuje i vraca obican prag omjera
    //=========================================================================================
    float secondBestApart = 10.0f;

    //Najveca dopustena udaljenost potpisa. Potpisi su jedinicne duljine skalirane na 512, pa je
    //najveca moguca udaljenost oko 724; sve iznad ovoga nije isti detalj nego slucajnost
    float maxDistance = 300.0f;

    //Odsijecanje pojedinacne vrijednosti nakon normiranja, pa se normira opet. Bez toga jedan
    //vrlo jak gradijent - odsjaj, rub prozora - preuzme cijeli potpis i okolina oko njega se
    //prestane razlikovati. 0.2 je vrijednost iz izvornog rada
    float clamp = 0.2f;

    //Racuna li se smjer okoline. Iskljuceno znaci USPRAVAN potpis: okolina se ne zakrece.
    //Na snimci iz ruke ili s gimbala zakret u ravnini slike je malen, pa procjena smjera cesto
    //unosi vise suma nego sto ispravlja - zato je ovo prekidac a ne odluka
    bool orient = true;
};

//Potpis okoline zadane tocke. False kad okolina ne stane u sliku
bool describeSift(const GrayImage& image, const glm::vec2& point, SiftDescriptor& out,
                  const SiftConfig& config = {});

//Potpisi za vise tocaka. Gradijenti se racunaju JEDNOM za cijelu sliku umjesto po tocki
std::vector<SiftDescriptor> describeSiftAll(const GrayImage& image,
                                            const std::vector<glm::vec2>& points,
                                            const SiftConfig& config = {});

//Euklidska udaljenost dvaju potpisa
float distance(const SiftDescriptor& a, const SiftDescriptor& b);

struct SiftMatch{
    uint32_t from = 0;
    uint32_t to = 0;
    float distance = 0.0f;
};

//Uzajamno najbolji parovi uz prag omjera, ograniceni na kandidate blize od radius piksela
std::vector<SiftMatch> matchSiftNear(const std::vector<SiftDescriptor>& from,
                                     const std::vector<glm::vec2>& fromPixels,
                                     const std::vector<SiftDescriptor>& to,
                                     const std::vector<glm::vec2>& toPixels,
                                     float radius,
                                     const SiftConfig& config = {});

}
