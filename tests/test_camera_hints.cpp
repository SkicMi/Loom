// Sto se o kameri da doznati iz same datoteke, i odakle se to zna.
//
// Ovo su cisti podaci - nema ni slike ni kartice - pa se testira tako da se SourceFacts sastavi
// rukom i provjeri sto iz njih ispadne.
//
// ZASTO JE OVO VRIJEDNO TESTA. Pretpostavka o zarisnoj ulazi u svaku pozu koju solver vrati, a
// pretpostavka koja se predstavlja kao procitani podatak je gora od nikakve: citatelj nema kako
// razlikovati. Zato se ovdje ne provjerava samo VRIJEDNOST nego i IZVOR.
//
// Sto se brani:
//
//   generican handler   ime zapisa koje svaki kontejner popuni ("Video Media Handler") ne smije
//                       postati proizvodjac. Sony ZV-E10 II je tako izlazio kao kamera tog imena
//   pravi handler       ime koje stvarno odaje proizvodjaca ("GoPro AVC encoder") se i dalje cita
//   telemetrija         zapis s vremenskim oznakama koji nije ni slika ni zvuk je telemetrija,
//                       a ne samo GoProov gpmd. Sonyjevih 66 MB se prijavljivalo kao "nema"
//   izvor               kad se nista ne zna, izvor mora biti Assumed - ne Metadata
#include "TestHarness.h"

#include <Engine/CameraHints.h>

#include <string>
#include <vector>

namespace{

Engine::SourceFacts base(){
    Engine::SourceFacts facts;
    facts.width = 3840;
    facts.height = 2160;
    facts.frameRate = 50.0;
    facts.codec = "h264";
    return facts;
}

bool mentions(const std::vector<std::string>& notes, const std::string& what){
    for(const std::string& note : notes){
        if(note.find(what) != std::string::npos) return true;
    }
    return false;
}

}

int main(){
    TestReport report("sto kamera o sebi govori");

    // -------------------------------------------------------------------------------
    // Generican naziv zapisa nije proizvodjac
    // -------------------------------------------------------------------------------

    {
        Engine::SourceFacts facts = base();
        facts.metadata.push_back({"handler_name", "Video Media Handler"});
        const Engine::CameraHints hints = Engine::hintsFrom(facts);

        report.check("generican handler ne postaje proizvodjac",
            hints.make.empty(),
            fmt("make = \"%s\"", hints.make.c_str()));

        report.check("kad se nista ne zna, izvor je pretpostavka",
            hints.focalSource == Engine::HintSource::Assumed,
            fmt("izvor = %s", Engine::sourceName(hints.focalSource)));
    }

    // -------------------------------------------------------------------------------
    // Ali handler koji STVARNO odaje proizvodjaca se i dalje cita
    // -------------------------------------------------------------------------------
    //
    // Bez ove provjere bi se popravak gornjeg mogao "rijesiti" tako da se handler nikad ne cita,
    // a onda bi GoPro i DJI izgubili jedini trag koji imaju

    {
        Engine::SourceFacts facts = base();
        facts.metadata.push_back({"handler_name", "GoPro AVC encoder"});
        const Engine::CameraHints hints = Engine::hintsFrom(facts);

        report.check("pravi handler se i dalje cita",
            !hints.make.empty() && hints.focalSource == Engine::HintSource::ModelTable,
            fmt("make = \"%s\", izvor = %s, %.1f st",
                hints.make.c_str(), Engine::sourceName(hints.focalSource), hints.horizontalFieldOfView));
    }

    // -------------------------------------------------------------------------------
    // Telemetrija je svaki zapis s vremenskim oznakama, ne samo GoProov
    // -------------------------------------------------------------------------------

    {
        Engine::SourceFacts sony = base();
        sony.streams.push_back("video h264 (Video Media Handler)");
        sony.streams.push_back("audio pcm_s16be (Sound Media Handler)");
        sony.streams.push_back("data none (Timed Metadata Media Handler)");
        const Engine::CameraHints hints = Engine::hintsFrom(sony);

        report.check("Sonyjev zapis s vremenskim oznakama se prijavi kao telemetrija",
            hints.hasTelemetry && mentions(hints.notes, "telemetrijski zapis"),
            fmt("telemetrija %s, zapis \"%s\"",
                hints.hasTelemetry ? "postoji" : "nema", hints.telemetryNote.c_str()));

        Engine::SourceFacts plain = base();
        plain.streams.push_back("video h264 (Video Media Handler)");
        plain.streams.push_back("audio aac (Sound Media Handler)");

        report.check("snimka bez telemetrije je ne prijavljuje",
            !Engine::hintsFrom(plain).hasTelemetry,
            "sama slika i zvuk");
    }

    // -------------------------------------------------------------------------------
    // Izravan podatak pobjedjuje tablicu, a tablica pretpostavku
    // -------------------------------------------------------------------------------

    {
        Engine::SourceFacts facts = base();
        facts.metadata.push_back({"make", "GoPro"});
        facts.metadata.push_back({"model", "HERO11 Black"});
        const Engine::CameraHints known = Engine::hintsFrom(facts);

        const Engine::CameraHints unknown = Engine::hintsFrom(base());

        report.check("poznat model daje uze vidno polje od opce pretpostavke",
            known.focalSource == Engine::HintSource::ModelTable &&
            unknown.focalSource == Engine::HintSource::Assumed &&
            known.horizontalFieldOfView != unknown.horizontalFieldOfView,
            fmt("poznat %.1f st (%s), nepoznat %.1f st (%s)",
                known.horizontalFieldOfView, Engine::sourceName(known.focalSource),
                unknown.horizontalFieldOfView, Engine::sourceName(unknown.focalSource)));

        //Zarisna mora slijediti vidno polje, a ne stajati zasebno
        const double expected = (0.5 * double(facts.width))
                              / std::tan(0.5 * known.horizontalFieldOfView * 3.14159265358979 / 180.0);
        report.check("zarisna se slaze s vidnim poljem",
            std::fabs(double(known.intrinsics.fx) - expected) < 1.0,
            fmt("fx %.1f px, iz vidnog polja %.1f px", double(known.intrinsics.fx), expected));
    }

    return report.result();
}
