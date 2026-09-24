#pragma once
//=============================================================================================
// POSLOVI KOJE EDITOR POKRECE: solve i trening, kao zasebni procesi.
//
// SOLVER SE POKRECE KAO ZASEBAN PROCES, ne kao funkcija u petlji editora, i to nije lijenost nego
// odluka:
//
//   - VideoSolve vec ispisuje sve sto treba znati, redak po redak. Taj ispis je nastajao uz svako
//     mjerenje i tocno je ono sto suicelje treba pokazati.
//   - Solve traje satima i alocira gigabajte. Kad padne, prozor ostaje ziv i kaze sto se dogodilo.
//   - Nijedna linija solvera se ne mijenja da bi suicelje postojalo.
//
// Izdvojeno iz loom_app.cpp kad je loom postao editor: prozor se promijenio, a posao nije
//=============================================================================================
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <string>
#include <vector>

namespace Loom{

//Faze onako kako ih VideoSolve ispisuje. Redoslijed je onaj kojim stvarno idu, pa se napredak
//cita iz toga koja se zadnja javila
struct Phase{
    const char* marker;     //sto se trazi u retku ispisa
    const char* label;      //sto pise u prozoru
    double share;           //priblizan udio ukupnog vremena, IZMJEREN na kamenom zidu
};

//UDJELI SU IZ STVARNOG MJERENJA, na OPTIMIZIRANOM buildu (C0257, 231 kadar 4K, ukupno 2381 s):
//
//    pracenje i kljucni kadrovi     183 s   kumulativno  0.08
//    prostor mjerila                501 s                0.29
//    graf spojen                    606 s                0.33
//    samokalibracija                 90 s                0.37
//    rekonstrukcija                 202 s                0.46
//    zapis 229 slika u 4K                                0.64
//    pune slicice                   430 s                0.82
//
// Broj je KUMULATIVAN udio ukupnog vremena u trenutku kad se taj redak ispise, pa se iz njega i
// proteklog vremena procijeni ostatak.
//
// PRVA VERZIJA OVE TABLICE BILA JE KRIVA ZA 3.4 PUTA jer je mjerena na Debug buildu - vidi
// warnIfUnoptimised nize. Brojke iznad su s -O2.
//
// I ovako su samo procjena: mjerene su na JEDNOJ snimci, a druga snimka ih ne mora slijediti -
// prazan bijeli zid provede vise vremena u poklapanju, bogata tekstura u potpisima. Zato u prozoru
// pise "jos oko", ne tocan broj
inline const Phase phases[] = {
    {"Snimka ",            "reading video",             0.01},
    {"kljucnih kadrova",   "tracking and keyframes", 0.08},
    {"prostor mjerila:",   "features and matching",      0.29},
    {"graf poklapanja:",   "match graph",                0.33},
    {"samokalibracija:",   "focal length",                    0.37},
    {"nakon pune obrade",  "reconstruction",             0.46},
    {"slika u ",           "writing images",                0.64},
    {"pune slicice:",      "full-size frames",               0.82},
    {"Zapisano u",         "done",                     1.00},
};

//Sto se trenutno vrti. Oba posla su vanjski procesi koji ispisuju napredak, pa ih jedna te ista
//masinerija prati - razlikuju se samo po tome sto se u ispisu trazi
enum class Task{ Solve, Train, WeaverMotion, AutoRig, Clean, Proxy };

struct Job{
    std::mutex lock;
    Task task = Task::Solve;
    std::atomic<float> fraction{-1.0f};   //trening zna tocno gdje je; solve samo procjenjuje
    std::vector<std::string> lines;
    std::atomic<bool> running{false};
    std::atomic<bool> failed{false};
    std::atomic<int> phase{-1};
    std::chrono::steady_clock::time_point started;
    std::string outputDirectory;
};

//Cita ispis procesa redak po redak. Svaki redak koji stigne odmah je vidljiv u prozoru - bez toga
//bi se cekalo da proces zavrsi, sto je bas ono od cega se htjelo pobjeci
inline void runSolve(Job& job, std::string command, std::string outputDirectory, Task task,
              int totalSteps){
    job.task = task;
    job.fraction = -1.0f;
    job.outputDirectory = outputDirectory;
    job.started = std::chrono::steady_clock::now();
    job.running = true;
    job.phase = -1;
    {
        std::lock_guard<std::mutex> guard(job.lock);
        job.lines.clear();
        job.lines.push_back("> " + command);
    }

    FILE* pipe = popen((command + " 2>&1").c_str(), "r");
    if(!pipe){
        job.failed = true;
        job.running = false;
        return;
    }

    char buffer[4096];
    while(std::fgets(buffer, sizeof(buffer), pipe)){
        std::string line(buffer);
        while(!line.empty() && (line.back() == '\n' || line.back() == '\r')) line.pop_back();
        if(line.empty()) continue;

        //Spoolove poruke o dekodiranju nisu ono sto korisnika zanima
        if(line.find("tragova zivo") != std::string::npos) continue;
        if(line.find("mov,mp4") != std::string::npos) continue;

        if(task == Task::Solve){
            for(int i = 0; i < int(sizeof(phases) / sizeof(phases[0])); ++i){
                if(line.find(phases[i].marker) != std::string::npos && i > job.phase) job.phase = i;
            }
        }else if(task == Task::Train){
            //TRENING ZNA TOCNO GDJE JE, pa se ne procjenjuje nego cita: redci su oblika
            //"   6999  gubitak 0.0330  gaussiana 3775883"
            if(line.find("gubitak") != std::string::npos && totalSteps > 0){
                const int step = std::atoi(line.c_str());
                if(step > 0) job.fraction = float(step) / float(totalSteps);
            }
            if(line.find("Spremljeno:") != std::string::npos) job.fraction = 1.0f;
        }

        std::lock_guard<std::mutex> guard(job.lock);
        job.lines.push_back(line);
        if(job.lines.size() > 400) job.lines.erase(job.lines.begin());
    }

    const int status = pclose(pipe);
    job.failed = (status != 0);
    job.running = false;
}


//=============================================================================================
// STO JE ZAPRAVO PALO, recenicom.
//
// ZASTO. Kad je trening pao na nedostajucem ninji, prozor je pokazao petnaest redaka Python
// tragova i korisnik je iz toga trebao sam izvuci da mu fali jedan alat u PATH-u. To je isto kao
// da nije pokazao nista.
//
// Ovdje se ispis pregleda unatrag i trazi poznat potpis. Kad se nadje, gore pise recenica; kad se
// ne nadje, i dalje stoje zadnji redci - dakle nikad manje nego prije
//=============================================================================================
struct KnownFailure{
    const char* signature;
    const char* meaning;
};

inline const KnownFailure knownFailures[] = {
    {"Ninja is required",      "gsplat needs `ninja` on PATH to compile its CUDA part"},
    {"out of memory",          "the GPU is out of memory - close what else is rendering and try again"},
    {"No module named",        "a package is missing from .venv; check the installation"},
    {"Premalo kljucnih",       "the video is too short or the camera does not move enough"},
    {"nema dovoljno",          "the video did not give enough shared points"},
    {"No such file",           "the path does not exist - check the video folder"},
    {"NEOPTIMIZIRAN BUILD",    "the tool was built without -O2; see the command in Output"},
};

inline std::string explainFailure(const std::vector<std::string>& lines){
    for(size_t i = lines.size(); i-- > 0; ){
        for(const KnownFailure& known : knownFailures){
            if(lines[i].find(known.signature) != std::string::npos) return known.meaning;
        }
    }
    return {};
}

inline std::string humanTime(double seconds){
    char text[64];
    if(seconds < 90.0){ std::snprintf(text, sizeof(text), "%.0f s", seconds); return text; }
    if(seconds < 5400.0){ std::snprintf(text, sizeof(text), "%.0f min", seconds / 60.0); return text; }
    std::snprintf(text, sizeof(text), "%.1f h", seconds / 3600.0);
    return text;
}


//=============================================================================================
// NEOPTIMIZIRAN BUILD SE MORA JAVITI, glasno.
//
// ZASTO OVO POSTOJI. Cijeli je projekt od 17. do 22. rujna stajao u build mapi postavljenoj na
// Debug - dakle bez ijedne -O zastavice. Nijedno mjerenje u tom razdoblju nije vrijedilo, a to se
// nije vidjelo jer alat radi jednako, samo cetiri puta sporije. Dan je potrosen na trazenje uskog
// grla u kodu koji prevoditelj nije ni pokusao optimizirati.
//
// CMakeLists vec brani od toga - postavlja RelWithDebInfo kad build type nije zadan - ali ta se
// zastita ne aktivira kad je Debug VEC u predmemoriji. Jedino sto pouzdano zna je li prevodjeno s
// optimizacijom je sam prevoditelj, pa se pita njega
//=============================================================================================
inline void warnIfUnoptimised(){
#ifndef __OPTIMIZE__
    std::printf("\n  !! UNOPTIMISED BUILD - measurements are not valid and the tool is about four times "
                "slower.\n     Fix: cmake -S . -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo\n\n");
#endif
}

}
