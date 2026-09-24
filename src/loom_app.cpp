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
//   loom <mapa> --snimi slika.png --rezultat C0256_loom [--kadar 120] [--kroz] [--kocka | --kocka-u 90] [--pokret hod.bvh]
//
// uveze rezultat, po zelji doda kocku (postavljenu u kadru 90) i gleda kroz rijesenu kameru, pa
// spremi kadar i izadje.
// Tako se editor provjerava okom, a ne samo testom racuna
//
// SNIMKA IZA SCENE (V). Kroz rijesenu kameru se iza scene crta pravi kadar snimke - ploca. Tu se
// matchmove presudjuje: kocka na podu mora stajati na istom mjestu snimke kroz cijeli kadar
#include "Core/LoomConfig.h"
#include "Core/LoomInitializer.h"

#include "LoomEditor.h"
#include "LoomJob.h"
#include "LoomPlate.h"
#include "LoomScene.h"
#include "LoomSplat.h"
#include "LoomEditorTools.h"
#include "LoomPbr.h"
#include "LoomViewport.h"
#include "LoomWeaverMotion.h"

#include "Vulkan/ImageData.h"
#include "Vulkan/Material.h"
#include "Vulkan/StreamingTexture.h"
#include "Vulkan/Texture.h"

#include <Spool/ImageFile.h>
#include <Warp/Project.h>
#include <Spool/VideoFile.h>
#include <Treadle/Ui.h>
#include <TreadlePaint/UiPainter.h>

#include <GLFW/glfw3.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cctype>
#include <cstdio>
#include <ctime>
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
    std::vector<fs::path> folders, videos, results, projects, motions, models, images;

    void refresh(){
        folders.clear();
        projects.clear();
        results = Loom::resultsIn(at);
        videos = Loom::videosIn(at);
        motions = Loom::weaverMotionFilesIn(at);
        models = Loom::modelFilesIn(at);
        images = Loom::imageFilesIn(at);
        std::error_code error;
        for(const auto& entry : fs::directory_iterator(at, error)){
            if(error) break;
            const std::string name = entry.path().filename().string();
            if(entry.is_regular_file(error) && entry.path().extension() == ".usda" &&
               Warp::isProjectFile(entry.path().string())){
                projects.push_back(entry.path());
                continue;
            }
            if(!entry.is_directory(error) || name.empty() || name[0] == '.') continue;
            if(Loom::isResultDirectory(entry.path())) continue;
            folders.push_back(entry.path());
        }
        std::sort(folders.begin(), folders.end());
        std::sort(projects.begin(), projects.end());
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
    if(entity.joint) return "zglob";
    if(entity.model) return "model (glTF)";
    return entity.children.empty() ? "nul" : "grupa";
}

