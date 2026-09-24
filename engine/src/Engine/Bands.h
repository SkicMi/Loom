#pragma once
#include <algorithm>
#include <thread>
#include <vector>

namespace Engine{

//=============================================================================================
// Posao podijeljen na pojaseve, jedan po dretvi.
//
// REZULTAT MORA OSTATI ISTI DO ZADNJEG BITA. Nigdje se ne zbraja preko pojaseva: svaka dretva pise
// u svoj dio izlaza, a spajaju se redom. Zato se ne moze dogoditi da dvije pokrenutosti dadu dva
// rezultata - a to je jedina vrsta ubrzanja koja ovdje ima smisla, jer bi inace testovi mjerili
// raspored dretvi umjesto racuna.
//
// Stajalo je u Track.cpp i koristilo se samo ondje. Poklapanju potpisa treba isto, i pod istim
// uvjetom, pa je izdvojeno umjesto prepisano
//=============================================================================================
//=============================================================================================
// POSAO VEC RASPODIJELJEN IZVANA. Kad vanjska petlja vec drzi sve jezgre - na primjer po jedan
// kadar po dretvi - pojasevi unutar njega samo bi stvarali dretve koje cekaju jedna drugu. Dok
// postoji SerialBands, inBands na toj dretvi radi u jednom komadu. Rezultat je isti: pojasevi se
// ionako spajaju redom, pa je jedan pojas isto sto i svi zaredom
//=============================================================================================
inline thread_local bool bandsSerial = false;

struct SerialBands{
    bool before;
    SerialBands() : before(bandsSerial){ bandsSerial = true; }
    ~SerialBands(){ bandsSerial = before; }
};

//=============================================================================================
// DIO JEZGRI. Kad nekoliko poslova ide usporedo (pokusaji pocetnog para u Reconstruct), svaki
// dobije svoj dio jezgri umjesto da svaki trazi sve - inace ih je cetiri puta vise nego jezgri i
// guraju se. Kao i SerialBands, ne mijenja rezultat, samo koliko dretvi ga racuna
//=============================================================================================
inline thread_local uint32_t bandsLimit = 0;

struct BandLimit{
    uint32_t before;
    explicit BandLimit(uint32_t limit) : before(bandsLimit){ bandsLimit = limit; }
    ~BandLimit(){ bandsLimit = before; }
};

inline uint32_t bandCount(int items, int minimumPerBand = 16){
    if(bandsSerial) return 1;
    uint32_t cores = std::max(1u, std::thread::hardware_concurrency());
    if(bandsLimit > 0) cores = std::min(cores, bandsLimit);
    //Ispod ovoga pokretanje dretve stoji vise nego posao koji bi dobila
    return std::max(1u, std::min(cores, uint32_t(std::max(1, items / minimumPerBand))));
}

//Tijelo dobiva REDNI BROJ pojasa, ne samo granice. Prva verzija ga je racunala natrag iz prve
//granice, pa su se dva pojasa mogla preslikati na isti broj i dvije dretve pisati u isti vektor -
//greska koja se ne vidi u kodu nego tek kao srusen program. Ovako je broj zadan, a ne pogodjen
template<typename Body>
void inBands(int from, int to, const Body& body, int minimumPerBand = 16){
    const int items = to - from;
    if(items <= 0) return;

    const uint32_t bands = bandCount(items, minimumPerBand);
    if(bands == 1){ body(0u, from, to); return; }

    std::vector<std::thread> workers;
    workers.reserve(bands);
    for(uint32_t band = 0; band < bands; ++band){
        const int start = from + int(uint64_t(items) * band / bands);
        const int stop  = from + int(uint64_t(items) * (band + 1) / bands);
        if(start >= stop) continue;
        workers.emplace_back([&body, band, start, stop]{ body(band, start, stop); });
    }
    for(std::thread& worker : workers) worker.join();
}

}
