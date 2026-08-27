#pragma once
#include "ImageFile.h"
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace Spool{

//Sve sto kontejner zna o snimci, sirovo i neprotumaceno.
//
//Ovo je namjerno IZVJESTAJ, a ne tumacenje. Spool kaze sto u fileu pise; sto to znaci za
//kameru - zarisna duljina, vidno polje, glavna tocka - odlucuje sloj iznad, jer bi inace
//Spool morao znati za Loomovu Camera klasu i granica bi pala.
struct VideoInfo{
    uint32_t width = 0;
    uint32_t height = 0;

    //Kao razlomak, jer 30000/1001 nije 29.97 i zaokruzeno se kroz tisucu frameova nakupi u
    //cijelu sekundu razlike
    uint32_t frameRateNumerator = 0;
    uint32_t frameRateDenominator = 1;
    double frameRate() const;

    int64_t frameCount = 0;
    double duration = 0.0;   //u sekundama

    //Neki kontejneri ne zapisu broj frameova, pa se on procijeni iz trajanja i broja slika u
    //sekundi. Procjena je obicno tocna, ali "obicno" nije isto sto i "jest"
    bool frameCountIsExact = false;

    //Iz matrice prikaza. Portretni mobitel snima landscape piksele i doda ovo - a snimka
    //citana bez toga je postrance, i glavna tocka joj je na krivom mjestu.
    //VideoReader ga VEC PRIMJENJUJE, pa su width i height gore stvarne dimenzije onoga sto
    //readFrame vrati. Stoji ovdje zato da se zna da se dogodilo
    int rotation = 0;         //0, 90, 180 ili 270

    double pixelAspect = 1.0; //anamorfik: piksel koji nije kvadrat

    std::string codec;
    std::string pixelFormat;
    std::string colorPrimaries;
    std::string colorTransfer;
    std::string colorSpace;

    //Sve sto kontejner i njegov video zapis nose, kljuc po kljuc. Ovdje zavrsavaju i
    //Appleovi com.apple.quicktime.* i Sonyjevi vlasnicki kljucevi - sto god da ih ima
    std::vector<std::pair<std::string, std::string>> metadata;

    //Prazan string kad kljuca nema. Usporedba imena ne razlikuje velika i mala slova, jer
    //se kontejneri o tome ne slazu
    std::string find(const std::string& key) const;
};

//Snimka, frame po frame.
//
//Dekoder zivi u procesu: nema medukoraka, nema temp fileova, i metapodaci dolaze iz istog
//mjesta iz kojeg i slike. Zaglavlje ne spominje nijedan tip biblioteke koja to radi - isto
//pravilo po kojem Spool ne pokazuje ni stb-u.
class VideoReader{
    public:
    //Baca s putanjom i razlogom. Snimka koja se tiho nije otvorila je crn ekran tri sloja
    //dalje, a putanja je jedino sto bi to objasnilo
    explicit VideoReader(const std::string& path);
    ~VideoReader();

    VideoReader(const VideoReader&) = delete;
    VideoReader& operator=(const VideoReader&) = delete;
    VideoReader(VideoReader&&) noexcept;
    VideoReader& operator=(VideoReader&&) noexcept;

    const VideoInfo& info() const;

    //Sljedeci frame po redu. Ovo je put kojim se snimka pretvara u sekvencu, i jedini na
    //kojem dekoder radi ono za sto je gradcen - cita naprijed
    Image readNext();

    //Ima li jos frameova
    bool atEnd() const;

    //Frame po broju. Trazenje ide na najblizi kljucni frame ISPRED trazenog pa dekodira
    //naprijed do njega - drugog nacina nema, jer se medufrejm ne da dekodirati sam.
    //Skupo je u usporedbi s citanjem naprijed, i to je razlog zasto se sekvenca izvozi
    //readNextom a ne petljom oko ovoga
    Image readFrame(int64_t index);

    //Vrati se na pocetak
    void rewind();

    //Koji je frame sljedeci na redu
    int64_t position() const;

    private:
    struct State;
    std::unique_ptr<State> state;
};

}
