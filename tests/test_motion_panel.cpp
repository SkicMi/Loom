// Pokret iz teksta: naredba za Kimodo, niz radnji, povijest - bez prozora i bez Kimoda.
//
// ZASTO SE OVO TESTIRA. Opis ide u ljusku (popen), pa je navodnik u opisu ili tocka na krivom
// mjestu vec problem: tocka dijeli radnje, pa "stop. then run" postane dvije radnje s jednim
// trajanjem, i Kimodo odbije ili tiho uzme krivo trajanje. Naredba se zato provjerava i pravom
// ljuskom (/bin/sh) - argumenti moraju stici tocno onakvi kakvi su napisani.
#include "TestHarness.h"

#include "../src/LoomMotionPanel.h"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <thread>

int main(){
    TestReport report("pokret iz teksta");

    Loom::MotionRequest request;
    request.actions = {{"a person walks forward. slowly", 3.0f}, {"  ", 2.0f}, {"sits down on it's chair\n", 2.5f}};
    request.seed = 7;
    request.diffusionSteps = 60;
    request.footCleanup = false;

    //-- 1. prazne radnje ispadaju, tocka u opisu postaje zarez ------------------------------------
    const std::vector<Loom::MotionAction> filled = Loom::filledActions(request.actions);
    report.check("prazna radnja ispada, tocka unutar opisa ne dijeli radnju",
        filled.size() == 2 && filled[0].prompt == "a person walks forward, slowly" && filled[1].prompt == "sits down on it's chair",
        filled.empty() ? "" : filled[0].prompt + " | " + filled.back().prompt);

    //-- 2. naredba: pravom ljuskom stizu tocno ti argumenti ----------------------------------------
    {
        //Umjesto kimodo_gen: skripta koja ispise svoje argumente, svaki u svom retku
        const std::filesystem::path echo = std::filesystem::temp_directory_path() / "loom kimodo echo.sh";
        {
            std::ofstream script(echo);
            script << "#!/bin/sh\nfor a in \"$@\"; do printf '%s\\n' \"$a\"; done\n";
        }
        std::filesystem::permissions(echo, std::filesystem::perms::owner_all);
        const std::string command = Loom::buildMotionCommand(echo, request, "/tmp/izlaz s razmakom/motion_1");
        FILE* shell = popen(command.c_str(), "r");
        std::vector<std::string> args;
        char line[512];
        while(shell && std::fgets(line, sizeof(line), shell)){
            std::string a(line);
            if(!a.empty() && a.back() == '\n') a.pop_back();
            args.push_back(a);
        }
        const int status = shell ? pclose(shell) : -1;
        auto has = [&](const std::string& flag, const std::string& value){
            for(size_t i = 0; i + 1 < args.size(); ++i) if(args[i] == flag && args[i + 1] == value) return true;
            return false;
        };
        const bool ok = status == 0 && !args.empty() &&
                        args[0] == "a person walks forward, slowly. sits down on it's chair" &&
                        has("--duration", "3.00 2.50") && has("--seed", "7") && has("--diffusion_steps", "60") &&
                        has("--output", "/tmp/izlaz s razmakom/motion_1") &&
                        std::find(args.begin(), args.end(), "--no-postprocess") != args.end() &&
                        std::find(args.begin(), args.end(), "--bvh") != args.end();
        report.check("naredba kroz /bin/sh: opisi spojeni tockom, trajanja, sjeme, koraci, izlaz s razmakom", ok,
            args.empty() ? "nema izlaza" : args[0]);

        //Bez sjemena i s cistenjem stopala: tih zastavica nema
        Loom::MotionRequest plain;
        plain.actions = {{"a person jumps", 2.0f}};
        const std::string simple = Loom::buildMotionCommand(echo, plain, "/tmp/m");
        report.check("bez sjemena nema --seed, s cistenjem nema --no-postprocess",
            simple.find("--seed") == std::string::npos && simple.find("--no-postprocess") == std::string::npos, simple);
        std::filesystem::remove(echo);
    }

    //-- 3. povijest: opis uz BVH, najnoviji prvi ---------------------------------------------------
    {
        const std::filesystem::path directory = std::filesystem::temp_directory_path() / "loom_pokreti_test";
        std::filesystem::remove_all(directory);
        std::filesystem::create_directories(directory);
        Loom::writeMotionSidecar(directory / "motion_1", request);
        std::ofstream(directory / "motion_1.bvh") << "HIERARCHY";
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        std::ofstream(directory / "motion_2.bvh") << "HIERARCHY";            //bez opisa
        const std::vector<Loom::MotionHistoryEntry> history = Loom::motionHistory(directory);
        report.check("povijest: najnoviji prvi, opis i trajanja iz .txt, bez opisa ime datoteke",
            history.size() == 2 && history[0].summary == "motion_2.bvh" && history[1].actions.size() == 2 &&
            history[1].actions[1].duration == 2.5f && history[1].summary == "a person walks forward, slowly  (+1)",
            history.size() == 2 ? history[0].summary + " | " + history[1].summary : "krivo");
        std::filesystem::remove_all(directory);
    }

    return report.result();
}
