// Spool EXR: ono sto se zapise mora se procitati natrag, a polovicni float mora zaokruzivati kao
// IEEE 754 - inace render u Nukeu ima drukcije brojeve nego u Loomu.
//
// NEGATIVNA KONTROLA: komprimirani EXR se ODBIJE s razlogom. Citac koji ne zna kompresiju a
// procita "nesto" dao bi crno nebo, i to bi se vidjelo tek na renderu.
#include "TestHarness.h"

#include <Spool/ExrFile.h>
#include <Spool/ImageFile.h>

#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <stdexcept>

int main(){
    TestReport report("S7 EXR");
    namespace fs = std::filesystem;
    const fs::path folder = fs::temp_directory_path() / "loom_test_exr";
    fs::create_directories(folder);

    //-- 1. polovicni float: poznate vrijednosti i zaokruzivanje ------------------------------------
    {
        struct Case{ float value; uint16_t bits; };
        const Case cases[] = {
            {0.0f, 0x0000}, {-0.0f, 0x8000}, {1.0f, 0x3c00}, {-2.0f, 0xc000}, {0.5f, 0x3800},
            {65504.0f, 0x7bff}, {1e6f, 0x7c00}, {5.960464477539063e-8f, 0x0001},     //najmanji subnormal
            {1.0009765625f, 0x3c01},
            {1.00048828125f, 0x3c00},                                                //pola ULP-a: na parni (dolje)
            {1.00146484375f, 0x3c02},                                                //1.5 ULP: na parni (gore)
            {std::numeric_limits<float>::infinity(), 0x7c00},
        };
        int wrong = 0;
        for(const Case& c : cases) if(Spool::floatToHalf(c.value) != c.bits) ++wrong;
        //Sve polovicne vrijednosti: half -> float -> half je identiteta (osim NaN-ova)
        int roundTrip = 0;
        for(uint32_t h = 0; h < 0x10000u; ++h){
            if((h & 0x7c00u) == 0x7c00u && (h & 0x3ffu)) continue;
            if(Spool::floatToHalf(Spool::halfToFloat(uint16_t(h))) != h) ++roundTrip;
        }
        report.check("half", wrong == 0 && roundTrip == 0, fmt("%d krivih poznatih, %d od 63488 ne ide natrag", wrong, roundTrip));
    }

    //-- 2. zapis i citanje: half i float kanali, sortirani po imenu -----------------------------------
    const std::string path = (folder / "slojevi.exr").string();
    {
        const uint32_t w = 7, h = 5;
        std::vector<Spool::ExrChannel> channels;
        for(const char* name : {"R", "G", "B", "A", "N.X"}){
            Spool::ExrChannel c;
            c.name = name;
            for(uint32_t i = 0; i < w * h; ++i) c.values.push_back(float(i) * 0.25f + float(name[0]) * 0.001f);
            channels.push_back(c);
        }
        Spool::ExrChannel z;
        z.name = "Z";
        z.half = false;
        for(uint32_t i = 0; i < w * h; ++i) z.values.push_back(123.456789f + float(i) * 1e-3f);
        channels.push_back(z);
        Spool::saveExr(path, w, h, channels);

        const Spool::ExrImage back = Spool::loadExr(path);
        bool sorted = back.channels.size() == 6;
        for(size_t i = 1; sorted && i < back.channels.size(); ++i) sorted = back.channels[i - 1].name < back.channels[i].name;
        float worstHalf = 0.0f, worstFloat = 0.0f;
        for(const Spool::ExrChannel& original : channels){
            const Spool::ExrChannel* read = back.find(original.name);
            if(!read){ sorted = false; continue; }
            for(size_t i = 0; i < original.values.size(); ++i){
                const float error = std::abs(read->values[i] - original.values[i]);
                if(original.half) worstHalf = std::max(worstHalf, error / std::max(1e-3f, std::abs(original.values[i])));
                else worstFloat = std::max(worstFloat, error);
            }
        }
        report.check("zapis i citanje", sorted && back.width == w && back.height == h && worstFloat == 0.0f && worstHalf < 1e-3f,
                     fmt("%zu kanala, %ux%u, float tocno (%.1e), half rel. %.1e", back.channels.size(), back.width, back.height,
                         double(worstFloat), double(worstHalf)));

        //Zaglavlje se da procitati i golim okom: magicni broj, pa "channels" kao prvi atribut
        std::ifstream file(path, std::ios::binary);
        char head[20] = {};
        file.read(head, sizeof(head));
        uint32_t magic;
        std::memcpy(&magic, head, 4);
        report.check("zaglavlje", magic == 20000630u && std::string(head + 8) == "channels", fmt("magic %u", magic));

        const Spool::FloatImage linear = Spool::loadImageLinear(path);
        report.check("loadImageLinear exr", linear.isValid() && std::abs(linear.pixels[4 * 3 + 0] - channels[0].values[3]) < 1e-2f && std::abs(linear.pixels[4 * 3 + 3] - channels[3].values[3]) < 1e-2f,
                     fmt("%ux%u", linear.width, linear.height));
    }

    //-- 3. negativna kontrola: kompresija se odbije --------------------------------------------------
    {
        std::ifstream in(path, std::ios::binary);
        std::vector<char> data((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        const std::string key = std::string("compression") + '\0' + "compression" + '\0';
        bool patched = false;
        for(size_t i = 0; i + key.size() + 5 < data.size(); ++i){
            if(std::memcmp(data.data() + i, key.data(), key.size()) == 0){ data[i + key.size() + 4] = 3; patched = true; break; }
        }
        const std::string zipped = (folder / "zip.exr").string();
        std::ofstream(zipped, std::ios::binary).write(data.data(), std::streamsize(data.size()));
        std::string reason;
        try{ Spool::loadExr(zipped); }catch(const std::exception& e){ reason = e.what(); }
        report.check("kompresija odbijena", patched && reason.find("komprimirani") != std::string::npos, reason);
    }

    fs::remove_all(folder);
    return report.result();
}
