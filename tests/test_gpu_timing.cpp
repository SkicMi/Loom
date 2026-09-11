// Vrijeme s karticinog sata: timestamp queryji po kadru.
//
// Mjerenje koje se ne provjerava nije mjerenje nego broj. Ovdje se brani:
//
//   jedinica          timestampPeriod na Intel UHD je 83.3 ns po otkucaju. Bez njega bi svako
//                     vrijeme bilo 83 puta premalo, a s njim dvaput 83 puta preveliko - i oboje
//                     izgleda uvjerljivo. Zato se karticino vrijeme velikog posla stavlja uz
//                     zidni sat procesora: ne smije ga prestici, a mora pokriti vecinu njega
//   osjetljivost      razmak bez ijedne naredbe je blizu nule, a razmak sa sortom nije
//   poredak i broj    oznake se vracaju onim redom i s onim imenima kojim su zapisane
//   granica           oznaka preko maxTimestamps baca, jer bi tiho izgubljena pomaknula sva
//                     kasnija vremena na kriva imena
//   iskljuceno        s maxTimestamps 0 nema poola, oznaka ne radi nista i nista se ne vraca
//
// Posao je RadixSort nad milijun kljuceva - dokazan u test_radix_sort, i dovoljno velik da
// se razlikuje od praznog hoda na svakoj kartici koju projekt ima.
#include "TestHarness.h"
#include "Core/LoomConfig.h"
#include "Core/LoomInitializer.h"
#include "Vulkan/RadixSort.h"

#include <chrono>
#include <random>
#include <stdexcept>
#include <string>
#include <vector>

int main(){
    TestReport report("vrijeme s kartice");

    LoomConfig config;
    config.width = 64; config.height = 64;
    config.appName = "gpu timing"; config.engineName = "Loom tests";
    config.headless = true;
    config.rendererConfig.maxTimestamps = 4;

    {
        LoomInitializer loom(config);

        report.check("kartica mjeri vrijeme", loom.renderer.measuresTime(),
            "timestamp queryji postoje na ovom uredjaju");
        if(!loom.renderer.measuresTime()){
            return report.result();
        }

        // ---------------------------------------------------------------------------
        // Veliki posao, izmjeren s obje strane
        // ---------------------------------------------------------------------------

        const uint32_t count = 1u << 20;
        VulkanBuffer keys(loom.device, vk::DeviceSize(count) * sizeof(uint32_t),
                          vk::BufferUsageFlagBits::eStorageBuffer, MemoryUsage::CPU_TO_GPU);
        VulkanBuffer values(loom.device, vk::DeviceSize(count) * sizeof(uint32_t),
                            vk::BufferUsageFlagBits::eStorageBuffer, MemoryUsage::CPU_TO_GPU);
        RadixSort sorter(loom.device, loom.getDescriptorPool(), keys, values, count);

        std::mt19937 random(20260911);
        std::vector<uint32_t> keyData(count), valueData(count);

        std::vector<GpuTimestamp> marks;
        double cpuWall = 0.0;

        //Tri kadra, a mjeri se zadnji: prvi zna nositi posao koji driver odgadja do prve upotrebe
        for(int frame = 0; frame < 3; ++frame){
            for(uint32_t i = 0; i < count; ++i){
                keyData[i] = random();
                valueData[i] = i;
            }
            keys.upload(keyData.data(), keyData.size() * sizeof(uint32_t));
            values.upload(valueData.data(), valueData.size() * sizeof(uint32_t));

            loom.renderer.beginFrame();
            loom.renderer.timestamp("pocetak");
            loom.renderer.timestamp("prazno");
            sorter.sort(loom.renderer, count);
            loom.renderer.timestamp("sort");

            //Kartica ne moze poceti prije nego sto je kadar poslan, ni zavrsiti poslije nego sto
            //waitIdle vrati - pa je sve sto izmjeri unutar ovog intervala
            const auto submitted = std::chrono::steady_clock::now();
            loom.renderer.endFrame();
            loom.waitIdle();
            cpuWall = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - submitted).count();

            marks = loom.renderer.readFrameTimes();
        }

        const bool shaped = marks.size() == 3 &&
                            marks[0].label == "pocetak" && marks[1].label == "prazno" && marks[2].label == "sort" &&
                            marks[0].milliseconds == 0.0 && marks[1].milliseconds >= 0.0 &&
                            marks[2].milliseconds >= marks[1].milliseconds;

        std::string trace;
        for(const GpuTimestamp& mark : marks) trace += fmt("%s=%.3f ", mark.label.c_str(), mark.milliseconds);
        report.check("oznake se vracaju redom i s imenima", shaped, trace);

        const double gpuEmpty = shaped ? marks[1].milliseconds - marks[0].milliseconds : -1.0;
        const double gpuSort  = shaped ? marks[2].milliseconds - marks[1].milliseconds : -1.0;

        report.check("jedinica: kartica ne pretjece zidni sat", shaped && gpuSort <= cpuWall * 1.02 + 0.1,
            fmt("sort %.2f ms na kartici, %.2f ms od slanja do kraja", gpuSort, cpuWall));

        report.check("jedinica: i pokriva vecinu njega", shaped && gpuSort >= 0.5 * cpuWall,
            fmt("kartica %.0f %% zidnog sata", cpuWall > 0.0 ? 100.0 * gpuSort / cpuWall : 0.0));

        report.check("prazan razmak je prazan", shaped && gpuEmpty < 0.05 * gpuSort,
            fmt("bez naredbi %.3f ms, sort %.2f ms", gpuEmpty, gpuSort));

        // ---------------------------------------------------------------------------
        // Granica i citanje u krivo vrijeme
        // ---------------------------------------------------------------------------

        loom.renderer.beginFrame();

        bool readThrew = false;
        try{ (void)loom.renderer.readFrameTimes(); } catch(const std::runtime_error&){ readThrew = true; }

        for(const char* label : {"1", "2", "3", "4"}) loom.renderer.timestamp(label);
        bool fifthThrew = false;
        std::string fifthMessage;
        try{ loom.renderer.timestamp("5"); } catch(const std::runtime_error& error){ fifthThrew = true; fifthMessage = error.what(); }

        loom.renderer.endFrame();
        loom.waitIdle();
        const std::vector<GpuTimestamp> four = loom.renderer.readFrameTimes();

        report.check("peta oznaka u kadru od cetiri baca", fifthThrew && four.size() == 4 && four[3].label == "4",
            fmt("\"%s\", vraceno %zu oznaka", fifthMessage.c_str(), four.size()));

        report.check("citanje usred kadra baca", readThrew, "readFrameTimes izmedju beginFrame i endFrame");
    }

    {
        LoomConfig off = config;
        off.rendererConfig.maxTimestamps = 0;
        LoomInitializer loom(off);

        loom.renderer.beginFrame();
        loom.renderer.timestamp("nista");
        loom.renderer.endFrame();
        loom.waitIdle();

        report.check("iskljuceno: nista se ne mjeri",
            !loom.renderer.measuresTime() && loom.renderer.readFrameTimes().empty(),
            "maxTimestamps 0: bez poola, oznaka ne radi nista");
    }

    report.checkNoValidationMessages();
    return report.result();
}
