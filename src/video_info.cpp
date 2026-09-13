// Sto snimka o sebi govori: sve sto se iz datoteke da procitati, i sto to znaci za solve.
//
//   ./VideoInfo snimka.mp4
//
// Ispis je namjerno podijeljen na dvoje: STO PISE (Spool, bez tumacenja) i STO IZ TOGA SLIJEDI
// (Engine, uz izvor svake pretpostavke). Kamera koja nije prepoznata nije greska - samo znaci da
// se zarisna mora traziti iz same snimke, i tada je posteno da to pise crno na bijelo.
#include <Spool/VideoFile.h>

#include <Engine/CameraHints.h>

#include <cstdio>
#include <string>

int main(int argc, char** argv){
    if(argc < 2){
        std::printf("Upotreba: VideoInfo snimka.mp4\n");
        return 1;
    }

    Spool::VideoReader reader(argv[1]);
    const Spool::VideoInfo& info = reader.info();

    std::printf("== STO PISE U DATOTECI ==\n");
    std::printf("  slika       %ux%u, %.3f fps (%u/%u), %s / %s\n", info.width, info.height,
                info.frameRate(), info.frameRateNumerator, info.frameRateDenominator,
                info.codec.c_str(), info.pixelFormat.c_str());
    std::printf("  trajanje    %.2f s, %lld kadrova%s\n", info.duration, (long long)info.frameCount,
                info.frameCountIsExact ? "" : " (procijenjeno)");
    std::printf("  rotacija    %d st, piksel %.4f, boja %s / %s / %s\n", info.rotation, info.pixelAspect,
                info.colorPrimaries.c_str(), info.colorTransfer.c_str(), info.colorSpace.c_str());

    std::printf("  zapisi:\n");
    for(const Spool::VideoInfo::StreamNote& stream : info.streams){
        std::printf("    %d: %-10s %-10s %s\n", stream.index, stream.kind.c_str(), stream.codec.c_str(),
                    stream.handler.c_str());
    }

    std::printf("  metapodaci (%zu):\n", info.metadata.size());
    for(const auto& entry : info.metadata){
        std::string value = entry.second;
        if(value.size() > 90) value = value.substr(0, 87) + "...";
        std::printf("    %-28s %s\n", entry.first.c_str(), value.c_str());
    }

    // ---------------------------------------------------------------------------------
    // Tumacenje
    // ---------------------------------------------------------------------------------

    Engine::SourceFacts facts;
    facts.width = info.width;
    facts.height = info.height;
    facts.frameRate = info.frameRate();
    facts.rotation = info.rotation;
    facts.pixelAspect = info.pixelAspect;
    facts.codec = info.codec;
    for(const auto& entry : info.metadata) facts.metadata.push_back(Engine::MetadataEntry{entry.first, entry.second});
    for(const Spool::VideoInfo::StreamNote& stream : info.streams){
        facts.streams.push_back(stream.kind + " " + stream.codec + " (" + stream.handler + ")");
    }

    const Engine::CameraHints hints = Engine::hintsFrom(facts);

    std::printf("\n== STO IZ TOGA SLIJEDI ==\n");
    std::printf("  kamera      %s %s%s\n",
                hints.make.empty() ? "(nepoznat proizvodjac)" : hints.make.c_str(),
                hints.model.c_str(),
                hints.serial.empty() ? "" : (" [SN " + hints.serial + "]").c_str());
    std::printf("  vidno polje %.1f st vodoravno  <- %s\n", hints.horizontalFieldOfView,
                Engine::sourceName(hints.focalSource));
    std::printf("  zarisna     fx %.1f px, fy %.1f px, glavna tocka %.1f, %.1f\n",
                double(hints.intrinsics.fx), double(hints.intrinsics.fy),
                double(hints.intrinsics.cx), double(hints.intrinsics.cy));
    if(hints.hasPosition){
        std::printf("  polozaj     %.6f, %.6f, visina %.1f m\n", hints.latitude, hints.longitude, hints.altitude);
    }
    std::printf("  telemetrija %s\n", hints.hasTelemetry ? "POSTOJI" : "nema");

    std::printf("\n  biljeske:\n");
    for(const std::string& note : hints.notes) std::printf("    - %s\n", note.c_str());

    return 0;
}
