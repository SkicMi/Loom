#pragma once
//=============================================================================================
// UZORCI: Sobolov niz s Owenovim mijesanjem, po parovima dimenzija (Burley 2020, "Practical
// Hash-based Owen Scrambling").
//
// ZASTO NE rand(). Monte Carlo sa slucajnim brojevima pada kao 1/sqrt(N): za dvaput manji sum
// treba cetiri puta vise uzoraka. Sobol rasporedi uzorke tako da svaki komad kvadrata dobije
// svoj udio, pa greska glatkih integrala (meka sjena, hrapav odsjaj, rub piksela) pada gotovo kao
// 1/N. Owenovo mijesanje cuva tu strukturu, a svakom pikselu daje drugaciji niz - bez njega bi
// svi pikseli dijelili istu gresku i slika bi imala uzorak umjesto suma.
//
// PO PAROVIMA. Sobol je dobar u prve dvije dimenzije; visoke dimenzije su mu medjusobno
// povezane. Zato svaka odluka putanje (piksel, leca, odabir svjetla, smjer odbijanja...) uzme
// SVOJ par (d0, d1), a indeks uzorka se za svaki par promijesa drugim sjemenom - parovi su tada
// neovisni, a svaki za sebe ima punu kvalitetu Sobola ("padding" iz Burleyjeva rada).
//=============================================================================================
#include <glm/glm.hpp>

#include <cstdint>

namespace Tracer{

namespace sampling{

inline uint32_t reverseBits(uint32_t x){
    x = (x << 16) | (x >> 16);
    x = ((x & 0x00ff00ffu) << 8) | ((x & 0xff00ff00u) >> 8);
    x = ((x & 0x0f0f0f0fu) << 4) | ((x & 0xf0f0f0f0u) >> 4);
    x = ((x & 0x33333333u) << 2) | ((x & 0xccccccccu) >> 2);
    x = ((x & 0x55555555u) << 1) | ((x & 0xaaaaaaaau) >> 1);
    return x;
}

//Laine-Karras permutacija: mijesa bitove tako da visi bitovi ovise samo o nizima - to je upravo
//Owenovo mijesanje nad obrnutim bitovima
inline uint32_t laineKarras(uint32_t x, uint32_t seed){
    x += seed;
    x ^= x * 0x6c50b47cu;
    x ^= x * 0xb82f1e52u;
    x ^= x * 0xc7afe638u;
    x ^= x * 0x8d22f6e6u;
    return x;
}

inline uint32_t owen(uint32_t x, uint32_t seed){
    return reverseBits(laineKarras(reverseBits(x), seed));
}

inline uint32_t hash(uint32_t x){
    //PCG-ov izlazni hash: dobro razbije i susjedne brojeve
    x = x * 747796405u + 2891336453u;
    x = ((x >> ((x >> 28u) + 4u)) ^ x) * 277803737u;
    return (x >> 22u) ^ x;
}

inline uint32_t hash(uint32_t a, uint32_t b){ return hash(a ^ hash(b + 0x9e3779b9u)); }

//Prve dvije Sobolove dimenzije. Prva je van der Corput (obrnuti bitovi), druga iz rekurzije
//smjernih brojeva v_k = v_{k-1} ^ (v_{k-1} >> 1)
inline uint32_t sobol0(uint32_t index){ return reverseBits(index); }
//Po bajtu indeksa tablica XOR-a smjernih brojeva: cetiri citanja umjesto do 32 koraka petlje.
//Owen promijesa indeks u slucajan 32-bitni broj, pa je petlja po bitovima uvijek bila puna
//(callgrind: next2D 6.3 % cijelog rendera)
struct SobolTables{
    uint32_t bytes[4][256];
    SobolTables(){
        uint32_t direction[32];
        uint32_t v = 1u << 31;
        for(int bit = 0; bit < 32; ++bit, v ^= v >> 1) direction[bit] = v;
        for(int b = 0; b < 4; ++b){
            for(uint32_t value = 0; value < 256; ++value){
                uint32_t x = 0;
                for(int bit = 0; bit < 8; ++bit) if(value & (1u << bit)) x ^= direction[b * 8 + bit];
                bytes[b][value] = x;
            }
        }
    }
};
inline const SobolTables& sobolTables(){
    static const SobolTables tables;
    return tables;
}
inline uint32_t sobol1(uint32_t index){
    const SobolTables& t = sobolTables();
    return t.bytes[0][index & 255u] ^ t.bytes[1][(index >> 8) & 255u] ^ t.bytes[2][(index >> 16) & 255u] ^ t.bytes[3][index >> 24];
}

inline float toUnit(uint32_t x){ return float(x >> 8) * (1.0f / 16777216.0f); }

}

//Uzorkivac jednog uzorka jednog piksela. Svaki poziv next2D() uzme novi par dimenzija
class Sampler{
public:
    Sampler(uint32_t pixelSeed, uint32_t sampleIndex) : seed(pixelSeed), index(sampleIndex){}

    glm::vec2 next2D(){
        const uint32_t pairSeed = sampling::hash(seed, dimension++);
        const uint32_t shuffled = sampling::owen(index, pairSeed);
        const uint32_t x = sampling::owen(sampling::sobol0(shuffled), sampling::hash(pairSeed, 0x51ed27u));
        const uint32_t y = sampling::owen(sampling::sobol1(shuffled), sampling::hash(pairSeed, 0xa3c59ac3u));
        return {sampling::toUnit(x), sampling::toUnit(y)};
    }
    float next1D(){ return next2D().x; }

private:
    uint32_t seed;
    uint32_t index;
    uint32_t dimension = 0;
};

}