//Rotacija iz matrice koja moze nositi i mjerilo: stupci se normiraju prije pretvorbe
glm::quat rotationOf(const glm::mat4& m){
    return glm::normalize(glm::quat_cast(glm::mat3(glm::normalize(glm::vec3(m[0])), glm::normalize(glm::vec3(m[1])),
                                                   glm::normalize(glm::vec3(m[2])))));
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
    //Splat rasterizator trazi 21 set i 71 storage buffer; zadanih 64 po tipu strog driver odbije
    //PBR materijali (LoomPbr.h) trebaju po materijalu set s pet mapa, u svakom kadru u letu
    config.maxDescriptorSets = 1024;
    LoomInitializer loom(config);

    GLFWwindow* window = loom.window->getWindow();
    Treadle::Ui ui;
    UiPainter painter(loom.device, loom.command, loom.getDescriptorPool(),
                      loom.getColorFormat(), vk::Format::eUndefined, 1u << 18);

    //Scena ima svoj slikar: oblak od sto tisuca tocaka ne stane u kapacitet suicelja
    UiPainter scenePainter(loom.device, loom.command, loom.getDescriptorPool(),
                           loom.getColorFormat(), vk::Format::eUndefined, 1u << 20);
    //Sloj IZNAD meseva: strelice, krugovi i alat plohe. Tocke i mreza su ispod njih - crte nemaju
    //dubinu, pa bi gusti zid tocaka iza kocke inace prekrio cijelu kocku
    UiPainter overlayPainter(loom.device, loom.command, loom.getDescriptorPool(),
                             loom.getColorFormat(), vk::Format::eUndefined, 1u << 16);

    //Argumenti: prva mapa, pa zastavice za snimku (vidi zaglavlje)
    fs::path startAt = fs::current_path();
    std::string startProject;             //loom projekt.usda otvara projekt
    std::string shotPath, shotResult, shotSave, shotMotion;
    std::string shotModel;                //--model: glTF na mjestu pogleda
    float shotSurface[4] = {0, 0, 0, 0};  //--ploha x y sirina visina: pravokutnik u pogledu, pa kocka na plohu
    bool shotSurfaceWanted = false;
    double shotFrame = -1.0;
    bool shotThrough = false, shotCube = false, shotNoSplat = false, shotRotate = false;
    double shotCubeFrame = -1.0;          //kadar u kojem se kocka postavi, kad nije isti kao snimljeni
    for(int i = 1; i < argc; ++i){
        const std::string argument = argv[i];
        if(argument == "--snimi" && i + 1 < argc) shotPath = argv[++i];
        else if(argument == "--rezultat" && i + 1 < argc) shotResult = argv[++i];
        else if(argument == "--kadar" && i + 1 < argc) shotFrame = std::atof(argv[++i]);
        else if(argument == "--kroz") shotThrough = true;
        else if(argument == "--bez-splata") shotNoSplat = true;
        else if(argument == "--rotacija") shotRotate = true;
        else if(argument == "--pokret" && i + 1 < argc) shotMotion = argv[++i];
        else if(argument == "--model" && i + 1 < argc) shotModel = argv[++i];
        else if(argument == "--ploha" && i + 4 < argc){
            for(int k = 0; k < 4; ++k) shotSurface[k] = float(std::atof(argv[++i]));
            shotSurfaceWanted = true;
        }
        else if(argument == "--spremi" && i + 1 < argc) shotSave = argv[++i];
        else if(argument == "--kocka") shotCube = true;
        else if(argument == "--kocka-u" && i + 1 < argc){ shotCube = true; shotCubeFrame = std::atof(argv[++i]); }
        else if(argument.size() > 5 && argument.substr(argument.size() - 5) == ".usda") startProject = argument;
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
    glm::vec2 menuPixel(0.0f);            //gdje je desni klik otvorio izbornik pogleda
    int menuMedia = -1;

    double frame = 1.0;
    bool playing = false;

    Loom::ViewportState view;
    Loom::SceneExtent extent;
    bool extentDirty = true;

    float mediaScroll = 0.0f, hierarchyScroll = 0.0f, propertiesScroll = 0.0f;
    std::string message;                  //zadnja poruka korisniku, u alatnoj traci
    bool motionWorkflowOpen = false;
    bool motionPromptFocused = false;
    std::string motionPrompt = "a biped walks forward";
    float motionDuration = 5.0f;
    fs::path generatedMotionPath;

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
    static std::string typedCharacters;
    glfwSetCharCallback(window, [](GLFWwindow*, unsigned int codepoint){
        if(codepoint < 32 || codepoint > 0x10ffff || (codepoint >= 0xd800 && codepoint <= 0xdfff)) return;
        if(codepoint <= 0x7f){
            typedCharacters.push_back(static_cast<char>(codepoint));
        }else if(codepoint <= 0x7ff){
            typedCharacters.push_back(static_cast<char>(0xc0 | (codepoint >> 6)));
            typedCharacters.push_back(static_cast<char>(0x80 | (codepoint & 0x3f)));
        }else if(codepoint <= 0xffff){
            typedCharacters.push_back(static_cast<char>(0xe0 | (codepoint >> 12)));
            typedCharacters.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3f)));
            typedCharacters.push_back(static_cast<char>(0x80 | (codepoint & 0x3f)));
        }else{
            typedCharacters.push_back(static_cast<char>(0xf0 | (codepoint >> 18)));
            typedCharacters.push_back(static_cast<char>(0x80 | ((codepoint >> 12) & 0x3f)));
            typedCharacters.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3f)));
            typedCharacters.push_back(static_cast<char>(0x80 | (codepoint & 0x3f)));
        }
    });

    //Strelice za pomicanje i rotacija u stupnjevima. Kutovi se pamte dok se uredjuju: kvaternion
    //natrag u Eulerove kutove nije jednoznacan, pa bi polje koje se vuce preko 90 st skocilo
    int gizmoAxisHeld = -1, gizmoAxisHot = -1;
    //W pomice, E okrece - kao u Mayi i Houdiniju
    enum class Tool{ Move, Rotate };
    Tool tool = shotRotate ? Tool::Rotate : Tool::Move;
    glm::vec3 eulerCache(0.0f);
    Warp::Id eulerFor = Warp::None;
    double eulerFrame = -1.0;

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
        //Za matchmove bez slika kadrova (vidi --samo-kamera u VideoSolveu); splat ih treba
        char command[1400];
        std::snprintf(command, sizeof(command), "./VideoSolve \"%s\" %d %d 0 \"%s\"%s",
                      video.string().c_str(), steps[stepIndex], int(frameCount), out.string().c_str(),
                      thenSplat ? "" : " --samo-kamera");
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

    auto importFolder = [&](const fs::path& directory, const std::string& givenPlate){
        //Snimka uz rezultat postaje ploca kamere i kad ju pozivatelj nije znao
        const std::string plate = givenPlate.empty() ? Loom::plateFor(directory) : givenPlate;
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



    //NOVO TIJELO SJEDI NA POVRSINI SNIMKE ondje kamo se gleda: na tockama oblaka pod sredinom
    //pogleda (ili pod misem, kad je dodano desnim klikom u pogled). Kroz rijesenu kameru to je
    //stvarni zid ili stol u kadru - bas ondje gdje se provjerava drzi li se kocka snimke. Kad pod
    //pikselom nema tocaka, zraka se spusti na pod (y = 0), a kad ni to ne ide, sredina scene.
    //Velicina je iz udaljenosti (solve nema metre): desetina puta do mjesta
    auto addMeshAt = [&](Warp::Shape shape, Warp::Id parent, glm::vec2 pixel, bool usePixel){
        const char* name = shape == Warp::Shape::Cube ? "Kocka" : "Ravnina";
        const Warp::Id id = stage.create(name, parent);
        Warp::Entity& entity = *stage.get(id);
        entity.mesh = Warp::Mesh{shape};

        int w = 0, h = 0;
        glfwGetWindowSize(window, &w, &h);
        const Loom::ViewCamera camera = Loom::viewCameraFor(stage, frame, Loom::layoutEditor(float(w), float(h)).viewport, view);
        if(!usePixel) pixel = glm::vec2(camera.frame.x + camera.frame.width * 0.5f, camera.frame.y + camera.frame.height * 0.5f);
        const glm::mat4 inverse = glm::inverse(camera.view);
        const glm::vec3 eye = glm::vec3(inverse[3]);
        const glm::vec3 ray = glm::normalize(glm::vec3(inverse * glm::vec4((pixel.x - camera.centre.x) / camera.focal,
                                                                            -(pixel.y - camera.centre.y) / camera.focal, -1.0f, 0.0f)));
        glm::vec3 place(extent.centre.x, 0.0f, extent.centre.z);
        bool onSurface = Loom::surfaceAt(stage, frame, camera, pixel, place);
        if(!onSurface && ray.y < -1e-3f && eye.y > 0.0f && -eye.y / ray.y < extent.radius * 20.0f){
            place = eye + ray * (-eye.y / ray.y);
        }
        const float distance = std::max(1e-4f, glm::length(place - eye));
        const float size = distance * (shape == Warp::Shape::Cube ? 0.1f : 0.4f);
        //Na podu kocka stoji NA njemu; na zidu ili stolu joj je sredina na plohi
        const glm::vec3 worldPosition = onSurface ? place
                                                  : glm::vec3(place.x, shape == Warp::Shape::Cube ? size * 0.5f : 0.0f, place.z);
        const glm::mat4 parentWorld = parent == Warp::None ? glm::mat4(1.0f) : stage.worldMatrix(parent, frame);
        entity.local.translation = glm::vec3(glm::inverse(parentWorld) * glm::vec4(worldPosition, 1.0f));
        entity.local.scale = glm::vec3(size);
        selected = id;
        focus = Focus::Entity;
    };
    auto addMesh = [&](Warp::Shape shape, Warp::Id parent){ addMeshAt(shape, parent, glm::vec2(0.0f), false); };

    //POKRET LIKA (WeaverMotion, NVIDIA Kimodo): BVH postaje kostur u sceni (vidi LoomWeaverMotion.h).
    //U praznoj sceni klip preuzme timeline. U sceni iz matchmovea pocinje na kadru glave, u vremenu
    //scene, i STOJI NA PODU ispod mjesta u koje pogled gleda:
    //
    //  mjesto    gdje sredisnja zraka pogleda presijece pod (y = 0); kad gleda vodoravno ili gore -
    //            snimka iz ruke gotovo uvijek - pod ispod tocke na koju gleda (povrsina snimke, ili
    //            tocka na udaljenosti scene)
    //  mjerilo   solve nema metre, ali kamera iz ruke je na visini oka, oko 1.5 m iznad poda. Visina
    //            kamere nad podom je zato najbolja procjena metra koju scena daje; Kimodo pise metre.
    //            Kamera na stativu, dronu ili niskom kutu to krsi - dotjeruje se na grupi
    auto newestMotionInBrowser = [&]() -> fs::path{
        fs::path newest;
        std::filesystem::file_time_type newestTime{};
        for(const fs::path& motion : browser.motions){
            std::error_code error;
            const auto modified = fs::last_write_time(motion, error);
            if(error) continue;
            if(newest.empty() || modified > newestTime){
                newest = motion;
                newestTime = modified;
            }
        }
        return newest;
    };

    auto importMotion = [&](const fs::path& path){
        Loom::MotionPlacement placement;
        if(stage.size() > 0){
            int w = 0, h = 0;
            glfwGetWindowSize(window, &w, &h);
            const Loom::ViewCamera camera = Loom::viewCameraFor(stage, frame, Loom::layoutEditor(float(w), float(h)).viewport, view);
            const glm::vec2 centre(camera.frame.x + camera.frame.width * 0.5f, camera.frame.y + camera.frame.height * 0.5f);
            const Loom::Ray ray = Loom::rayThrough(camera, centre);
            glm::vec3 place;
            const bool hitsFloor = ray.direction.y < -1e-3f && ray.origin.y > 0.0f &&
                                   -ray.origin.y / ray.direction.y < extent.radius * 20.0f;
            if(hitsFloor){
                place = ray.origin + ray.direction * (-ray.origin.y / ray.direction.y);
            }else{
                glm::vec3 looked;
                if(!Loom::surfaceAt(stage, frame, camera, centre, looked)){
                    looked = ray.origin + ray.direction * glm::length(extent.centre - ray.origin);
                }
                place = glm::vec3(looked.x, 0.0f, looked.z);
            }
            placement.position = place;
            placement.startFrame = std::round(frame);
            placement.sceneFps = stage.framesPerSecond;
            Engine::WeaverMotion::Clip clip;
            std::string error;
            if(!Engine::WeaverMotion::readKimodoBvh(path.string(), clip, error)){ message = "pokret se ne da procitati: " + error; return; }
            const float eyeHeight = ray.origin.y;
            placement.scale = eyeHeight > 1e-4f ? eyeHeight / 1.5f
                                                : 0.35f * std::max(1e-4f, glm::length(place - ray.origin)) /
                                                  std::max(1e-4f, Loom::motionRestHeight(clip));
            const Loom::WeaverMotionImportReport report = Loom::importWeaverMotionClip(stage, clip, path.stem().string(), placement);
            if(!report.problem.empty()){ message = "pokret: " + report.problem; return; }
            selected = report.group;
            collapsed.insert(report.root);
            char text[256];
            std::snprintf(text, sizeof(text), "pokret (NVIDIA Kimodo): %zu zglobova, kadrovi %.0f-%.0f, mjerilo %.3f",
                          report.joints, report.firstFrame, report.lastFrame, placement.scale);
            message = text;
        }else{
            const Loom::WeaverMotionImportReport report = Loom::importWeaverMotionBvh(stage, path, placement);
            if(!report.problem.empty()){ message = "pokret se ne da procitati: " + report.problem; return; }
            selected = report.group;
            collapsed.insert(report.root);
            frame = stage.startFrame;
            extentDirty = true;
            view.lookThrough = Warp::None;
            view.orbit.target = glm::vec3(0.0f, report.height * 0.5f, 0.0f);
            view.orbit.distance = std::max(1.0f, report.height * 2.5f);
            char text[256];
            std::snprintf(text, sizeof(text), "pokret (NVIDIA Kimodo): %zu zglobova, %zu kadrova @ %.0f fps",
                          report.joints, report.frames, stage.framesPerSecond);
            message = text;
        }
        focus = Focus::Entity;
    };

    auto importNewestMotion = [&](){
        const fs::path motion = newestMotionInBrowser();
        if(motion.empty()){
            message = "U otvorenoj mapi nema BVH-a za uvoz.";
            return;
        }
        importMotion(motion);
        if(!message.empty()) message = "WeaverMotion: " + message;
    };

    auto startMotionGeneration = [&](){
        if(job.running){
            message = "Drugi Loom posao još radi.";
            return;
        }
        const auto firstText = std::find_if_not(motionPrompt.begin(), motionPrompt.end(),
            [](unsigned char c){ return std::isspace(c) != 0; });
        if(firstText == motionPrompt.end()){
            message = "Upiši opis pokreta prije generiranja.";
            motionPromptFocused = true;
            return;
        }

        const fs::path runner = fs::path(LOOM_ROOT_DIR) /
                                "tools/weavermotion/.venv-clean/bin/kimodo_gen";
        if(!fs::is_regular_file(runner)){
            message = "Kimodo runner nije instaliran; vidi tools/weavermotion/README.md.";
            return;
        }

        const fs::path outputDirectory = browser.at / "WeaverMotion";
        std::error_code error;
        fs::create_directories(outputDirectory, error);
        if(error){
            message = "Ne mogu napraviti izlaznu mapu WeaverMotion: " + error.message();
            return;
        }

        const auto stamp = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        const fs::path outputStem = outputDirectory / ("motion_" + std::to_string(stamp));
        generatedMotionPath = outputStem;
        generatedMotionPath += ".bvh";

        const std::string command = Loom::buildWeaverMotionCommand(
            runner, motionPrompt, motionDuration, outputStem);
        afterJob = After::Nothing;
        startJob(command, outputDirectory.string(), Loom::Task::WeaverMotion, 0);
        message = "Kimodo generira na GPU-u; LLM2Vec encoder radi na CPU-u.";
        showLog = true;
    };

    auto openMotionWorkflow = [&](){
        motionWorkflowOpen = true;
        motionPromptFocused = true;
    };

    //-- projekt ----------------------------------------------------------------------------------
    //Projekt je .usda (vidi Warp/Project.h). Prvo spremanje ga stavi u mapu koju media prozor
    //pregledava, pod imenom koje jos ne postoji - nikad preko tudjeg projekta
    fs::path projectPath;
    fs::path pendingProject;              //ceka potvrdu, jer otvaranje zamjenjuje scenu

    //NESPREMLJENO: otisak scene u trenutku zadnjeg spremanja ili otvaranja (vidi
    //Stage::fingerprint). Prazna scena na pocetku nije nespremljena
    uint64_t savedFingerprint = stage.fingerprint();
    bool quitting = false;

    auto saveProjectNow = [&]() -> bool{
        if(projectPath.empty()){
            projectPath = browser.at / "loom_projekt.usda";
            for(int n = 2; fs::exists(projectPath); ++n) projectPath = browser.at / ("loom_projekt_" + std::to_string(n) + ".usda");
        }
        const auto started = std::chrono::steady_clock::now();
        std::string error;
        if(Warp::saveProject(stage, projectPath.string(), error)){
            char text[256];
            std::snprintf(text, sizeof(text), "spremljeno: %s (%.1f s)", projectPath.filename().string().c_str(),
                          std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count());
            message = text;
            browser.refresh();
            savedFingerprint = stage.fingerprint();
            return true;
        }
        message = "spremanje nije uspjelo: " + error;
        return false;
    };

    auto openProject = [&](const fs::path& path){
        std::string error;
        Warp::Stage opened;
        if(!Warp::loadProject(path.string(), opened, error)){ message = "ne mogu otvoriti: " + error; return; }
        stage = std::move(opened);
        projectPath = path;
        selected = Warp::None;
        selectedMedia = -1;
        collapsed.clear();
        view = Loom::ViewportState{};
        frame = stage.startFrame;
        extent = Loom::sceneExtent(stage, frame);
        extentDirty = false;
        Loom::frameAll(stage, frame, view.orbit);
        message = "otvoren projekt " + path.filename().string();
        savedFingerprint = stage.fingerprint();
    };

    auto newScene = [&](){
        stage = Warp::Stage{};
        projectPath.clear();
        selected = Warp::None;
        selectedMedia = -1;
        view = Loom::ViewportState{};
        frame = 1.0;
        extentDirty = true;
        message = "nova scena";
        savedFingerprint = stage.fingerprint();
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

    //-- ploca iza kamere ----------------------------------------------------------------------
    //Cjevovod je Loomov fullscreen prolaz; crta se u pravokutnik kadra suzenjem viewporta, a
    //material.baseColor mnozi snimku - to je svjetlina ploce
    PipelineConfig plateConfig;
    plateConfig.vertexBindings.clear();
    plateConfig.vertexAttributes.clear();
    plateConfig.descriptorBindings = {Texture::getLayoutBinding(), Material::getDataLayoutBinding()};
    plateConfig.vertShaderPath = std::string(LOOM_SHADER_DIR) + "/fullscreen.vert.spv";
    plateConfig.fragShaderPath = std::string(LOOM_SHADER_DIR) + "/fullscreen.frag.spv";
    plateConfig.cullMode = vk::CullModeFlagBits::eNone;
    VulkanGraphicsPipeline platePipeline = loom.createPipeline(plateConfig);
    std::unique_ptr<StreamingTexture> plateTexture;
    std::unique_ptr<Material> plateMaterial;
    Loom::PlateStream plateStream;
    bool showPlate = true;
    float plateBrightness = 1.0f;
    bool plateReady = false;
    int64_t plateShown = -1;
    std::vector<uint8_t> platePixels;

    //-- splat u pogledu -----------------------------------------------------------------------------
    Loom::ViewportSplat viewportSplat(loom);

    //-- PBR meshevi, materijali, ploha iz odabira (LoomPbr.h, LoomEditorTools.h) -------------------
    Loom::ViewportMeshes viewportMeshes(loom);
    Loom::MaterialPanelState materialState;
    Loom::SurfaceTool surfaceTool;
    view.gpuMeshes = true;

    //Poslije uvoza modela: odabran cvor s mrezom (pa se vide njegovi materijali), a u sceni koja je
    //bila prazna pogled se uokviri na model - "Uokviri" gleda oblak tocaka, a modela bez oblaka nema
    auto afterModelImport = [&](const Loom::ModelImportReport& report, bool wasEmpty){
        Warp::Id meshNode = report.group;
        std::vector<Warp::Id> pending{report.group};
        while(!pending.empty()){
            const Warp::Id id = pending.back();
            pending.pop_back();
            const Warp::Entity* e = stage.get(id);
            if(!e) continue;
            if(e->model){ meshNode = id; break; }
            pending.insert(pending.end(), e->children.rbegin(), e->children.rend());
        }
        selected = meshNode;
        focus = Focus::Entity;
        extentDirty = true;
        if(wasEmpty && stage.get(report.group)){
            const Warp::Entity& group = *stage.get(report.group);
            const float height = std::max(1e-3f, (report.high.y - report.low.y) * group.local.scale.y);
            view.lookThrough = Warp::None;
            view.orbit.target = group.local.translation + glm::vec3(0.0f, 0.5f * height + report.low.y * group.local.scale.y, 0.0f);
            view.orbit.distance = height * 2.2f;
        }
    };
    bool showSplat = !shotNoSplat;
    bool splatWasLoading = false;
    int splatSettledFrames = 0;
    int meshSettledFrames = 0;

    uint32_t framesDrawn = 0;
    if(!startProject.empty()){
        openProject(startProject);
        if(shotFrame >= 0.0) frame = shotFrame;
        if(shotThrough) view.lookThrough = firstCamera();
        browser.at = fs::absolute(startProject).parent_path();
        browser.refresh();
    }
    if(!shotResult.empty()){
        importFolder(shotResult, "");
        if(shotFrame >= 0.0) frame = shotFrame;
        if(shotThrough) view.lookThrough = firstCamera();
        if(shotCube){
            //Kocka se postavi u jednom kadru a snima u drugom - to je provjera drzi li se snimke
            const double shown = frame;
            if(shotCubeFrame >= 0.0) frame = shotCubeFrame;
            addMesh(Warp::Shape::Cube, Warp::None);
            frame = shown;
        }
    }
    if(!shotMotion.empty()){ importMotion(shotMotion); std::printf("%s\n", message.c_str()); }
    if(!shotModel.empty() || shotSurfaceWanted){
        int w = 0, h = 0;
        glfwGetWindowSize(window, &w, &h);
        const Loom::ViewCamera camera = Loom::viewCameraFor(stage, frame, Loom::layoutEditor(float(w), float(h)).viewport, view);
        extent = Loom::sceneExtent(stage, frame);
        if(!shotModel.empty()){
            const bool wasEmpty = stage.size() == 0;
            const Loom::ModelImportReport report = Loom::importModelAtView(stage, shotModel, frame, camera, extent);
            std::printf("model: %s%zu cvorova, %zu mreza, %zu materijala\n", report.problem.c_str(), report.nodes, report.meshes, report.materials);
            afterModelImport(report, wasEmpty);
        }
        if(shotSurfaceWanted){
            //Pravokutnik je zadan u pikselima POGLEDA (od njegovog gornjeg lijevog kuta)
            const Treadle::Rect r{camera.rect.x + shotSurface[0], camera.rect.y + shotSurface[1], shotSurface[2], shotSurface[3]};
            surfaceTool.active = true;
            surfaceTool.selected = Loom::selectFrontPoints(stage, frame, camera, r);
            surfaceTool.fit = Loom::fitSurface(surfaceTool.selected, camera.eye);
            selected = Loom::placeOnSurface(stage, surfaceTool, Warp::Shape::Cube);
            surfaceTool.active = false;         //snimka pokazuje kocku, ne odabir preko nje
            std::printf("ploha: %zu tocaka, %zu u ravnini, normala %.3f %.3f %.3f\n", surfaceTool.fit.total, surfaceTool.fit.used,
                        surfaceTool.fit.normal.x, surfaceTool.fit.normal.y, surfaceTool.fit.normal.z);
        }
        focus = Focus::Entity;
    }
    if(!shotSave.empty()){
        projectPath = shotSave;
        saveProjectNow();
        std::printf("%s\n", message.c_str());
    }

    std::string windowTitle;
    while(!quitting){
        glfwPollEvents();
        if(motionWorkflowOpen && motionPromptFocused){
            if(motionPrompt.size() + typedCharacters.size() <= 4096) motionPrompt += typedCharacters;
            typedCharacters.clear();
            static bool backspaceWasDown = false;
            const bool backspaceDown = glfwGetKey(window, GLFW_KEY_BACKSPACE) == GLFW_PRESS;
            if(backspaceDown && !backspaceWasDown && !motionPrompt.empty()){
                size_t first = motionPrompt.size() - 1;
                while(first > 0 && (static_cast<unsigned char>(motionPrompt[first]) & 0xc0) == 0x80) --first;
                motionPrompt.erase(first);
            }
            backspaceWasDown = backspaceDown;
        }else{
            typedCharacters.clear();
        }

        //Otisak svaki kadar: prolaz kroz stablo i kljuceve, desetinka milisekunde i na 2301 kljucu
        const bool dirty = stage.fingerprint() != savedFingerprint;
        const std::string title = std::string("Loom - ") + (projectPath.empty() ? "nova scena" : projectPath.filename().string()) +
                                  (dirty ? " *" : "");
        if(title != windowTitle){ glfwSetWindowTitle(window, title.c_str()); windowTitle = title; }

        //ZATVARANJE PROZORA S NESPREMLJENIM se zaustavi i pita. Bez nespremljenog - izlaz
        if(glfwWindowShouldClose(window)){
            if(!dirty) break;
            glfwSetWindowShouldClose(window, GLFW_FALSE);
            int w = 0, h = 0;
            glfwGetWindowSize(window, &w, &h);
            ui.openMenuAt("izlaz", float(w) * 0.5f - 200.0f, float(h) * 0.4f);
        }

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
            }else if(job.task == Loom::Task::WeaverMotion){
                std::error_code error;
                if(fs::is_regular_file(generatedMotionPath, error)){
                    browser.at = generatedMotionPath.parent_path();
                    browser.refresh();
                    importMotion(generatedMotionPath);
                    if(!message.empty()) message = "Kimodo gotovo; " + message;
                    motionWorkflowOpen = true;
                    motionPromptFocused = false;
                }else{
                    message = "Kimodo nije izradio očekivani BVH; pregledaj Ispis.";
                    showLog = true;
                }
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
            auto [moveTool, afterMove] = toolButton("W", afterNul + 12.0f, y, h, tool == Tool::Move);
            if(moveTool) tool = Tool::Move;
            auto [rotateTool, afterRotateTool] = toolButton("E", afterMove, y, h, tool == Tool::Rotate);
            if(rotateTool) tool = Tool::Rotate;
            //Ploha iz odabira: vucenjem u pogledu se oznaci komad plohe (LoomEditorTools.h)
            auto [surfaceButton, afterRotate] = toolButton("S", afterRotateTool, y, h, surfaceTool.active);
            if(surfaceButton) surfaceTool.active = !surfaceTool.active;
            auto [fit, afterFit] = toolButton("Uokviri (F)", afterRotate + 12.0f, y, h);
            if(fit){ view.lookThrough = Warp::None; Loom::frameAll(stage.size() ? stage : live, frame, view.orbit); }
            auto [through, afterThrough] = toolButton("Kroz kameru (0)", afterFit, y, h, view.lookThrough != Warp::None);
            if(through){
                if(view.lookThrough != Warp::None) view.lookThrough = Warp::None;
                else{
                    const Warp::Entity* chosen = stage.get(selected);
                    view.lookThrough = chosen && chosen->camera ? selected : firstCamera();
                }
            }
            auto [plateButton, afterPlate] = toolButton("Snimka (V)", afterThrough, y, h, showPlate);
            if(plateButton) showPlate = !showPlate;
            auto [splatButton, afterSplat] = toolButton("Splat (B)", afterPlate, y, h, showSplat);
            if(splatButton) showSplat = !showSplat;
            auto [motionButton, afterMotion] = toolButton("Text->Motion", afterSplat + 12.0f, y, h);
            if(motionButton) openMotionWorkflow();
            auto [saveButton, afterSave] = toolButton(dirty ? "Spremi *" : "Spremi", afterMotion + 12.0f, y, h, dirty);
            if(saveButton) saveProjectNow();
            auto [newButton, afterNew] = toolButton("Novi", afterSave, y, h);
            if(newButton) ui.openMenu("novi");
            auto [logButton, afterLog] = toolButton("Ispis", afterNew, y, h, showLog);
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
                }else if(job.task == Loom::Task::WeaverMotion){
                    status = "Kimodo generira motion - " + Loom::humanTime(elapsed);
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
            if(ui.button("Start motion from text")) openMotionWorkflow();
            ui.label(tail(browser.at.string(), size_t(std::max(8.0f, (layout.media.width - 30.0f) / 12.0f))));
            if(ui.selectable("^ mapa iznad", false)){
                browser.at = browser.at.parent_path();
                browser.refresh();
                mediaScroll = 0.0f;
            }
            for(const fs::path& folder : browser.folders){
                if(ui.folderRow(folder.filename().string(), false)){
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
            for(const fs::path& project : browser.projects){
                if(ui.selectable("[projekt] " + project.filename().string(), project == projectPath)){
                    pendingProject = project;
                    ui.openMenu("projekt");
                }
            }
            for(const fs::path& motion : browser.motions){
                if(ui.selectable("[pokret] " + motion.filename().string(), false)){
                    importMotion(motion);
                }
            }
            for(const fs::path& model : browser.models){
                if(ui.selectable("[model] " + model.filename().string(), false)){
                    const Loom::ViewCamera camera = Loom::viewCameraFor(stage, frame, layout.viewport, view);
                    const bool wasEmpty = stage.size() == 0;
                    const Loom::ModelImportReport report = Loom::importModelAtView(stage, model, frame, camera, extent);
                    if(!report.problem.empty()) message = "model: " + report.problem;
                    else{
                        afterModelImport(report, wasEmpty);
                        char text[192];
                        std::snprintf(text, sizeof(text), "model %s: %zu cvorova, %zu mreza, %zu materijala",
                                      model.filename().string().c_str(), report.nodes, report.meshes, report.materials);
                        message = text;
                    }
                }
            }
            //Slike samo dok mapa materijala ceka sliku - inace bi popis bio pun tekstura
            if(materialState.armed()){
                for(const fs::path& image : browser.images){
                    if(ui.selectable("[slika] " + image.filename().string(), false)){
                        if(Loom::assignArmedImage(stage, materialState, image.string())) message = "mapa: " + image.filename().string();
                    }
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
            if(motionWorkflowOpen){
                ui.label("WEAVERMOTION");
                ui.value("engine", "NVIDIA Kimodo / SOMA RP v1.1");
                ui.label("TEXT TO MOTION PROMPT");
                const std::string promptLabel = motionPrompt.empty() ? "(klikni i upiši prompt)" : motionPrompt;
                if(ui.selectable(Treadle::fitText(promptLabel + (motionPromptFocused ? "|" : ""),
                                                  layout.properties.width - 28.0f, theme.textScale),
                                 motionPromptFocused)){
                    motionPromptFocused = true;
                }
                ui.label("Klikni polje pa tipkaj; Backspace briše; Enter generira.");
                ui.slider("trajanje", &motionDuration, 1.0f, 10.0f, "s");
                ui.value("tekst encoder", "LLM2Vec na CPU-u");
                const fs::path runner = fs::path(LOOM_ROOT_DIR) /
                                        "tools/weavermotion/.venv-clean/bin/kimodo_gen";
                ui.value("Kimodo runner", fs::is_regular_file(runner) ? "spreman" : "nije instaliran");
                const fs::path motion = newestMotionInBrowser();
                ui.value("zadnji BVH", motion.empty() ? "nema u ovoj mapi" : motion.filename().string());
                const fs::path biped = fs::path("/home/danijel/Downloads/Meshy_AI_Clockwork_Sentinel_biped/Meshy_AI_Clockwork_Sentinel_biped_Animation_Running_withSkin.fbx");
                ui.value("WeaverMascott", fs::exists(biped) ? "rigged FBX pronađen" : "FBX nije pronađen");
                if(ui.button("Generiraj animaciju")) startMotionGeneration();
                if(ui.button("Uvezi BVH iz mape")) importNewestMotion();
                if(ui.button("Ocisti prompt")) motionPrompt.clear();
                if(ui.button("Zatvori WeaverMotion")){
                    motionWorkflowOpen = false;
                    motionPromptFocused = false;
                }
                ui.label("Viewport zasad prikazuje animirani Kimodo kostur; Loom još ne deformira skinned FBX mesh.");
            }else if(focus == Focus::Media && selectedMedia >= 0 && selectedMedia < int(stage.media.size())){
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
                if(!media.result.empty() && fs::is_directory(fs::path(media.result) / "images") && !job.running &&
                   ui.button("Treniraj splat iz rezultata")) startTrain(media.result);
                ui.label("(isto i desnim klikom)");
            }else if(entity){
                ui.value("ime", Treadle::fitText(entity->name, 160.0f, theme.textScale));
                ui.value("vrsta", kindOf(*entity));
                //Grupa lika iz pokreta: vidi se odakle je pokret
                if(!entity->children.empty() && stage.get(entity->children.front()) &&
                   stage.get(entity->children.front())->joint && !entity->joint){
                    ui.value("pokret", Engine::WeaverMotion::poweredBy);
                }
                ui.checkbox("vidljivo", &entity->visible);
                ui.separator();
                //TRANSFORMACIJA U OVOM KADRU. Brzina vucenja je iz velicine scene: solve nema
                //metre, pa bi stalni korak u jednoj snimci bio nevidljiv, a u drugoj golem
                Warp::Transform local = stage.localAt(entity->id, frame);
                bool edited = false;
                float translation[3] = {local.translation.x, local.translation.y, local.translation.z};
                if(ui.dragVector("pomak", translation, extent.radius * 0.004f)){
                    local.translation = glm::vec3(translation[0], translation[1], translation[2]);
                    edited = true;
                }
                if(eulerFor != entity->id || eulerFrame != frame){
                    eulerCache = glm::degrees(glm::eulerAngles(local.rotation));
                    eulerFor = entity->id;
                    eulerFrame = frame;
                }
                float rotation[3] = {eulerCache.x, eulerCache.y, eulerCache.z};
                if(ui.dragVector("rotacija (st)", rotation, 0.5f)){
                    eulerCache = glm::vec3(rotation[0], rotation[1], rotation[2]);
                    local.rotation = glm::normalize(glm::quat(glm::radians(eulerCache)));
                    edited = true;
                }
                float scale[3] = {local.scale.x, local.scale.y, local.scale.z};
                const float scaleSpeed = std::max(1e-5f, (std::fabs(scale[0]) + std::fabs(scale[1]) + std::fabs(scale[2])) * 0.0015f);
                if(ui.dragVector("mjerilo", scale, scaleSpeed)){
                    local.scale = glm::vec3(scale[0], scale[1], scale[2]);
                    edited = true;
                }
                //Jednoliko mjerilo: za kocku, i za grupu solvea kad se scena svodi na metre
                float uniform = 1.0f;
                if(ui.dragFloat("sve osi x", &uniform, 0.004f) && uniform > 0.0f){
                    local.scale *= uniform;
                    edited = true;
                }
                if(edited) stage.setLocalAt(entity->id, frame, local);
                if(entity->animated()) ui.label("(os s kljucevima dobiva kljuc u ovom kadru)");
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
                    if(!entity->camera->plate.empty()){
                        ui.checkbox("snimka iza (V)", &showPlate);
                        ui.slider("svjetlina snimke", &plateBrightness, 0.0f, 1.0f);
                        char plateText[64];
                        std::snprintf(plateText, sizeof(plateText), "%lld", (long long)plateShown);
                        if(view.lookThrough == entity->id && showPlate) ui.value("kadar snimke", plateText);
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
                Loom::materialPanel(ui, stage, *entity, materialState);
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

            //Kljucevi odabranog: skok, postavljanje, brisanje
            const bool haveEntity = stage.get(selected) != nullptr;
            auto [previousKey, b1] = toolButton("<K", a5 + 16.0f, rowY, rowH);
            auto [nextKey, b2] = toolButton("K>", b1, rowY, rowH);
            auto [setKey, b3] = toolButton("Kljuc (K)", b2, rowY, rowH);
            auto [dropKey, b4] = toolButton("Obrisi kljuc", b3, rowY, rowH);
            double jump = 0.0;
            if(previousKey && haveEntity && stage.neighbourKey(selected, frame, -1, jump)) frame = jump;
            if(nextKey && haveEntity && stage.neighbourKey(selected, frame, +1, jump)) frame = jump;
            if(setKey && haveEntity) stage.keyAll(selected, std::round(frame));
            if(dropKey && haveEntity) stage.eraseKeysAt(selected, std::round(frame));

            //Raspon: od i do glave, ili cijelo - od prvog do zadnjeg kljuca u sceni
            auto [rangeFrom, c1] = toolButton("Od", b4 + 16.0f, rowY, rowH);
            auto [rangeTo, c2] = toolButton("Do", c1, rowY, rowH);
            auto [rangeAll, c3] = toolButton("Cijelo", c2, rowY, rowH);
            if(rangeFrom) stage.startFrame = std::min(std::round(frame), stage.endFrame - 1.0);
            if(rangeTo) stage.endFrame = std::max(std::round(frame), stage.startFrame + 1.0);
            if(rangeAll){
                double low = 1e18, high = -1e18;
                stage.walk([&](const Warp::Entity& e, int){
                    for(const std::vector<double>* times : {&e.translationKeys.times, &e.rotationKeys.times, &e.scaleKeys.times}){
                        if(times->empty()) continue;
                        low = std::min(low, times->front());
                        high = std::max(high, times->back());
                    }
                });
                if(high > low){ stage.startFrame = low; stage.endFrame = high; }
            }
            frame = std::clamp(frame, stage.startFrame, stage.endFrame);

            char text[96];
            std::snprintf(text, sizeof(text), "kadar %d   (%.0f - %.0f, %.0f fps)", int(std::floor(frame)),
                          stage.startFrame, stage.endFrame, stage.framesPerSecond);
            canvas.text(c3 + 16.0f, rowY + 6.0f, text, theme.text, theme.textScale);

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

        //== PLOHA IZ ODABIRA: sto je odabrano i sto se s tim moze ================================
        if(surfaceTool.active){
            const Treadle::Rect& v = layout.viewport;
            ui.panel("Ploha (S)", v.x + 10.0f, v.y + 10.0f, 300.0f);
            if(!surfaceTool.fit.valid){
                ui.label("Vuci pravokutnik preko tocaka");
                ui.label("ili gaussiana jedne plohe.");
            }else{
                char text[96];
                std::snprintf(text, sizeof(text), "%zu od %zu u ravnini", surfaceTool.fit.used, surfaceTool.fit.total);
                ui.value("tocke", text);
                if(ui.button("Kocka na plohu")){
                    selected = Loom::placeOnSurface(stage, surfaceTool, Warp::Shape::Cube);
                    focus = Focus::Entity;
                }
                if(ui.button("Ravnina na plohu")){
                    selected = Loom::placeOnSurface(stage, surfaceTool, Warp::Shape::Plane);
                    focus = Focus::Entity;
                }
                if(ui.button("Ocisti odabir")) surfaceTool = Loom::SurfaceTool{true};
            }
            if(ui.button("Zatvori")) surfaceTool.active = false;
        }

        //== IZBORNICI ============================================================================
        //Pogled nema widget koji bi javio desni klik, pa ga pita ovdje: nad pogledom, a ne nad
        //nekim stupcem ili izbornikom
        if(rightDown && !rightWasDown && layout.viewport.contains(float(cursorX), float(cursorY)) &&
           !ui.wantsMouse() && !ui.menuOpen("media") && !ui.menuOpen("entitet")){
            ui.openMenu("pogled");
            menuPixel = glm::vec2(float(cursorX), float(cursorY));
        }
        //Otvaranje, nova scena i izlaz PITAJU kad ima nespremljenog - i nude spremanje prvo
        if(ui.beginMenu("projekt")){
            const std::string name = pendingProject.filename().string();
            if(dirty){
                if(ui.menuItem("Spremi pa otvori " + name) && saveProjectNow()) openProject(pendingProject);
                if(ui.menuItem("Otvori " + name + " bez spremanja")) openProject(pendingProject);
            }else if(ui.menuItem("Otvori " + name)){
                openProject(pendingProject);
            }
            ui.menuItem("Odustani");
            ui.endMenu();
        }
        if(ui.beginMenu("novi")){
            if(dirty){
                if(ui.menuItem("Spremi pa nova scena") && saveProjectNow()) newScene();
                if(ui.menuItem("Nova scena bez spremanja")) newScene();
            }else if(ui.menuItem("Nova prazna scena")){
                newScene();
            }
            ui.menuItem("Odustani");
            ui.endMenu();
        }
        if(ui.beginMenu("izlaz")){
            ui.menuItem(projectPath.empty() ? "Scena nije spremljena." : projectPath.filename().string() + " ima nespremljene promjene.", false);
            if(job.running) ui.menuItem("(solve jos tece - izlaz ceka da zavrsi)", false);
            ui.menuSeparator();
            if(ui.menuItem("Spremi i izadji") && saveProjectNow()) quitting = true;
            if(ui.menuItem("Izadji bez spremanja")) quitting = true;
            ui.menuItem("Odustani");
            ui.endMenu();
        }
        if(ui.beginMenu("media")){
            const bool valid = menuMedia >= 0 && menuMedia < int(stage.media.size());
            if(ui.menuItem("Solve kamere (matchmove)", valid && !job.running)) startSolve(menuMedia, false);
            if(ui.menuItem("Solve + Gaussian splat", valid && !job.running)) startSolve(menuMedia, true);
            const bool hasResult = valid && !stage.media[size_t(menuMedia)].result.empty();
            if(ui.menuItem("Otvori rezultat", hasResult)){
                importFolder(stage.media[size_t(menuMedia)].result, stage.media[size_t(menuMedia)].path);
            }
            //Trening trazi slike kadrova, a solve za matchmove ih ne pise
            const bool canTrain = hasResult && fs::is_directory(fs::path(stage.media[size_t(menuMedia)].result) / "images");
            if(ui.menuItem(canTrain || !hasResult ? "Treniraj splat iz rezultata" : "Treniraj splat (solve bez slika)",
                           canTrain && !job.running)){
                jobVideo = stage.media[size_t(menuMedia)].path;
                startTrain(stage.media[size_t(menuMedia)].result);
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
            if(ui.menuItem("Dodaj kocku ovdje")) addMeshAt(Warp::Shape::Cube, Warp::None, menuPixel, true);
            if(ui.menuItem("Dodaj ravninu ovdje")) addMeshAt(Warp::Shape::Plane, Warp::None, menuPixel, true);
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
        if(leftDown && !leftWasDown && overViewport) openMotionWorkflow();
        const Loom::ViewCamera pickCamera = Loom::viewCameraFor(stage, frame, viewportRect, view);

        //Strelice odabranog: vide se i hvataju prije okretanja pogleda
        const Warp::Entity* chosen = stage.get(selected);
        Loom::Gizmo gizmo;
        if(chosen && chosen->visible && selected != view.lookThrough && focus == Focus::Entity){
            gizmo = Loom::gizmoFor(pickCamera, glm::vec3(stage.worldMatrix(selected, frame)[3]));
        }
        const glm::vec2 mouse{float(cursorX), float(cursorY)};
        gizmoAxisHot = gizmoAxisHeld >= 0 ? gizmoAxisHeld
                     : !overViewport ? -1
                     : tool == Tool::Move ? Loom::gizmoAxisAt(pickCamera, gizmo, mouse) : Loom::ringAxisAt(pickCamera, gizmo, mouse);

        //Alat plohe uzima lijevi mis u pogledu: pravokutnik umjesto odabira i okretanja
        Loom::surfaceToolMouse(surfaceTool, leftDown, leftWasDown, mouse, overViewport, stage, frame, pickCamera,
            [&](const std::function<void(const glm::vec3&)>& visit){
                //Sredista gaussiana prvog vidljivog splata, u svijet kroz njegovu grupu
                Warp::Id splatId = Warp::None;
                stage.walk([&](const Warp::Entity& e, int){ if(splatId == Warp::None && e.visible && e.splat) splatId = e.id; });
                if(splatId == Warp::None || !showSplat) return;
                const glm::mat4 world = stage.worldMatrix(splatId, frame);
                viewportSplat.forEachCentre(0.3f, [&](const glm::vec3& p){ visit(glm::vec3(world * glm::vec4(p, 1.0f))); });
            });

        //Lijevi: strelica pomice, klik bira, vucenje okrece
        if(leftDown && !leftWasDown && !surfaceTool.active){
            leftInViewport = overViewport;
            dragging = false;
            pressX = cursorX; pressY = cursorY;
            gizmoAxisHeld = overViewport ? gizmoAxisHot : -1;
        }
        if(!leftDown) gizmoAxisHeld = -1;
        if(leftDown && gizmoAxisHeld >= 0 && chosen && tool == Tool::Rotate){
            //Okretanje oko osi SVIJETA kroz srediste odabranog; u lokalnu rotaciju kroz roditelja
            const float angle = Loom::ringDrag(pickCamera, gizmo, gizmoAxisHeld, glm::vec2(float(lastX), float(lastY)), mouse);
            if(angle != 0.0f){
                const glm::quat world = rotationOf(stage.worldMatrix(selected, frame));
                const glm::quat parent = chosen->parent == Warp::None ? glm::quat(1.0f, 0.0f, 0.0f, 0.0f)
                                                                     : rotationOf(stage.worldMatrix(chosen->parent, frame));
                Warp::Transform local = stage.localAt(selected, frame);
                local.rotation = glm::normalize(glm::inverse(parent) * glm::angleAxis(angle, Loom::gizmoAxis(gizmoAxisHeld)) * world);
                stage.setLocalAt(selected, frame, local);
                eulerFrame = -1.0;              //kutovi u svojstvima se procitaju iznova
            }
            dragging = true;
        }else if(leftDown && gizmoAxisHeld >= 0 && chosen){
            const float amount = Loom::gizmoDrag(pickCamera, gizmo, gizmoAxisHeld,
                                                 glm::vec2(float(cursorX - lastX), float(cursorY - lastY)));
            if(amount != 0.0f){
                const glm::vec3 world = gizmo.origin + Loom::gizmoAxis(gizmoAxisHeld) * amount;
                const glm::mat4 parentWorld = chosen->parent == Warp::None ? glm::mat4(1.0f)
                                                                          : stage.worldMatrix(chosen->parent, frame);
                Warp::Transform local = stage.localAt(selected, frame);
                local.translation = glm::vec3(glm::inverse(parentWorld) * glm::vec4(world, 1.0f));
                stage.setLocalAt(selected, frame, local);
            }
            dragging = true;                    //nije klik: ne bira nista kad se pusti
        }else if(leftDown && leftInViewport){
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

        if(motionWorkflowOpen){
            keys.pressed(window, GLFW_KEY_SPACE);
            keys.pressed(window, GLFW_KEY_K);
            keys.pressed(window, GLFW_KEY_V);
            keys.pressed(window, GLFW_KEY_B);
            keys.pressed(window, GLFW_KEY_W);
            keys.pressed(window, GLFW_KEY_E);
            keys.pressed(window, GLFW_KEY_S);
            keys.pressed(window, GLFW_KEY_RIGHT);
            keys.pressed(window, GLFW_KEY_LEFT);
            keys.pressed(window, GLFW_KEY_HOME);
            keys.pressed(window, GLFW_KEY_END);
            keys.pressed(window, GLFW_KEY_F);
            keys.pressed(window, GLFW_KEY_0);
            keys.pressed(window, GLFW_KEY_KP_0);
            keys.pressed(window, GLFW_KEY_DELETE);
            const bool enter = keys.pressed(window, GLFW_KEY_ENTER);
            const bool keypadEnter = keys.pressed(window, GLFW_KEY_KP_ENTER);
            const bool escape = keys.pressed(window, GLFW_KEY_ESCAPE);
            if(motionPromptFocused && (enter || keypadEnter)) startMotionGeneration();
            if(escape){
                motionWorkflowOpen = false;
                motionPromptFocused = false;
            }
        }else{
        if(keys.pressed(window, GLFW_KEY_SPACE)) playing = !playing;
        if(keys.pressed(window, GLFW_KEY_K) && stage.get(selected)) stage.keyAll(selected, std::round(frame));
        if(keys.pressed(window, GLFW_KEY_V)) showPlate = !showPlate;
        if(keys.pressed(window, GLFW_KEY_B)) showSplat = !showSplat;
        if(keys.pressed(window, GLFW_KEY_W)) tool = Tool::Move;
        if(keys.pressed(window, GLFW_KEY_E)) tool = Tool::Rotate;
        const bool control = glfwGetKey(window, GLFW_KEY_LEFT_CONTROL) == GLFW_PRESS ||
                             glfwGetKey(window, GLFW_KEY_RIGHT_CONTROL) == GLFW_PRESS;
        //S sam pali alat plohe, Ctrl+S sprema - ista tipka, pa se pita jednom
        const bool sKey = keys.pressed(window, GLFW_KEY_S);
        if(sKey && control) saveProjectNow();
        else if(sKey) surfaceTool.active = !surfaceTool.active;
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
            if(ui.menuOpen("pogled") || ui.menuOpen("media") || ui.menuOpen("entitet") || ui.menuOpen("projekt") ||
               ui.menuOpen("novi") || ui.menuOpen("izlaz")) ui.closeMenu();
            else if(view.lookThrough != Warp::None) view.lookThrough = Warp::None;
        }

        }

        //== CRTANJE =================================================================================
        Treadle::DrawList scene, overlay;
        {
            const Loom::ViewCamera camera = Loom::viewCameraFor(stage, frame, viewportRect, view);
            Loom::paintStage(stage, frame, camera, view, extent, selected, scene);
            if(surfaceTool.active) Loom::paintSurfaceTool(surfaceTool, camera, overlay);
            const Warp::Entity* chosenNow = stage.get(selected);
            if(chosenNow && chosenNow->visible && selected != view.lookThrough && focus == Focus::Entity){
                const Loom::Gizmo shown = Loom::gizmoFor(camera, glm::vec3(stage.worldMatrix(selected, frame)[3]));
                if(tool == Tool::Move) Loom::paintGizmo(overlay, camera, shown, gizmoAxisHot);
                else Loom::paintRings(overlay, camera, shown, gizmoAxisHot);
            }
            if(live.size() > 0){
                Loom::ViewportState liveView = view;
                liveView.showGrid = stage.size() == 0 && view.showGrid;
                liveView.lookThrough = Warp::None;
                Loom::paintStage(live, 1.0, camera, liveView, extent, Warp::None, scene);
            }
        }

        //PLOCA: koji kadar snimke odgovara kadru timelinea, i je li stigao iz niti
        const Warp::Entity* throughEntity = stage.get(view.lookThrough);
        const bool plateWanted = showPlate && throughEntity && throughEntity->camera &&
                                 !throughEntity->camera->plate.empty();
        bool plateArrived = false;
        uint32_t plateWidth = 0, plateHeight = 0;
        if(plateWanted){
            plateStream.open(throughEntity->camera->plate);
            plateStream.request(int64_t(throughEntity->camera->plateFirstFrame) + int64_t(std::llround(frame)) - 1);
            int64_t index = -1;
            plateArrived = plateStream.take(platePixels, plateWidth, plateHeight, index);
            if(plateArrived){
                plateShown = index;
                //Tekstura se (ponovno) stvara kad se velicina promijeni - druga snimka
                if(!plateTexture || plateTexture->getExtent().width != plateWidth ||
                   plateTexture->getExtent().height != plateHeight){
                    loom.waitIdle();
                    plateMaterial.reset();
                    StreamingTextureConfig textureConfig;
                    textureConfig.format = vk::Format::eR8G8B8A8Srgb;
                    plateTexture = std::make_unique<StreamingTexture>(loom.device, loom.command,
                                                                      vk::Extent2D{plateWidth, plateHeight}, textureConfig);
                    plateMaterial = std::make_unique<Material>(loom.device, loom.command, loom.getDescriptorPool(),
                                                               platePipeline, plateTexture->getSampled());
                    plateReady = false;
                }
            }
        }

        //SPLAT: prvi vidljivi splat u sceni, kroz kameru pogleda i svjetsku matricu svog entiteta
        int framebufferWidth = 0, framebufferHeight = 0;
        glfwGetFramebufferSize(window, &framebufferWidth, &framebufferHeight);
        const float pixelScaleX = float(framebufferWidth) / float(std::max(1, windowWidth));
        bool splatActive = false;
        Treadle::Rect splatArea;
        {
            Warp::Id splatId = Warp::None;
            stage.walk([&](const Warp::Entity& e, int){ if(splatId == Warp::None && e.visible && e.splat) splatId = e.id; });
            if(showSplat && splatId != Warp::None){
                viewportSplat.want(stage.get(splatId)->splat->path);
                const Loom::ViewCamera camera = Loom::viewCameraFor(stage, frame, viewportRect, view);
                const glm::mat4 world = stage.worldMatrix(splatId, frame);
                const glm::vec3 eyeLocal = glm::vec3(glm::inverse(world) * glm::vec4(camera.eye, 1.0f));
                splatArea = camera.rect;
                splatActive = viewportSplat.prepare(camera.view * world, eyeLocal, camera.focal, camera.centre,
                                                    camera.rect, pixelScaleX);
            }
            const bool loadingNow = viewportSplat.isLoading();
            if(loadingNow && !splatWasLoading) message = "splat se cita...";
            if(!loadingNow && splatWasLoading){
                const std::string problem = viewportSplat.error();
                message = problem.empty() ? "splat u pogledu: " + std::to_string(viewportSplat.count()) + " gaussiana"
                                          : "splat se ne da procitati: " + problem;
            }
            splatWasLoading = loadingNow;
        }

        //PBR MESHEVI: modeli i tijela s materijalima, u svoju metu (LoomPbr.h)
        bool meshesActive = false;
        Treadle::Rect meshArea;
        {
            const Loom::ViewCamera camera = Loom::viewCameraFor(stage, frame, viewportRect, view);
            meshArea = camera.rect;
            const float nearPlane = std::max(1e-5f, extent.radius * 1e-3f);
            meshesActive = viewportMeshes.prepare(stage, frame, camera, pixelScaleX, nearPlane, std::max(100.0f, extent.radius * 500.0f));
            for(const std::string& problem : viewportMeshes.takeErrors()) message = "model se ne da procitati: " + problem;
        }

        if(!loom.renderer.beginFrame()) continue;
        if(splatActive) viewportSplat.compute();
        if(meshesActive) viewportMeshes.render();
        //Tek NAKON beginFrame: prsten teksture se oslanja na to da je renderer vec pricekao
        if(plateArrived && plateTexture){
            plateTexture->update(platePixels.data(), platePixels.size());
            plateMaterial->setSampledImage(plateTexture->getSampled());
            plateReady = true;
        }
        loom.renderer.beginPass();
        if(plateWanted && plateReady && plateMaterial){
            //Viewport suzen na kadar kamere; prozor i okvir mogu imati razlicite piksele (HiDPI)
            const float sx = float(framebufferWidth) / float(std::max(1, windowWidth));
            const float sy = float(framebufferHeight) / float(std::max(1, windowHeight));
            const Loom::ViewCamera through = Loom::viewCameraFor(stage, frame, viewportRect, view);
            plateMaterial->setBaseColor(glm::vec4(plateBrightness, plateBrightness, plateBrightness, 1.0f));
            const vk::raii::CommandBuffer& commands = loom.renderer.borrowCommands();
            commands.setViewport(0, vk::Viewport{through.frame.x * sx, through.frame.y * sy,
                                                 through.frame.width * sx, through.frame.height * sy, 0.0f, 1.0f});
            commands.setScissor(0, vk::Rect2D{{int32_t(through.frame.x * sx), int32_t(through.frame.y * sy)},
                                              {uint32_t(through.frame.width * sx), uint32_t(through.frame.height * sy)}});
            loom.renderer.drawFullscreen(*plateMaterial);
            commands.setViewport(0, vk::Viewport{0.0f, 0.0f, float(framebufferWidth), float(framebufferHeight), 0.0f, 1.0f});
            commands.setScissor(0, vk::Rect2D{{0, 0}, {uint32_t(framebufferWidth), uint32_t(framebufferHeight)}});
        }
        //Splat preko ploce (premultiplicirano: gdje ga nema, snimka se vidi), ispod crta scene
        if(splatActive){
            const float sx = pixelScaleX, sy = float(framebufferHeight) / float(std::max(1, windowHeight));
            viewportSplat.present(vk::Rect2D{{int32_t(splatArea.x * sx), int32_t(splatArea.y * sy)},
                                             {uint32_t(splatArea.width * sx), uint32_t(splatArea.height * sy)}},
                                  vk::Extent2D{uint32_t(framebufferWidth), uint32_t(framebufferHeight)});
        }
        //Tocke, mreza i kamere, pa meshevi PREKO njih (prekrivaju ono sto je iza), pa strelice
        if(!scene.vertices.empty()){
            scenePainter.draw(loom.renderer, scene, uint32_t(windowWidth), uint32_t(windowHeight));
        }
        if(meshesActive){
            const float sx = pixelScaleX, sy = float(framebufferHeight) / float(std::max(1, windowHeight));
            viewportMeshes.present(vk::Rect2D{{int32_t(meshArea.x * sx), int32_t(meshArea.y * sy)},
                                              {uint32_t(meshArea.width * sx), uint32_t(meshArea.height * sy)}},
                                   vk::Extent2D{uint32_t(framebufferWidth), uint32_t(framebufferHeight)});
        }
        if(!overlay.vertices.empty()){
            overlayPainter.draw(loom.renderer, overlay, uint32_t(windowWidth), uint32_t(windowHeight));
        }
        painter.draw(loom.renderer, ui.drawn(), uint32_t(windowWidth), uint32_t(windowHeight));
        loom.renderer.endPass();
        loom.renderer.endFrame();

        //Snimka: nekoliko kadrova da se raspored i scena slegnu, pa jedan u datoteku
        //Kroz kameru se ceka i da ploca stigne iz niti - inace bi snimka pokazala pogled bez nje
        const bool plateSettled = !plateWanted || !plateStream.error().empty() ||
            plateShown == int64_t(throughEntity->camera->plateFirstFrame) + int64_t(std::llround(frame)) - 1;
        //I splat mora stici iz niti: datoteka od stotina MB se cita sekundama
        splatSettledFrames = viewportSplat.isLoading() ? 0 : splatSettledFrames + 1;
        const bool splatSettled = splatSettledFrames >= 3;
        meshSettledFrames = viewportMeshes.loading() ? 0 : meshSettledFrames + 1;
        const bool meshSettled = meshSettledFrames >= 3;
        if(!shotPath.empty() && ++framesDrawn >= 6 && ((plateSettled && splatSettled && meshSettled) || framesDrawn > 3000)){
            loom.waitIdle();
            const ImageData shot = loom.renderer.readLastFrame();
            Spool::Image image = Spool::imageFromPixels(shot.pixels.data(), shot.extent.width, shot.extent.height,
                isBgraFormat(shot.format) ? Spool::ChannelOrder::BGRA : Spool::ChannelOrder::RGBA);
            //Prozor je neproziran, a alfa u swapchainu je ono sto su plohe suicelja slucajno upisale
            //(0 u panelima) - snimka bi ih pokazala bijelima. Sprema se kako se prozor VIDI
            for(size_t i = 3; i < image.pixels.size(); i += 4) image.pixels[i] = 255;
            Spool::saveImage(shotPath, image);
            std::printf("Snimljeno %s (%ux%u)\n", shotPath.c_str(), image.width, image.height);
            break;
        }
    }

    if(worker.joinable()) worker.join();
    plateStream.close();
    loom.waitIdle();
    return 0;
}
