// What can be learned about the camera from the file itself, and where that is known from.
//
// These are pure data - no image, no card - so it is tested by assembling SourceFacts by
// hand and checking what comes out of them.
//
// WHY THIS IS WORTH TESTING. The focal-length assumption enters every pose the solver
// returns, and an assumption posing as a read piece of data is worse than none: the reader
// has no way to tell them apart. That is why here not just the VALUE is checked but also
// the SOURCE.
//
// What is defended:
//
//   generic handler   a track name every container fills in ("Video Media Handler") must not
//                     become a manufacturer. Sony ZV-E10 II used to come out as a camera of
//                     that name
//   real handler      a name that actually reveals the manufacturer ("GoPro AVC encoder") is
//                     still read
//   telemetry         a timed track that is neither image nor sound is telemetry, not just
//                     GoPro's gpmd. Sony's 66 MB used to be reported as "none"
//   source            when nothing is known, the source must be Assumed - not Metadata
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
    // A generic track name is not a manufacturer
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
// But a handler that REALLY reveals the manufacturer is still read
// -------------------------------------------------------------------------------
    //
    // Without this check the fix for the above could be "solved" by never reading the handler
    // at all, and then GoPro and DJI would lose the only trace they have

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
    // Telemetry is any timed track, not just GoPro's
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
    // Direct data beats the table, and the table beats the assumption
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

        //The focal length must follow the field of view, not sit separately
        const double expected = (0.5 * double(facts.width))
                              / std::tan(0.5 * known.horizontalFieldOfView * 3.14159265358979 / 180.0);
        report.check("zarisna se slaze s vidnim poljem",
            std::fabs(double(known.intrinsics.fx) - expected) < 1.0,
            fmt("fx %.1f px, iz vidnog polja %.1f px", double(known.intrinsics.fx), expected));
    }

    return report.result();
}
