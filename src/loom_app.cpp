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

//Udjeli su iz stvarnog mjerenja (231 kadar, 4K, 8166 s ukupno): pracenje 386 s, graf 3942 s od
//cega poklapanje 2585, rekonstrukcija 694 s, pune slicice ~260 s. Ostatak je dekodiranje i zapis
const Phase phases[] = {
    {"Snimka ",            "citanje snimke",        0.03},
    {"kljucnih kadrova",   "pracenje i kljucni kadrovi", 0.05},
    {"prostor mjerila:",   "znacajke i poklapanje", 0.30},
    {"graf poklapanja:",   "graf poklapanja",       0.20},
    {"samokalibracija:",   "zariste",               0.10},
    {"Najbolje:",          "rekonstrukcija",        0.20},
    {"pune slicice:",      "pune slicice",          0.08},
    {"Zapisano u",         "gotovo",                1.00},
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

std::string humanTime(double seconds){
    char text[64];
    if(seconds < 90.0){ std::snprintf(text, sizeof(text), "%.0f s", seconds); return text; }
    if(seconds < 5400.0){ std::snprintf(text, sizeof(text), "%.0f min", seconds / 60.0); return text; }
    std::snprintf(text, sizeof(text), "%.1f h", seconds / 3600.0);
    return text;
}

}

int main(int argc, char** argv){
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
    uint32_t snapshotPoints = 0;
    auto lastRead = std::chrono::steady_clock::now();
    const auto appStarted = std::chrono::steady_clock::now();

    bool wasDown = false;
    double lastScroll = 0.0;
    (void)lastScroll;

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
        wasDown = down;

        //Novi snimak svakih pola sekunde dok posao traje
        const auto now = std::chrono::steady_clock::now();
        if(job.running && std::chrono::duration<double>(now - lastRead).count() > 0.5){
            lastRead = now;
            Loom::Snapshot fresh;
            if(Loom::readSnapshot(job.outputDirectory + "/napredak.bin", fresh)) snapshot = std::move(fresh);
        }

        //Oblak se crta ISPOD suicelja, pa ide u svoj popis i prvi na red
        Treadle::DrawList scene;
        if(screen == Screen::Running && !snapshot.points.empty()){
            const float angle = float(std::chrono::duration<double>(now - appStarted).count()) * 0.18f;
            snapshotPoints = Loom::paintSnapshot(snapshot, scene, float(windowWidth), float(windowHeight), angle);
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
            ui.panel(title, 40, 40, 380);
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
                char text[48];
                std::snprintf(text, sizeof(text), "%zu kamera, %zu tocaka",
                              snapshot.cameras.size(), snapshot.points.size());
                ui.value("scena", text);
                if(snapshotPoints > 0){
                    std::snprintf(text, sizeof(text), "%u u kadru", snapshotPoints);
                    ui.value("nacrtano", text);
                }
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
                                      "cd \"%s\" && ./.venv/bin/python tools/splat/train_splats.py "
                                      "\"%s\" \"%s/images\" \"%s\" --steps %d",
                                      LOOM_ROOT_DIR, job.outputDirectory.c_str(),
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
                    ui.label("Pao je. Zadnji redci desno kazu zasto.");
                }
                ui.separator();
                if(ui.button("natrag na izbornik")) screen = Screen::Menu;
            }
            ui.end();

            //ISPIS SOLVERA, onakav kakav jest. Ne prepricava se nego se pokazuje - jer je svaki
            //broj u njemu nastao uz neko mjerenje i znaci nesto
            ui.panel("Sto alat govori", 450, 40, float(std::max(320, windowWidth - 490)));
            {
                std::lock_guard<std::mutex> guard(job.lock);
                const size_t show = 26;
                const size_t from = job.lines.size() > show ? job.lines.size() - show : 0;
                for(size_t i = from; i < job.lines.size(); ++i){
                    std::string line = job.lines[i];
                    if(line.size() > 96) line = line.substr(0, 96);
                    ui.label(line);
                }
            }
            ui.end();
        }

        if(!loom.renderer.beginFrame()) continue;
        loom.renderer.beginPass();
        if(!scene.vertices.empty()){
            painter.draw(loom.renderer, scene, uint32_t(windowWidth), uint32_t(windowHeight));
        }
        painter.draw(loom.renderer, ui.drawn(), uint32_t(windowWidth), uint32_t(windowHeight));
        loom.renderer.endPass();
        loom.renderer.endFrame();
    }

    if(worker.joinable()) worker.join();
    return 0;
}
