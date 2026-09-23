// Loom kao editor: `loom` u terminalu otvori prozor u kojem se snimka pretvara u scenu.
//
//   media (lijevo)        snimke projekta i datoteke na disku; desni klik na snimku solva
//   pogled (sredina)      scena: oblak, kamere, kocke - slobodno ili kroz rijesenu kameru
//   hijerarhija (desno)   stablo scene (Warp), ispod njega svojstva odabranog
//   timeline (dolje)      kadar snimke; rijesena kamera se po njemu krece
//
// ZASTO EDITOR. Solve je dosad zavrsavao datotekom, a je li dobar vidjelo se tek u Nukeu ili
// Blenderu. Matchmove se provjerava tako da se kocka postavi u scenu i gleda kroz rijesenu kameru
// preko cijelog kadra - to je sada ovdje, jedan desni klik od snimke.
//
// Solve i trening se i dalje pokrecu kao zasebni procesi (vidi LoomJob.h); editor cita njihov
// ispis i zivi snimak, a kad zavrse, rezultat ulazi u scenu sam.
//
// SNIMKA PROZORA IZ SAMOG EDITORA. Prozor se na ovom sustavu ne da snimiti izvana, pa editor zna
// sam odraditi ono sto bi korisnik kliknuo i spremiti kadar:
//
//   loom <mapa> --snimi slika.png --rezultat C0256_loom [--kadar 120] [--kroz] [--kocka]
//
// uveze rezultat, po zelji doda kocku i gleda kroz rijesenu kameru, pa spremi kadar i izadje.
// Tako se editor provjerava okom, a ne samo testom racuna
#include "Core/LoomConfig.h"
#include "Core/LoomInitializer.h"

#include "LoomEditor.h"
#include "LoomJob.h"
#include "LoomScene.h"
#include "LoomViewport.h"

#include "Vulkan/ImageData.h"

#include <Spool/ImageFile.h>
#include <Spool/VideoFile.h>
#include <Treadle/Ui.h>
#include <TreadlePaint/UiPainter.h>

#include <GLFW/glfw3.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <set>
#include <string>
#include <thread>
#include <vector>

namespace{

namespace fs = std::filesystem;

//Sto je u mapi koju media prozor pregledava. Osvjezava se kad se mapa promijeni i povremeno,
//jer solve u pozadini stvara nove mape rezultata
struct Browser{
    fs::path at;
    std::vector<fs::path> folders, videos, results;

    void refresh(){
        folders.clear();
        results = Loom::resultsIn(at);
        videos = Loom::videosIn(at);
        std::error_code error;
        for(const auto& entry : fs::directory_iterator(at, error)){
            if(error) break;
            const std::string name = entry.path().filename().string();
            if(!entry.is_directory(error) || name.empty() || name[0] == '.') continue;
            if(Loom::isResultDirectory(entry.path())) continue;
            folders.push_back(entry.path());
        }
        std::sort(folders.begin(), folders.end());
    }
};

//Tipka kao dogadjaj: true samo u kadru u kojem je pritisnuta
struct Keys{
    bool was[GLFW_KEY_LAST + 1] = {false};
    bool pressed(GLFWwindow* window, int key){
        const bool down = glfwGetKey(window, key) == GLFW_PRESS;
        const bool result = down && !was[key];
        was[key] = down;
        return result;
    }
};

//Podaci o snimci za projekt. Snimka koja se ne da otvoriti ulazi svejedno, s nulama - solve ce
//reci zasto, glasnije nego sto bi to ovdje moglo
Warp::Media probeMedia(const fs::path& path){
    Warp::Media media;
    media.path = path.string();
    try{
        Spool::VideoReader reader(path.string());
        const Spool::VideoInfo& info = reader.info();
        media.width = info.width;
        media.height = info.height;
        media.frames = uint32_t(std::max<int64_t>(0, info.frameCount));
        media.framesPerSecond = info.frameRate() > 0.0 ? info.frameRate() : 25.0;
    }catch(const std::exception&){}
    const fs::path result = Loom::resultFolderFor(path);
    if(Loom::isResultDirectory(result)) media.result = result.string();
    return media;
}

std::string tail(const std::string& text, size_t length){
    return text.size() > length ? ".." + text.substr(text.size() - length + 2) : text;
}

std::string number(float value){
    char text[32];
    std::snprintf(text, sizeof(text), std::fabs(value) < 100.0f ? "%.3f" : "%.1f", double(value));
    return text;
}

std::string vectorText(const glm::vec3& v){
    return number(v.x) + " " + number(v.y) + " " + number(v.z);
}

//Sto entitet jest, za svojstva
std::string kindOf(const Warp::Entity& entity){
    if(entity.camera) return "kamera";
    if(entity.points) return "oblak tocaka";
    if(entity.mesh) return entity.mesh->shape == Warp::Shape::Cube ? "kocka" : "ravnina";
    if(entity.splat) return "gaussian splat";
    return entity.children.empty() ? "nul" : "grupa";
}

enum class Focus{ Entity, Media };
enum class After{ Nothing, Import, ImportAndTrain, AddSplat };

}

