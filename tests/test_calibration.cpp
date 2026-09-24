// 2b: calibration - relative depth into meters.
//
// This is the only place where the estimate meets the real world, and therefore the only
// place it can go wrong so that the image still looks convincing. Light falls off with the
// inverse square and needs METERS: a scene scaled twice looks like a scene with half the
// light, and nothing in it looks like an error.
//
// The model gives something proportional to the RECIPROCAL distance and knows neither scale
// nor offset - two numbers, two unknowns. That is why two known distances are exactly
// enough, and why the line is found in reciprocal-distance space rather than distance space.
#include "TestHarness.h"

#include "Vulkan/PositionMap.h"

#include <cmath>
#include <functional>
#include <string>
#include <vector>

int main(){
    TestReport report("2b kalibracija");

    // -------------------------------------------------------------------------------
    // Range: what the ends of the map mean
    // -------------------------------------------------------------------------------

    {
        const DepthMapping mapping = DepthMapping::fromRange(1.5f, 20.0f);

        report.check("krajevi raspona su tocno ono sto smo rekli",
            std::abs(mapping.distanceAt(1.0f) - 1.5f) < 1e-4f &&
            std::abs(mapping.distanceAt(0.0f) - 20.0f) < 1e-4f,
            fmt("vrijednost 1 -> %.4f m, vrijednost 0 -> %.4f m", 
                mapping.distanceAt(1.0f), mapping.distanceAt(0.0f)));

        //The middle of the map is NOT the middle of the range, and that is the whole point. The
        //harmonic mean of 1.5 and 20 is 2*1.5*20/21.5 = 2.79, not 10.75
        const float middle = mapping.distanceAt(0.5f);
        const float harmonic = 2.0f * 1.5f * 20.0f / (1.5f + 20.0f);
        const float arithmetic = 0.5f * (1.5f + 20.0f);

        report.check("sredina karte je harmonijska, ne aritmeticka",
            std::abs(middle - harmonic) < 1e-3f && std::abs(middle - arithmetic) > 5.0f,
            fmt("na pola karte je %.4f m; harmonijska sredina je %.4f, aritmeticka bi bila %.4f",
                middle, harmonic, arithmetic));
    }

    // -------------------------------------------------------------------------------
    // Two known distances
    // -------------------------------------------------------------------------------

    //This is calibration as it is really done: the range of the map is unknown and need not
    //be normalized; you point at two things in the image and say how far they are
    {
        //A map that is NOT normalized - the kind the model emits
        const float rawNear = 3721.0f;
        const float rawFar = 418.0f;

        const DepthMapping mapping = DepthMapping::fromReferences(rawNear, 2.4f, rawFar, 17.0f);

        report.check("dvije reference vrate te dvije udaljenosti",
            std::abs(mapping.distanceAt(rawNear) - 2.4f) < 1e-3f &&
            std::abs(mapping.distanceAt(rawFar) - 17.0f) < 1e-3f,
            fmt("%.1f -> %.4f m (rekli smo 2.4), %.1f -> %.4f m (rekli smo 17)",
                rawNear, mapping.distanceAt(rawNear), rawFar, mapping.distanceAt(rawFar)));

        //And that the rest of the map is on the line through those two points - in reciprocal space
        const float middleValue = 0.5f * (rawNear + rawFar);
        const float expected = 1.0f / (0.5f * (1.0f / 2.4f + 1.0f / 17.0f));

        report.check("izmedu njih se ide reciprocno",
            std::abs(mapping.distanceAt(middleValue) - expected) < 1e-3f,
            fmt("na pola izmedu referenci je %.4f m, a pravac kroz njihove reciprocne "
                "vrijednosti daje %.4f", mapping.distanceAt(middleValue), expected));
    }

    // -------------------------------------------------------------------------------
    // fromRange is a special case of fromReferences
    // -------------------------------------------------------------------------------

    //If that is not exact we compute the same thing twice - and two computations of the same
    //thing sooner or later diverge
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
        const DepthMapping mapping = DepthMapping::metric(0.001f);   //millimeters to meters
        report.check("metricka karta se samo skalira",
            std::abs(mapping.distanceAt(2400.0f) - 2.4f) < 1e-5f,
            fmt("2400 -> %.5f m", mapping.distanceAt(2400.0f)));
    }

    // -------------------------------------------------------------------------------
    // What MUST be refused
    // -------------------------------------------------------------------------------

    //A calibration that quietly agrees with nonsense is a calibration that will one day quietly
    //lie about the whole scene
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
    // And that a doubled scene really reads as doubled
    // -------------------------------------------------------------------------------

    //This is the error that justifies calibration existing at all. Same map, range
    //mis-estimated by a factor of two: every distance doubles, so light falling by the
    //inverse square at the same spot gives FOUR times less light
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
