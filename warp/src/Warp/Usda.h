#pragma once
#include <string>
#include <utility>
#include <vector>

namespace Warp::usda{

//=============================================================================================
// Citanje USDA teksta: prim, atributi, vrijednosti i timeSamples - bez OpenUSD-a.
//
// ZASTO VLASTITI CITAC. OpenUSD je stotine tisuca redaka, Python i vlastiti sustav gradnje, a
// projekt editora treba procitati natrag ono sto je sam zapisao. Ovo cita GRAMATIKU USDA teksta
// (def, metapodaci u zagradama, atributi s tipom, nizovi, torke, rjecnici, timeSamples), ne
// njezino znacenje - sto koji atribut znaci odlucuje Project.cpp.
//
// Sto se NE podrzava: kompozicija (references, payloads, varijante, sublayeri), klase i "over".
// Datoteka koja ih koristi se procita bez njih - ne pada, ali ne dobije ni ono sto bi slojevi
// donijeli. Za datoteke koje pise Loom to se ne dogadja.
//=============================================================================================

struct Value{
    enum class Kind{ None, Number, String, List, Dictionary, Samples };
    Kind kind = Kind::None;
    double number = 0.0;
    std::string text;                                   //String: tekst, @put@ ili </put> ili ime
    std::vector<Value> items;                           //List: torka ili niz
    std::vector<std::pair<std::string, Value>> entries; //Dictionary: kljuc -> vrijednost
    std::vector<std::pair<double, Value>> samples;      //Samples: kadar -> vrijednost

    //Pomocno: broj iz liste na mjestu i, ili zadano
    double at(size_t i, double fallback = 0.0) const{
        return i < items.size() && items[i].kind == Kind::Number ? items[i].number : fallback;
    }
};

struct Attribute{
    std::string type;       //"double3", "point3f[]", "token"...
    std::string name;       //bez ".timeSamples"
    Value value;            //zadana vrijednost; None kad je nema
    Value samples;          //Samples kad ih ima; inace None
};

struct Prim{
    std::string specifier;  //"def"
    std::string type;       //"Xform", "Camera"... prazno kad nije zadano
    std::string name;
    std::vector<Attribute> attributes;
    std::vector<Prim> children;

    const Attribute* find(const std::string& name) const;
};

struct Layer{
    std::vector<std::pair<std::string, Value>> metadata;
    std::vector<Prim> prims;

    const Value* meta(const std::string& key) const;
};

//false uz razlog i redak kad tekst nije USDA koji ovaj citac razumije
bool parse(const std::string& text, Layer& out, std::string& error);

}
