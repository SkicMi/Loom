// G2: sortiranje na kartici, i dokaz da je stabilno.
//
// Ovo je jedini korak faze G gdje BRZINA JEST ISPRAVNOST. Alfa kompozicija nije komutativna,
// pa krivi redoslijed nije "malo drugacija slika" nego kriva slika - a poredak se mijenja
// svaki put kad se kamera pomakne, za svih 741883 gaussiana odjednom.
//
// Referenca je std::sort na procesoru: tocan odgovor, kljuc po kljuc, bez ijednog "priblizno".
//
// Sto se provjerava, i zasto bas to:
//
//   poredak            svaki kljuc na svom mjestu, usporedjen s CPU-om
//   vrijednost prati   kljuc bez svoje vrijednosti je beskorisan - sortira se indeks splata,
//     svoj kljuc       a ne dubina
//   STABILNOST         jednaki kljucevi zadrzavaju izvorni poredak. Bez toga trik s dva
//                      prolaza (dubina, pa pločica) ne radi UOPCE, i ista scena iz iste
//                      kamere daje dvije razlicite slike
//   negativni float    kljuc iz floata mora ocuvati poredak kroz cijeli raspon, ne samo za
//                      pozitivne. Dubina jest pozitivna, ali funkcija koja radi za pola
//                      ulaza je mina koja ceka prvi drugi poziv
//   rubni ulazi        vec sortirano, obrnuto, sve isto, jedan element, broj koji nije
//                      visekratnik bloka
//
// I kontrola koja cuva sve gornje: usporedba mora GRISTI - jedan zamijenjen par mora pasti.
#include "TestHarness.h"
#include "TestScene.h"
#include "Core/LoomConfig.h"
#include "Core/LoomInitializer.h"
#include "Vulkan/RadixSort.h"

#include <algorithm>
#include <cstring>
#include <numeric>
#include <random>
#include <string>
#include <vector>

namespace{

//Kljuc i vrijednost, kakvi ulaze i kakvi izlaze
struct Pair{
    uint32_t key = 0;
    uint32_t value = 0;
};

//Sto bi std::sort dao. STABLE_sort, jer je to tvrdnja koju branimo
std::vector<Pair> sortedOnCpu(std::vector<Pair> pairs){
    std::stable_sort(pairs.begin(), pairs.end(),
        [](const Pair& a, const Pair& b){return a.key < b.key;});
    return pairs;
}

}

