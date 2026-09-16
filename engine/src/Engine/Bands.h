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
inline uint32_t bandCount(int items, int minimumPerBand = 16){
    const uint32_t cores = std::max(1u, std::thread::hardware_concurrency());
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
