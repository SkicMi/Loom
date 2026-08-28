// Spool cita karte dubine.
//
// Dubina nije slika u boji i osam bita joj nije dovoljno: scena duboka trideset metara u 256
// koraka ima korak od dvanaest centimetara, a normala izvedena iz takve dubine je stepenica.
// Zato se sve ucitava u float, a pise se u PFM - format kojim se dubina razmjenjuje u
// istrazivanju i jedini ovdje koji nista ne zaokruzuje.
//
// Mjeri se ono sto se lako izgubi: da PFM prode bit za bit, da se cjelobrojni zapisi vrate u
// pravi raspon, i da se format prepozna po SADRZAJU a ne po nastavku.
#include "TestHarness.h"

#include <Spool/DepthFile.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <vector>

namespace{

const uint32_t width = 37;    //neparno namjerno: zaokruzeni redovi su cesta greska
const uint32_t height = 23;

//Vrijednosti koje se ne daju zamijeniti jedna za drugu, i koje pokrivaju cijeli raspon
Spool::DepthImage makeDepth(){
    Spool::DepthImage depth;
    depth.width = width;
    depth.height = height;
    depth.sourceBits = 32;
    depth.values.resize(size_t(width) * height);

    for(uint32_t y = 0; y < height; ++y){
        for(uint32_t x = 0; x < width; ++x){
            //Kosina po obje osi, pa se svaka zamjena redaka ili stupaca vidi
            depth.values[size_t(y) * width + x] =
                float(x) / float(width - 1) * 0.7f + float(y) / float(height - 1) * 0.3f;
        }
    }
    return depth;
}

bool haveFfmpeg(){
    return std::system("ffmpeg -version > /dev/null 2>&1") == 0;
}

}

