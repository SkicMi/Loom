// Prefiksni zbroj: koliko ih ide prije mene.
//
// Preduvjet za binning splatova - svaki splat mora znati gdje u zajednickom polju parova
// pocinje njegov dio, a to je zbroj svih pločica koje su prije njega zauzeli drugi.
//
// Referenca je zbrajanje na procesoru: tocan odgovor, mjesto po mjesto. I jedna stvar koju
// takav test lako previdi, pa je ovdje izricito: UKUPAN ZBROJ nije isto sto i zadnji pomak.
// Ekskluzivni zbroj zadnjeg mjesta ne ukljucuje sam to mjesto, pa se ukupno mora racunati
// posebno - i tocno tu je prva verzija ovog koda bila kriva, jer je zadnju vrijednost htjela
// procitati iz polja koje ju je vec prepisalo.
#include "TestHarness.h"
#include "TestScene.h"
#include "Core/LoomConfig.h"
#include "Core/LoomInitializer.h"
#include "Vulkan/PrefixSum.h"

#include <numeric>
#include <random>
#include <string>
#include <vector>

int main(){
    TestReport report("prefiksni zbroj");

    LoomConfig config;
    config.width = 64; config.height = 64;
    config.appName = "scan"; config.engineName = "Loom tests";
    config.headless = true;
    LoomInitializer loom(config);

    const uint32_t capacity = 1u << 20;
    const vk::BufferUsageFlags usage = vk::BufferUsageFlagBits::eStorageBuffer |
                                       vk::BufferUsageFlagBits::eTransferSrc |
                                       vk::BufferUsageFlagBits::eTransferDst;

    VulkanBuffer values(loom.device, vk::DeviceSize(capacity) * sizeof(uint32_t), usage, MemoryUsage::GPU_ONLY);
    VulkanBuffer upload(loom.device, vk::DeviceSize(capacity) * sizeof(uint32_t),
                        vk::BufferUsageFlagBits::eTransferSrc, MemoryUsage::CPU_TO_GPU);
    VulkanBuffer download(loom.device, vk::DeviceSize(capacity) * sizeof(uint32_t),
                          vk::BufferUsageFlagBits::eTransferDst, MemoryUsage::GPU_TO_CPU);
    VulkanBuffer totalBack(loom.device, sizeof(uint32_t),
                           vk::BufferUsageFlagBits::eTransferDst, MemoryUsage::GPU_TO_CPU);

    PrefixSum scanner(loom.device, loom.getDescriptorPool(), values, capacity);

    //Jedno zbrajanje: podaci gore, tri dispatcha, podaci dolje
    auto runScan = [&](const std::vector<uint32_t>& input){
        const uint32_t count = uint32_t(input.size());
        upload.upload(input.data(), count * sizeof(uint32_t));
        loom.command.copyBuffer(upload.getBuffer(), values.getBuffer(), count * sizeof(uint32_t));

        loom.renderer.beginFrame();
        scanner.scan(loom.renderer, count);
        loom.renderer.endFrame();
        loom.waitIdle();

        loom.command.copyBuffer(values.getBuffer(), download.getBuffer(), count * sizeof(uint32_t));
        std::vector<uint32_t> offsets(count);
        download.download(offsets.data(), count * sizeof(uint32_t));

        loom.command.copyBuffer(scanner.getTotal().getBuffer(), totalBack.getBuffer(), sizeof(uint32_t));
        uint32_t sum = 0;
        totalBack.download(&sum, sizeof(uint32_t));

        return std::pair<std::vector<uint32_t>, uint32_t>{offsets, sum};
    };

    //Sto bi procesor dao
    auto onCpu = [](const std::vector<uint32_t>& input){
        std::vector<uint32_t> offsets(input.size());
        uint32_t running = 0;
        for(size_t i = 0; i < input.size(); ++i){
            offsets[i] = running;
            running += input[i];
        }
        return std::pair<std::vector<uint32_t>, uint32_t>{offsets, running};
    };

    auto compare = [&](const char* name, const std::vector<uint32_t>& input){
        const auto got = runScan(input);
        const auto want = onCpu(input);

        size_t wrong = 0;
        for(size_t i = 0; i < input.size(); ++i){
            if(got.first[i] != want.first[i]) ++wrong;
        }

        report.check(name, wrong == 0 && got.second == want.second,
            fmt("%zu elemenata: %zu pomaka odstupa, ukupno %u naspram %u",
                input.size(), wrong, got.second, want.second));
        return wrong == 0 && got.second == want.second;
    };

    // -- prava velicina, nasumicno --------------------------------------------------------

    std::mt19937 random(20260909);
    std::vector<uint32_t> scene(741883);
    for(uint32_t& value : scene) value = random() % 40;   //koliko pločica splat zahvaca
    compare("prava velicina", scene);

    // -- sve jedinice: pomak je vlastiti indeks -------------------------------------------

    {
        //Analiticki: kad su sve vrijednosti jedan, pomak na mjestu i JEST i, a ukupno je n.
        //Ovo hvata pomak za jedan koji bi se u nasumicnom polju izgubio u brojevima
        const uint32_t count = 100000;
        std::vector<uint32_t> ones(count, 1);
        const auto got = runScan(ones);

        size_t wrong = 0;
        for(uint32_t i = 0; i < count; ++i) if(got.first[i] != i) ++wrong;

        report.check("sve jedinice daju vlastiti indeks",
            wrong == 0 && got.second == count,
            fmt("%zu mjesta odstupa, ukupno %u naspram %u", wrong, got.second, count));
    }

    // -- rubne velicine i oblici ----------------------------------------------------------

    {
        std::string trace;
        bool allHold = true;
        //Brojevi oko granice bloka (1024): tamo se rubni element lako izgubi
        for(uint32_t count : {1u, 1023u, 1024u, 1025u, 2048u, 3000u}){
            std::vector<uint32_t> input(count);
            for(uint32_t& value : input) value = random() % 7;
            const auto got = runScan(input);
            const auto want = onCpu(input);
            bool holds = got.second == want.second;
            for(uint32_t i = 0; i < count && holds; ++i) holds = got.first[i] == want.first[i];
            if(!holds) allHold = false;
            trace += fmt("%u:%s ", count, holds ? "ok" : "PAO");
        }
        report.check("rubne velicine", allHold, trace.c_str());
    }

    {
        //Same nule: svi pomaci nula, ukupno nula. Zvuci trivijalno, ali je jedini slucaj u
        //kojem se greska "ukupno je zadnji pomak" ne vidi - pa je vrijedan para s onim ispod
        std::vector<uint32_t> zeroes(5000, 0);
        compare("sve nule", zeroes);

        //I jedna jedina vrijednost na kraju: ukupno je ta vrijednost, a zadnji pomak je nula.
        //Ovo je test koji je uhvatio krivu prvu verziju racunanja ukupnog zbroja
        std::vector<uint32_t> lastOnly(5000, 0);
        lastOnly.back() = 12345;
        const auto got = runScan(lastOnly);
        report.check("ukupno nije zadnji pomak",
            got.second == 12345 && got.first.back() == 0,
            fmt("zadnji pomak %u (treba 0), ukupno %u (treba 12345)", got.first.back(), got.second));
    }

    // -- i da se dva ista zbrajanja ne razlikuju -------------------------------------------

    {
        std::vector<uint32_t> input(50000);
        for(uint32_t& value : input) value = random() % 13;
        const auto first = runScan(input);
        const auto second = runScan(input);

        size_t different = 0;
        for(size_t i = 0; i < input.size(); ++i) if(first.first[i] != second.first[i]) ++different;

        report.check("isti ulaz daje isti izlaz",
            different == 0 && first.second == second.second,
            fmt("%zu mjesta razlike, ukupno %u naspram %u", different, first.second, second.second));
    }

    report.checkNoValidationMessages();
    return report.result();
}