int main(){
    TestReport report("G2 radix sort");

    LoomConfig config;
    config.width = 64; config.height = 64;
    config.appName = "radix"; config.engineName = "Loom tests";
    config.headless = true;
    LoomInitializer loom(config);

    // -------------------------------------------------------------------------------
    // Kljuc iz floata: poredak mora prezivjeti pretvorbu, kroz cijeli raspon
    // -------------------------------------------------------------------------------

    {
        std::vector<float> values = {
            -1e30f, -1000.0f, -1.5f, -1.0f, -0.001f, -0.0f, 0.0f, 0.001f,
            1.0f, 1.5f, 2.0f, 1000.0f, 1e30f
        };
        std::sort(values.begin(), values.end());

        bool monotone = true;
        std::string worst;
        for(size_t i = 0; i + 1 < values.size(); ++i){
            const uint32_t a = RadixSort::keyFromFloat(values[i]);
            const uint32_t b = RadixSort::keyFromFloat(values[i+1]);
            //Jednaki floatovi (-0.0 i 0.0) smiju dati jednake kljuceve, ali nikad obrnute
            if(!(a <= b)){
                monotone = false;
                worst = fmt("%g -> %u, a %g -> %u", double(values[i]), a, double(values[i+1]), b);
            }
        }

        report.check("kljuc iz floata cuva poredak", monotone,
            monotone ? fmt("kroz %zu vrijednosti od -1e30 do 1e30, ukljucujuci obje nule", values.size())
                     : worst.c_str());
    }

    // -------------------------------------------------------------------------------
    // Sortiranje samo
    // -------------------------------------------------------------------------------

    const uint32_t capacity = 1u << 20;   //milijun i nesto, red velicine prave scene

    const vk::BufferUsageFlags usage = vk::BufferUsageFlagBits::eStorageBuffer |
                                       vk::BufferUsageFlagBits::eTransferSrc |
                                       vk::BufferUsageFlagBits::eTransferDst;

    VulkanBuffer keys(loom.device, vk::DeviceSize(capacity) * sizeof(uint32_t), usage, MemoryUsage::GPU_ONLY);
    VulkanBuffer values(loom.device, vk::DeviceSize(capacity) * sizeof(uint32_t), usage, MemoryUsage::GPU_ONLY);

    VulkanBuffer upload(loom.device, vk::DeviceSize(capacity) * sizeof(uint32_t),
                        vk::BufferUsageFlagBits::eTransferSrc, MemoryUsage::CPU_TO_GPU);
    VulkanBuffer download(loom.device, vk::DeviceSize(capacity) * sizeof(uint32_t),
                          vk::BufferUsageFlagBits::eTransferDst, MemoryUsage::GPU_TO_CPU);

    RadixSort sorter(loom.device, loom.getDescriptorPool(), keys, values, capacity);

    //Jedan sort: podaci gore, dispatch, podaci dolje
    auto runSortWith = [&](RadixSort& which, const std::vector<Pair>& input){
        const uint32_t count = uint32_t(input.size());

        std::vector<uint32_t> flat(count);
        for(uint32_t i = 0; i < count; ++i) flat[i] = input[i].key;
        upload.upload(flat.data(), count * sizeof(uint32_t));
        loom.command.copyBuffer(upload.getBuffer(), keys.getBuffer(), count * sizeof(uint32_t));

        for(uint32_t i = 0; i < count; ++i) flat[i] = input[i].value;
        upload.upload(flat.data(), count * sizeof(uint32_t));
        loom.command.copyBuffer(upload.getBuffer(), values.getBuffer(), count * sizeof(uint32_t));

        loom.renderer.beginFrame();
        which.sort(loom.renderer, count);
        loom.renderer.endFrame();
        loom.waitIdle();

        std::vector<Pair> output(count);
        std::vector<uint32_t> back(count);

        loom.command.copyBuffer(keys.getBuffer(), download.getBuffer(), count * sizeof(uint32_t));
        download.download(back.data(), count * sizeof(uint32_t));
        for(uint32_t i = 0; i < count; ++i) output[i].key = back[i];

        loom.command.copyBuffer(values.getBuffer(), download.getBuffer(), count * sizeof(uint32_t));
        download.download(back.data(), count * sizeof(uint32_t));
        for(uint32_t i = 0; i < count; ++i) output[i].value = back[i];

        return output;
    };

    auto runSort = [&](const std::vector<Pair>& input){
        return runSortWith(sorter, input);
    };

    //Koliko se mjesta razlikuje od onoga sto je dao procesor
    auto compare = [](const std::vector<Pair>& got, const std::vector<Pair>& want){
        size_t wrongKeys = 0, wrongValues = 0;
        for(size_t i = 0; i < want.size(); ++i){
            if(got[i].key != want[i].key) ++wrongKeys;
            if(got[i].value != want[i].value) ++wrongValues;
        }
        return std::pair<size_t,size_t>{wrongKeys, wrongValues};
    };

    // -- nasumicno, u velicini prave scene ----------------------------------------------

    std::mt19937 random(20260909);
    std::vector<Pair> scene(741883);
    for(uint32_t i = 0; i < scene.size(); ++i){
        //Dubine kakve scena stvarno ima, pretvorene istim putem kojim ce ici i na kartici
        const float depth = 0.1f + 60.0f * float(random() % 1000000) / 1000000.0f;
        scene[i].key = RadixSort::keyFromFloat(depth);
        scene[i].value = i;
    }

    {
        const std::vector<Pair> got = runSort(scene);
        const std::vector<Pair> want = sortedOnCpu(scene);
        const auto wrong = compare(got, want);

        report.check("prava velicina, nasumicne dubine",
            wrong.first == 0 && wrong.second == 0,
            fmt("%zu elemenata: %zu kljuceva i %zu vrijednosti odstupa od std::stable_sort",
                scene.size(), wrong.first, wrong.second));

        //KONTROLA: usporedba mora gristi. Zamijeni dva susjedna i mora puknuti
        std::vector<Pair> nudged = got;
        std::swap(nudged[12345], nudged[12346]);
        const auto nudgedWrong = compare(nudged, want);
        report.check("usporedba grize", nudgedWrong.first > 0 || nudgedWrong.second > 0,
            fmt("jedan zamijenjen par dao je %zu kljuceva i %zu vrijednosti razlike",
                nudgedWrong.first, nudgedWrong.second));
    }

    // -- stabilnost: mnogo jednakih kljuceva --------------------------------------------

    {
        //Samo 64 razlicita kljuca na 300000 elemenata, pa svaki ima tisuce jednakih. Vrijednost
        //je izvorni indeks, pa se poredak unutar skupine da procitati izravno
        std::vector<Pair> many(300000);
        for(uint32_t i = 0; i < many.size(); ++i){
            many[i].key = (random() % 64) * 1000;
            many[i].value = i;
        }

        const std::vector<Pair> got = runSort(many);

        //Unutar svake skupine jednakih kljuceva vrijednosti moraju rasti. To JE stabilnost
        size_t inversions = 0;
        for(size_t i = 0; i + 1 < got.size(); ++i){
            if(got[i].key == got[i+1].key && got[i].value > got[i+1].value) ++inversions;
        }

        const auto wrong = compare(got, sortedOnCpu(many));

        report.check("stabilnost",
            inversions == 0 && wrong.first == 0 && wrong.second == 0,
            fmt("%zu elemenata na 64 razlicita kljuca: %zu obrnutih parova unutar skupine, "
                "%zu odstupanja od stable_sorta", many.size(), inversions, wrong.first + wrong.second));
    }

    // -- rubni ulazi ---------------------------------------------------------------------

    {
        std::string trace;
        bool allHold = true;

        struct Case{
            const char* name;
            uint32_t count;
        };

        //Brojevi oko granice bloka (1024) su ovdje jer se rubni element lako izgubi
        const Case cases[] = {
            {"jedan", 1}, {"blok bez jednog", 1023}, {"tocno blok", 1024},
            {"blok i jedan", 1025}, {"dva bloka i nesto", 2100}
        };

        for(const Case& one : cases){
            std::vector<Pair> input(one.count);
            for(uint32_t i = 0; i < one.count; ++i){
                input[i].key = random();
                input[i].value = i;
            }
            const auto wrong = compare(runSort(input), sortedOnCpu(input));
            if(wrong.first != 0 || wrong.second != 0) allHold = false;
            trace += fmt("%s(%u):%s ", one.name, one.count,
                         (wrong.first == 0 && wrong.second == 0) ? "ok" : "PAO");
        }

        report.check("rubne velicine", allHold, trace.c_str());
    }

    {
        std::string trace;
        bool allHold = true;
        const uint32_t count = 5000;

        //Tri ulaza na kojima sortovi vole pasti
        std::vector<std::pair<const char*, std::vector<Pair>>> shapes;

        std::vector<Pair> ascending(count), descending(count), same(count);
        for(uint32_t i = 0; i < count; ++i){
            ascending[i] = {i, i};
            descending[i] = {count - i, i};
            same[i] = {7u, i};
        }
        shapes.push_back({"vec sortirano", ascending});
        shapes.push_back({"obrnuto", descending});
        shapes.push_back({"sve isto", same});

        for(const auto& shape : shapes){
            const auto wrong = compare(runSort(shape.second), sortedOnCpu(shape.second));
            if(wrong.first != 0 || wrong.second != 0) allHold = false;
            trace += fmt("%s:%s ", shape.first, (wrong.first == 0 && wrong.second == 0) ? "ok" : "PAO");
        }

        report.check("rubni oblici", allHold, trace.c_str());
    }

    // -- i da se dva ista sorta ne razlikuju ---------------------------------------------

    {
        //Determinizam nije sitnica: cijeli ovaj projekt mjeri slike bajt po bajt, a sort koji
        //dva puta vrati razlicit poredak cini takvu provjeru neprovjerljivom
        std::vector<Pair> input(50000);
        for(uint32_t i = 0; i < input.size(); ++i){
            input[i].key = random() % 500;
            input[i].value = i;
        }

        const std::vector<Pair> first = runSort(input);
        const std::vector<Pair> second = runSort(input);

        size_t differences = 0;
        for(size_t i = 0; i < first.size(); ++i){
            if(first[i].key != second[i].key || first[i].value != second[i].value) ++differences;
        }

        report.check("isti ulaz daje isti izlaz", differences == 0,
            fmt("%zu od %zu mjesta razlike izmedju dva pokretanja", differences, first.size()));
    }

    // -- uzi kljuc, manje prolaza --------------------------------------------------------
    //
    // Kljuc pločice ima 12 bita, pa sortirati ga kroz svih 32 znaci cetiri prolaza koja samo
    // premjestaju nule. Sort sa 16 bita mora dati isto sto i puni za kljuceve koji stanu - i MORA
    // POGRIJESITI za one koji ne stanu. Da i to prode, ne bi bilo dokazano da je prolaza manje.

    {
        RadixSort narrow(loom.device, loom.getDescriptorPool(), keys, values, capacity, nullptr, 16);

        std::mt19937 narrowRandom(20260911);
        std::vector<Pair> fits(300000), spills(300000);
        for(uint32_t i = 0; i < fits.size(); ++i){
            fits[i].key = narrowRandom() & 0xFFFFu;
            fits[i].value = i;
            spills[i].key = narrowRandom();
            spills[i].value = i;
        }

        const auto fitWrong = compare(runSortWith(narrow, fits), sortedOnCpu(fits));
        const auto spillWrong = compare(runSortWith(narrow, spills), sortedOnCpu(spills));

        report.check("16 bita, kljucevi koji stanu",
            fitWrong.first == 0 && fitWrong.second == 0 && narrow.getPasses() == 4 && sorter.getPasses() == 8,
            fmt("%zu kljuceva i %zu vrijednosti odstupa; prolaza %u (puni sort %u)",
                fitWrong.first, fitWrong.second, narrow.getPasses(), sorter.getPasses()));

        report.check("16 bita, kljucevi koji ne stanu ostanu neporedani", spillWrong.first > 0,
            fmt("%zu od %zu kljuceva nije na mjestu", spillWrong.first, spills.size()));

        std::string bitsTrace;
        bool allEven = true;
        for(uint32_t bits : {1u, 4u, 5u, 12u, 13u, 16u, 17u, 32u}){
            const uint32_t passes = RadixSort::passesFor(bits);
            if(passes % 2 != 0 || passes * 4 < bits) allEven = false;
            bitsTrace += fmt("%u:%u ", bits, passes);
        }
        report.check("prolaza je uvijek paran broj i pokriva kljuc", allEven, bitsTrace);
    }

    report.checkNoValidationMessages();
    return report.result();
}
