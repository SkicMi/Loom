// Compositor (LoomCompositor.h): graf cvorova i slaganje kadra.
//
// ZASTO SE OVO TESTIRA. Pregled se slaze na procesoru iz sekvenci koje pisu Python alati (maska
// osobe, preosvijetljena snimka). Kriva maska na Mergeu (A i B zamijenjeni, alfa ne pomnozena) ili
// kriv kadar sekvence (matte_00012 za kadar 13) izgledaju kao "maska malo kasni" - tesko ih je
// uociti okom, a lako izmjeriti na slikama poznatih vrijednosti.
#include "TestHarness.h"

#include "../src/LoomCompositor.h"

#include <filesystem>

namespace fs = std::filesystem;

namespace{

void solid(const fs::path& path, uint8_t value, uint32_t w, uint32_t h){
    std::vector<uint8_t> pixels(size_t(w) * h * 4, value);
    for(size_t i = 3; i < pixels.size(); i += 4) pixels[i] = 255;
    Spool::savePng(path.string(), Spool::imageFromPixels(pixels.data(), w, h));
}

}

int main(){
    TestReport report("compositor: graf i slaganje kadra");
    const fs::path directory = fs::temp_directory_path() / "loom_test_compositor";
    fs::remove_all(directory);
    fs::create_directories(directory / "matte");
    fs::create_directories(directory / "relit");

    //-- zadani graf -------------------------------------------------------------------------
    Loom::Compositor comp;
    Loom::compositorDefaultGraph(comp, "/tmp/snimka.mp4");
    int reads = 0, viewers = 0;
    const Loom::CompNode* merge = nullptr;
    for(const Loom::CompNode& n : comp.nodes){
        reads += n.kind == Loom::CompKind::Read;
        viewers += n.kind == Loom::CompKind::Viewer;
        if(n.kind == Loom::CompKind::Merge) merge = &n;
    }
    const Loom::CompNode* a = merge ? comp.find(merge->input[0]) : nullptr;
    const Loom::CompNode* b = merge ? comp.find(merge->input[1]) : nullptr;
    report.check("zadani graf: Read, Merge s maskom na A i preosvjetljenjem na B, Viewer",
                 reads == 1 && viewers == 1 && a && b && a->kind == Loom::CompKind::PersonMatte &&
                 b->kind == Loom::CompKind::Relight, fmt("%zu cvorova", comp.nodes.size()));

    //-- Merge: A preko B po alfi A ---------------------------------------------------------
    //Bez snimke: dva Backgrounda, A kroz Person Matte s maskom 25 % (64/255)
    Loom::Compositor plain;
    Loom::CompNode& back = plain.add(Loom::CompKind::Background, 0, 0);
    back.checker = false; back.colour[0] = 0.0f; back.colour[1] = 0.0f; back.colour[2] = 1.0f;
    const int backId = back.id;
    Loom::CompNode& front = plain.add(Loom::CompKind::Background, 0, 0);
    front.checker = false; front.colour[0] = 1.0f; front.colour[1] = 0.0f; front.colour[2] = 0.0f;
    const int frontId = front.id;
    Loom::CompNode& matte = plain.add(Loom::CompKind::PersonMatte, 0, 0);
    matte.input[0] = frontId;
    matte.cache = (directory / "matte").string();
    const int matteId = matte.id;
    Loom::CompNode& mix = plain.add(Loom::CompKind::Merge, 0, 0);
    mix.input[0] = matteId;
    mix.input[1] = backId;
    const int mixId = mix.id;
    solid(directory / "matte" / "matte_00013.png", 64, 32, 18);

    bool settled = true;
    Loom::CompImage out = Loom::compEvaluate(plain, mixId, 13, 16, 9, settled);
    const float expected = 64.0f / 255.0f;
    report.check("Merge: crvena maska 25 % preko plave",
                 out.valid() && std::fabs(out.rgba[0] - expected) < 0.01f && std::fabs(out.rgba[2] - (1.0f - expected)) < 0.01f &&
                 std::fabs(out.rgba[3] - 1.0f) < 1e-4f,
                 out.valid() ? fmt("r %.3f b %.3f a %.3f", double(out.rgba[0]), double(out.rgba[2]), double(out.rgba[3])) : "nema slike");

    //Kadar bez maske na disku: maska se ne izmislja, A ostaje neproziran
    Loom::CompImage missing = Loom::compEvaluate(plain, mixId, 14, 16, 9, settled);
    report.check("kadar bez maske: A ostaje cijeli (nema tihe prozirnosti)",
                 missing.valid() && std::fabs(missing.rgba[0] - 1.0f) < 1e-4f && std::fabs(missing.rgba[2]) < 1e-4f, "");

    //-- Relight: RGB iz sekvence, alfa od ulaza ---------------------------------------------
    Loom::CompNode& relight = plain.add(Loom::CompKind::Relight, 0, 0);
    relight.input[0] = matteId;
    relight.cache = (directory / "relit").string();
    const int relightId = relight.id;
    solid(directory / "relit" / "relit_00013.png", 128, 32, 18);
    Loom::CompImage lit = Loom::compEvaluate(plain, relightId, 13, 16, 9, settled);
    report.check("Relight: boja iz sekvence, alfa iz maske",
                 lit.valid() && std::fabs(lit.rgba[1] - 128.0f / 255.0f) < 0.01f && std::fabs(lit.rgba[3] - expected) < 0.01f,
                 lit.valid() ? fmt("g %.3f a %.3f", double(lit.rgba[1]), double(lit.rgba[3])) : "nema slike");

    //-- brisanje cvora odspaja ulaze --------------------------------------------------------
    plain.remove(matteId);
    const Loom::CompNode* after = plain.find(mixId);
    report.check("brisanje cvora odspaja ulaze koji su ga koristili",
                 after && after->input[0] == -1 && after->input[1] == backId, "");

    //-- datoteka kadra ----------------------------------------------------------------------
    report.check("ime kadra sekvence", Loom::compFrameFile("/x", "matte", 7) == "/x/matte_00007.png",
                 Loom::compFrameFile("/x", "matte", 7));

    fs::remove_all(directory);
    return report.result();
}
