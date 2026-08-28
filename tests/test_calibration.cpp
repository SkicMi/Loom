// 2b: kalibracija - relativna dubina u metre.
//
// Ovo je jedino mjesto na kojem se procjena spaja sa stvarnim svijetom, i zato jedino na
// kojem se moze pogrijesiti tako da slika i dalje izgleda uvjerljivo. Svjetlo pada po
// inverznom kvadratu i treba METRE: scena skalirana dvostruko izgleda kao scena s upola
// slabijim svjetlom, a nista u njoj ne izgleda kao greska.
//
// Model daje nesto proporcionalno RECIPROCNOJ udaljenosti i ne zna ni razmjer ni pomak - dva
// broja, dvije nepoznanice. Zato su i dvije poznate udaljenosti tocno dovoljne, i zato se
// pravac trazi u prostoru reciprocnih udaljenosti a ne udaljenosti.
#include "TestHarness.h"

#include "Vulkan/PositionMap.h"

#include <cmath>
#include <functional>
#include <string>
#include <vector>

int main(){
    TestReport report("2b kalibracija");

    // -------------------------------------------------------------------------------
    // Raspon: sto znace krajevi karte
    // -------------------------------------------------------------------------------

    {
        const DepthMapping mapping = DepthMapping::fromRange(1.5f, 20.0f);

        report.check("krajevi raspona su tocno ono sto smo rekli",
            std::abs(mapping.distanceAt(1.0f) - 1.5f) < 1e-4f &&
            std::abs(mapping.distanceAt(0.0f) - 20.0f) < 1e-4f,
            fmt("vrijednost 1 -> %.4f m, vrijednost 0 -> %.4f m", 
                mapping.distanceAt(1.0f), mapping.distanceAt(0.0f)));

        //Sredina karte NIJE sredina raspona, i to je cijela poanta. Harmonijska sredina od
        //1.5 i 20 je 2*1.5*20/21.5 = 2.79, ne 10.75
        const float middle = mapping.distanceAt(0.5f);
        const float harmonic = 2.0f * 1.5f * 20.0f / (1.5f + 20.0f);
        const float arithmetic = 0.5f * (1.5f + 20.0f);

        report.check("sredina karte je harmonijska, ne aritmeticka",
            std::abs(middle - harmonic) < 1e-3f && std::abs(middle - arithmetic) > 5.0f,
            fmt("na pola karte je %.4f m; harmonijska sredina je %.4f, aritmeticka bi bila %.4f",
                middle, harmonic, arithmetic));
    }

    // -------------------------------------------------------------------------------
    // Dvije poznate udaljenosti
    // -------------------------------------------------------------------------------

    //Ovo je kalibracija kakva se stvarno radi: ne zna se raspon karte niti je treba
    //normalizirati, pokaze se na dvije stvari u slici i kaze koliko su daleko
    {
        //Karta koja NIJE normalizirana - kakvu model i izbaci
        const float rawNear = 3721.0f;
        const float rawFar = 418.0f;

        const DepthMapping mapping = DepthMapping::fromReferences(rawNear, 2.4f, rawFar, 17.0f);

        report.check("dvije reference vrate te dvije udaljenosti",
            std::abs(mapping.distanceAt(rawNear) - 2.4f) < 1e-3f &&
            std::abs(mapping.distanceAt(rawFar) - 17.0f) < 1e-3f,
            fmt("%.1f -> %.4f m (rekli smo 2.4), %.1f -> %.4f m (rekli smo 17)",
                rawNear, mapping.distanceAt(rawNear), rawFar, mapping.distanceAt(rawFar)));

        //I da je ostatak karte na pravcu kroz te dvije tocke - u reciprocnom prostoru
        const float middleValue = 0.5f * (rawNear + rawFar);
        const float expected = 1.0f / (0.5f * (1.0f / 2.4f + 1.0f / 17.0f));

        report.check("izmedu njih se ide reciprocno",
            std::abs(mapping.distanceAt(middleValue) - expected) < 1e-3f,
            fmt("na pola izmedu referenci je %.4f m, a pravac kroz njihove reciprocne "
                "vrijednosti daje %.4f", mapping.distanceAt(middleValue), expected));
    }

    // -------------------------------------------------------------------------------
    // fromRange je poseban slucaj fromReferences
    // -------------------------------------------------------------------------------

    //Ako to nije tocno, dva puta racunamo istu stvar - a dva racuna iste stvari se prije ili
    //poslije raziđu
    {
        const DepthMapping viaRange = DepthMapping::fromRange(1.5f, 20.0f);
        const DepthMapping viaReferences = DepthMapping::fromReferences(1.0f, 1.5f, 0.0f, 20.0f);

        report.check("raspon je samo dvije reference",
            viaRange.disparityScale == viaReferences.disparityScale &&
            viaRange.disparityOffset == viaReferences.disparityOffset,
            fmt("razmjer %.9f / %.9f, pomak %.9f / %.9f",
                viaRange.disparityScale, viaReferences.disparityScale,
                viaRange.disparityOffset, viaReferences.disparityOffset));
    }

    // -------------------------------------------------------------------------------
    // Metricki modeli
    // -------------------------------------------------------------------------------

    {
        const DepthMapping mapping = DepthMapping::metric(0.001f);   //milimetri u metre
        report.check("metricka karta se samo skalira",
            std::abs(mapping.distanceAt(2400.0f) - 2.4f) < 1e-5f,
            fmt("2400 -> %.5f m", mapping.distanceAt(2400.0f)));
    }

    // -------------------------------------------------------------------------------
    // Sto se MORA odbiti
    // -------------------------------------------------------------------------------

    //Kalibracija koja se tiho slozi s besmislicom je kalibracija koja ce jednom tiho slagati
    //cijelu scenu
    struct Case{
        const char* what;
        std::function<void()> run;
    };

    const std::vector<Case> refused = {
        {"far manji od neara",      []{ DepthMapping::fromRange(20.0f, 1.5f); }},
        {"near nula",               []{ DepthMapping::fromRange(0.0f, 20.0f); }},
        {"dvije reference na istoj vrijednosti", []{ DepthMapping::fromReferences(0.5f, 2.0f, 0.5f, 9.0f); }},
        {"udaljenost iza kamere",   []{ DepthMapping::fromReferences(1.0f, -2.0f, 0.0f, 9.0f); }},
    };

    size_t accepted = 0;
    std::string names;
    for(const Case& item : refused){
        bool threw = false;
        try{ item.run(); } catch(const std::exception&){ threw = true; }
        if(!threw){ ++accepted; names += std::string(names.empty() ? "" : ", ") + item.what; }
    }

    report.check("besmislena kalibracija se odbija", accepted == 0,
        accepted == 0 ? fmt("sva %zu slucaja bacaju", refused.size())
                      : fmt("%zu je proslo: %s", accepted, names.c_str()));

    // -------------------------------------------------------------------------------
    // I da se dvostruka scena stvarno vidi kao dvostruka
    // -------------------------------------------------------------------------------

    //Ovo je greska zbog koje kalibracija uopce postoji. Ista karta, dvostruko krivo
    //procijenjen raspon: svaka udaljenost se udvostruci, pa svjetlo koje pada po inverznom
    //kvadratu na istom mjestu daje CETIRI puta manje svjetla
    {
        const DepthMapping right = DepthMapping::fromRange(2.0f, 20.0f);
        const DepthMapping doubled = DepthMapping::fromRange(4.0f, 40.0f);

        const float a = right.distanceAt(0.35f);
        const float b = doubled.distanceAt(0.35f);

        report.check("dvostruko procijenjen raspon udvostrucuje scenu",
            std::abs(b - 2.0f * a) < 1e-3f,
            fmt("%.4f m naspram %.4f m - svjetlo na istom mjestu daje %.1f puta manje",
                a, b, (b*b)/(a*a)));
    }

    return report.result();
}
