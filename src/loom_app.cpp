// Loom kao desktop alat: `Loom` u terminalu otvori prozor i odande se bira sto alat radi.
//
// ZASTO POSTOJI. Sve dosad je bilo naredbe u terminalu s osam pozicijskih argumenata i ispisom
// koji se cita nakon sto sve zavrsi. Za mjerenje je to bilo dovoljno; za koristenje nije. Onaj tko
// solva snimku treba vidjeti DOK TRAJE: koja faza ide, koliko je proslo, sto je alat nasao i sto
// mu ne valja.
//
// SOLVER SE POKRECE KAO ZASEBAN PROCES, ne kao funkcija u ovoj petlji, i to nije lijenost nego
// odluka:
//
//   - VideoSolve vec ispisuje sve sto treba znati, redak po redak. Taj ispis je nastajao uz svako
//     mjerenje i tocno je ono sto suicelje treba pokazati.
//   - Solve traje satima i alocira gigabajte. Kad padne, prozor ostaje ziv i kaze sto se dogodilo.
//   - Nijedna linija solvera se ne mijenja da bi suicelje postojalo. Da se ugradjivao u petlju,
//     trebalo bi ga presloziti oko povratnih poziva - a onda bi se mjerenja morala ponoviti.
//
// Kad zatreba zivi oblak tocaka, VideoSolve ce ga morati povremeno zapisati; citanje toga je onda
// sitnica. Ovako prvo postoji alat koji radi.
#include "Core/LoomConfig.h"
#include "Core/LoomInitializer.h"

#include "LoomProgress.h"

#include <Treadle/Ui.h>
#include <TreadlePaint/UiPainter.h>

#include <GLFW/glfw3.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <fstream>
#include <cstdio>
#include <filesystem>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace{

enum class Screen{ Menu, PickVideo, Running, Result };

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
const Phase phases[] = {
    {"Snimka ",            "citanje snimke",             0.01},
    {"kljucnih kadrova",   "pracenje i kljucni kadrovi", 0.08},
    {"prostor mjerila:",   "znacajke i poklapanje",      0.29},
    {"graf poklapanja:",   "graf spojen",                0.33},
    {"samokalibracija:",   "zariste",                    0.37},
    {"nakon pune obrade",  "rekonstrukcija",             0.46},
    {"slika u ",           "zapis slika",                0.64},
    {"pune slicice:",      "pune slicice",               0.82},
    {"Zapisano u",         "gotovo",                     1.00},
};

