#pragma once
#include "Engine/SyntheticScene.h"

#include <string>
#include <vector>

namespace Engine{

//=============================================================================================
// Sto se o kameri da doznati iz same datoteke, i odakle se to zna.
//
// Solveru treba zarisna duljina, a snimka je rijetko nosi izravno. Ali nosi tragove: proizvodjaca,
// model, rotaciju, GPS, ponekad cijelu telemetriju. Ovdje se ti tragovi pretvaraju u pretpostavke
// - i uz svaku stoji ODAKLE je, jer nije isto je li zarisna procitana iz datoteke, uzeta iz
// tablice modela ili izmisljena.
//
// GRANICA: Spool cita datoteku i kaze sto u njoj pise; ovo tumaci sto to znaci za kameru. Zato
// ovdje ne ulazi nijedan Spoolov tip nego obicni parovi kljuc-vrijednost.
//
// STO OVO NIJE: kalibracija. Tablica modela daje tvornicko vidno polje, a stvarna kamera odstupa,
// ima distorziju, a stabilizacija mijenja i sam kadar. Ovo je POCETNA VRIJEDNOST koja solveru
// stedi pogadjanje - ne izgovor da se ne mjeri.
//=============================================================================================

struct MetadataEntry{
    std::string key;
    std::string value;
};

//Ono sto je Spool procitao, bez tumacenja
struct SourceFacts{
    uint32_t width = 0;
    uint32_t height = 0;
    double frameRate = 0.0;
    int rotation = 0;
    double pixelAspect = 1.0;
    std::string codec;

    std::vector<MetadataEntry> metadata;

    //Kako ih Spool prijavi: "data gpmd (GoPro MET)" i slicno
    std::vector<std::string> streams;
};

enum class HintSource{
    Unknown,      //nista se nije naslo
    Assumed,      //nista o kameri nije poznato, uzeta je opca pretpostavka
    ModelTable,   //poznat model, tvornicko vidno polje iz tablice
    Metadata      //procitano iz same datoteke
};

struct CameraHints{
    Intrinsics intrinsics;
    HintSource focalSource = HintSource::Unknown;
    double horizontalFieldOfView = 0.0;

    std::string make;
    std::string model;
    std::string serial;

    bool hasPosition = false;
    double latitude = 0.0;
    double longitude = 0.0;
    double altitude = 0.0;

    //Telemetrija (GoPro GPMF i slicno): jos je ne citamo, ali se javlja da postoji - u njoj su
    //zirokop i GPS, dakle pocetne rotacije i mjerilo
    bool hasTelemetry = false;
    std::string telemetryNote;

    int rotation = 0;

    //=========================================================================================
    // RASPON MOGUCEG VIDNOG POLJA, kad se znaju senzor i objektiv.
    //
    // Ovo nije procjena zarista nego OGRADA oko njega, i vrijedi vise od procjene: kad se zarisna
    // trazi po reprojekciji, ona se s njom trguje i pretraga zna odlutati. Izmjereno na ZV-E10M2:
    // kandidati 94, 102 i 110 st dali su 1.311, 1.312 i 1.312 px - ravan plato, izbor iz sest
    // tisucinki piksela. A objektiv 18-50 mm na APS-C senzoru fizicki NE MOZE dati vise od
    // sezdesetak stupnjeva, pa je pola tog raspona bilo izvan mogucega.
    //
    // Senzor se zna iz modela, zariste iz imena objektiva - oboje iz pratece datoteke, jer Sonyjev
    // XAVC to ne pise u samu snimku.
    //
    // OGRADA JE SIRA NEGO STO IZGLEDA: u videu se senzor obicno izrezuje, a zoom se tijekom kadra
    // moze pomaknuti. Zato se uzima cijeli raspon objektiva, ne jedna vrijednost
    //=========================================================================================
    //Sirina senzora u milimetrima kad se model prepozna, inace nula. Nije samo za ogradu ispod:
    //izvoz u USD time pise STVARNU zarisnu duljinu, onakvu kakvu umjetnik ocekuje vidjeti
    double sensorWidthMillimetres = 0.0;

    bool hasFieldOfViewRange = false;
    double widestFieldOfView = 0.0;      //kod najkraceg zarista
    double narrowestFieldOfView = 0.0;   //kod najduljeg
    std::string lens;

    //Izvjestaj je DIO REZULTATA, ne ispis sa strane: solver koji ne zna odakle mu zarisna ne moze
    //reci koliko vrijedi njegov odgovor
    std::vector<std::string> notes;
};

CameraHints hintsFrom(const SourceFacts& facts);

//Ime izvora, za ispis
const char* sourceName(HintSource source);

}
