#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace Spool{

//=============================================================================================
// OpenEXR - format u kojem VFX razmjenjuje slike: linearno, s pomicnim zarezom, s proizvoljno
// mnogo imenovanih kanala u jednoj datoteci.
//
// ZASTO. Render u PNG-u je vec prosao kroz prikaz: svjetlo iznad 1 je odrezano, sjena je
// kvantizirana u 256 koraka, a dubina i normala nemaju gdje stati. Nuke, Resolve i Blender iz
// EXR-a dobiju ono sto je tracer izracunao: R G B A plus slojevi (dubina Z, normala N.*, albedo.*,
// sjena catchera shadow.*) - kompozitor tada sam odlucuje o ekspoziciji i slaganju.
//
// STO SE PISE: scanline, BEZ kompresije, jedan redak po bloku, polovicni float (half) ili float
// po kanalu. To je najjednostavniji ispravan EXR i citaju ga svi. Nekomprimiran je velik (1080p s
// dvanaest half kanala ~50 MB); kompresija je sljedeci korak kad zatreba.
//
// STO SE CITA: samo nekomprimirani scanline EXR (ono sto ovdje pisemo, i HDR-ovi izvezeni bez
// kompresije). Komprimirani (ZIP, PIZ, ...) se odbiju s razlogom - ne pretvaraju se tiho u crno.
//=============================================================================================

struct ExrChannel{
    std::string name;           //"R", "G", "B", "A", "Z", "N.X", "albedo.R"...
    std::vector<float> values;  //sirina * visina, prvi redak prvi
    bool half = true;           //false: puni float (dubina - half ima samo 11 bita mantise)
};

struct ExrImage{
    uint32_t width = 0, height = 0;
    std::vector<ExrChannel> channels;
    const ExrChannel* find(const std::string& name) const;
};

//Kanali se sortiraju po imenu (EXR to trazi). Baca s putanjom i razlogom. Stvara mape iznad
void saveExr(const std::string& path, uint32_t width, uint32_t height, std::vector<ExrChannel> channels);

ExrImage loadExr(const std::string& path);

//Polovicni float, zaokruzivanje na najblizi (paran kod jednakosti), sa subnormalima i beskonacnoscu
uint16_t floatToHalf(float value);
float halfToFloat(uint16_t value);

}