int main(){
    TestReport report("spool dubina");

    const std::filesystem::path work = std::filesystem::temp_directory_path() / "loom_spool_depth";
    std::filesystem::remove_all(work);
    std::filesystem::create_directories(work);

    const Spool::DepthImage source = makeDepth();

    // -------------------------------------------------------------------------------
    // PFM: bez gubitka znaci bez gubitka
    // -------------------------------------------------------------------------------

    const std::string pfm = (work / "depth.pfm").string();
    Spool::saveDepthImage(pfm, source);

    const Spool::DepthImage back = Spool::loadDepthImage(pfm);

    report.check("dimenzije prezive",
        back.width == width && back.height == height && back.sourceBits == 32,
        fmt("%ux%u, %u bita", back.width, back.height, back.sourceBits));

    size_t different = 0;
    float worst = 0.0f;
    for(size_t i = 0; i < source.values.size() && i < back.values.size(); ++i){
        const float delta = std::abs(source.values[i] - back.values[i]);
        worst = std::max(worst, delta);
        if(delta != 0.0f) ++different;
    }

    //Float u float bez ijedne pretvorbe. Tolerancija bi ovdje sakrila zamijenjene retke,
    //sto je tocno ono sto PFM svojim redoslijedom odozdo prema gore i poziva
    report.check("PFM se vraca bit za bit", different == 0 && worst == 0.0f,
        fmt("%zu od %zu vrijednosti odstupa, najvise za %.9f",
            different, source.values.size(), worst));

    //Gornji lijevi i donji desni se moraju razlikovati, inace bi okrenuta slika prosla
    report.check("gore i dolje se razlikuju",
        std::abs(back.at(0,0) - back.at(width-1, height-1)) > 0.5f,
        fmt("(0,0) = %.4f, (%u,%u) = %.4f", back.at(0,0), width-1, height-1,
            back.at(width-1, height-1)));

    report.check("raspon je zabiljezen",
        std::abs(back.minValue - 0.0f) < 1e-6f && std::abs(back.maxValue - 1.0f) < 1e-6f,
        fmt("od %.6f do %.6f", back.minValue, back.maxValue));

    // -------------------------------------------------------------------------------
    // Sesnaest bita: koliko se izgubi, izgubi se predvidljivo
    // -------------------------------------------------------------------------------

    {
        const std::string pgm = (work / "depth.pgm").string();
        std::ofstream out(pgm, std::ios::binary);
        out << "P5\n" << width << " " << height << "\n65535\n";
        for(float value : source.values){
            const uint32_t stored = uint32_t(std::lround(value * 65535.0f));
            const uint8_t bytes[2] = {uint8_t(stored >> 8), uint8_t(stored & 0xFF)};
            out.write(reinterpret_cast<const char*>(bytes), 2);
        }
        out.close();

        const Spool::DepthImage sixteen = Spool::loadDepthImage(pgm);

        float worstSixteen = 0.0f;
        for(size_t i = 0; i < source.values.size() && i < sixteen.values.size(); ++i){
            worstSixteen = std::max(worstSixteen, std::abs(source.values[i] - sixteen.values[i]));
        }

        //Pola koraka od 1/65535 je sve sto zaokruzivanje smije stajati. Prag se izvodi iz
        //broja bita, ne bira
        const float step = 0.5f / 65535.0f;

        report.check("16 bita se vraca unutar svog koraka",
            sixteen.width == width && sixteen.sourceBits == 16 && worstSixteen <= step,
            fmt("najvece odstupanje %.8f, pola koraka od 1/65535 je %.8f", worstSixteen, step));
    }

    // -------------------------------------------------------------------------------
    // Format se prepoznaje po sadrzaju
    // -------------------------------------------------------------------------------

    //Datoteka nazvana .png koja to nije je cesca nego sto bi se ocekivalo, a poruka o krivom
    //formatu je korisnija od tiho ucitanog smeca
    const std::string lying = (work / "zapravo_pfm.png").string();
    std::filesystem::copy_file(pfm, lying);

    const Spool::DepthImage despiteName = Spool::loadDepthImage(lying);
    report.check("nastavak ne odlucuje",
        despiteName.sourceBits == 32 && despiteName.width == width,
        fmt("file s nastavkom .png koji je zapravo PFM ucitan je kao %u-bitni %ux%u",
            despiteName.sourceBits, despiteName.width, despiteName.height));

    // -------------------------------------------------------------------------------
    // I 16-bitni PNG, jer ga modeli najcesce i pisu
    // -------------------------------------------------------------------------------

    if(haveFfmpeg()){
        const std::string png = (work / "depth16.png").string();
        const std::string command = "ffmpeg -y -f rawvideo -pix_fmt gray16le -s " +
            std::to_string(width) + "x" + std::to_string(height) +
            " -i " + (work / "depth.raw").string() + " -pix_fmt gray16be " + png + " > /dev/null 2>&1";

        std::ofstream raw((work / "depth.raw").string(), std::ios::binary);
        for(float value : source.values){
            const uint16_t stored = uint16_t(std::lround(value * 65535.0f));
            raw.write(reinterpret_cast<const char*>(&stored), 2);
        }
        raw.close();

        if(std::system(command.c_str()) == 0){
            const Spool::DepthImage fromPng = Spool::loadDepthImage(png);

            float worstPng = 0.0f;
            for(size_t i = 0; i < source.values.size() && i < fromPng.values.size(); ++i){
                worstPng = std::max(worstPng, std::abs(source.values[i] - fromPng.values[i]));
            }

            report.check("16-bitni PNG",
                fromPng.sourceBits == 16 && worstPng <= 0.5f / 65535.0f,
                fmt("%u bita, najvece odstupanje %.8f", fromPng.sourceBits, worstPng));
        }
        else{
            report.check("16-bitni PNG", false, "ffmpeg ga nije napravio");
        }
    }
    else{
        report.check("16-bitni PNG", false, "ffmpeg nije na putanji, preskoceno");
    }

    // -------------------------------------------------------------------------------
    // I da se ne ucita nista sto nije dubina
    // -------------------------------------------------------------------------------

    bool threw = false;
    try{
        Spool::loadDepthImage((work / "nema_me.pfm").string());
    }
    catch(const std::exception&){ threw = true; }

    report.check("nepostojeci file baca", threw,
        "dubina koja se tiho ucitala kao nista je scena bez ijedne plohe");

    std::filesystem::remove_all(work);
    return report.result();
}