int main(int argc, char** argv){
    Loom::warnIfUnoptimised();
    LoomConfig config;
    config.width = 1600;
    config.height = 940;
    config.appName = "Loom";
    config.engineName = "Loom";
    config.enableDepth = false;
    LoomInitializer loom(config);

    GLFWwindow* window = loom.window->getWindow();
    Treadle::Ui ui;
    UiPainter painter(loom.device, loom.command, loom.getColorFormat(), vk::Format::eUndefined, 1u << 18);

    //Scena ima svoj slikar: oblak od sto tisuca tocaka ne stane u kapacitet suicelja
    UiPainter scenePainter(loom.device, loom.command, loom.getColorFormat(), vk::Format::eUndefined, 1u << 20);

    //Argumenti: prva mapa, pa zastavice za snimku (vidi zaglavlje)
    fs::path startAt = fs::current_path();
    std::string shotPath, shotResult;
    double shotFrame = -1.0;
    bool shotThrough = false, shotCube = false;
    for(int i = 1; i < argc; ++i){
        const std::string argument = argv[i];
        if(argument == "--snimi" && i + 1 < argc) shotPath = argv[++i];
        else if(argument == "--rezultat" && i + 1 < argc) shotResult = argv[++i];
        else if(argument == "--kadar" && i + 1 < argc) shotFrame = std::atof(argv[++i]);
        else if(argument == "--kroz") shotThrough = true;
        else if(argument == "--kocka") shotCube = true;
        else if(argument.rfind("--", 0) != 0) startAt = argument;
    }

    Browser browser;
    browser.at = startAt;
    browser.refresh();
    auto lastBrowse = std::chrono::steady_clock::now();

    //-- scena i odabir -------------------------------------------------------------------------
    Warp::Stage stage;
    Warp::Id selected = Warp::None;
    std::set<Warp::Id> collapsed;
    int selectedMedia = -1;
    Focus focus = Focus::Entity;
    Warp::Id menuEntity = Warp::None;
    int menuMedia = -1;

    double frame = 1.0;
    bool playing = false;

    Loom::ViewportState view;
    Loom::SceneExtent extent;
    bool extentDirty = true;

    float mediaScroll = 0.0f, hierarchyScroll = 0.0f, propertiesScroll = 0.0f;
    std::string message;                  //zadnja poruka korisniku, u alatnoj traci

    //-- poslovi ----------------------------------------------------------------------------------
    Loom::Job job;
    std::thread worker;
    const int steps[] = {1, 5, 10, 20};
    int stepIndex = 2;
    float frameCount = 231.0f;
    float trainSteps = 7000.0f;
    After afterJob = After::Nothing;
    std::string jobVideo;                 //snimka koja se solva, za kameru u sceni
    bool wasRunning = false;
    bool showLog = false;

    //Zivi snimak dok solve tece: zasebna scena, da se ne mijesa s onim sto je umjetnik slozio
    Warp::Stage live;
    auto lastRead = std::chrono::steady_clock::now();
    auto lastFrame = std::chrono::steady_clock::now();

    static float scrollAccumulated = 0.0f;
    glfwSetScrollCallback(window, [](GLFWwindow*, double, double y){ scrollAccumulated += float(y); });

    Keys keys;
    bool leftWasDown = false, middleWasDown = false, rightWasDown = false;
    bool leftInViewport = false, dragging = false, middleDragging = false;
    double pressX = 0.0, pressY = 0.0, lastX = 0.0, lastY = 0.0;

    auto startJob = [&](const std::string& command, const std::string& output, Loom::Task task, int total){
        if(worker.joinable()) worker.join();
        worker = std::thread(Loom::runSolve, std::ref(job), command, output, task, total);
    };

    auto startSolve = [&](int mediaIndex, bool thenSplat){
        if(job.running || mediaIndex < 0 || mediaIndex >= int(stage.media.size())) return;
        const fs::path video = stage.media[size_t(mediaIndex)].path;
        const fs::path out = Loom::resultFolderFor(video);
        std::error_code ignored;
        fs::create_directories(out, ignored);
        //STARI SNIMAK SE BRISE prije novog posla. Bez toga bi se prvih sekundi crtala scena iz
        //proslog prolaza iste snimke - uredna, uvjerljiva i kriva
        fs::remove(out / "napredak.bin", ignored);
        char command[1400];
        std::snprintf(command, sizeof(command), "./VideoSolve \"%s\" %d %d 0 \"%s\"",
                      video.string().c_str(), steps[stepIndex], int(frameCount), out.string().c_str());
        jobVideo = video.string();
        afterJob = thenSplat ? After::ImportAndTrain : After::Import;
        live = Warp::Stage{};
        startJob(command, out.string(), Loom::Task::Solve, 0);
        message = "solve krenuo: " + video.filename().string();
    };

    auto startTrain = [&](const std::string& directory){
        if(job.running) return;
        const std::string splat = directory + "/scena.ply";
        char command[1600];
        std::snprintf(command, sizeof(command),
                      //VENV SE AKTIVIRA, ne zaobilazi: torch trazi `ninja` u PATH-u da prevede
                      //gsplatovu CUDA ekstenziju, a izravno pozvan python ne stavi .venv/bin u PATH
                      "cd \"%s\" && PATH=\"%s/.venv/bin:$PATH\" "
                      "./.venv/bin/python tools/splat/train_splats.py "
                      "\"%s\" \"%s/images\" \"%s\" --steps %d",
                      LOOM_ROOT_DIR, LOOM_ROOT_DIR, directory.c_str(), directory.c_str(), splat.c_str(),
                      int(trainSteps));
        afterJob = After::AddSplat;
        startJob(command, directory, Loom::Task::Train, int(trainSteps));
        message = "trening krenuo";
    };

    auto importFolder = [&](const fs::path& directory, const std::string& plate){
        const Loom::ImportReport report = Loom::importResult(stage, directory, plate);
        if(!report.problem.empty()){ message = report.problem; return; }
        selected = report.camera;
        focus = Focus::Entity;
        frame = stage.startFrame;
        extent = Loom::sceneExtent(stage, frame);
        extentDirty = false;
        view.lookThrough = Warp::None;
        Loom::frameAll(stage, frame, view.orbit);
        char text[160];
        std::snprintf(text, sizeof(text), "uvezeno %s: %zu kljuceva kamere%s", directory.filename().string().c_str(),
                      report.cameraKeys, report.upright ? ", uspravno" : "");
        message = text;
    };

    //Novo tijelo: velicina iz scene (solve nema metre), postavljeno na pod usred scene
    auto addMesh = [&](Warp::Shape shape, Warp::Id parent){
        const char* name = shape == Warp::Shape::Cube ? "Kocka" : "Ravnina";
        const Warp::Id id = stage.create(name, parent);
        Warp::Entity& entity = *stage.get(id);
        entity.mesh = Warp::Mesh{shape};
        const float size = extent.radius * (shape == Warp::Shape::Cube ? 0.2f : 0.8f);
        const glm::vec3 worldPosition(extent.centre.x, shape == Warp::Shape::Cube ? size * 0.5f : 0.0f, extent.centre.z);
        const glm::mat4 parentWorld = parent == Warp::None ? glm::mat4(1.0f) : stage.worldMatrix(parent, frame);
        entity.local.translation = glm::vec3(glm::inverse(parentWorld) * glm::vec4(worldPosition, 1.0f));
        entity.local.scale = glm::vec3(size);
        selected = id;
        focus = Focus::Entity;
    };

    auto firstCamera = [&](){
        Warp::Id found = Warp::None;
        stage.walk([&](const Warp::Entity& e, int){ if(found == Warp::None && e.camera) found = e.id; });
        return found;
    };

    auto removeSelected = [&](Warp::Id id){
        stage.remove(id);
        if(view.lookThrough != Warp::None && !stage.contains(view.lookThrough)) view.lookThrough = Warp::None;
        selected = Warp::None;
        extentDirty = true;
    };

    uint32_t framesDrawn = 0;
    if(!shotResult.empty()){
        importFolder(shotResult, "");
        if(shotFrame >= 0.0) frame = shotFrame;
        if(shotCube) addMesh(Warp::Shape::Cube, Warp::None);
        if(shotThrough) view.lookThrough = firstCamera();
    }

    while(!glfwWindowShouldClose(window)){
        glfwPollEvents();

        int windowWidth = 0, windowHeight = 0;
        glfwGetWindowSize(window, &windowWidth, &windowHeight);
        double cursorX = 0.0, cursorY = 0.0;
        glfwGetCursorPos(window, &cursorX, &cursorY);
        const bool shift = glfwGetKey(window, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS ||
                           glfwGetKey(window, GLFW_KEY_RIGHT_SHIFT) == GLFW_PRESS;

        Treadle::Input input;
        input.mouseX = float(cursorX);
        input.mouseY = float(cursorY);
        const bool leftDown = glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS;
        const bool rightDown = glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS;
        const bool middleDown = glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_MIDDLE) == GLFW_PRESS;
        input.down[uint32_t(Treadle::MouseButton::Left)] = leftDown;
        input.down[uint32_t(Treadle::MouseButton::Right)] = rightDown;
        input.down[uint32_t(Treadle::MouseButton::Middle)] = middleDown;
        input.wheel = scrollAccumulated;
        input.shift = shift;

        const Loom::EditorLayout layout = Loom::layoutEditor(float(windowWidth), float(windowHeight));

        const auto now = std::chrono::steady_clock::now();
        const float frameSeconds = std::min(0.1f, float(std::chrono::duration<double>(now - lastFrame).count()));
        lastFrame = now;

        if(playing){
            frame += double(frameSeconds) * stage.framesPerSecond;
            if(frame > stage.endFrame) frame = stage.startFrame;
        }

        //-- posao: zivi snimak, i sto kad zavrsi ------------------------------------------------
        const bool justFinished = wasRunning && !job.running;
        wasRunning = job.running;
        if(job.running && job.task == Loom::Task::Solve &&
           std::chrono::duration<double>(now - lastRead).count() > 0.7){
            lastRead = now;
            Loom::Snapshot snapshot;
            if(Loom::readSnapshot(job.outputDirectory + "/napredak.bin", snapshot) && !snapshot.points.empty()){
                live = Warp::Stage{};
                Loom::addSnapshot(live, snapshot, "solve_u_tijeku");
                if(stage.size() == 0){
                    //Prazna scena: pogled prati ono sto solve nalazi
                    Loom::frameAll(live, 1.0, view.orbit);
                    extent = Loom::sceneExtent(live, 1.0);
                }
            }
        }
        if(justFinished){
            live = Warp::Stage{};
            if(job.failed){
                std::lock_guard<std::mutex> guard(job.lock);
                const std::string why = Loom::explainFailure(job.lines);
                message = why.empty() ? "posao je pao - vidi ispis" : "pao: " + why;
                showLog = true;
            }else if(job.task == Loom::Task::Solve){
                importFolder(job.outputDirectory, jobVideo);
                for(Warp::Media& media : stage.media){
                    if(media.path == jobVideo) media.result = job.outputDirectory;
                }
                if(afterJob == After::ImportAndTrain) startTrain(job.outputDirectory);
            }else if(job.task == Loom::Task::Train){
                //Splat ide u grupu svoje mape, ako je u sceni
                std::string name = fs::path(job.outputDirectory).filename().string();
                if(name.size() > 5 && name.substr(name.size() - 5) == "_loom") name.resize(name.size() - 5);
                const Warp::Id group = stage.find("/" + name);
                const Warp::Id id = stage.create("Splat", group);
                stage.get(id)->splat = Warp::Splat{job.outputDirectory + "/scena.ply"};
                message = "splat gotov: " + job.outputDirectory + "/scena.ply";
            }
            browser.refresh();
        }
        if(std::chrono::duration<double>(now - lastBrowse).count() > 3.0){
            lastBrowse = now;
            browser.refresh();
        }

        if(extentDirty){
            extent = Loom::sceneExtent(stage, frame);
            extentDirty = false;
        }

        ui.begin(input, float(windowWidth), float(windowHeight));
        const Treadle::Theme& theme = ui.style();

        //Gumb za alatnu traku i timeline: vodoravno, na zadanom mjestu. Treadleovi gumbi idu
        //u stupac, a traka je red. Vraca je li kliknut i gdje pocinje sljedeci
        auto toolButton = [&](const std::string& label, float x, float y, float height, bool on = false){
            const float width = Treadle::textWidth(label, theme.textScale) + 2.0f * theme.padding;
            const Treadle::Ui::Region region = ui.region("gumb:" + label, Treadle::Rect{x, y, width, height});
            ui.canvas().rect(region.box, on ? theme.accent : (region.hot ? theme.hot : theme.control));
            ui.canvas().text(x + theme.padding, y + (height - Treadle::textHeight(theme.textScale)) * 0.5f,
                             label, theme.text, theme.textScale);
            return std::make_pair(region.pressed, x + width + 6.0f);
        };

        //== ALATNA TRAKA =========================================================================
        {
            const Treadle::Rect& bar = layout.toolbar;
            ui.canvas().rect(bar, Treadle::Color{0.07f, 0.08f, 0.09f, 1.0f});
            ui.canvas().text(bar.x + 12.0f, bar.y + 13.0f, "LOOM", theme.title, theme.textScale);
            const float y = bar.y + 7.0f, h = bar.height - 14.0f;
            auto [cube, afterCube] = toolButton("+ Kocka", bar.x + 90.0f, y, h);
            if(cube) addMesh(Warp::Shape::Cube, Warp::None);
            auto [plane, afterPlane] = toolButton("+ Ravnina", afterCube, y, h);
            if(plane) addMesh(Warp::Shape::Plane, Warp::None);
            auto [nul, afterNul] = toolButton("+ Nul", afterPlane, y, h);
            if(nul){ selected = stage.create("Nul"); focus = Focus::Entity; }
            auto [fit, afterFit] = toolButton("Uokviri (F)", afterNul + 12.0f, y, h);
            if(fit){ view.lookThrough = Warp::None; Loom::frameAll(stage.size() ? stage : live, frame, view.orbit); }
            auto [through, afterThrough] = toolButton("Kroz kameru (0)", afterFit, y, h, view.lookThrough != Warp::None);
            if(through){
                if(view.lookThrough != Warp::None) view.lookThrough = Warp::None;
                else{
                    const Warp::Entity* chosen = stage.get(selected);
                    view.lookThrough = chosen && chosen->camera ? selected : firstCamera();
                }
            }
            auto [logButton, afterLog] = toolButton("Ispis", afterThrough, y, h, showLog);
            if(logButton) showLog = !showLog;

            //Stanje posla ili zadnja poruka, desno
            std::string status = message;
            Treadle::Color colour = theme.dim;
            if(job.running){
                const double elapsed = std::chrono::duration<double>(now - job.started).count();
                const int phase = job.phase;
                const int phaseCount = int(sizeof(Loom::phases) / sizeof(Loom::phases[0]));
                if(job.task == Loom::Task::Train){
                    char text[96];
                    std::snprintf(text, sizeof(text), "trening %.0f %% - %s",
                                  job.fraction >= 0.0f ? 100.0 * double(job.fraction) : 0.0,
                                  Loom::humanTime(elapsed).c_str());
                    status = text;
                }else{
                    status = "solve: ";
                    status += (phase >= 0 && phase < phaseCount) ? Loom::phases[phase].label : "pocinje";
                    status += " - " + Loom::humanTime(elapsed);
                    //PROCJENA JE OZNACENA KAO PROCJENA: udjeli su izmjereni na jednoj snimci
                    if(phase >= 0 && phase < phaseCount && Loom::phases[phase].share > 0.02 &&
                       Loom::phases[phase].share < 1.0){
                        status += ", jos oko " + Loom::humanTime(std::max(0.0, elapsed / Loom::phases[phase].share - elapsed));
                    }
                }
                colour = theme.accent;
            }else if(job.failed){
                colour = theme.warning;
            }
            const float room = bar.width - afterLog - 20.0f;
            const std::string fitted = Treadle::fitText(status, room, theme.textScale);
            ui.canvas().text(bar.x + bar.width - Treadle::textWidth(fitted, theme.textScale) - 14.0f, bar.y + 13.0f,
                             fitted, colour, theme.textScale);
        }

        //== MEDIA ================================================================================
        ui.dock("Media", layout.media, &mediaScroll);
        {
            ui.label("PROJEKT");
            if(stage.media.empty()) ui.label("(dodaj snimku ispod)");
            for(size_t i = 0; i < stage.media.size(); ++i){
                const Warp::Media& media = stage.media[i];
                std::string label = fs::path(media.path).filename().string();
                if(job.running && media.path == jobVideo) label = "* " + label;
                else if(!media.result.empty()) label += " [rijeseno]";
                if(ui.selectable(label, focus == Focus::Media && selectedMedia == int(i))){
                    selectedMedia = int(i);
                    focus = Focus::Media;
                }
                if(ui.rightClicked()){
                    selectedMedia = int(i);
                    focus = Focus::Media;
                    menuMedia = int(i);
                    ui.openMenu("media");
                }
            }
            ui.separator();
            ui.label("DATOTEKE");
            ui.label(tail(browser.at.string(), size_t(std::max(8.0f, (layout.media.width - 30.0f) / 12.0f))));
            if(ui.selectable("^ mapa iznad", false)){
                browser.at = browser.at.parent_path();
                browser.refresh();
                mediaScroll = 0.0f;
            }
            for(const fs::path& folder : browser.folders){
                if(ui.selectable("> " + folder.filename().string(), false)){
                    browser.at = folder;
                    browser.refresh();
                    mediaScroll = 0.0f;
                }
            }
            for(const fs::path& video : browser.videos){
                if(ui.selectable("+ " + video.filename().string(), false)){
                    bool known = false;
                    for(size_t i = 0; i < stage.media.size(); ++i){
                        if(stage.media[i].path == video.string()){ known = true; selectedMedia = int(i); }
                    }
                    if(!known){
                        stage.media.push_back(probeMedia(video));
                        selectedMedia = int(stage.media.size()) - 1;
                    }
                    focus = Focus::Media;
                }
            }
            for(const fs::path& result : browser.results){
                if(ui.selectable("[rezultat] " + result.filename().string(), false)){
                    //Snimka uz rezultat, ako postoji, postaje ploca kamere
                    std::string plate;
                    std::string stem = result.filename().string();
                    if(stem.size() > 5) stem.resize(stem.size() - 5);
                    for(const fs::path& video : browser.videos){
                        if(video.stem().string() == stem) plate = video.string();
                    }
                    importFolder(result, plate);
                }
            }
        }

        //== HIJERARHIJA ==========================================================================
        ui.dock("Scena", layout.hierarchy, &hierarchyScroll);
        {
            if(stage.size() == 0) ui.label("(prazna - uvezi rezultat)");
            int hiddenBelow = -1;               //dubina zatvorenog pretka; dublji se preskacu
            stage.walk([&](const Warp::Entity& entity, int depth){
                if(hiddenBelow >= 0 && depth > hiddenBelow) return;
                hiddenBelow = -1;
                const bool expanded = collapsed.count(entity.id) == 0;
                if(!expanded) hiddenBelow = depth;
                const Treadle::Ui::TreeClick click = ui.treeRow(entity.name, depth, !entity.children.empty(), expanded,
                                                                focus == Focus::Entity && selected == entity.id);
                if(click == Treadle::Ui::TreeClick::Toggle){
                    if(expanded) collapsed.insert(entity.id); else collapsed.erase(entity.id);
                }else if(click == Treadle::Ui::TreeClick::Select){
                    selected = entity.id;
                    focus = Focus::Entity;
                }
                if(ui.rightClicked()){
                    selected = entity.id;
                    focus = Focus::Entity;
                    menuEntity = entity.id;
                    ui.openMenu("entitet");
                }
            });
        }

        //== SVOJSTVA =============================================================================
        ui.dock("Svojstva", layout.properties, &propertiesScroll);
        {
            Warp::Entity* entity = focus == Focus::Entity ? stage.get(selected) : nullptr;
            if(focus == Focus::Media && selectedMedia >= 0 && selectedMedia < int(stage.media.size())){
                const Warp::Media media = stage.media[size_t(selectedMedia)];
                ui.value("snimka", Treadle::fitText(fs::path(media.path).filename().string(), 150.0f, theme.textScale));
                char text[64];
                std::snprintf(text, sizeof(text), "%u x %u", media.width, media.height);
                ui.value("velicina", text);
                std::snprintf(text, sizeof(text), "%u @ %.2f fps", media.frames, media.framesPerSecond);
                ui.value("kadrova", text);
                ui.value("rezultat", media.result.empty() ? "nema" : "ima");
                ui.separator();
                std::vector<std::string> stepLabels;
                for(int value : steps) stepLabels.push_back(std::to_string(value));
                ui.choice("solve uzima svaki n-ti kadar", stepLabels, &stepIndex);
                ui.slider("najvise kadrova", &frameCount, 30.0f, 600.0f);
                ui.slider("koraka treninga", &trainSteps, 1000.0f, 30000.0f);
                if(ui.button("Solve kamere")) startSolve(selectedMedia, false);
                if(ui.button("Solve + Gaussian splat")) startSolve(selectedMedia, true);
                if(!media.result.empty() && ui.button("Otvori rezultat")) importFolder(media.result, media.path);
                ui.label("(isto i desnim klikom)");
            }else if(entity){
                ui.value("ime", Treadle::fitText(entity->name, 160.0f, theme.textScale));
                ui.value("vrsta", kindOf(*entity));
                ui.checkbox("vidljivo", &entity->visible);
                ui.separator();
                const Warp::Transform local = stage.localAt(entity->id, frame);
                ui.label("pomak");
                ui.label("  " + vectorText(local.translation));
                ui.label("rotacija (st)");
                ui.label("  " + vectorText(glm::degrees(glm::eulerAngles(local.rotation))));
                ui.label("mjerilo");
                ui.label("  " + vectorText(local.scale));
                if(entity->animated()){
                    ui.value("kljuceva", std::to_string(std::max(entity->translationKeys.size(), entity->rotationKeys.size())));
                }
                if(entity->camera){
                    ui.separator();
                    char text[64];
                    std::snprintf(text, sizeof(text), "%.0f px", double(entity->camera->focalPixels));
                    ui.value("zarisna", text);
                    std::snprintf(text, sizeof(text), "%u x %u", entity->camera->width, entity->camera->height);
                    ui.value("kadar", text);
                    if(!entity->camera->plate.empty()){
                        ui.value("snimka", Treadle::fitText(fs::path(entity->camera->plate).filename().string(), 150.0f,
                                                            theme.textScale));
                    }
                    if(ui.button(view.lookThrough == entity->id ? "Izadji iz kamere" : "Gledaj kroz kameru")){
                        view.lookThrough = view.lookThrough == entity->id ? Warp::None : entity->id;
                    }
                }
                if(entity->points){
                    ui.separator();
                    ui.value("tocaka", std::to_string(entity->points->positions.size()));
                    ui.value("boje", entity->points->colours.empty() ? "ne" : "da");
                }
                if(entity->splat){
                    ui.separator();
                    ui.label(Treadle::fitText(entity->splat->path, layout.properties.width - 30.0f, theme.textScale));
                    if(ui.button("Otvori u SplatVieweru")){
                        char command[1400];
                        std::snprintf(command, sizeof(command), "./SplatViewer \"%s\" 1 16 0 0 pogled.png 3 0 \"%s\" &",
                                      entity->splat->path.c_str(),
                                      fs::path(entity->splat->path).parent_path().string().c_str());
                        if(std::system(command) != 0){ /* preglednik javlja sam */ }
                    }
                }
                ui.separator();
                if(ui.button("Obrisi (Del)")) removeSelected(entity->id);
            }else{
                ui.label("Nista nije odabrano.");
            }
        }

        //== TIMELINE =============================================================================
        {
            const Treadle::Rect& area = layout.timeline;
            Treadle::DrawList& canvas = ui.canvas();
            canvas.rect(area, Treadle::Color{0.08f, 0.09f, 0.10f, 1.0f});
            canvas.rect(area.x, area.y, area.width, 1.0f, theme.panelEdge);

            const float rowY = area.y + 8.0f, rowH = 26.0f;
            auto [toStart, a1] = toolButton("|<", area.x + 10.0f, rowY, rowH);
            if(toStart) frame = stage.startFrame;
            auto [back, a2] = toolButton("<", a1, rowY, rowH);
            if(back) frame = std::max(stage.startFrame, std::floor(frame) - 1.0);
            auto [play, a3] = toolButton(playing ? "pauza" : "play", a2, rowY, rowH, playing);
            if(play) playing = !playing;
            auto [forward, a4] = toolButton(">", a3, rowY, rowH);
            if(forward) frame = std::min(stage.endFrame, std::floor(frame) + 1.0);
            auto [toEnd, a5] = toolButton(">|", a4, rowY, rowH);
            if(toEnd) frame = stage.endFrame;
            char text[96];
            std::snprintf(text, sizeof(text), "kadar %d   (%.0f - %.0f, %.0f fps)", int(std::floor(frame)),
                          stage.startFrame, stage.endFrame, stage.framesPerSecond);
            canvas.text(a5 + 12.0f, rowY + 6.0f, text, theme.text, theme.textScale);

            //Traka: kadrovi, kljucevi odabranog, glava
            const float trackTop = rowY + rowH + 12.0f;
            const Treadle::Rect track{area.x + 16.0f, trackTop, area.width - 32.0f,
                                      std::max(24.0f, area.y + area.height - trackTop - 12.0f)};
            canvas.rect(track, Treadle::Color{0.13f, 0.14f, 0.16f, 1.0f});
            const double span = std::max(1.0, stage.endFrame - stage.startFrame);
            auto xOf = [&](double f){ return track.x + float((f - stage.startFrame) / span) * track.width; };

            //Oznake kadrova: razmak koji daje barem 70 piksela medju brojevima
            const double pixelsPerFrame = double(track.width) / span;
            double tick = 1.0;
            for(double candidate : {1.0, 2.0, 5.0, 10.0, 20.0, 25.0, 50.0, 100.0, 200.0, 250.0, 500.0, 1000.0, 2000.0, 5000.0}){
                tick = candidate;
                if(candidate * pixelsPerFrame >= 70.0) break;
            }
            for(double f = std::ceil(stage.startFrame / tick) * tick; f <= stage.endFrame; f += tick){
                const float x = xOf(f);
                canvas.rect(x, track.y, 1.0f, 8.0f, theme.dim);
                canvas.text(x + 3.0f, track.y + 3.0f, std::to_string(int(f)), theme.dim, 1.0f);
            }
            if(const Warp::Entity* entity = stage.get(selected)){
                const std::vector<double>& times = entity->translationKeys.size() >= entity->rotationKeys.size()
                                                   ? entity->translationKeys.times : entity->rotationKeys.times;
                float lastKey = -10.0f;
                for(double t : times){
                    const float x = xOf(t);
                    if(x - lastKey < 2.0f) continue;       //tisuce kljuceva: jedan po pikselu
                    canvas.rect(x, track.y + track.height - 12.0f, 1.5f, 10.0f, Treadle::Color{1.0f, 0.78f, 0.25f, 0.8f});
                    lastKey = x;
                }
            }
            const float head = xOf(frame);
            canvas.rect(head - 1.0f, track.y - 4.0f, 2.0f, track.height + 8.0f, Treadle::Color{0.95f, 0.35f, 0.30f, 1.0f});

            const Treadle::Ui::Region scrub = ui.region("timeline", track);
            if(scrub.held){
                const double f = stage.startFrame + double((scrub.mouseX - track.x) / track.width) * span;
                frame = std::clamp(std::round(f), stage.startFrame, stage.endFrame);
                playing = false;
            }
        }

        //== ISPIS POSLA, u dnu pogleda ============================================================
        if(showLog || job.running){
            const size_t show = showLog ? 14 : 4;
            const Treadle::Rect& v = layout.viewport;
            const float height = 50.0f + float(show) * 20.0f;
            ui.panel(job.running ? "Ispis (tece)" : "Ispis", v.x + 10.0f, v.y + v.height - height - 10.0f, v.width - 20.0f);
            const size_t fits = size_t(std::max(20.0f, (v.width - 40.0f) / 12.0f));
            std::lock_guard<std::mutex> guard(job.lock);
            const size_t from = job.lines.size() > show ? job.lines.size() - show : 0;
            if(job.lines.empty()) ui.label("(jos nista)");
            for(size_t i = from; i < job.lines.size(); ++i){
                ui.label(job.lines[i].size() > fits ? job.lines[i].substr(0, fits) : job.lines[i]);
            }
        }

        //== IZBORNICI ============================================================================
        //Pogled nema widget koji bi javio desni klik, pa ga pita ovdje: nad pogledom, a ne nad
        //nekim stupcem ili izbornikom
        if(rightDown && !rightWasDown && layout.viewport.contains(float(cursorX), float(cursorY)) &&
           !ui.wantsMouse() && !ui.menuOpen("media") && !ui.menuOpen("entitet")){
            ui.openMenu("pogled");
        }
        if(ui.beginMenu("media")){
            const bool valid = menuMedia >= 0 && menuMedia < int(stage.media.size());
            if(ui.menuItem("Solve kamere (matchmove)", valid && !job.running)) startSolve(menuMedia, false);
            if(ui.menuItem("Solve + Gaussian splat", valid && !job.running)) startSolve(menuMedia, true);
            const bool hasResult = valid && !stage.media[size_t(menuMedia)].result.empty();
            if(ui.menuItem("Otvori rezultat", hasResult)){
                importFolder(stage.media[size_t(menuMedia)].result, stage.media[size_t(menuMedia)].path);
            }
            ui.menuSeparator();
            if(ui.menuItem("Ukloni iz projekta", valid)){
                stage.media.erase(stage.media.begin() + menuMedia);
                selectedMedia = -1;
            }
            ui.endMenu();
        }
        if(ui.beginMenu("entitet")){
            const Warp::Entity* entity = stage.get(menuEntity);
            if(ui.menuItem("Gledaj kroz kameru", entity && entity->camera)) view.lookThrough = menuEntity;
            if(ui.menuItem("Dodaj kocku ovdje", entity != nullptr)) addMesh(Warp::Shape::Cube, menuEntity);
            if(ui.menuItem("Dodaj ravninu ovdje", entity != nullptr)) addMesh(Warp::Shape::Plane, menuEntity);
            ui.menuSeparator();
            if(ui.menuItem("Obrisi", entity != nullptr)) removeSelected(menuEntity);
            ui.endMenu();
        }
        if(ui.beginMenu("pogled")){
            if(ui.menuItem("Dodaj kocku")) addMesh(Warp::Shape::Cube, Warp::None);
            if(ui.menuItem("Dodaj ravninu")) addMesh(Warp::Shape::Plane, Warp::None);
            ui.menuSeparator();
            if(ui.menuItem("Uokviri sve")){
                view.lookThrough = Warp::None;
                Loom::frameAll(stage.size() ? stage : live, frame, view.orbit);
            }
            if(ui.menuItem(view.showPoints ? "Sakrij tocke" : "Pokazi tocke")) view.showPoints = !view.showPoints;
            if(ui.menuItem(view.showPaths ? "Sakrij putanje" : "Pokazi putanje")) view.showPaths = !view.showPaths;
            if(ui.menuItem(view.showGrid ? "Sakrij mrezu" : "Pokazi mrezu")) view.showGrid = !view.showGrid;
            ui.endMenu();
        }

        ui.end();

        //== POGLED: mis i tipke, tek kad suicelje nije uzelo mis ==================================
        const Treadle::Rect& viewportRect = layout.viewport;
        const bool overViewport = viewportRect.contains(float(cursorX), float(cursorY)) && !ui.wantsMouse();
        const Loom::ViewCamera pickCamera = Loom::viewCameraFor(stage, frame, viewportRect, view);

        //Lijevi: klik bira, vucenje okrece
        if(leftDown && !leftWasDown){
            leftInViewport = overViewport;
            dragging = false;
            pressX = cursorX; pressY = cursorY;
        }
        if(leftDown && leftInViewport){
            if(!dragging && std::hypot(cursorX - pressX, cursorY - pressY) > 4.0) dragging = true;
            if(dragging){
                view.lookThrough = Warp::None;
                view.orbit.yaw -= float(cursorX - lastX) * 0.008f;
                view.orbit.pitch = std::clamp(view.orbit.pitch + float(cursorY - lastY) * 0.008f, -1.5f, 1.5f);
            }
        }
        if(!leftDown && leftWasDown && leftInViewport && !dragging){
            selected = Loom::pickEntity(stage, frame, pickCamera, glm::vec2(float(cursorX), float(cursorY)));
            focus = Focus::Entity;
        }
        if(!leftDown) leftInViewport = false;

        //Srednji gumb kao u Blenderu: okretanje, sa shiftom pomicanje
        if(middleDown && !middleWasDown) middleDragging = overViewport;
        if(!middleDown) middleDragging = false;
        if(middleDragging){
            view.lookThrough = Warp::None;
            if(shift){
                const glm::mat4 inverse = glm::inverse(pickCamera.view);
                const float perPixel = view.orbit.distance / pickCamera.focal;
                view.orbit.target -= glm::vec3(inverse[0]) * float(cursorX - lastX) * perPixel;
                view.orbit.target += glm::vec3(inverse[1]) * float(cursorY - lastY) * perPixel;
            }else{
                view.orbit.yaw -= float(cursorX - lastX) * 0.008f;
                view.orbit.pitch = std::clamp(view.orbit.pitch + float(cursorY - lastY) * 0.008f, -1.5f, 1.5f);
            }
        }
        if(overViewport && scrollAccumulated != 0.0f && view.lookThrough == Warp::None){
            view.orbit.distance = std::clamp(view.orbit.distance * std::pow(0.88f, scrollAccumulated), 1e-4f, 1e5f);
        }
        scrollAccumulated = 0.0f;
        leftWasDown = leftDown;
        middleWasDown = middleDown;
        rightWasDown = rightDown;
        lastX = cursorX; lastY = cursorY;

        if(keys.pressed(window, GLFW_KEY_SPACE)) playing = !playing;
        if(keys.pressed(window, GLFW_KEY_RIGHT)) frame = std::min(stage.endFrame, std::floor(frame) + 1.0);
        if(keys.pressed(window, GLFW_KEY_LEFT)) frame = std::max(stage.startFrame, std::floor(frame) - 1.0);
        if(keys.pressed(window, GLFW_KEY_HOME)) frame = stage.startFrame;
        if(keys.pressed(window, GLFW_KEY_END)) frame = stage.endFrame;
        if(keys.pressed(window, GLFW_KEY_F)){
            view.lookThrough = Warp::None;
            const Warp::Entity* entity = stage.get(selected);
            if(entity && (entity->mesh || entity->camera)){
                view.orbit.target = glm::vec3(stage.worldMatrix(selected, frame)[3]);
                view.orbit.distance = extent.radius * 0.8f;
            }else{
                Loom::frameAll(stage.size() ? stage : live, frame, view.orbit);
            }
        }
        const bool zero = keys.pressed(window, GLFW_KEY_0);
        const bool padZero = keys.pressed(window, GLFW_KEY_KP_0);
        if(zero || padZero){
            if(view.lookThrough != Warp::None) view.lookThrough = Warp::None;
            else{
                const Warp::Entity* entity = stage.get(selected);
                view.lookThrough = entity && entity->camera ? selected : firstCamera();
            }
        }
        if(keys.pressed(window, GLFW_KEY_DELETE) && selected != Warp::None && focus == Focus::Entity){
            removeSelected(selected);
        }
        if(keys.pressed(window, GLFW_KEY_ESCAPE)){
            if(ui.menuOpen("pogled") || ui.menuOpen("media") || ui.menuOpen("entitet")) ui.closeMenu();
            else if(view.lookThrough != Warp::None) view.lookThrough = Warp::None;
        }

        //== CRTANJE =================================================================================
        Treadle::DrawList scene;
        {
            const Loom::ViewCamera camera = Loom::viewCameraFor(stage, frame, viewportRect, view);
            Loom::paintStage(stage, frame, camera, view, extent, selected, scene);
            if(live.size() > 0){
                Loom::ViewportState liveView = view;
                liveView.showGrid = stage.size() == 0 && view.showGrid;
                liveView.lookThrough = Warp::None;
                Loom::paintStage(live, 1.0, camera, liveView, extent, Warp::None, scene);
            }
        }

        if(!loom.renderer.beginFrame()) continue;
        loom.renderer.beginPass();
        if(!scene.vertices.empty()){
            scenePainter.draw(loom.renderer, scene, uint32_t(windowWidth), uint32_t(windowHeight));
        }
        painter.draw(loom.renderer, ui.drawn(), uint32_t(windowWidth), uint32_t(windowHeight));
        loom.renderer.endPass();
        loom.renderer.endFrame();

        //Snimka: nekoliko kadrova da se raspored i scena slegnu, pa jedan u datoteku
        if(!shotPath.empty() && ++framesDrawn >= 6){
            loom.waitIdle();
            const ImageData shot = loom.renderer.readLastFrame();
            const Spool::Image image = Spool::imageFromPixels(shot.pixels.data(), shot.extent.width, shot.extent.height,
                isBgraFormat(shot.format) ? Spool::ChannelOrder::BGRA : Spool::ChannelOrder::RGBA);
            Spool::saveImage(shotPath, image);
            std::printf("Snimljeno %s (%ux%u)\n", shotPath.c_str(), image.width, image.height);
            break;
        }
    }

    if(worker.joinable()) worker.join();
    return 0;
}
