#pragma once
#include <string>
#include <vector>

//Jedan trenutak koji je KARTICA zabiljezila unutar kadra.
//
//Vrijeme je karticino, ne procesorovo. Stoperica na procesoru mjeri kad je naredba poslana, a
//kartica je izvrsi kad stigne - pa bi pripisala jednom koraku vrijeme koje je potrosio drugi.
//Ovdje svaka oznaka stoji u command bufferu izmedju dvije naredbe, i kartica je upise kad do
//nje dodje.
//
//Oznaka imenuje ono sto je UPRAVO ZAVRSILO: razmak izmedju oznake i one prije nje je vrijeme
//koraka koji nosi njezino ime. Prva oznaka u kadru je zato samo pocetak i uvijek je nula.
//
//Namjerno bez ijednog Vulkan tipa, kao i ShadingRate: brojevi i imena, nista vise.
struct GpuTimestamp{
    std::string label;
    double milliseconds = 0.0;   //od prve oznake u istom kadru
};