//Sto se trenutno vrti. Oba posla su vanjski procesi koji ispisuju napredak, pa ih jedna te ista
//masinerija prati - razlikuju se samo po tome sto se u ispisu trazi
enum class Task{ Solve, Train };

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
void runSolve(Job& job, std::string command, std::string outputDirectory, Task task,
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
        }else{
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

std::vector<std::filesystem::path> videosIn(const std::filesystem::path& directory){
    std::vector<std::filesystem::path> found;
    std::error_code error;
    for(const auto& entry : std::filesystem::directory_iterator(directory, error)){
        if(error) break;
        if(!entry.is_regular_file()) continue;
        std::string extension = entry.path().extension().string();
        std::transform(extension.begin(), extension.end(), extension.begin(),
                       [](unsigned char c){ return char(std::tolower(c)); });
        if(extension == ".mp4" || extension == ".mov" || extension == ".mkv" || extension == ".avi"){
            found.push_back(entry.path());
        }
    }
    std::sort(found.begin(), found.end());
    return found;
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

const KnownFailure knownFailures[] = {
    {"Ninja is required",      "gsplatu treba `ninja` u PATH-u da prevede CUDA dio"},
    {"out of memory",          "kartica je puna - zatvori sto jos crta pa probaj ponovno"},
    {"No module named",        "u .venv fali paket; provjeri instalaciju"},
    {"Premalo kljucnih",       "snimka je prekratka ili se kamera ne mice dovoljno"},
    {"nema dovoljno",          "snimka nije dala dovoljno zajednickih tocaka"},
    {"No such file",           "putanja ne postoji - provjeri mapu snimke"},
    {"NEOPTIMIZIRAN BUILD",    "alat je preveden bez -O2; vidi naredbu u ispisu"},
};

std::string explainFailure(const std::vector<std::string>& lines){
    for(size_t i = lines.size(); i-- > 0; ){
        for(const KnownFailure& known : knownFailures){
            if(lines[i].find(known.signature) != std::string::npos) return known.meaning;
        }
    }
    return {};
}

std::string humanTime(double seconds){
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
void warnIfUnoptimised(){
#ifndef __OPTIMIZE__
    std::printf("\n  !! NEOPTIMIZIRAN BUILD - mjerenja ne vrijede, a alat je oko cetiri puta "
                "sporiji.\n     Popravak: cmake -S . -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo\n\n");
#endif
}
}

int main(int argc, char** argv){
    warnIfUnoptimised();
    LoomConfig config;
    config.width = 1180;
    config.height = 760;
    config.appName = "Loom";
    config.engineName = "Loom";
    config.enableDepth = false;
    LoomInitializer loom(config);

    GLFWwindow* window = loom.window->getWindow();
    Treadle::Ui ui;
    UiPainter painter(loom.device, loom.command, loom.getColorFormat(), vk::Format::eUndefined, 1u << 17);

    Screen screen = Screen::Menu;

    //Gdje se traze snimke. Zadano je mapa iz koje je alat pokrenut; strelicama se ide gore i u
    //podmape, jer okvira za upis teksta nemamo a ni ne treba nam
    std::filesystem::path browseAt = argc > 1 ? std::filesystem::path(argv[1])
                                              : std::filesystem::current_path();
    std::vector<std::filesystem::path> videos = videosIn(browseAt);
    std::filesystem::path chosenVideo;

    int stepIndex = 2;                 //svaki n-ti kadar
    const int steps[] = {1, 5, 10, 20};
    float frameCount = 231.0f;
    float trainSteps = 7000.0f;
    std::string splatPath;             //popunjava se kad trening zavrsi

    Job job;
    std::thread worker;

    //Zivi snimak: cita se povremeno, ne svaki kadar - datoteka moze imati milijune tocaka, a
    //solver je ionako pise rjedje nego sto se crta
    Loom::Snapshot snapshot;
    Loom::PreparedScene prepared;          //snimak spreman za crtanje - racuna se kad stigne novi
    Loom::ViewState view;
    Loom::PaintReport painted;
    bool wasRunning = false;
    bool longLog = false;
    auto lastRead = std::chrono::steady_clock::now();
    auto lastFrame = std::chrono::steady_clock::now();

    //Scena ima svoj slikar: oblak od sto tisuca tocaka ne stane u kapacitet suicelja, a slikar na
    //kapacitetu reze i javlja upozorenje
    UiPainter scenePainter(loom.device, loom.command, loom.getColorFormat(), vk::Format::eUndefined,
                           1u << 19);

    //Kotacic GLFW javlja dogadjajem, ne stanjem, pa se skuplja ovdje i nulira svaki kadar
    static float scrollAccumulated = 0.0f;
    glfwSetScrollCallback(window, [](GLFWwindow*, double, double y){ scrollAccumulated += float(y); });
    double dragX = 0.0, dragY = 0.0;
    bool dragging = false;


    while(!glfwWindowShouldClose(window)){
        glfwPollEvents();
        if(glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS && !job.running) break;

        int windowWidth = 0, windowHeight = 0;
        glfwGetWindowSize(window, &windowWidth, &windowHeight);
        double cursorX = 0.0, cursorY = 0.0;
        glfwGetCursorPos(window, &cursorX, &cursorY);

        Treadle::Input input;
        input.mouseX = float(cursorX);
        input.mouseY = float(cursorY);
        const bool down = glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS;
        input.down[uint32_t(Treadle::MouseButton::Left)] = down;

        //NOVI SNIMAK svakih pola sekunde dok posao traje - i jos JEDNOM kad zavrsi, jer tada
        //VideoSolve zapise konacni: uspravan i s bojama
        const auto now = std::chrono::steady_clock::now();
        const bool justFinished = wasRunning && !job.running;
        wasRunning = job.running;
        if((job.running && std::chrono::duration<double>(now - lastRead).count() > 0.5) || justFinished){
            lastRead = now;
            Loom::Snapshot fresh;
            if(Loom::readSnapshot(job.outputDirectory + "/napredak.bin", fresh)){
                snapshot = std::move(fresh);
                prepared = Loom::prepareScene(snapshot);
            }
        }

        //Sam se okrece dok ga nitko ne dira; cim ga netko pomakne, ostaje gdje ga je ostavio
        const float frameSeconds = std::min(0.1f, float(std::chrono::duration<double>(now - lastFrame).count()));
        lastFrame = now;
        if(view.autoRotate) view.yaw += frameSeconds * 0.18f;

        //Oblak se crta ISPOD suicelja, pa ide u svoj popis i prvi na red
        Treadle::DrawList scene;
        if(screen == Screen::Running && !prepared.points.empty()){
            painted = Loom::paintScene(prepared, scene, float(windowWidth), float(windowHeight), view);
        }

        ui.begin(input, float(windowWidth), float(windowHeight));

        if(screen == Screen::Menu){
            ui.panel("Loom", 40, 40, 380);
            ui.label("Rekonstrukcija kamere i scene iz snimke.");
            ui.separator();
            if(ui.button("Solve kamere")){
                videos = videosIn(browseAt);
                screen = Screen::PickVideo;
            }
            ui.label("");
            ui.label("Gaussian splat - trening trazi rijesenu");
            ui.label("snimku, pa ide poslije solvea.");
            ui.separator();
            ui.value("mapa", browseAt.filename().string());
            ui.end();

        }else if(screen == Screen::PickVideo){
            ui.panel("Odaberi snimku", 40, 40, 560);
            ui.value("mapa", browseAt.string().size() > 46
                             ? "..." + browseAt.string().substr(browseAt.string().size() - 43)
                             : browseAt.string());
            if(ui.button("^ mapa iznad")){
                browseAt = browseAt.parent_path();
                videos = videosIn(browseAt);
            }
            ui.separator();

            if(videos.empty()) ui.label("(nema snimki u ovoj mapi)");
            for(const std::filesystem::path& video : videos){
                if(ui.button(video.filename().string())) chosenVideo = video;
            }

            ui.separator();
            if(!chosenVideo.empty()){
                ui.value("odabrano", chosenVideo.filename().string());
                std::vector<std::string> stepLabels;
                for(int value : steps) stepLabels.push_back("svaki " + std::to_string(value) + ".");
                ui.choice("kadrovi", stepLabels, &stepIndex);
                ui.slider("najvise kadrova", &frameCount, 30.0f, 600.0f);

                if(ui.button("KRENI")){
                    const std::filesystem::path out = chosenVideo.parent_path() /
                        (chosenVideo.stem().string() + "_loom");
                    std::filesystem::create_directories(out);

                    char command[1024];
                    std::snprintf(command, sizeof(command), "./VideoSolve \"%s\" %d %d 0 \"%s\"",
                                  chosenVideo.string().c_str(), steps[stepIndex],
                                  int(frameCount), out.string().c_str());

                    if(worker.joinable()) worker.join();

                    //STARI SNIMAK SE BRISE prije novog posla. Bez toga bi se prvih sekundi crtala
                    //scena iz proslog prolaza iste snimke - uredna, uvjerljiva i kriva
                    snapshot = Loom::Snapshot{};
                    prepared = Loom::PreparedScene{};
                    view = Loom::ViewState{};
                    std::error_code ignored;
                    std::filesystem::remove(out / "napredak.bin", ignored);

                    worker = std::thread(runSolve, std::ref(job), std::string(command),
                                         out.string(), Task::Solve, 0);
                    screen = Screen::Running;
                }
            }else{
                ui.label("Klikni snimku da je odaberes.");
            }
            if(ui.button("natrag")) screen = Screen::Menu;
            ui.end();

        }else{
            const double elapsed = std::chrono::duration<double>(
                std::chrono::steady_clock::now() - job.started).count();
            const int phase = job.phase;

            const char* what = job.task == Task::Train ? "Trening" : "Solve";
            char title[64];
            std::snprintf(title, sizeof(title), "%s %s", what,
                          job.running ? "tece" : (job.failed ? "je pao" : "gotov"));
            ui.panel(title, 20, 20, 340);
            ui.value("snimka", chosenVideo.filename().string());
            ui.value("proteklo", humanTime(elapsed));

            if(phase >= 0 && phase < int(sizeof(phases) / sizeof(phases[0]))){
                ui.value("faza", phases[phase].label);

                //PROCJENA JE OZNACENA KAO PROCJENA. Udjeli su izmjereni na jednoj snimci (231
                //kadar, 4K) i druga snimka ih ne mora slijediti - zato "oko", ne broj s tocnoscu
                if(job.running && phases[phase].share > 0.02 && phases[phase].share < 1.0){
                    const double total = elapsed / phases[phase].share;
                    ui.value("jos oko", humanTime(std::max(0.0, total - elapsed)));
                }
            }
            if(!snapshot.points.empty()){
                char text[64];
                std::snprintf(text, sizeof(text), "%zu kamera, %zu tocaka",
                              snapshot.cameras.size(), snapshot.points.size());
                ui.value("scena", text);
                if(prepared.upright){
                    std::snprintf(text, sizeof(text), "da (bila nagnuta %.1f st)", prepared.tiltDegrees);
                    ui.value("uspravno", text);
                }else if(!snapshot.orientations.empty()){
                    ui.value("uspravno", "ne - kamere se ne slazu");
                }
                if(snapshot.colours.size() == snapshot.points.size()) ui.value("boje", "da");
            }
            ui.separator();

            if(job.running && job.task == Task::Train && job.fraction >= 0.0f){
                char text[32];
                std::snprintf(text, sizeof(text), "%.0f %%", 100.0f * job.fraction);
                ui.value("napredak", text);
                if(job.fraction > 0.02f){
                    ui.value("jos oko", humanTime(elapsed * (1.0 / job.fraction - 1.0)));
                }
            }

            if(!job.running){
                if(!job.failed && job.task == Task::Solve){
                    ui.label("Zapisano:");
                    ui.label(job.outputDirectory.size() > 40
                             ? "..." + job.outputDirectory.substr(job.outputDirectory.size() - 37)
                             : job.outputDirectory);
                    ui.label("kamera.usda - za Nuke/Houdini/Blender");
                    ui.separator();

                    //DRUGI KORAK LANCA. Solve daje poze i tocke; splat od toga radi scenu. Trener
                    //trazi slike iz solvea, pa ide bas ta mapa i nijedna druga
                    ui.slider("koraka treninga", &trainSteps, 1000.0f, 30000.0f);
                    if(ui.button("TRENIRAJ SPLAT")){
                        splatPath = job.outputDirectory + "/scena.ply";
                        char command[1400];
                        std::snprintf(command, sizeof(command),
                                      //VENV SE AKTIVIRA, ne zaobilazi. Pozvati ./.venv/bin/python
                                      //izravno pokrene pravi interpreter, ali NE stavi .venv/bin u
                                      //PATH - a torch trazi `ninja` bas ondje da bi preveo gsplatovu
                                      //CUDA ekstenziju. Bez toga trening padne nakon pedesetak
                                      //sekundi uz "Ninja is required to load C++ extensions", iako
                                      //je ninja uredno instaliran u venvu
                                      "cd \"%s\" && PATH=\"%s/.venv/bin:$PATH\" "
                                      "./.venv/bin/python tools/splat/train_splats.py "
                                      "\"%s\" \"%s/images\" \"%s\" --steps %d",
                                      LOOM_ROOT_DIR, LOOM_ROOT_DIR, job.outputDirectory.c_str(),
                                      job.outputDirectory.c_str(), splatPath.c_str(), int(trainSteps));
                        if(worker.joinable()) worker.join();
                        worker = std::thread(runSolve, std::ref(job), std::string(command),
                                             job.outputDirectory, Task::Train, int(trainSteps));
                    }
                }else if(!job.failed && job.task == Task::Train){
                    ui.label("Scena je gotova:");
                    ui.label(splatPath.size() > 40
                             ? "..." + splatPath.substr(splatPath.size() - 37) : splatPath);
                    ui.separator();
                    if(ui.button("POGLEDAJ SCENU")){
                        //Preglednik je zaseban prozor i zivi svojim zivotom - ovaj ga samo
                        //pokrene i ne ceka ga
                        char command[1400];
                        std::snprintf(command, sizeof(command),
                                      "./SplatViewer \"%s\" 1 16 0 0 pogled.png 3 0 \"%s\" &",
                                      splatPath.c_str(), job.outputDirectory.c_str());
                        if(std::system(command) != 0){ /* preglednik javlja sam */ }
                    }
                }else{
                    //Recenica kad je uzrok prepoznat; inace stara uputa, koja je i dalje tocna
                    std::string why;
                    {
                        std::lock_guard<std::mutex> guard(job.lock);
                        why = explainFailure(job.lines);
                    }
                    if(!why.empty()){
                        ui.label("Pao je:");
                        ui.label(why);
                    }else{
                        ui.label("Pao je. Zadnji redci desno kazu zasto.");
                    }
                }
                ui.separator();
                if(ui.button("natrag na izbornik")) screen = Screen::Menu;
            }
            ui.end();

            //ISPIS SOLVERA, onakav kakav jest - ali DOLJE i kratak. Prije je stajao preko sredine
            //prozora i pokrivao upravo ono sto se htjelo gledati: oblak se vidio samo po rubovima
            {
                const size_t show = longLog ? 22 : 7;
                const float logHeight = 50.0f + float(show) * 20.0f;
                const float logWidth = float(std::max(400, windowWidth - 40));
                const size_t fits = size_t(std::max(20.0f, (logWidth - 20.0f) / 12.0f));
                ui.panel("Sto alat govori", 20, float(windowHeight) - logHeight - 20.0f, logWidth);
                std::lock_guard<std::mutex> guard(job.lock);
                const size_t from = job.lines.size() > show ? job.lines.size() - show : 0;
                for(size_t i = from; i < job.lines.size(); ++i){
                    std::string line = job.lines[i];
                    if(line.size() > fits) line = line.substr(0, fits);
                    ui.label(line);
                }
                ui.end();
            }

            //POGLED: sto se crta. Svaki sloj se da ugasiti, jer gust oblak zna sakriti putanju
            if(!prepared.points.empty()){
                ui.panel("Pogled", float(windowWidth) - 250.0f, 20, 230);
                ui.checkbox("mreza na podu", &view.showGrid);
                ui.checkbox("osi", &view.showAxes);
                ui.checkbox("putanja kamere", &view.showPath);
                ui.checkbox("smjer kamera", &view.showDirections);
                ui.checkbox("sam se okrece", &view.autoRotate);
                ui.checkbox("cijeli ispis", &longLog);
                if(ui.button("vrati pogled")){
                    view.yaw = 0.6f; view.pitch = 0.45f; view.zoom = 1.0f;
                }
                ui.label("lijevi mis: okretanje");
                ui.label("kotacic: priblizavanje");
                ui.end();
            }
        }

        //MIS POMICE POGLED TEK KAD GA SUICELJE NIJE UZELO - pravilo iz Treadle/Ui.h: odgovor na
        //to pitanje postoji tek kad su svi widgeti ovog kadra vidjeli mis
        if(screen == Screen::Running && !prepared.points.empty()){
            if(down && !dragging && !ui.wantsMouse()){
                dragging = true; dragX = cursorX; dragY = cursorY;
            }
            if(!down) dragging = false;
            if(dragging){
                view.yaw -= float(cursorX - dragX) * 0.008f;
                view.pitch = std::clamp(view.pitch + float(cursorY - dragY) * 0.008f, -1.45f, 1.45f);
                if(cursorX != dragX || cursorY != dragY) view.autoRotate = false;
                dragX = cursorX; dragY = cursorY;
            }
            if(scrollAccumulated != 0.0f && !ui.wantsMouse()){
                view.zoom = std::clamp(view.zoom * std::pow(0.88f, scrollAccumulated), 0.08f, 8.0f);
            }
        }
        scrollAccumulated = 0.0f;

        if(!loom.renderer.beginFrame()) continue;
        loom.renderer.beginPass();
        if(!scene.vertices.empty()){
            scenePainter.draw(loom.renderer, scene, uint32_t(windowWidth), uint32_t(windowHeight));
        }
        painter.draw(loom.renderer, ui.drawn(), uint32_t(windowWidth), uint32_t(windowHeight));
        loom.renderer.endPass();
        loom.renderer.endFrame();
    }

    if(worker.joinable()) worker.join();
    return 0;
}
