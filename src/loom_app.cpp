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
    #include "LoomTerminal.h"
    #include "LoomPlate.h"
    #include "LoomCompositor.h"
    #include "LoomScene.h"
    #include "LoomSplat.h"
    #include "LoomSplatCut.h"
    #include "LoomUndo.h"
    #include "LoomAutosave.h"
    #include "LoomEditorTools.h"
    #include "LoomImporter.h"
    #include "LoomPbr.h"
    #include "LoomViewport.h"
    #include "LoomWeaverMotion.h"
    #include "LoomMotionPanel.h"
    #include "LoomProcedura.h"
    #include "LoomMotionLive.h"
    #include "LoomPoseBlend.h"
    #include "LoomMoodboard.h"
    #include "LoomAutoRig.h"
    
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
    #include <ctime>
    #include <fstream>
    #include <cmath>
    #include <cctype>
    #include <cstdint>
    #include <cstdio>
    #include <initializer_list>
    #include <cstdlib>
    #include <filesystem>
    #include <limits>
    #include <mutex>
    #include <set>
    #include <string>
    #include <thread>
    #include <unordered_map>
    #include <vector>
    
    namespace{
    
    namespace fs = std::filesystem;
    
    bool hasMotionBricksCheckpoints(const fs::path& motionBricksRoot){
        const auto isModelFile = [](const fs::path& path, std::uintmax_t minimumBytes){
            std::error_code error;
            return fs::is_regular_file(path, error) && fs::file_size(path, error) >= minimumBytes;
        };
        return isModelFile(motionBricksRoot / "out/G1-clip.ckpt", 1024u * 1024u) &&
            isModelFile(motionBricksRoot / "out/motionbricks_vqvae/version_1/checkpoints/model-step=2000000.ckpt", 100u * 1024u * 1024u) &&
            isModelFile(motionBricksRoot / "out/motionbricks_pose/version_1/checkpoints/model-step=2000000.ckpt", 100u * 1024u * 1024u) &&
            isModelFile(motionBricksRoot / "out/motionbricks_root/version_1/checkpoints/model-step=2000000.ckpt", 100u * 1024u * 1024u);
    }
    
    //Sto je u mapi koju media prozor pregledava. Osvjezava se kad se mapa promijeni i povremeno,
    //jer solve u pozadini stvara nove mape rezultata
    struct Browser{
        fs::path at;
        std::vector<fs::path> folders, videos, results, projects, motions, models, images, splats;
    
        void refresh(){
            folders.clear();
            projects.clear();
            splats.clear();
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
                if(entry.is_regular_file(error) && entry.path().extension() == ".ply" && Loom::isGaussianPly(entry.path().string())){
                    splats.push_back(entry.path());
                    continue;
                }
                if(!entry.is_directory(error) || name.empty() || name[0] == '.') continue;
                if(Loom::isResultDirectory(entry.path())) continue;
                folders.push_back(entry.path());
            }
            std::sort(folders.begin(), folders.end());
            std::sort(projects.begin(), projects.end());
            std::sort(splats.begin(), splats.end());
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
        if(entity.camera) return "Camera";
        if(entity.points) return "Point Cloud";
        if(entity.mesh) return entity.mesh->shape == Warp::Shape::Cube ? "Cube" : "Plane";
        if(entity.splat) return "Gaussian Splat";
        if(entity.joint) return "Joint";
        if(entity.model) return "Model (glTF)";
        return entity.children.empty() ? "Null" : "Group";
    }
    
    Treadle::Color sceneAccent(const Warp::Entity& entity){
        if(entity.camera) return {0.18f, 0.88f, 1.0f, 1.0f};
        if(entity.splat) return {0.94f, 0.42f, 0.96f, 1.0f};
        if(entity.joint) return {1.0f, 0.55f, 0.23f, 1.0f};
        if(entity.points) return {0.40f, 0.94f, 0.56f, 1.0f};
        if(entity.mesh || entity.model) return {0.31f, 0.61f, 1.0f, 1.0f};
        if(entity.animator) return {0.38f, 0.94f, 0.60f, 1.0f};
        return {0.88f, 0.72f, 0.35f, 1.0f};
    }
    
    std::string sceneTag(const Warp::Entity& entity){
        if(entity.camera) return "CAM";
        if(entity.splat) return "SPLAT";
        if(entity.joint) return "BONE";
        if(entity.points) return "POINTS";
        if(entity.mesh) return entity.mesh->shape == Warp::Shape::Cube ? "CUBE" : "PLANE";
        if(entity.model) return "MODEL";
        if(entity.animator) return "RIG";
        return entity.children.empty() ? "NODE" : "GROUP";
    }
    
    
    struct AssetStyle{ std::string badge; Treadle::Color colour; };
    
    AssetStyle assetStyle(const fs::path& path, const std::string& kind){
        std::string extension = path.extension().string();
        std::transform(extension.begin(), extension.end(), extension.begin(),
                       [](unsigned char c){ return char(std::toupper(c)); });
        if(!extension.empty() && extension.front() == '.') extension.erase(extension.begin());
        if(kind == "video"){
            if(extension == "MOV") return {extension, {0.28f, 0.55f, 1.0f, 1.0f}};
            if(extension == "MKV") return {extension, {0.72f, 0.43f, 1.0f, 1.0f}};
            if(extension == "AVI") return {extension, {1.0f, 0.32f, 0.47f, 1.0f}};
            return {extension.empty() ? "VID" : extension, {0.16f, 0.87f, 1.0f, 1.0f}};
        }
        if(kind == "image") return {extension == "JPEG" ? "JPG" : (extension.empty() ? "IMG" : extension), {1.0f, 0.30f, 0.73f, 1.0f}};
        if(kind == "project") return {"USD", {0.42f, 1.0f, 0.42f, 1.0f}};
        if(kind == "motion") return {"BVH", {1.0f, 0.52f, 0.19f, 1.0f}};
        if(kind == "model") return {extension == "GLTF" ? "GLTF" : (extension.empty() ? "3D" : extension), {0.34f, 0.58f, 1.0f, 1.0f}};
        return {"SOLV", {1.0f, 0.78f, 0.20f, 1.0f}};
    }
    
    //Rotacija iz matrice koja moze nositi i mjerilo: stupci se normiraju prije pretvorbe
    glm::quat rotationOf(const glm::mat4& m){
        return glm::normalize(glm::quat_cast(glm::mat3(glm::normalize(glm::vec3(m[0])), glm::normalize(glm::vec3(m[1])),
                                                       glm::normalize(glm::vec3(m[2])))));
    }
    
    enum class Focus{ Entity, Media };
    enum class RailPane{ None, Media, Scene, Components, Motion, Procedura, Timeline, Terminal, AiChat, Compositor };
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
        //Kapacitet: odabir plohe istakne do 12000 tocaka (po 4 vrha), plus strelice i disk plohe
        UiPainter overlayPainter(loom.device, loom.command, loom.getDescriptorPool(),
                                 loom.getColorFormat(), vk::Format::eUndefined, 1u << 17);
    
        //Argumenti: prva mapa, pa zastavice za snimku (vidi zaglavlje)
        fs::path startAt = fs::current_path();
        std::string startProject;             //loom projekt.usda otvara projekt
        std::string shotPath, shotResult, shotSave, shotMotion;
        std::string shotModel;                //--model: glTF na mjestu pogleda
        std::vector<std::string> shotMotionText;  //--tekst: panel pokreta otvoren, jedna radnja po zastavici
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
            else if(argument == "--tekst" && i + 1 < argc) shotMotionText.push_back(argv[++i]);
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
        Warp::Id lastTrailSelection = Warp::None;
        std::vector<Warp::Id> selectionTrail;
        std::set<Warp::Id> collapsed;
        int selectedMedia = -1;
        Focus focus = Focus::Entity;
        Warp::Id menuEntity = Warp::None;
        std::vector<bool> entityOrbitFavorites{false, true, false, false};
        std::vector<bool> viewOrbitFavorites{true, false, false, true, false, false, false};
        glm::vec2 menuPixel(0.0f);            //gdje je desni klik otvorio izbornik pogleda
        int motionPathMenuFrame = -1;         //poza se dodaje na kadar putanje koji je otvorio izbornik
        int menuMedia = -1;
        fs::path menuModelAsset;
    
        double frame = 1.0;
        bool playing = false;
        bool showSplat = !shotNoSplat;
        bool followAnimatorPreview = false;
    
        Loom::ViewportState view;
        Loom::SceneExtent extent;
        bool extentDirty = true;
    
        float mediaScroll = 0.0f, hierarchyScroll = 0.0f, propertiesScroll = 0.0f;
        bool outlineVisible = true, componentsVisible = true, timelineVisible = true;
        bool railHoverOpen = false;
        float railReveal = 0.0f;
        static bool compassMinimized = false;
        bool animatorExpanded = true, transformExpanded = true, cameraExpanded = true;
        bool pointsExpanded = true, splatExpanded = true, splatCutExpanded = true, materialExpanded = false;
        RailPane activeRailPane = RailPane::None;
        std::string message;                  //zadnja poruka korisniku, u alatnoj traci
        //Pokret iz teksta (LoomMotionPanel.h): panel u pogledu s radnjama, postavkama i povijescu
        Loom::MotionPanelState motionPanel;
        Loom::WeaverProceduraPanelState proceduraPanel;
        int proceduraCurveDragPoint = -1;
        glm::vec3 proceduraDragPlanePoint{0.0f};
        glm::vec3 proceduraDragPlaneNormal{0.0f, 0.0f, 1.0f};
        struct PoseEditSession{
            bool active = false;
            Warp::Id rig = Warp::None;
            size_t animation = 0;
            double frame = 0.0;
            Warp::AnimationClip original;
            std::vector<std::pair<Warp::Id, Warp::Transform>> basePose;   //zglobovi riga (id), poza pri pocetku
            std::vector<Loom::PoseKey> keys;    //uredjene poze po kadrovima; izmedju njih pretapanje
        } poseEdit;
        int poseKeyEnding = 0;                  //0: povratak u pokret iza zadnjeg kljuca, 1: drzi pozu
        float poseBlendInFrames = 8.0f;
        float poseBlendHoldFrames = 24.0f;
        float poseBlendOutFrames = 12.0f;
        struct MotionRigDragState{
            int control = -1;
            int pose = -1;
            glm::vec2 startMouse{0.0f};
            glm::vec3 startWorld{0.0f};
            glm::vec3 planeNormal{0.0f, 0.0f, 1.0f};
            glm::vec3 planeStartHit{0.0f};
            Loom::MotionPoseConstraint startPose;
        } motionRigDrag;
        struct AnimatorRigDragState{
            int control = -1;
            Warp::Id rig = Warp::None;
            double frame = 0.0;
            glm::vec2 startMouse{0.0f};
            glm::vec3 startWorld{0.0f};
            glm::vec3 planeNormal{0.0f, 0.0f, 1.0f};
            glm::vec3 planeStartHit{0.0f};
            std::array<int, 3> jointIndices{{-1, -1, -1}};
            std::array<Warp::Transform, 3> startLocal{};
            size_t jointCount = 0;
        } animatorRigDrag;
        float motionPanelScroll = 0.0f;
        struct MotionPlaybackCheckpoint{ std::string label; double first = 1.0, last = 1.0; };
        std::unordered_map<Warp::Id, std::unordered_map<size_t, std::vector<MotionPlaybackCheckpoint>>> motionPlaybackCheckpoints;
        Warp::Id motionPathAnchor = Warp::None;
        auto motionRootAnchor = [&](Warp::Id rigRoot){
            const Loom::MotionRigRestPose pose = Loom::motionRigRestPose(stage, rigRoot);
            std::array<Warp::Id, 52> ids;
            if(Loom::motionFindVerifiedUniRig52(pose, ids)) return ids[0];
            for(const Loom::MotionRigJointRest& joint : pose.joints){
                const std::string key = Loom::motionJointKey(joint.name);
                if(key == "hips" || key == "hip" || key == "pelvis") return joint.id;
            }
            return rigRoot;
        };
        auto motionDirectRigJointIds = [&](Warp::Id rigRoot){
            std::array<Warp::Id, 30> ids;
            ids.fill(Warp::None);
            const Loom::MotionRigRestPose rest = Loom::motionRigRestPose(stage, rigRoot);
            std::array<Warp::Id, 52> uniRig;
            if(Loom::motionFindVerifiedUniRig52(rest, uniRig)){
                static constexpr int directToUniRig[30] = {
                    0, 1, 2, 3, -1, -1, 5, -1, -1, -1,
                    6, 7, 8, 9, 12, 18, 25, 26, 27, 28,
                    31, 37, 44, 45, 46, 47, 48, 49, 50, 51
                };
                for(size_t i = 0; i < ids.size(); ++i)
                    if(directToUniRig[i] >= 0) ids[i] = uniRig[size_t(directToUniRig[i])];
                return ids;
            }
            std::unordered_set<Warp::Id> used;
            const auto& bones = Loom::motionDirectBones();
            for(size_t i = 0; i < bones.size(); ++i){
                const std::string key = Loom::motionJointKey(bones[i].name);
                for(const std::string& alias : Loom::motionTargetAliases(key)){
                    Warp::Id candidate = Warp::None;
                    bool ambiguous = false;
                    for(const Loom::MotionRigJointRest& joint : rest.joints){
                        if(Loom::motionJointKey(joint.name) != alias || used.count(joint.id)) continue;
                        if(candidate != Warp::None){ ambiguous = true; break; }
                        candidate = joint.id;
                    }
                    if(candidate != Warp::None && !ambiguous){
                        ids[i] = candidate;
                        used.insert(candidate);
                        break;
                    }
                }
            }
            return ids;
        };
        auto togglePlayback = [&]{
            if(!playing){
                const Warp::Id rigId = Loom::motionCharacterForEntity(stage, selected);
                const Warp::Entity* rig = stage.get(rigId);
                if(rig && rig->animator && rig->animator->enabled &&
                   rig->animator->activeAnimation < rig->animator->animations.size()){
                    const Warp::AnimationClip& clip = rig->animator->animations[rig->animator->activeAnimation];
                    if(!clip.loop && frame >= clip.endFrame - 1e-9) frame = clip.startFrame;
                }else if(frame >= stage.endFrame - 1e-9){
                    frame = stage.startFrame;
                }
            }
            playing = !playing;
        };
        fs::path generatedMotionPath;
        Warp::Id generatedMotionTarget = Warp::None;
        std::vector<Loom::MotionCharacter> sceneMotionCharacters;
        std::chrono::steady_clock::time_point sceneMotionCharactersRead{};
        Loom::PlateFloorWatch plateWatch;       //pod snimke pod likom i zakljucanost stopala (LoomPlateFloor.h)
        Loom::AutoRigState autoRig;
        float autoRigScroll = 0.0f;
    
        //-- poslovi ----------------------------------------------------------------------------------
        Loom::Job job;
        std::thread worker;
        const int steps[] = {1, 5, 10, 20};
        int stepIndex = 2;
        float frameCount = 231.0f;
        float trainSteps = 15000.0f;
        After afterJob = After::Nothing;
        std::string jobVideo;                 //snimka koja se solva, za kameru u sceni
        bool wasRunning = false;
        Loom::TerminalState terminal;
        terminal.add("Loom ready. Operations, debug output and errors appear here.");
        //Zivi snimak dok solve tece: zasebna scena, da se ne mijesa s onim sto je umjetnik slozio
        Warp::Stage live;
        auto lastRead = std::chrono::steady_clock::now();
        auto lastFrame = std::chrono::steady_clock::now();
        float smoothedFps = 0.0f;
    
        static float scrollAccumulated = 0.0f;
        glfwSetScrollCallback(window, [](GLFWwindow*, double, double y){ scrollAccumulated += float(y); });
        static bool mousePressedEvents[uint32_t(Treadle::MouseButton::Count)]{};
        static bool mouseReleasedEvents[uint32_t(Treadle::MouseButton::Count)]{};
        static float mousePressX[uint32_t(Treadle::MouseButton::Count)]{};
        static float mousePressY[uint32_t(Treadle::MouseButton::Count)]{};
        glfwSetMouseButtonCallback(window, [](GLFWwindow* source, int button, int action, int){
            if(button < 0 || button >= int(Treadle::MouseButton::Count)) return;
            if(action == GLFW_PRESS){
                mousePressedEvents[button] = true;
                double x = 0.0, y = 0.0;
                glfwGetCursorPos(source, &x, &y);
                mousePressX[button] = float(x);
                mousePressY[button] = float(y);
            }else if(action == GLFW_RELEASE){
                mouseReleasedEvents[button] = true;
            }
        });
        //Tipke za polje za tekst, s PONAVLJANJEM (GLFW_REPEAT) - drzani Backspace brise dalje
        static std::vector<Treadle::KeyEvent> typedKeys;
        glfwSetKeyCallback(window, [](GLFWwindow*, int key, int, int action, int mods){
            if(action != GLFW_PRESS && action != GLFW_REPEAT) return;
            Treadle::KeyEvent event;
            event.shift = (mods & GLFW_MOD_SHIFT) != 0;
            event.ctrl = (mods & GLFW_MOD_CONTROL) != 0;
            switch(key){
                case GLFW_KEY_LEFT: event.key = Treadle::Key::Left; break;
                case GLFW_KEY_RIGHT: event.key = Treadle::Key::Right; break;
                case GLFW_KEY_UP: event.key = Treadle::Key::Up; break;
                case GLFW_KEY_DOWN: event.key = Treadle::Key::Down; break;
                case GLFW_KEY_HOME: event.key = Treadle::Key::Home; break;
                case GLFW_KEY_END: event.key = Treadle::Key::End; break;
                case GLFW_KEY_BACKSPACE: event.key = Treadle::Key::Backspace; break;
                case GLFW_KEY_DELETE: event.key = Treadle::Key::Delete; break;
                case GLFW_KEY_ENTER: case GLFW_KEY_KP_ENTER: event.key = Treadle::Key::Enter; break;
                case GLFW_KEY_ESCAPE: event.key = Treadle::Key::Escape; break;
                case GLFW_KEY_TAB: event.key = Treadle::Key::Tab; break;
                case GLFW_KEY_A: event.key = Treadle::Key::A; break;
                case GLFW_KEY_C: event.key = Treadle::Key::C; break;
                case GLFW_KEY_X: event.key = Treadle::Key::X; break;
                case GLFW_KEY_V: event.key = Treadle::Key::V; break;
                default: return;
            }
            typedKeys.push_back(event);
        });
        ui.getClipboard = [window](){ const char* text = glfwGetClipboardString(window); return std::string(text ? text : ""); };
        ui.setClipboard = [window](const std::string& text){ glfwSetClipboardString(window, text.c_str()); };
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
        glm::mat4 transformGhostStart{1.0f};
        Warp::Id transformGhostId = Warp::None;
        bool transformGhostActive = false;
        int pathDragIndex = -1;
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
            {
                std::lock_guard<std::mutex> guard(job.lock);
                job.lines.clear();
                job.firstLineIndex = 0;
                job.nextLineIndex = 0;
                terminal.jobNextLineIndex = 0;
            }
            terminal.add("Starting operation: " + command, Loom::TerminalLevel::Running);
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
            //Za matchmove bez slika kadrova (vidi --samo-kamera u VideoSolveu); splat ih treba.
            //SPLAT UZ PUNE SLICICE (--then): VideoSolve pokrene trening cim zapise COLMAP, pa kartica
            //trenira dok procesor lokalizira medjukadrove - 5.5 minuta manje na cijeloj C0257
            char command[3000];
            std::string then;
            if(thenSplat){
                const std::string o = out.string();
                then = " --then 'cd \"" + std::string(LOOM_ROOT_DIR) + "\" && PATH=\"" + std::string(LOOM_ROOT_DIR) +
                       "/.venv/bin:$PATH\" ./.venv/bin/python tools/splat/train_splats.py \"" + o + "\" \"" + o +
                       "/images\" \"" + o + "/scena.ply\" --steps " + std::to_string(int(trainSteps)) + "'";
            }
            std::snprintf(command, sizeof(command), "./VideoSolve \"%s\" %d %d 0 \"%s\"%s%s",
                          video.string().c_str(), steps[stepIndex], int(frameCount), out.string().c_str(),
                          thenSplat ? "" : " --samo-kamera", then.c_str());
            jobVideo = video.string();
            afterJob = thenSplat ? After::ImportAndTrain : After::Import;
            live = Warp::Stage{};
            startJob(command, out.string(), Loom::Task::Solve, 0);
            message = "Solve started: " + video.filename().string();
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
            message = "Training started";
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
            std::snprintf(text, sizeof(text), "Imported %s: %zu camera keys%s", directory.filename().string().c_str(),
                          report.cameraKeys, report.upright ? ", upright" : "");
            message = text;
        };
    
    
    
        //NOVO TIJELO SJEDI NA POVRSINI SNIMKE ondje kamo se gleda: na tockama oblaka pod sredinom
        //pogleda (ili pod misem, kad je dodano desnim klikom u pogled). Kroz rijesenu kameru to je
        //stvarni zid ili stol u kadru - bas ondje gdje se provjerava drzi li se kocka snimke. Kad pod
        //pikselom nema tocaka, zraka se spusti na pod (y = 0), a kad ni to ne ide, sredina scene.
        //Velicina je iz udaljenosti (solve nema metre): desetina puta do mjesta
        auto addMeshAt = [&](Warp::Shape shape, Warp::Id parent, glm::vec2 pixel, bool usePixel){
            const char* name = shape == Warp::Shape::Cube ? "Cube" : "Plane";
            const Warp::Id id = stage.create(name, parent);
            Warp::Entity& entity = *stage.get(id);
            entity.mesh = Warp::Mesh{shape};
    
            int w = 0, h = 0;
            glfwGetWindowSize(window, &w, &h);
            const Loom::ViewCamera camera = Loom::viewCameraFor(stage, frame, Loom::layoutEditor(float(w), float(h), outlineVisible, componentsVisible, timelineVisible, terminal.visible, (motionPanel.open || proceduraPanel.open), railReveal).viewport, view);
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
    
        auto importMotion = [&](const fs::path& path, Warp::Id targetCharacter,
                                Loom::MotionCompareMode compare = Loom::MotionCompareMode::None){
            Loom::MotionPlacement placement;
            if(stage.size() > 0){
                int w = 0, h = 0;
                glfwGetWindowSize(window, &w, &h);
                const Loom::ViewCamera camera = Loom::viewCameraFor(stage, frame, Loom::layoutEditor(float(w), float(h), outlineVisible, componentsVisible, timelineVisible, terminal.visible, (motionPanel.open || proceduraPanel.open), railReveal).viewport, view);
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
                placement.animationName = Loom::motionClipName(path);
                Engine::WeaverMotion::Clip clip;
                std::string error;
                if(!Engine::WeaverMotion::readKimodoBvh(path.string(), clip, error)){ message = "Could not read motion: " + error; return; }
                const float eyeHeight = ray.origin.y;
                placement.scale = eyeHeight > 1e-4f ? eyeHeight / 1.5f
                                                    : 0.35f * std::max(1e-4f, glm::length(place - ray.origin)) /
                                                      std::max(1e-4f, Loom::motionRestHeight(clip));
                Warp::Id resolvedTarget = compare == Loom::MotionCompareMode::SourceSkeleton
                    ? Warp::None : Loom::motionCharacterForEntity(stage, targetCharacter);
                if(compare == Loom::MotionCompareMode::SourceSkeleton){
                    const Warp::Id previewTarget = Loom::motionCharacterForEntity(stage, targetCharacter);
                    if(previewTarget != Warp::None){
                        const Loom::MotionRigRestPose targetRest = Loom::motionRigRestPose(stage, previewTarget);
                        const float sourceHeight = Loom::motionRestHeight(clip);
                        if(targetRest.bounds.valid && sourceHeight > 1e-5f)
                            placement.scale = targetRest.bounds.height() / sourceHeight;
                        placement.position = glm::vec3(stage.worldMatrix(previewTarget, frame)[3]);
                        placement.position.x += 1.5f;
                        placement.position.y = 0.0f;
                    }
                    placement.animationName += " [source BVH]";
                }
                if(compare != Loom::MotionCompareMode::SourceSkeleton && resolvedTarget == Warp::None){
                    const std::vector<Loom::MotionCharacter> characters = Loom::motionCharactersIn(stage);
                    if(characters.size() == 1) resolvedTarget = characters.front().id;
                    else if(characters.size() > 1){
                        message = "Choose the character that should receive this animation.";
                        motionPanel.open = true;
                        return;
                    }
                }
                if((compare == Loom::MotionCompareMode::RigNoIk ||
                    compare == Loom::MotionCompareMode::RigWithIk) && resolvedTarget == Warp::None){
                    message = "Select a rigged character for the quality comparison.";
                    return;
                }
                bool relaxedTargetPose = false;
                if(resolvedTarget != Warp::None){
                    const Loom::MotionRigRestPose targetRest = Loom::motionRigRestPose(stage, resolvedTarget);
                    std::array<Warp::Id, 52> uniRigIds;
                    if(compare == Loom::MotionCompareMode::None &&
                       Loom::motionFindVerifiedUniRig52(targetRest, uniRigIds))
                        relaxedTargetPose = Loom::applyUniRigRelaxedRestPose(stage, resolvedTarget);
                    placement.parent = resolvedTarget;
                    placement.position = glm::vec3(0.0f);
                    placement.scale = 1.0f;
                    placement.fitToParentRig = true;
                    placement.footContactIK = compare == Loom::MotionCompareMode::RigNoIk ? false :
                        compare == Loom::MotionCompareMode::RigWithIk ? true : motionPanel.effectiveFootContactIK();
                    if(compare == Loom::MotionCompareMode::RigNoIk) placement.animationName += " [rig, no IK]";
                    if(compare == Loom::MotionCompareMode::RigWithIk) placement.animationName += " [rig, IK]";
                }
                const std::string importName = path.stem().string() +
                    (compare == Loom::MotionCompareMode::SourceSkeleton ? " [source BVH]" : "");
                const Loom::WeaverMotionImportReport report = Loom::importWeaverMotionClip(stage, clip, importName, placement);
                if(!report.problem.empty()){ message = "Motion: " + report.problem; return; }
                size_t authoredDetails = 0;
                if(placement.fitToParentRig){
                    std::vector<Loom::MotionActionInterval> intervals;
                    size_t firstSourceFrame = 0;
                    for(const Loom::MotionAction& action : Loom::motionActionsForClip(path)){
                        const size_t count = size_t(std::max(1, Loom::kimodoMotionFrameCount({action})));
                        if(firstSourceFrame >= clip.frames.size()) break;
                        intervals.push_back({action.prompt, firstSourceFrame,
                                             std::min(clip.frames.size() - 1, firstSourceFrame + count - 1)});
                        firstSourceFrame += count;
                    }
                    if(compare == Loom::MotionCompareMode::None)
                        authoredDetails = Loom::applyUniRigActionDetails(
                            stage, placement.parent, clip, intervals, report.firstFrame,
                            placement.sceneFps / std::max(1e-6, clip.framesPerSecond));
                    Warp::Entity* checkRig = stage.get(placement.parent);
                    if(checkRig && checkRig->animator){
                        auto& checkpoints = motionPlaybackCheckpoints[placement.parent][checkRig->animator->activeAnimation];
                        checkpoints.clear();
                        const double sceneStep = placement.sceneFps / std::max(1e-6, clip.framesPerSecond);
                        for(const Loom::MotionActionInterval& interval : intervals){
                            std::string label = interval.prompt;
                            std::string lower = label;
                            std::transform(lower.begin(), lower.end(), lower.begin(),
                                [](unsigned char c){ return char(std::tolower(c)); });
                            if(lower.find("peace") != std::string::npos || lower.find("victory sign") != std::string::npos)
                                label = "Peace sign";
                            else if(lower.find("forward") != std::string::npos && lower.find("roll") != std::string::npos)
                                label = "Forward roll";
                            else if(lower.find("walk") != std::string::npos && lower.find("wave") != std::string::npos)
                                label = "Walk + wave";
                            else if(lower.find("wave") != std::string::npos) label = "Wave";
                            else if(lower.find("walk") != std::string::npos) label = "Walk";
                            checkpoints.push_back({label,
                                report.firstFrame + double(interval.first) * sceneStep,
                                report.firstFrame + double(interval.last) * sceneStep});
                        }
                    }
                }
                selected = report.group;
                collapsed.insert(report.root);
                if(placement.fitToParentRig){ frame = report.firstFrame; playing = false; followAnimatorPreview = true; }
                else if(compare == Loom::MotionCompareMode::SourceSkeleton){ frame = report.firstFrame; playing = false; }
                char text[384];
                if(placement.fitToParentRig){
                    std::snprintf(text, sizeof(text),
                                  "Kimodo: %zu source joints; frames %.0f-%.0f; mapped %zu bones (%s); scale %.3f; floor %s",
                                  report.joints, report.firstFrame, report.lastFrame, report.mappedJoints,
                                  report.rigProfile.c_str(), report.importedScale, report.floorSource.c_str());
                }else{
                    std::snprintf(text, sizeof(text), "Motion (NVIDIA Kimodo): %zu joints, frames %.0f-%.0f, scale %.3f",
                                  report.joints, report.firstFrame, report.lastFrame, report.importedScale);
                }
                message = text;
                if(relaxedTargetPose) message += "; natural arm and finger pose enabled";
                if(authoredDetails > 0)
                    message += "; " + std::to_string(authoredDetails) + " authored gesture/roll details applied";
                if(report.footContactFrames > 0)
                    message += "; automatic foot IK on " + std::to_string(report.footContactFrames) + " contact frames";
            }else{
                const Loom::WeaverMotionImportReport report = Loom::importWeaverMotionBvh(stage, path, placement);
                if(!report.problem.empty()){ message = "Could not read motion: " + report.problem; return; }
                selected = report.group;
                collapsed.insert(report.root);
                frame = stage.startFrame;
                extentDirty = true;
                view.lookThrough = Warp::None;
                view.orbit.target = glm::vec3(0.0f, report.height * 0.5f, 0.0f);
                view.orbit.distance = std::max(1.0f, report.height * 2.5f);
                char text[256];
                std::snprintf(text, sizeof(text), "Motion (NVIDIA Kimodo): %zu joints, %zu frames @ %.0f fps",
                              report.joints, report.frames, stage.framesPerSecond);
                message = text;
            }
            focus = Focus::Entity;
        };
    
        auto importNewestMotion = [&](){
            const fs::path motion = newestMotionInBrowser();
            if(motion.empty()){
                message = "No BVH files to import in the current folder.";
                return;
            }
            importMotion(motion, motionPanel.targetCharacter);
            if(!message.empty()) message = "WeaverMotion: " + message;
        };
    
        //Mapa u koju Kimodo pise: "WeaverMotion" uz mapu koju media prozor pregledava (ili ta mapa sama)
        auto motionDirectory = [&](){
            return browser.at.filename() == "WeaverMotion" ? browser.at : browser.at / "WeaverMotion";
        };
    
        bool generatedMotionIsBricks = false;
        Loom::MotionBricksLiveSession motionLive;
        auto startMotionGeneration = [&](){
            if(job.running){
                message = "Another Loom job is still running.";
                return;
            }
            Loom::MotionRequest request = motionPanel.request();
            if(!request.allowFastPath && !request.rootWaypoints.empty()){
                const std::string prompt = request.actions.empty() ? std::string{} : request.actions.front().prompt;
                const std::string speedProblem = Loom::motionPathSpeedProblem(request.rootWaypoints, request.smoothRootPath, prompt);
                if(!speedProblem.empty()){ message = speedProblem; return; }
            }
            if(!request.directedFlow && !request.rootWaypoints.empty() && !request.constraints.empty()){
                message = "Choose either the authored root path or an external constraints JSON, not both.";
                return;
            }
            if(request.directedFlow){
                const int last = Loom::kimodoMotionLastFrame(request.actions);
                const std::string poseProblem = Loom::motionDirectPoseProblem(request.poseConstraints, last);
                if(!poseProblem.empty()){ message = poseProblem; return; }
                if(request.rootWaypoints.empty() && request.poseConstraints.empty()){
                    message = "Add a route or pose key before generating.";
                    return;
                }
            }
            if(Loom::filledActions(request.actions).empty()){
                message = "Enter a motion prompt before generating.";
                ui.focusTextField(request.directedFlow ? "directed-motion-prompt" : "action0");
                return;
            }
            if(!request.constraints.empty() && !fs::is_regular_file(request.constraints)){
                message = "Kimodo constraints file does not exist: " + request.constraints.string();
                return;
            }
            const fs::path runner = fs::path(LOOM_ROOT_DIR) / "tools/weavermotion/.venv-clean/bin/python";
            const fs::path adapter = fs::path(LOOM_ROOT_DIR) / "tools/weavermotion/kimodo_cli.py";
            const fs::path installedKimodo = fs::path(LOOM_ROOT_DIR) / "tools/weavermotion/.venv-clean/bin/kimodo_gen";
            if(!fs::is_regular_file(runner) || !fs::is_regular_file(adapter) || !fs::is_regular_file(installedKimodo)){
                message = "Kimodo runner is not installed; see tools/weavermotion/README.md.";
                return;
            }
            const fs::path outputDirectory = motionDirectory();
            std::error_code error;
            fs::create_directories(outputDirectory, error);
            if(error){
                message = "Could not create WeaverMotion output folder: " + error.message();
                return;
            }
            const auto stamp = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch()).count();
            const fs::path outputStem = outputDirectory / ("motion_" + std::to_string(stamp));
            if(request.directedFlow || !request.rootWaypoints.empty()){
                std::string problem;
                request.constraints = outputStem.string() + ".constraints.json";
                const bool saved = request.directedFlow
                    ? Loom::writeMotionDirectedConstraints(request.constraints, request.rootWaypoints,
                        request.poseConstraints, request.constrainRootHeading,
                        Loom::kimodoMotionLastFrame(request.actions), problem, request.smoothRootPath)
                    : Loom::writeMotionRootConstraints(request.constraints, request.rootWaypoints,
                        request.constrainRootHeading, Loom::kimodoMotionLastFrame(request.actions),
                        problem, request.smoothRootPath);
                if(!saved){ message = problem; return; }
            }
            generatedMotionPath = outputStem;
            generatedMotionTarget = request.targetCharacter;
            generatedMotionIsBricks = false;
            //Opis uz BVH, za povijest u panelu
            Loom::writeMotionSidecar(outputStem, request);
            motionPanel.allowFastPath = false; // Override applies to this generation only.
            afterJob = After::Nothing;
            startJob(Loom::buildMotionCommand(runner, request, outputStem, adapter), outputDirectory.string(), Loom::Task::WeaverMotion, 0);
            message = "Kimodo runs on the GPU; the LLM2Vec encoder runs on the CPU.";
        };
    
        auto startMotionBricksGeneration = [&](){
            if(job.running){ message = "Another Loom job is still running."; return; }
            const fs::path root(LOOM_ROOT_DIR);
            const fs::path adapter = root / "tools/motionbricks/generate.py";
            const fs::path python = root / "tools/motionbricks/.venv/bin/python";
            const fs::path upstream = root / "tools/motionbricks/vendor/GR00T-WholeBodyControl/motionbricks";
            if(!fs::is_regular_file(python) || !fs::is_regular_file(adapter) ||
               !hasMotionBricksCheckpoints(upstream)){
                message = "MotionBricks needs its official G1 checkpoints. Run tools/motionbricks/setup.sh.";
                return;
            }
            if(motionPanel.rootPathEnabled && !motionPanel.constraintsPath.empty()){
                message = "Choose either the authored path or an external constraints JSON.";
                return;
            }
            const fs::path outputDirectory = motionDirectory();
            std::error_code error;
            fs::create_directories(outputDirectory, error);
            if(error){ message = "Could not create motion output folder: " + error.message(); return; }
            const auto stamp = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch()).count();
            const fs::path outputStem = outputDirectory / ("bricks_" + std::to_string(stamp));
            fs::path constraints = motionPanel.constraintsPath;
            if(motionPanel.rootPathEnabled){
                constraints = outputStem.string() + ".constraints.json";
                std::string problem;
                if(!Loom::writeMotionRootConstraints(constraints, motionPanel.rootWaypoints,
                    motionPanel.constrainRootHeading, Loom::kimodoMotionLastFrame(motionPanel.actions),
                    problem, motionPanel.smoothRootPath)){
                    message = problem; return;
                }
            }
            const std::vector<std::string>& styles = Loom::motionBricksStyleIds();
            const int style = std::clamp(motionPanel.motionBricksStyle, 0, int(styles.size()) - 1);
            float duration = 0.0f;
            for(const Loom::MotionAction& step : motionPanel.actions)
                duration += std::clamp(step.duration, 1.0f, 10.0f);
            duration = std::clamp(duration, 1.0f, 30.0f);
            const fs::path outputBvh = outputStem.string() + ".bvh";
            std::string command = Loom::shellQuoteArgument(python.string()) + " -u " +
                Loom::shellQuoteArgument(adapter.string()) + " --upstream " +
                Loom::shellQuoteArgument(upstream.string()) + " --output " +
                Loom::shellQuoteArgument(outputBvh.string()) + " --style " +
                Loom::shellQuoteArgument(styles[size_t(style)]) + " --duration " + std::to_string(duration);
            if(!constraints.empty())
                command += " --constraints " + Loom::shellQuoteArgument(constraints.string());
            if(motionPanel.fixedSeed)
                command += " --seed " + std::to_string(std::max(0, int(std::lround(motionPanel.seed))));
            std::ofstream sidecar(outputStem.string() + ".txt");
            sidecar << "MotionBricks " << styles[size_t(style)] << "\t" << duration << "\n";
            generatedMotionPath = outputStem;
            generatedMotionTarget = motionPanel.targetCharacter;
            generatedMotionIsBricks = true;
            afterJob = After::Nothing;
            startJob(command, outputDirectory.string(), Loom::Task::WeaverMotion, 0);
            message = "MotionBricks is generating a G1 locomotion clip.";
        };
    
        auto livePathControl = [&](int pathFrame){
            float dx = 0.0f, dz = 0.0f, speed = 0.0f, heading = motionPanel.firstHeadingAngle;
            if(motionPanel.rootPathEnabled && motionPanel.rootWaypoints.size() >= 2){
                const Loom::MotionRootWaypoint here = Loom::motionRootPathAt(
                    motionPanel.rootWaypoints, float(pathFrame), motionPanel.smoothRootPath);
                const Loom::MotionRootWaypoint ahead = Loom::motionRootPathAt(
                    motionPanel.rootWaypoints, float(pathFrame + 15), motionPanel.smoothRootPath);
                dx = ahead.x - here.x;
                dz = ahead.z - here.z;
                const float distance = std::hypot(dx, dz);
                speed = distance * 2.0f;
                if(motionPanel.constrainRootHeading) heading = here.heading;
                else if(distance > 1e-4f) heading = std::atan2(dx, dz);
                else heading = here.heading;
                if(!motionPanel.constrainRootHeading && distance <= 1e-4f && pathFrame > 0){
                    const Loom::MotionRootWaypoint before = Loom::motionRootPathAt(
                        motionPanel.rootWaypoints, float(std::max(0, pathFrame - 15)), motionPanel.smoothRootPath);
                    dx = here.x - before.x;
                    dz = here.z - before.z;
                    const float priorDistance = std::hypot(dx, dz);
                    if(priorDistance > 1e-4f && !motionPanel.constrainRootHeading) heading = std::atan2(dx, dz);
                }
            }
            return std::array<float, 4>{dx, dz, std::clamp(speed, 0.0f, 4.0f), heading};
        };
    
        auto recordMotionBricksPathThrough = [&](size_t sampleCount){
            while(motionLive.pathSamples.size() < sampleCount){
                const size_t index = motionLive.pathSamples.size();
                const bool active = motionPanel.rootPathEnabled && motionPanel.rootWaypoints.size() >= 2;
                motionLive.pathSampleValid.push_back(active);
                if(active){
                    const Loom::MotionRootWaypoint point = Loom::motionRootPathAt(
                        motionPanel.rootWaypoints, float(index), motionPanel.smoothRootPath);
                    motionLive.pathSamples.emplace_back(point.x, 0.0f, point.z);
                    motionLive.pathDriven = true;
                }else motionLive.pathSamples.emplace_back(0.0f);
            }
        };
    
        auto motionBricksPathRootTranslation = [&](const glm::vec3& pathOffset, float fitScale){
            const Warp::Entity* rig = stage.get(motionLive.character);
            if(!rig) return glm::vec3(0.0f);
            const glm::quat rigWorldRotation = Loom::motionRotationOf(
                stage.worldMatrix(motionLive.character, motionLive.startFrame));
            const glm::mat4 parentWorld = rig->parent != Warp::None
                ? stage.worldMatrix(rig->parent, motionLive.startFrame) : glm::mat4(1.0f);
            const glm::mat4 parentWorldInverse = glm::inverse(parentWorld);
            const glm::vec3 sourceDelta(pathOffset.x, 0.0f, pathOffset.z);
            const glm::vec3 worldDelta = rigWorldRotation * sourceDelta * fitScale;
            const glm::vec3 parentDelta = glm::vec3(parentWorldInverse * glm::vec4(worldDelta, 0.0f));
            return rig->local.translation + parentDelta;
        };
    
        auto sendMotionBricksLiveControl = [&](bool stop){
            if(motionLive.directory.empty()) return false;
            const std::vector<std::string>& styles = Loom::motionBricksStyleIds();
            const size_t styleIndex = size_t(std::clamp(motionPanel.motionBricksStyle, 0, int(styles.size()) - 1));
            const int pathFrame = int(motionLive.recordedFrames);
            const std::array<float, 4> path = livePathControl(pathFrame);
            return Loom::writeMotionBricksLiveControl(motionLive, stop, pathFrame, styles[styleIndex],
                path[0], path[1], path[2], path[3], motionLive.seed);
        };
    
        auto startMotionBricksLive = [&](){
            if(motionLive.active){ message = "A MotionBricks recording is already active."; return; }
            if(job.running){ message = "Wait for the current Loom job before starting live motion."; return; }
            const fs::path root(LOOM_ROOT_DIR);
            const fs::path python = root / "tools/motionbricks/.venv/bin/python";
            const fs::path script = root / "tools/motionbricks/realtime.py";
            const fs::path upstream = root / "tools/motionbricks/vendor/GR00T-WholeBodyControl/motionbricks";
            if(!fs::is_regular_file(python) || !fs::is_regular_file(script) || !hasMotionBricksCheckpoints(upstream)){
                message = "MotionBricks realtime requires its Python environment and G1 checkpoints; see tools/motionbricks/setup.sh.";
                return;
            }
            if(!Loom::motionPresetSettings(motionPanel.locomotionPreset).realtime){
                message = "This preset uses Kimodo text generation; choose Idle, Walk, Crawl, or Crouch for MotionBricks live.";
                return;
            }
            Warp::Id character = Loom::motionCharacterForEntity(stage, motionPanel.targetCharacter);
            if(character == Warp::None){
                const std::vector<Loom::MotionCharacter> characters = Loom::motionCharactersIn(stage);
                if(characters.size() == 1) character = characters.front().id;
            }
            if(character == Warp::None){ message = "Choose one rigged humanoid before recording live motion."; return; }
            if(motionPanel.rootPathEnabled){
                const std::string pathProblem = Loom::motionRootPathProblem(
                    motionPanel.rootWaypoints, Loom::kimodoMotionLastFrame(motionPanel.actions));
                if(!pathProblem.empty()){ message = "Live route: " + pathProblem; return; }
            }
            if(!Loom::ensureRigAnimator(stage, character)){
                message = "Could not add an Animator to the selected character.";
                return;
            }
            const fs::path outputDirectory = motionDirectory();
            std::error_code error;
            fs::create_directories(outputDirectory, error);
            if(error){ message = "Could not create WeaverMotion folder: " + error.message(); return; }
            const auto stamp = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch()).count();
            const fs::path sessionDirectory = fs::temp_directory_path() /
                ("loom-motionbricks-live-" + std::to_string(stamp));
            motionLive = Loom::MotionBricksLiveSession{};
            motionLive.directory = sessionDirectory;
            motionLive.controlFile = sessionDirectory / "control.json";
            motionLive.poseFile = sessionDirectory / "pose.bvh";
            motionLive.sequenceFile = sessionDirectory / "pose.seq";
            motionLive.statusFile = sessionDirectory / "status.txt";
            motionLive.logFile = sessionDirectory / "motionbricks.log";
            motionLive.character = character;
            motionLive.startFrame = std::round(frame);
            motionLive.footContactIK = motionPanel.effectiveFootContactIK();
            motionLive.seed = motionPanel.fixedSeed ? std::max(0, int(std::lround(motionPanel.seed)))
                                                    : int(stamp % 2147483647);
            motionLive.outputFile = outputDirectory / ("motionbricks_live_" + std::to_string(stamp) + ".bvh");
            recordMotionBricksPathThrough(1);
            Warp::Entity* rig = stage.get(character);
            if(!rig || !rig->animator){ message = "The selected Animator disappeared."; return; }
            Warp::AnimationClip clip;
            const Loom::MotionPresetInfo& preset = Loom::motionPresetSettings(motionPanel.locomotionPreset);
            clip.name = std::string("MotionBricks - ") + preset.label;
            clip.startFrame = motionLive.startFrame;
            clip.endFrame = motionLive.startFrame;
            clip.loop = false;
            clip.inPlace = false;
            rig->animator->animations.push_back(std::move(clip));
            motionLive.animationIndex = rig->animator->animations.size() - 1;
            rig->animator->activeAnimation = motionLive.animationIndex;
            motionLive.active = true;
            motionLive.ready = false;
            selected = character;
            motionPanel.targetCharacter = character;
            playing = false;
            followAnimatorPreview = true;
            frame = motionLive.startFrame;
            if(!sendMotionBricksLiveControl(false)){
                motionLive.active = false;
                rig->animator->animations.pop_back();
                message = "Could not write MotionBricks live control file.";
                return;
            }
            const std::string command = "nohup " + Loom::shellQuoteArgument(python.string()) + " -u " +
                Loom::shellQuoteArgument(script.string()) + " --upstream " + Loom::shellQuoteArgument(upstream.string()) +
                " --session " + Loom::shellQuoteArgument(sessionDirectory.string()) + " --output " +
                Loom::shellQuoteArgument(motionLive.outputFile.string()) + " --seed " + std::to_string(motionLive.seed) +
                " > " + Loom::shellQuoteArgument(motionLive.logFile.string()) + " 2>&1 < /dev/null &";
            if(std::system(command.c_str()) != 0){
                motionLive.active = false;
                rig->animator->animations.pop_back();
                message = "Could not start the MotionBricks realtime process.";
                return;
            }
            std::ofstream sidecar(motionLive.outputFile.string() + ".txt");
            const std::vector<std::string>& recipeStyles = Loom::motionBricksStyleIds();
            sidecar << "# movement_preset\t" << preset.label << "\n";
            sidecar << "# motionbricks_style\t" << recipeStyles[size_t(std::clamp(motionPanel.motionBricksStyle, 0, int(recipeStyles.size()) - 1))] << "\n";
            sidecar << "# contact_settings\t" << (motionPanel.contactSettingsMode == 0 ? "automatic" : "manual") << "\n";
            sidecar << "# foot_contact_ik\t" << (motionLive.footContactIK ? "on" : "off") << "\n";
            sidecar << "# kimodo_postprocess\tnot_applicable\n";
            sidecar << "# root_path\t" << (motionPanel.rootPathEnabled ? "authored"
                : !motionPanel.constraintsPath.empty() ? "external constraints" : "none") << "\n";
            sidecar << "MotionBricks live " << preset.label << "\t"
                    << motionPanel.actions[size_t(std::clamp(motionPanel.activeAction, 0, int(motionPanel.actions.size()) - 1))].duration << "\n";
            message = "MotionBricks is initializing; the character will animate and record as soon as G1 is ready.";
        };
    
        auto stopMotionBricksLive = [&](){
            if(!motionLive.active || motionLive.stopRequested) return;
            motionLive.stopRequested = true;
            sendMotionBricksLiveControl(true);
            motionLive.message = "Saving recorded BVH...";
        };
    
        auto tickMotionBricksLive = [&](){
            if(!motionLive.active) return;
            if(!motionLive.stopRequested) motionLive.footContactIK = motionPanel.effectiveFootContactIK();
            Loom::refreshMotionBricksLiveStatus(motionLive);
            if(motionLive.failed){
                motionLive.active = false;
                message = "MotionBricks live failed: " + motionLive.message + "; log: " + motionLive.logFile.string();
                terminal.visible = true;
                return;
            }
            Warp::Entity* rig = stage.get(motionLive.character);
            if(!rig || !rig->animator || motionLive.animationIndex >= rig->animator->animations.size()){
                stopMotionBricksLive();
                message = "The live MotionBricks character or Animator was removed; saving the take.";
                return;
            }
            if(motionLive.complete){
                const fs::path finalBvh = motionLive.outputFile;
                if(!fs::is_regular_file(finalBvh)){
                    motionLive.active = false;
                    message = "MotionBricks stopped without a final BVH; log: " + motionLive.logFile.string();
                    terminal.visible = true;
                    return;
                }
                recordMotionBricksPathThrough(motionLive.recordedFrames);
                const size_t partialIndex = motionLive.animationIndex;
                const size_t previousCount = rig->animator->animations.size();
                motionPanel.targetCharacter = motionLive.character;
                if(motionPanel.contactSettingsMode != 0) motionPanel.footContactIK = motionLive.footContactIK;
                frame = motionLive.startFrame;
                importMotion(finalBvh, motionLive.character);
                rig = stage.get(motionLive.character);
                const bool imported = rig && rig->animator && rig->animator->animations.size() > previousCount;
                if(imported){
                    Warp::Animator& animator = *rig->animator;
                    const size_t activeBeforeErase = animator.activeAnimation;
                    if(partialIndex < animator.animations.size()){
                        animator.animations.erase(animator.animations.begin() + partialIndex);
                        if(activeBeforeErase > partialIndex) animator.activeAnimation = activeBeforeErase - 1;
                        else if(activeBeforeErase == partialIndex && !animator.animations.empty())
                            animator.activeAnimation = std::min(partialIndex, animator.animations.size() - 1);
                    }
                    auto& checkpointMap = motionPlaybackCheckpoints[motionLive.character];
                    std::unordered_map<size_t, std::vector<MotionPlaybackCheckpoint>> remappedCheckpoints;
                    for(auto& [index, checkpoints] : checkpointMap){
                        if(index == partialIndex) continue;
                        remappedCheckpoints[index > partialIndex ? index - 1 : index] = std::move(checkpoints);
                    }
                    checkpointMap = std::move(remappedCheckpoints);
                    if(animator.activeAnimation < animator.animations.size()){
                        Warp::AnimationClip& finalClip = animator.animations[animator.activeAnimation];
                        frame = finalClip.startFrame;
                        stage.endFrame = std::max(stage.endFrame, finalClip.endFrame);
                        if(motionLive.pathDriven){
                            auto rootTrack = std::find_if(finalClip.tracks.begin(), finalClip.tracks.end(), [&](const Warp::AnimatorTrack& track){
                                return track.target == motionLive.character && track.rootMotion;
                            });
                            if(rootTrack != finalClip.tracks.end()){
                                Engine::WeaverMotion::Clip fullSource;
                                std::string bvhProblem;
                                if(Engine::WeaverMotion::readKimodoBvh(finalBvh.string(), fullSource, bvhProblem)){
                                    const double stepFrames = stage.framesPerSecond / std::max(1e-6, fullSource.framesPerSecond);
                                    const size_t count = std::min(fullSource.frames.size(), motionLive.pathSamples.size());
                                    for(size_t sample = 0; sample < count; ++sample){
                                        if(!motionLive.pathSampleValid[sample]) continue;
                                        const double keyFrame = finalClip.startFrame + double(sample) * stepFrames;
                                        rootTrack->translationKeys.set(keyFrame, motionBricksPathRootTranslation(
                                            motionLive.pathSamples[sample], motionLive.rigFitScale));
                                    }
                                }
                            }
                        }
                    }
                    selected = motionLive.character;
                    followAnimatorPreview = true;
                    motionPanel.open = true;
                    browser.refresh();
                    message = "MotionBricks take saved to the Animator and full BVH: " + finalBvh.filename().string();
                }else{
                    motionLive.active = false;
                    message = "MotionBricks BVH was saved, but Loom could not finalize the Animator clip: " + finalBvh.string();
                    terminal.visible = true;
                }
                motionLive.active = false;
                return;
            }
            if(!motionLive.stopRequested) sendMotionBricksLiveControl(false);
            Engine::WeaverMotion::Clip source;
            long sequence = -1;
            std::string problem;
            if(!Loom::readMotionBricksLivePose(motionLive, source, sequence, problem)){
                if(!problem.empty() && problem != "could not open BVH file") motionLive.message = problem;
                return;
            }
            if(sequence <= 0 || source.frames.size() < 2) return;
            recordMotionBricksPathThrough(size_t(sequence) + 1);
            const Loom::MotionRigRestPose rest = Loom::motionRigRestPose(stage, motionLive.character);
            struct SavedJointTracks{
                Warp::Id id;
                Warp::Track<glm::vec3> translation, scale;
                Warp::Track<glm::quat> rotation;
            };
            std::vector<SavedJointTracks> saved;
            saved.reserve(rest.joints.size());
            for(const Loom::MotionRigJointRest& joint : rest.joints){
                Warp::Entity* entity = stage.get(joint.id);
                if(!entity) continue;
                saved.push_back({joint.id, entity->translationKeys, entity->scaleKeys, entity->rotationKeys});
                entity->translationKeys = {};
                entity->scaleKeys = {};
                entity->rotationKeys = {};
            }
            const double step = double(stage.framesPerSecond) / Loom::kimodoMotionFps;
            const double first = motionLive.startFrame + double(sequence - 1) * step;
            const double second = motionLive.startFrame + double(sequence) * step;
            Loom::MotionRigFit fit;
            Loom::MotionRigMapping mapping;
            Warp::Track<glm::vec3> rootMotionKeys;
            const bool retargeted = Loom::retargetMotionToRig(stage, source, motionLive.character, first, step,
                fit, mapping, problem, rootMotionKeys, motionLive.footContactIK);
            if(retargeted){
                motionLive.rigFitScale = fit.scale;
                Warp::AnimationClip& clip = rig->animator->animations[motionLive.animationIndex];
                auto findTrack = [&](Warp::Id target, bool rootMotion) -> Warp::AnimatorTrack*{
                    auto found = std::find_if(clip.tracks.begin(), clip.tracks.end(), [&](const Warp::AnimatorTrack& track){
                        return track.target == target && track.rootMotion == rootMotion;
                    });
                    if(found != clip.tracks.end()) return &*found;
                    Warp::AnimatorTrack track;
                    track.target = target;
                    track.targetPath = stage.path(target);
                    track.rootMotion = rootMotion;
                    clip.tracks.push_back(std::move(track));
                    return &clip.tracks.back();
                };
                if(!rootMotionKeys.empty()){
                    Warp::AnimatorTrack* track = findTrack(motionLive.character, true);
                    if(sequence == 1) track->translationKeys.set(first, rootMotionKeys.at(first));
                    const size_t pathIndex = size_t(sequence);
                    const glm::vec3 currentRoot = motionLive.pathDriven && pathIndex < motionLive.pathSamples.size() &&
                        motionLive.pathSampleValid[pathIndex]
                        ? motionBricksPathRootTranslation(motionLive.pathSamples[pathIndex], fit.scale)
                        : rootMotionKeys.at(second);
                    track->translationKeys.set(second, currentRoot);
                    if(sequence == 1 && motionLive.pathDriven && !motionLive.pathSamples.empty() &&
                       motionLive.pathSampleValid[0])
                        track->translationKeys.set(first,
                            motionBricksPathRootTranslation(motionLive.pathSamples[0], fit.scale));
                }
                for(const Loom::MotionRigJointRest& joint : rest.joints){
                    Warp::Entity* entity = stage.get(joint.id);
                    if(!entity) continue;
                    Warp::AnimatorTrack* track = nullptr;
                    if(!entity->translationKeys.empty() || !entity->rotationKeys.empty() || !entity->scaleKeys.empty())
                        track = findTrack(entity->id, false);
                    if(!track) continue;
                    if(!entity->translationKeys.empty()){
                        if(sequence == 1) track->translationKeys.set(first, entity->translationKeys.at(first));
                        track->translationKeys.set(second, entity->translationKeys.at(second));
                    }
                    if(!entity->rotationKeys.empty()){
                        if(sequence == 1) track->rotationKeys.set(first, entity->rotationKeys.at(first));
                        track->rotationKeys.set(second, entity->rotationKeys.at(second));
                    }
                    if(!entity->scaleKeys.empty()){
                        if(sequence == 1) track->scaleKeys.set(first, entity->scaleKeys.at(first));
                        track->scaleKeys.set(second, entity->scaleKeys.at(second));
                    }
                }
                clip.endFrame = std::max(clip.endFrame, second);
                stage.endFrame = std::max(stage.endFrame, second);
                frame = second;
                selected = motionLive.character;
                followAnimatorPreview = true;
            }else if(!problem.empty()){
                motionLive.message = "Retarget warning: " + problem;
            }
            for(const SavedJointTracks& old : saved) if(Warp::Entity* entity = stage.get(old.id)){
                entity->translationKeys = old.translation;
                entity->scaleKeys = old.scale;
                entity->rotationKeys = old.rotation;
            }
        };
    
        auto openAutoRig = [&](const fs::path& source = fs::path()){
            activeRailPane = RailPane::None;
            autoRig.open = true;
            motionPanel.open = false;
            if(!source.empty()) autoRig.source = source.string();
            else if(const Warp::Entity* e = stage.get(selected); e && e->model) autoRig.source = e->model->path;
            ui.focusTextField("autorig-source");
        };
    
        auto openMotionWorkflow = [&](){
            activeRailPane = RailPane::None;
            autoRig.open = false;
            motionPanel.open = true;
            timelineVisible = true;
            Warp::Id rigRoot = stage.contains(motionPanel.targetCharacter)
                ? Loom::motionCharacterForEntity(stage, motionPanel.targetCharacter)
                : Loom::motionCharacterForEntity(stage, selected);
            if(rigRoot != Warp::None){
                motionPanel.targetCharacter = rigRoot;
                selected = rigRoot;
                focus = Focus::Entity;
                const Loom::MotionRigRestPose rest = Loom::motionRigRestPose(stage, rigRoot);
                if(rest.bounds.valid){
                    const glm::vec3 centre = (rest.bounds.low + rest.bounds.high) * 0.5f;
                    view.orbit.target = glm::vec3(stage.worldMatrix(rigRoot, frame) * glm::vec4(centre, 1.0f));
                    view.orbit.distance = std::clamp(rest.bounds.height() * 3.6f, 2.6f, 40.0f);
                }
            }
            ui.focusTextField("action" + std::to_string(motionPanel.activeAction));
        };

        auto frameProceduraCurve = [&]{
            namespace Panel = Loom::WeaverProceduraUi;
            namespace Proc = Engine::WeaverProcedura;
            Proc::Node* node = Panel::activeCurveNode(proceduraPanel);
            if(!node) return false;
            const Proc::Curve& curve = std::get<Proc::CurveNode>(node->payload).curve;
            if(curve.points.empty()) return false;
            glm::vec3 low = curve.points.front(), high = curve.points.front();
            for(const glm::vec3& point : curve.points){ low = glm::min(low, point); high = glm::max(high, point); }
            view.lookThrough = Warp::None;
            view.orbit.target = (low + high) * 0.5f;
            view.orbit.distance = std::clamp(std::max(0.5f, glm::length(high - low) * 0.5f) * 2.8f,
                                             2.0f, 100000.0f);
            return true;
        };
    
        //-- projekt ----------------------------------------------------------------------------------
        //Projekt je .usda (vidi Warp/Project.h). Prvo spremanje ga stavi u mapu koju media prozor
        //pregledava, pod imenom koje jos ne postoji - nikad preko tudjeg projekta
        fs::path projectPath;
        fs::path pendingProject;              //ceka potvrdu, jer otvaranje zamjenjuje scenu
        Loom::MoodboardState moodboard;
        fs::path moodboardHome = startAt;
        bool moodboardRestoreOutline = outlineVisible;
        bool moodboardRestoreComponents = componentsVisible;
        auto setMoodboardOpen = [&](bool open){
            if(moodboard.open == open) return;
            if(open){
                moodboardRestoreOutline = outlineVisible;
                moodboardRestoreComponents = componentsVisible;
                outlineVisible = false;
                componentsVisible = false;
                activeRailPane = RailPane::None;
                motionPanel.open = false;
                autoRig.open = false;
            }else{
                outlineVisible = moodboardRestoreOutline;
                componentsVisible = moodboardRestoreComponents;
            }
            moodboard.open = open;
        };
    
        //NESPREMLJENO: otisak scene u trenutku zadnjeg spremanja ili otvaranja (vidi
        //Stage::fingerprint). Prazna scena na pocetku nije nespremljena
        uint64_t savedFingerprint = stage.fingerprint();
        bool quitting = false;
        //Undo/redo of the whole scene (LoomUndo.h) and autosave beside the project (LoomAutosave.h)
        Loom::UndoHistory history;
        bool historyStarted = false;
        Loom::Autosave autosave;
        fs::path offeredAutosave;             //newer autosave waiting for the user's answer
        bool autosaveAsked = false;           //its question is on screen
    
        auto saveProjectNow = [&]() -> bool{
            if(projectPath.empty()){
                projectPath = browser.at / "loom_project.usda";
                for(int n = 2; fs::exists(projectPath); ++n) projectPath = browser.at / ("loom_project_" + std::to_string(n) + ".usda");
            }
            const auto started = std::chrono::steady_clock::now();
            std::string error;
            const bool wasUntitled = autosave.lastPath() == Loom::autosavePathFor({});
            if(Warp::saveProject(stage, projectPath.string(), error)){
                moodboardHome = projectPath.parent_path();
                Loom::saveMoodboard(moodboard, Loom::moodboardStoragePath(projectPath, moodboardHome));
                autosave.discard(projectPath);
                if(wasUntitled) autosave.discard({});
                char text[256];
                std::snprintf(text, sizeof(text), "Saved %s (%.1f s)", projectPath.filename().string().c_str(),
                              std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count());
                message = text;
                browser.refresh();
                savedFingerprint = stage.fingerprint();
                return true;
            }
            message = "Save failed: " + error;
            return false;
        };
    
        auto openProject = [&](const fs::path& path){
            std::string error;
            Warp::Stage opened;
            if(!Warp::loadProject(path.string(), opened, error)){ message = "Could not open: " + error; return; }
            stage = std::move(opened);
            projectPath = path;
            moodboardHome = path.parent_path();
            Loom::loadMoodboard(moodboard, Loom::moodboardStoragePath(projectPath, moodboardHome));
            selected = Warp::None;
            selectedMedia = -1;
            collapsed.clear();
            view = Loom::ViewportState{};
            frame = stage.startFrame;
            extent = Loom::sceneExtent(stage, frame);
            extentDirty = false;
            Loom::frameAll(stage, frame, view.orbit);
            message = "Opened project " + path.filename().string();
            savedFingerprint = stage.fingerprint();
            history.reset(stage);
            fs::path newer;
            if(Loom::newerAutosave(path, newer)) offeredAutosave = newer;
        };
    
        auto newScene = [&](){
            stage = Warp::Stage{};
            if(!projectPath.empty()) moodboardHome = projectPath.parent_path();
            projectPath.clear();
            Loom::loadMoodboard(moodboard, Loom::moodboardStoragePath(projectPath, moodboardHome));
            selected = Warp::None;
            selectedMedia = -1;
            view = Loom::ViewportState{};
            frame = 1.0;
            extentDirty = true;
            message = "New scene";
            savedFingerprint = stage.fingerprint();
            history.reset(stage);
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
        //COMPOSITOR (LoomCompositor.h): radni prostor s cvorovima; slika pregleda ide istim putem kao ploca
        Loom::Compositor compositor;
        std::unique_ptr<StreamingTexture> compositorTexture;
        std::unique_ptr<Material> compositorMaterial;
        bool compositorReady = false;
        Loom::CompositorLayout compositorLayout;
        Loom::PlateStream plateStream;
        bool showPlate = true;
        float plateBrightness = 1.0f;
        bool plateReady = false;
        int64_t plateShown = -1;
        std::vector<uint8_t> platePixels;
    
        //-- splat u pogledu -----------------------------------------------------------------------------
        Loom::ViewportSplat viewportSplat(loom);
        //Rezanje i ciscenje splata (LoomSplatCut.h). Koliko je u kocki se broji samo kad se kocka,
        //rezanje ili splat promijene - prolaz kroz cijeli oblak
        Warp::Id splatShownId = Warp::None;          //splat koji pogled crta (iz proslog kadra)
        Warp::Id splatCleanTarget = Warp::None;      //ciji splat posao ciscenja zamijeni
        Warp::Id splatFrameWhenLoaded = Warp::None;  //splat otvoren iz preglednika: pogled na njega kad stigne
        std::string splatCleanOutput;
        Warp::Id proxyTarget = Warp::None;           //splat ciji je proxy mesh u izradi; proxy postaje njegovo dijete
        std::string proxyOutput;
        glm::mat4 cutBoxSeen(0.0f);
        size_t cutCountSeen = 0, cutInside = 0;
        std::string cutPathSeen;
    
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
        auto importAutoRigModel = [&](const fs::path& path){
            int width = 0, height = 0;
            glfwGetWindowSize(window, &width, &height);
            const Loom::ViewCamera camera = Loom::viewCameraFor(stage, frame, Loom::layoutEditor(float(width), float(height), outlineVisible, componentsVisible, timelineVisible, terminal.visible, (motionPanel.open || proceduraPanel.open), railReveal).viewport, view);
            const bool wasEmpty = stage.size() == 0;
            const Loom::ModelImportReport report = Loom::importModelAtView(stage, path, frame, camera, extent);
            if(!report.problem.empty()){ message = "Auto Rig import: " + report.problem; return; }
            afterModelImport(report, wasEmpty);
            const Warp::Id rigRoot = Loom::motionCharacterForEntity(stage, report.group);
            const Loom::MotionRigRestPose rest = Loom::motionRigRestPose(stage, rigRoot);
            const bool hasRig = !rest.joints.empty();
            const bool animatorReady = hasRig && Loom::ensureRigAnimator(stage, rigRoot);
            bool naturalPose = false;
            if(animatorReady && path.filename() != "bend_preview.glb"){
                std::array<Warp::Id, 52> ids;
                if(Loom::motionFindVerifiedUniRig52(rest, ids))
                    naturalPose = Loom::applyUniRigRelaxedRestPose(stage, rigRoot);
            }
            if(naturalPose) message = "Humanoid imported with Animator and natural arm and finger pose.";
            else if(animatorReady) message = "Humanoid imported with Animator ready.";
            else message = "Imported " + path.filename().string() + "; no skeleton was found.";
        };
        auto startAutoRig = [&](){
            if(job.running) return;
            const fs::path source(autoRig.source);
            std::error_code error;
            std::string sourceExtension = source.extension().string();
            std::transform(sourceExtension.begin(), sourceExtension.end(), sourceExtension.begin(), [](unsigned char c){ return char(std::tolower(c)); });
            if(!fs::is_regular_file(source, error) || (sourceExtension != ".glb" && sourceExtension != ".gltf")){
                message = "Auto Rig: select an unrigged .glb or .gltf file first."; return;
            }
            const fs::path root(LOOM_ROOT_DIR);
            const auto stamp = std::chrono::system_clock::now().time_since_epoch().count();
            autoRig.output = root / "tools/autorig/outputs" / ("rig_" + std::to_string(stamp));
            afterJob = After::Nothing;
            terminal.visible = true;
            startJob(Loom::autoRigCommand(root, fs::absolute(source), autoRig.output),
                     autoRig.output.string(), Loom::Task::AutoRig, 0);
            message = "Auto Rig: generating a new skeleton and skin weights.";
        };
        auto autoRigBackendReady = [&](){
            const fs::path root(LOOM_ROOT_DIR);
            return fs::is_regular_file(root / "tools/autorig/.venv/bin/python") &&
                   fs::is_regular_file(root / "tools/autorig/vendor/UniRig/run.py");
        };
        auto importModelAsset = [&](const fs::path& path, const Treadle::Rect& viewport){
            const Loom::ViewCamera camera = Loom::viewCameraFor(stage, frame, viewport, view);
            const bool wasEmpty = stage.size() == 0;
            const Loom::ModelImportReport report = Loom::importModelAtView(stage, path, frame, camera, extent);
            if(!report.problem.empty()) message = "Model: " + report.problem;
            else{
                afterModelImport(report, wasEmpty);
                char text[192];
                std::snprintf(text, sizeof(text), "Model %s: %zu nodes, %zu meshes, %zu materials",
                              path.filename().string().c_str(), report.nodes, report.meshes, report.materials);
                message = text;
            }
        };
        auto importHumanoidAsset = [&](const fs::path& path){
            if(job.running){ message = "Wait for the current job to finish before importing a humanoid."; return; }
            Spool::GltfScene sourceScene;
            std::string error;
            if(!Spool::loadGltf(path.string(), sourceScene, error, Spool::GltfLoadConfig{false})){
                message = "Humanoid import: " + error;
                return;
            }
            motionPanel.open = false;
            if(!sourceScene.skins.empty()){
                autoRig.open = false;
                importAutoRigModel(path);
                return;
            }
            if(!autoRigBackendReady()){
                message = "Auto Rig is not installed. Run tools/autorig/setup.sh first.";
                return;
            }
            autoRig.source = path.string();
            autoRig.open = true;
            startAutoRig();
        };
        auto addHumanoidMascott = [&](){
            const fs::path root(LOOM_ROOT_DIR);
            fs::path mascot = root / "tools/autorig/outputs/mascot-03/rigged.glb";
            std::error_code error;
            if(!fs::is_regular_file(mascot, error)){
                mascot.clear();
                const fs::path outputs = root / "tools/autorig/outputs";
                for(const auto& entry : fs::directory_iterator(outputs, error)){
                    if(error) break;
                    if(!entry.is_directory(error) || entry.path().filename().string().rfind("mascot-", 0) != 0) continue;
                    const fs::path candidate = entry.path() / "rigged.glb";
                    if(fs::is_regular_file(candidate, error) && fs::is_regular_file(entry.path() / "complete.json", error))
                        mascot = candidate;
                }
            }
            if(mascot.empty()){
                message = "HumanoidMascott is missing; run Auto Rig on the mascot model first.";
                return;
            }
            const size_t before = stage.size();
            autoRig.open = false;
            motionPanel.open = false;
            importAutoRigModel(mascot);
            if(stage.size() > before) message = "HumanoidMascott added with its Animator ready.";
        };
        //== IMPORTER (LoomImporter.h): jedan prozor za sve sto editor zna uvesti ==================
        Loom::ImporterState importer;
        auto openImporter = [&](){
            importer.show(importer.at.empty() ? browser.at : importer.at);
            importer.status.clear();
        };
        //Splat sam za sebe; pogled crta prvi vidljivi, pa se ostali sakriju
        auto importSplatFile = [&](const fs::path& splatFile){
            stage.walk([&](const Warp::Entity& e, int){ if(e.splat) stage.get(e.id)->visible = false; });
            const Warp::Id id = stage.create(splatFile.stem().string());
            stage.get(id)->splat = Warp::Splat{splatFile.string()};
            selected = id;
            focus = Focus::Entity;
            showSplat = true;
            splatFrameWhenLoaded = id;
            message = "Splat: " + splatFile.filename().string();
        };
        //Snimka u medije projekta (jednom); vraca njezin indeks
        auto addVideoToProject = [&](const fs::path& video){
            for(size_t i = 0; i < stage.media.size(); ++i){
                if(stage.media[i].path == video.string()){ selectedMedia = int(i); focus = Focus::Media; return int(i); }
            }
            stage.media.push_back(probeMedia(video));
            selectedMedia = int(stage.media.size()) - 1;
            focus = Focus::Media;
            message = "Media: " + video.filename().string();
            return selectedMedia;
        };
        //Nova kamera tocno iz trenutnog pogleda (polozaj, smjer, vidno polje)
        auto addCameraHere = [&](Warp::Id parent){
            int w = 0, h = 0;
            glfwGetWindowSize(window, &w, &h);
            const Loom::ViewCamera camera = Loom::viewCameraFor(stage, frame, Loom::layoutEditor(float(w), float(h), outlineVisible, componentsVisible, timelineVisible, terminal.visible, (motionPanel.open || proceduraPanel.open), railReveal).viewport, view);
            const Warp::Id id = Loom::addCameraFromView(stage, glm::inverse(camera.view), camera.focal, camera.frame.height, frame, parent);
            selected = id;
            focus = Focus::Entity;
            extentDirty = true;
            message = "Added " + stage.get(id)->name + " (from view; 0 looks through it)";
        };
        auto addEmpty = [&](Warp::Id parent){
            const Warp::Id id = stage.create("Empty", parent);
            selected = id;
            focus = Focus::Entity;
        };
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
        if(!shotMotionText.empty()){
            motionPanel.actions.clear();
            for(const std::string& text : shotMotionText) motionPanel.actions.push_back(Loom::MotionAction{text, 3.0f});
            openMotionWorkflow();
        }
        if(!shotModel.empty() || shotSurfaceWanted){
            int w = 0, h = 0;
            glfwGetWindowSize(window, &w, &h);
            const Loom::ViewCamera camera = Loom::viewCameraFor(stage, frame, Loom::layoutEditor(float(w), float(h), outlineVisible, componentsVisible, timelineVisible, terminal.visible, (motionPanel.open || proceduraPanel.open), railReveal).viewport, view);
            extent = Loom::sceneExtent(stage, frame);
            if(!shotModel.empty()){
                const bool wasEmpty = stage.size() == 0;
                const Loom::ModelImportReport report = Loom::importModelAtView(stage, shotModel, frame, camera, extent);
                std::printf("model: %s%zu nodes, %zu meshes, %zu materials\n", report.problem.c_str(), report.nodes, report.meshes, report.materials);
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
                std::printf("surface: %zu points, %zu on the plane, normal %.3f %.3f %.3f\n", surfaceTool.fit.total, surfaceTool.fit.used,
                            surfaceTool.fit.normal.x, surfaceTool.fit.normal.y, surfaceTool.fit.normal.z);
            }
            focus = Focus::Entity;
        }
        if(!shotMotion.empty()){
            // For command-line previews, --kadar selects the displayed frame. Place the clip at
            // the scene start first, then scrub to the requested sample after import.
            if(shotFrame >= 0.0) frame = stage.startFrame;
            importMotion(shotMotion, motionPanel.targetCharacter);
            if(shotFrame >= 0.0) frame = std::clamp(shotFrame, stage.startFrame, stage.endFrame);
            std::printf("%s\n", message.c_str());
        }
        if(!shotSave.empty()){
            projectPath = shotSave;
            saveProjectNow();
            std::printf("%s\n", message.c_str());
        }
    
        std::string windowTitle;
        while(!quitting){
            glfwPollEvents();
            Loom::loadMoodboard(moodboard, Loom::moodboardStoragePath(projectPath, moodboardHome));
    
            //Otisak svaki kadar: prolaz kroz stablo i kljuceve, desetinka milisekunde i na 2301 kljucu
            const bool dirty = stage.fingerprint() != savedFingerprint;
            if(!historyStarted){
                //Whatever the command line opened is the starting point, not an undo step. A newer
                //autosave of an untitled scene is offered once at start
                history.reset(stage);
                historyStarted = true;
                fs::path newer;
                if(projectPath.empty() && stage.size() == 0 && Loom::newerAutosave({}, newer)) offeredAutosave = newer;
            }
            if(autosave.tick(stage, projectPath, dirty)) message = "Autosaved";
            const std::string title = std::string("Loom - ") + (projectPath.empty() ? "untitled" : projectPath.filename().string()) +
                                      (dirty ? " *" : "");
            if(title != windowTitle){ glfwSetWindowTitle(window, title.c_str()); windowTitle = title; }
    
            //ZATVARANJE PROZORA S NESPREMLJENIM se zaustavi i pita. Bez nespremljenog - izlaz
            if(glfwWindowShouldClose(window)){
                if(!dirty && !viewportSplat.hasCuts()) break;
                glfwSetWindowShouldClose(window, GLFW_FALSE);
                int w = 0, h = 0;
                glfwGetWindowSize(window, &w, &h);
                ui.openMenuAt("Exit", float(w) * 0.5f - 200.0f, float(h) * 0.4f);
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
            for(uint32_t button = 0; button < uint32_t(Treadle::MouseButton::Count); ++button){
                input.pressedEvent[button] = mousePressedEvents[button];
                input.releasedEvent[button] = mouseReleasedEvents[button];
                input.pressX[button] = mousePressX[button];
                input.pressY[button] = mousePressY[button];
                mousePressedEvents[button] = false;
                mouseReleasedEvents[button] = false;
            }
            const bool leftPressed = input.pressedEvent[uint32_t(Treadle::MouseButton::Left)] || (leftDown && !leftWasDown);
            const bool leftReleased = input.releasedEvent[uint32_t(Treadle::MouseButton::Left)] || (!leftDown && leftWasDown);
            const bool middlePressed = input.pressedEvent[uint32_t(Treadle::MouseButton::Middle)] || (middleDown && !middleWasDown);
            const bool rightPressed = input.pressedEvent[uint32_t(Treadle::MouseButton::Right)] || (rightDown && !rightWasDown);
            const double leftClickX = input.pressedEvent[uint32_t(Treadle::MouseButton::Left)]
                ? input.pressX[uint32_t(Treadle::MouseButton::Left)] : cursorX;
            const double leftClickY = input.pressedEvent[uint32_t(Treadle::MouseButton::Left)]
                ? input.pressY[uint32_t(Treadle::MouseButton::Left)] : cursorY;
            const double rightClickX = input.pressedEvent[uint32_t(Treadle::MouseButton::Right)]
                ? input.pressX[uint32_t(Treadle::MouseButton::Right)] : cursorX;
            const double rightClickY = input.pressedEvent[uint32_t(Treadle::MouseButton::Right)]
                ? input.pressY[uint32_t(Treadle::MouseButton::Right)] : cursorY;
            if(input.pressedEvent[uint32_t(Treadle::MouseButton::Left)]){
                cursorX = leftClickX;
                cursorY = leftClickY;
                input.mouseX = float(leftClickX);
                input.mouseY = float(leftClickY);
            }
            input.wheel = scrollAccumulated;
            input.shift = shift;
            //Utipkano i pritisnute tipke od proslog kadra; Treadle ih da polju u fokusu
            input.text.swap(typedCharacters);
            typedCharacters.clear();
            input.keys.swap(typedKeys);
            typedKeys.clear();
    
            if(selected != lastTrailSelection){
                if(stage.contains(lastTrailSelection) && lastTrailSelection != selected){
                    selectionTrail.erase(std::remove(selectionTrail.begin(), selectionTrail.end(), lastTrailSelection),
                                         selectionTrail.end());
                    selectionTrail.insert(selectionTrail.begin(), lastTrailSelection);
                    if(selectionTrail.size() > 6) selectionTrail.resize(6);
                }
                lastTrailSelection = selected;
            }
            selectionTrail.erase(std::remove_if(selectionTrail.begin(), selectionTrail.end(), [&](Warp::Id id){
                return !stage.contains(id) || id == selected;
            }), selectionTrail.end());
    
            const auto now = std::chrono::steady_clock::now();
            const float rawFrameSeconds = float(std::chrono::duration<double>(now - lastFrame).count());
            const float frameSeconds = std::min(0.1f, rawFrameSeconds);
            input.timeSeconds = float(std::chrono::duration<double>(now.time_since_epoch()).count());
            if(rawFrameSeconds > 1e-6f){
                const float instantaneousFps = 1.0f / rawFrameSeconds;
                smoothedFps = smoothedFps > 0.0f ? smoothedFps + 0.15f * (instantaneousFps - smoothedFps)
                                                   : instantaneousFps;
            }
            lastFrame = now;

            const float railFullWidth = std::min(56.0f, std::max(46.0f, float(windowWidth) * 0.045f));
            const float railTimelineHeight = timelineVisible ? std::clamp(float(windowHeight) * 0.2f, 110.0f, 190.0f) : 0.0f;
            const float railTerminalHeight = terminal.visible ? std::min(
                std::clamp(float(windowHeight) * 0.23f, 150.0f, 250.0f),
                std::max(0.0f, float(windowHeight) - 40.0f - railTimelineHeight - 170.0f)) : 0.0f;
            const float railMiddleHeight = std::max(0.0f, float(windowHeight) - 40.0f -
                                                    railTimelineHeight - railTerminalHeight);
            const float handleHeight = std::min(58.0f, railMiddleHeight);
            const Treadle::Rect railHoverArea{0.0f, 40.0f + std::max(0.0f, (railMiddleHeight - handleHeight) * 0.5f),
                                              16.0f, handleHeight};
            const Treadle::Rect fullRailArea{0.0f, 40.0f, railFullWidth, railMiddleHeight};
            railHoverOpen = railHoverOpen ? fullRailArea.contains(input.mouseX, input.mouseY)
                                          : railHoverArea.contains(input.mouseX, input.mouseY);
            const float revealTarget = railHoverOpen ? 1.0f : 0.0f;
            railReveal += (revealTarget - railReveal) * (1.0f - std::exp(-13.0f * frameSeconds));
            if(std::fabs(revealTarget - railReveal) < 0.002f) railReveal = revealTarget;
            const Loom::EditorLayout layout = Loom::layoutEditor(float(windowWidth), float(windowHeight), outlineVisible, componentsVisible,
                                                                  timelineVisible, terminal.visible, (motionPanel.open || proceduraPanel.open), railReveal);
    
            if(playing){
                double playbackStart = stage.startFrame, playbackEnd = stage.endFrame;
                bool loopPlayback = false;
                const Warp::Id playingRig = Loom::motionCharacterForEntity(stage, selected);
                const Warp::Entity* rig = stage.get(playingRig);
                if(rig && rig->animator && rig->animator->enabled &&
                   rig->animator->activeAnimation < rig->animator->animations.size()) {
                    const Warp::AnimationClip& clip = rig->animator->animations[rig->animator->activeAnimation];
                    if(clip.endFrame > clip.startFrame){
                        playbackStart = clip.startFrame;
                        playbackEnd = clip.endFrame;
                        loopPlayback = clip.loop;
                    }
                }
                if(!Loom::advanceClipPlaybackFrame(frame, double(frameSeconds) * stage.framesPerSecond,
                                                   playbackStart, playbackEnd, loopPlayback)) playing = false;
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
                    message = why.empty() ? "Job failed - see Terminal" : "Failed: " + why;
                    terminal.visible = true;
                }else if(job.task == Loom::Task::Solve){
                    importFolder(job.outputDirectory, jobVideo);
                    for(Warp::Media& media : stage.media){
                        if(media.path == jobVideo) media.result = job.outputDirectory;
                    }
                    //Splat je vec istreniran u istom poslu (--then)
                    std::error_code splatError;
                    if(afterJob == After::ImportAndTrain && fs::is_regular_file(job.outputDirectory + "/scena.ply", splatError)){
                        std::string name = fs::path(job.outputDirectory).filename().string();
                        if(name.size() > 5 && name.substr(name.size() - 5) == "_loom") name.resize(name.size() - 5);
                        const Warp::Id id = stage.create("Splat", stage.find("/" + name));
                        stage.get(id)->splat = Warp::Splat{job.outputDirectory + "/scena.ply"};
                        message = "Solve + splat ready: " + job.outputDirectory + "/scena.ply";
                    }
                }else if(job.task == Loom::Task::Train){
                    //Splat ide u grupu svoje mape, ako je u sceni
                    std::string name = fs::path(job.outputDirectory).filename().string();
                    if(name.size() > 5 && name.substr(name.size() - 5) == "_loom") name.resize(name.size() - 5);
                    const Warp::Id group = stage.find("/" + name);
                    const Warp::Id id = stage.create("Splat", group);
                    stage.get(id)->splat = Warp::Splat{job.outputDirectory + "/scena.ply"};
                    message = "Splat ready: " + job.outputDirectory + "/scena.ply";
                }else if(job.task == Loom::Task::Clean){
                    //Ocisceni splat zamijeni izvor u sceni; izvor ostaje na disku
                    Warp::Entity* target = stage.get(splatCleanTarget);
                    std::error_code error;
                    if(target && target->splat && fs::is_regular_file(splatCleanOutput, error)){
                        const bool same = target->splat->path == splatCleanOutput;
                        target->splat->path = splatCleanOutput;
                        if(same) viewportSplat.reload();
                        browser.refresh();
                        message = "Floaters cleaned: " + fs::path(splatCleanOutput).filename().string();
                    }else{
                        message = "Cleaning failed; see Terminal.";
                        terminal.visible = true;
                    }
                }else if(job.task == Loom::Task::Proxy){
                    //Proxy je u sustavu splata, pa ide kao njegovo dijete bez pomaka i mjerila
                    std::error_code error;
                    Spool::GltfScene proxyScene;
                    std::string problem;
                    Spool::GltfLoadConfig proxyConfig;
                    proxyConfig.decodeImages = false;
                    if(stage.contains(proxyTarget) && fs::is_regular_file(proxyOutput, error) &&
                       Spool::loadGltf(proxyOutput, proxyScene, problem, proxyConfig)){
                        const Loom::ModelImportReport report = Loom::importGltf(stage, proxyScene, proxyTarget, glm::vec3(0.0f), 1.0f);
                        if(Warp::Entity* proxy = stage.get(report.group)){
                            proxy->name = fs::path(proxyOutput).stem().string();
                            //importGltf model spusti na pod i centrira (za likove); proxy mora ostati
                            //tocno u sustavu splata
                            proxy->local.translation = glm::vec3(0.0f);
                            proxy->local.scale = glm::vec3(1.0f);
                        }
                        selected = report.group;
                        focus = Focus::Entity;
                        browser.refresh();
                        message = "Proxy mesh: " + fs::path(proxyOutput).filename().string() + " (+ .obj for Blender, same space as kamera.usda)";
                    }else{
                        message = "Proxy mesh failed; see Terminal." + (problem.empty() ? std::string() : " " + problem);
                        terminal.visible = true;
                    }
                }else if(job.task == Loom::Task::AutoRig){
                    const fs::path output(job.outputDirectory);
                    std::error_code error;
                    if(fs::is_regular_file(output / "complete.json", error) &&
                       fs::is_regular_file(output / "rigged.glb", error)){
                        autoRig.output = output;
                        browser.at = output;
                        importAutoRigModel(output / "rigged.glb");
                        message = "Auto Rig complete: skeleton + weights passed deformation checks. Inspect the imported rig.";
                        autoRig.open = true;
                        motionPanel.open = false;
    
                    }else{
                        message = "Auto Rig failed; full log: " + (output / "autorig.log").string();
                        terminal.visible = true;
                    }
                }else if(job.task == Loom::Task::WeaverMotion){
                    std::error_code error;
                    fs::path outputBvh = generatedMotionPath;
                    outputBvh += ".bvh";
                    if(fs::is_regular_file(outputBvh, error)){
                        browser.at = outputBvh.parent_path();
                        browser.refresh();
                        importMotion(outputBvh, generatedMotionTarget);
                        if(!message.empty()) message =
                            std::string(generatedMotionIsBricks ? "MotionBricks" : "Kimodo") + " complete; " + message;
                        motionPanel.open = true;
                    }else if(fs::is_directory(generatedMotionPath, error)){
                        const std::vector<fs::path> samples = Loom::weaverMotionFilesIn(generatedMotionPath);
                        browser.at = generatedMotionPath.parent_path();
                        browser.refresh();
                        motionPanel.historyRead = {};
                        if(!samples.empty()){
                            if(samples.size() == 1){
                                importMotion(samples.front(), generatedMotionTarget);
                                message = "Kimodo generated one BVH sample; it is available in Recent Motions.";
                            }else{
                                //Najbolja po mjeri (LoomMotionQuality.h) ide na lik odmah; ostale
                                //cekaju u Reviewu. Prije je trebalo otvoriti svaku da se nadje dobra
                                const int best = Loom::bestMotionVariant(samples, motionPanel.qualityCache);
                                if(best >= 0){
                                    importMotion(samples[size_t(best)], generatedMotionTarget);
                                    message = "Kimodo generated " + std::to_string(samples.size()) +
                                        " variations; loaded #" + std::to_string(best + 1) + " (" +
                                        Engine::MotionQuality::summary(motionPanel.qualityCache.get(samples[size_t(best)])) +
                                        "). Others are in Review.";
                                }else{
                                    message = "Kimodo generated " + std::to_string(samples.size()) + " BVH variations; choose one in Review.";
                                }
                            }
                            motionPanel.open = true;
                        }else{
                            message = "Kimodo saved native NPZ/CSV outputs; Loom's timeline preview currently imports SOMA BVH only.";
                            motionPanel.open = true;
                        }
                    }else{
                        fs::path outputNpz = generatedMotionPath;
                        outputNpz += ".npz";
                        fs::path outputCsv = generatedMotionPath;
                        outputCsv += ".csv";
                        if(fs::is_regular_file(outputNpz, error) || fs::is_regular_file(outputCsv, error)){
                            browser.at = generatedMotionPath.parent_path();
                            browser.refresh();
                            message = "Kimodo saved native NPZ/CSV outputs; Loom's timeline preview currently imports SOMA BVH only.";
                            motionPanel.open = true;
                        }else{
                            message = "Kimodo did not produce a recognized motion output; check Terminal.";
                            terminal.visible = true;
                        }
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
                if(on) ui.canvas().outline(region.box, 1.0f, theme.title);
                ui.canvas().text(x + theme.padding, y + (height - Treadle::textHeight(theme.textScale)) * 0.5f,
                                 label, on ? theme.textOnAccent : theme.text, theme.textScale);
                return std::make_pair(region.pressed, x + width + 6.0f);
            };
    
            //== ALATNA TRAKA =========================================================================
            {
                const Treadle::Rect& bar = layout.toolbar;
                ui.canvas().rect(bar, Treadle::Color{0.045f, 0.065f, 0.050f, 1.0f});
                ui.canvas().text(bar.x + 12.0f, bar.y + 13.0f,
                                 compositor.open ? "COMPOSITOR" : proceduraPanel.open ? "PROCEDURA" : motionPanel.open ? "ANIMATOR" : "LOOM", theme.title,
                                 compositor.open ? theme.textScale * 0.64f : proceduraPanel.open ? theme.textScale * 0.72f : theme.textScale);
                const float y = bar.y + 7.0f, h = bar.height - 14.0f;
                float afterBoard = bar.x + 90.0f;
                if(proceduraPanel.open){
                    auto [backToScene, afterBack] = toolButton("Back to Scene", bar.x + 90.0f, y, h);
                    afterBoard = afterBack;
                    if(backToScene){ proceduraPanel.open = false; activeRailPane = RailPane::None; }
                }else if(motionPanel.open){
                    auto [backToScene, afterBack] = toolButton("Back to Scene", bar.x + 90.0f, y, h);
                    if(backToScene) motionPanel.open = false;
                    auto [playPause, afterPlayback] = toolButton(playing ? "Pause" : "Play", afterBack, y, h, playing);
                    if(playPause) playing = !playing;
                    auto [saveButton, afterSave] = toolButton(dirty ? "Save *" : "Save", afterPlayback, y, h, dirty);
                    if(saveButton) saveProjectNow();
                    afterBoard = afterSave;
                }else{
                    auto [moveTool, afterMove] = toolButton("W", bar.x + 90.0f, y, h, tool == Tool::Move);
                    if(moveTool) tool = Tool::Move;
                    auto [rotateTool, afterRotateTool] = toolButton("E", afterMove, y, h, tool == Tool::Rotate);
                    if(rotateTool) tool = Tool::Rotate;
                    //Ploha iz odabira: vucenjem u pogledu se oznaci komad plohe (LoomEditorTools.h)
                    auto [surfaceButton, afterRotate] = toolButton("S", afterRotateTool, y, h, surfaceTool.active);
                    if(surfaceButton) surfaceTool.active = !surfaceTool.active;
                    auto [fit, afterFit] = toolButton("Frame (F)", afterRotate + 12.0f, y, h);
                    if(fit){ view.lookThrough = Warp::None; Loom::frameAll(stage.size() ? stage : live, frame, view.orbit); }
                    auto [through, afterThrough] = toolButton("Camera (0)", afterFit, y, h, view.lookThrough != Warp::None);
                    if(through){
                        if(view.lookThrough != Warp::None) view.lookThrough = Warp::None;
                        else{
                            const Warp::Entity* chosen = stage.get(selected);
                            view.lookThrough = chosen && chosen->camera ? selected : firstCamera();
                        }
                    }
                    auto [plateButton, afterPlate] = toolButton("Plate (V)", afterThrough, y, h, showPlate);
                    if(plateButton) showPlate = !showPlate;
                    auto [rigButton, afterRig] = toolButton("Auto Rig", afterPlate + 12.0f, y, h);
                    if(rigButton) openAutoRig();
                    auto [saveButton, afterSave] = toolButton(dirty ? "Save *" : "Save", afterRig + 12.0f, y, h, dirty);
                    if(saveButton) saveProjectNow();
                    auto [newButton, afterNew] = toolButton("New", afterSave, y, h);
                    if(newButton) ui.openMenu("New");
                    auto [boardButton, afterBoardNormal] = toolButton("Moodboard (M)", afterNew, y, h, moodboard.open);
                    if(boardButton) setMoodboardOpen(!moodboard.open);
                    afterBoard = afterBoardNormal;
                }
    
                //Stanje posla ili zadnja poruka, desno
                std::string status = message;
                Treadle::Color colour = theme.dim;
                if(job.running){
                    const double elapsed = std::chrono::duration<double>(now - job.started).count();
                    const int phase = job.phase;
                    const int phaseCount = int(sizeof(Loom::phases) / sizeof(Loom::phases[0]));
                    if(job.task == Loom::Task::Train){
                        char text[96];
                        std::snprintf(text, sizeof(text), "Training %.0f%% - %s",
                                      job.fraction >= 0.0f ? 100.0 * double(job.fraction) : 0.0,
                                      Loom::humanTime(elapsed).c_str());
                        status = text;
                    }else if(job.task == Loom::Task::WeaverMotion){
                        status = "Kimodo generating motion - " + Loom::humanTime(elapsed);
                    }else if(job.task == Loom::Task::AutoRig){
                        status = "Auto Rig - " + Loom::humanTime(elapsed);
                    }else if(job.task == Loom::Task::Clean){
                        status = "Cleaning floaters - " + Loom::humanTime(elapsed);
                    }else if(job.task == Loom::Task::Proxy){
                        status = "Building proxy / blockers - " + Loom::humanTime(elapsed);
                    }else{
                        status = "Solve: ";
                        status += (phase >= 0 && phase < phaseCount) ? Loom::phases[phase].label : "starting";
                        status += " - " + Loom::humanTime(elapsed);
                        //PROCJENA JE OZNACENA KAO PROCJENA: udjeli su izmjereni na jednoj snimci
                        if(phase >= 0 && phase < phaseCount && Loom::phases[phase].share > 0.02 &&
                           Loom::phases[phase].share < 1.0){
                            status += ", about " + Loom::humanTime(std::max(0.0, elapsed / Loom::phases[phase].share - elapsed));
                        }
                    }
                    colour = theme.accent;
                }else if(job.failed){
                    colour = theme.warning;
                }
                std::string sceneName = projectPath.empty() ? "Untitled" : projectPath.stem().string();
                const Warp::Entity* activeCamera = stage.get(view.lookThrough);
                const std::string cameraName = activeCamera && activeCamera->camera ? activeCamera->name : "Orbit";
                char fpsText[24];
                std::snprintf(fpsText, sizeof(fpsText), "%.0f", double(smoothedFps));
    
                const float metadataStart = bar.x + afterBoard + 8.0f;
                const float metadataEnd = bar.x + bar.width - 10.0f;
                const float metadataRoom = std::max(0.0f, metadataEnd - metadataStart);
                const float metaScale = theme.textScale * 0.68f;
                float cursorRight = metadataEnd;
                auto headerChip = [&](const std::string& key, const std::string& value, float width,
                                      const Treadle::Color& accent){
                    const std::string label = Treadle::fitText(key + " " + value, width - 12.0f, metaScale);
                    const float actualWidth = std::min(width, std::max(30.0f, Treadle::textWidth(label, metaScale) + 12.0f));
                    const Treadle::Rect chip{cursorRight - actualWidth, bar.y + 8.0f, actualWidth, bar.height - 16.0f};
                    ui.canvas().rect(chip, Treadle::Color{accent.r, accent.g, accent.b, 0.10f});
                    ui.canvas().outline(chip, 1.0f, Treadle::Color{accent.r, accent.g, accent.b, 0.48f});
                    ui.canvas().text(chip.x + (chip.width - Treadle::textWidth(label, metaScale)) * 0.5f,
                                     chip.y + (chip.height - Treadle::textHeight(metaScale)) * 0.5f,
                                     label, accent, metaScale);
                    cursorRight = chip.x - 4.0f;
                };
    
                if(metadataRoom >= 282.0f){
                    headerChip("FPS", fpsText, 55.0f, {0.36f, 0.95f, 0.61f, 1.0f});
                    headerChip("CAM", cameraName, 102.0f, {0.18f, 0.88f, 1.0f, 1.0f});
                    headerChip("SCN", sceneName, 128.0f, theme.accent);
                }else if(metadataRoom >= 232.0f){
                    headerChip("FPS", fpsText, 50.0f, {0.36f, 0.95f, 0.61f, 1.0f});
                    headerChip("CAM", cameraName, 78.0f, {0.18f, 0.88f, 1.0f, 1.0f});
                    headerChip("SCN", sceneName, 96.0f, theme.accent);
                }else if(metadataRoom >= 182.0f){
                    headerChip("FPS", fpsText, 44.0f, {0.36f, 0.95f, 0.61f, 1.0f});
                    headerChip("CAM", cameraName, 62.0f, {0.18f, 0.88f, 1.0f, 1.0f});
                    headerChip("SCN", sceneName, 68.0f, theme.accent);
                }else if(metadataRoom > 0.0f){
                    const std::string summary = Treadle::fitText("SCN " + sceneName + "  CAM " + cameraName + "  " + fpsText + " FPS",
                                                                  metadataRoom - 12.0f, metaScale);
                    const Treadle::Rect chip{metadataStart, bar.y + 8.0f, metadataRoom, bar.height - 16.0f};
                    ui.canvas().rect(chip, Treadle::Color{theme.accent.r, theme.accent.g, theme.accent.b, 0.10f});
                    ui.canvas().outline(chip, 1.0f, Treadle::Color{theme.accent.r, theme.accent.g, theme.accent.b, 0.48f});
                    ui.canvas().text(chip.x + 6.0f, chip.y + (chip.height - Treadle::textHeight(metaScale)) * 0.5f,
                                     summary, theme.title, metaScale);
                    cursorRight = metadataStart;
                }
    
                const float statusRoom = std::max(0.0f, cursorRight - metadataStart - 4.0f);
                if(statusRoom > 8.0f){
                    const float statusScale = theme.textScale * 0.78f;
                    const std::string fitted = Treadle::fitText(status, statusRoom, statusScale);
                    ui.canvas().text(cursorRight - Treadle::textWidth(fitted, statusScale), bar.y + 13.0f,
                                     fitted, colour, statusScale);
                }
            }
    
            const float drawerWidth = std::min(layout.viewport.width,
                std::clamp(layout.viewport.width * 0.30f, 260.0f, 360.0f));
            const Treadle::Rect drawerBox{layout.viewport.x + layout.viewport.width - drawerWidth, layout.viewport.y,
                                           drawerWidth, layout.viewport.height};
            {
                Treadle::DrawList& canvas = ui.canvas();
                canvas.rect(layout.rail, theme.panel);
                ui.region("loom-rail-surface", layout.rail);
                if(railReveal > 0.72f){
                    canvas.marble(layout.rail, Treadle::Color{0.30f, 0.48f, 0.34f, 0.25f},
                                  Treadle::Color{0.02f, 0.035f, 0.025f, 0.22f}, input.timeSeconds);
                    canvas.rect(layout.rail.x + layout.rail.width - 1.0f, layout.rail.y, 1.0f,
                                layout.rail.height, theme.panelEdge);
                    auto railItem = [&](RailPane pane, const std::string& label, int row){
                        const float side = layout.rail.width - 12.0f;
                        const float stride = std::min(62.0f, std::max(34.0f, (layout.rail.height - 16.0f) / 9.0f));
                        const Treadle::Rect box{layout.rail.x + 6.0f, layout.rail.y + 8.0f + float(row) * stride,
                                                side, std::min(54.0f, stride - 3.0f)};
                        const Treadle::Ui::Region hit = ui.region("loom-rail-" + label, box);
                        if(hit.pressed){
                            if(pane == RailPane::Timeline){
                                proceduraPanel.open = false;
                                timelineVisible = !timelineVisible;
                            }else if(pane == RailPane::Terminal){
                                proceduraPanel.open = false;
                                terminal.visible = !terminal.visible;
                                if(terminal.visible) terminal.unreadError = false;
                            }else if(pane == RailPane::Compositor){
                                //COMPOSITOR: vlastiti radni prostor; prvi put zadani graf nad plocom kamere
                                proceduraPanel.open = false;
                                compositor.open = !compositor.open;
                                if(compositor.open){
                                    motionPanel.open = false;
                                    autoRig.open = false;
                                    std::string video;
                                    if(const Warp::Entity* through = stage.get(view.lookThrough); through && through->camera) video = through->camera->plate;
                                    if(video.empty() && !browser.videos.empty()) video = browser.videos.front().string();
                                    Loom::compositorDefaultGraph(compositor, video);
                                }
                            }else if(pane == RailPane::Motion){
                                compositor.open = false;
                                proceduraPanel.open = false;
                                if(motionPanel.open){ motionPanel.open = false; autoRig.open = false; }
                                else openMotionWorkflow();
                            }else if(pane == RailPane::Procedura){
                                compositor.open = false;
                                motionPanel.open = false;
                                autoRig.open = false;
                                proceduraPanel.open = !proceduraPanel.open;
                                if(proceduraPanel.open) frameProceduraCurve();
                                activeRailPane = proceduraPanel.open ? RailPane::Procedura : RailPane::None;
                            }else if(pane == RailPane::Scene || pane == RailPane::Components){
                                proceduraPanel.open = false;
                                motionPanel.open = false;
                                autoRig.open = false;
                                if(pane == RailPane::Scene) outlineVisible = !outlineVisible;
                                else componentsVisible = !componentsVisible;
                                activeRailPane = RailPane::None;
                            }else{
                                proceduraPanel.open = false;
                                motionPanel.open = false;
                                autoRig.open = false;
                                activeRailPane = activeRailPane == pane ? RailPane::None : pane;
                            }
                        }
                        const bool active = pane == RailPane::Compositor ? compositor.open : pane == RailPane::Motion ? motionPanel.open :
                            (pane == RailPane::Procedura ? proceduraPanel.open :
                            (pane == RailPane::Scene ? outlineVisible :
                             (pane == RailPane::Components ? componentsVisible :
                              (pane == RailPane::Timeline ? timelineVisible :
                               (pane == RailPane::Terminal ? terminal.visible : activeRailPane == pane)))));
                        const Treadle::Color ink = active ? theme.title : theme.dim;
                        if(active) canvas.rect(box, Treadle::Color{theme.accent.r, theme.accent.g, theme.accent.b, 0.16f});
                        else if(hit.hot) canvas.rect(box, theme.hot);
                        canvas.outline(box, active ? 1.5f : 1.0f, active ? theme.accent : theme.panelEdge);
    
                        const float cx = box.x + box.width * 0.5f;
                        const float iy = box.y + 5.0f;
                        if(pane == RailPane::Media){
                            canvas.folderIcon(cx - 9.0f, iy + 1.0f, 18.0f, ink);
                            canvas.triangle(cx - 2.0f, iy + 4.0f, cx - 2.0f, iy + 12.0f, cx + 5.0f, iy + 8.0f,
                                            active ? theme.accent : ink);
                        }else if(pane == RailPane::Scene){
                            const float cy = iy + 9.0f;
                            canvas.line(cx, cy - 6.0f, cx - 8.0f, cy + 5.0f, 1.4f, ink);
                            canvas.line(cx, cy - 6.0f, cx + 8.0f, cy + 5.0f, 1.4f, ink);
                            canvas.line(cx - 8.0f, cy + 5.0f, cx + 8.0f, cy + 5.0f, 1.4f, ink);
                            canvas.rect(cx - 2.5f, cy - 9.0f, 5.0f, 5.0f, ink);
                            canvas.rect(cx - 10.5f, cy + 3.0f, 5.0f, 5.0f, ink);
                            canvas.rect(cx + 5.5f, cy + 3.0f, 5.0f, 5.0f, ink);
                        }else if(pane == RailPane::Procedura){
                            canvas.outline(Treadle::Rect{cx - 12.0f, iy + 2.0f, 9.0f, 7.0f}, 1.4f, ink);
                            canvas.outline(Treadle::Rect{cx + 3.0f, iy + 11.0f, 9.0f, 7.0f}, 1.4f, ink);
                            canvas.line(cx - 3.0f, iy + 5.5f, cx + 3.0f, iy + 14.5f, 1.4f, active ? theme.accent : ink);
                        }else if(pane == RailPane::Compositor){
                            //Dva cvora i veza: A preko B
                            canvas.outline(Treadle::Rect{cx - 12.0f, iy + 2.0f, 9.0f, 7.0f}, 1.4f, ink);
                            canvas.outline(Treadle::Rect{cx + 3.0f, iy + 11.0f, 9.0f, 7.0f}, 1.4f, ink);
                            canvas.line(cx - 3.0f, iy + 5.5f, cx + 3.0f, iy + 14.5f, 1.4f, active ? theme.accent : ink);
                        }else if(pane == RailPane::Components){
                            for(int i = 0; i < 3; ++i){
                                const float y = iy + 4.0f + float(i) * 6.0f;
                                canvas.line(cx - 8.0f, y + 2.0f, cx + 8.0f, y + 2.0f, 1.5f, ink);
                                canvas.rect(cx - 8.0f + float((i + 1) % 3) * 6.0f, y, 4.0f, 4.0f,
                                            active ? theme.accent : theme.title);
                            }
                        }else if(pane == RailPane::Timeline){
                            canvas.line(cx - 11.0f, iy + 4.0f, cx + 11.0f, iy + 4.0f, 1.4f, ink);
                            canvas.line(cx - 11.0f, iy + 17.0f, cx + 11.0f, iy + 17.0f, 1.4f, ink);
                            for(int tick = 0; tick < 4; ++tick){
                                const float x = cx - 9.0f + float(tick) * 6.0f;
                                canvas.line(x, iy + 5.0f, x, iy + 12.0f, 1.2f, ink);
                            }
                            canvas.line(cx + 2.0f, iy + 2.0f, cx + 2.0f, iy + 19.0f, 2.0f, theme.accent);
                        }else if(pane == RailPane::Terminal){
                            canvas.outline(Treadle::Rect{cx - 11.0f, iy + 2.0f, 22.0f, 17.0f}, 1.2f, ink);
                            canvas.text(cx - 8.0f, iy + 3.0f, ">_", ink, theme.textScale * 0.7f);
                            if(terminal.unreadError) canvas.rect(cx + 8.0f, iy, 5.0f, 5.0f, theme.warning);
                            else if(job.running || motionLive.active) canvas.rect(cx + 8.0f, iy, 5.0f, 5.0f, theme.accent);
                        }else if(pane == RailPane::AiChat){
                            canvas.outline(Treadle::Rect{cx - 10.0f, iy + 2.0f, 20.0f, 14.0f}, 1.3f, ink);
                            canvas.line(cx - 3.0f, iy + 16.0f, cx - 7.0f, iy + 20.0f, 1.3f, ink);
                            canvas.line(cx - 7.0f, iy + 20.0f, cx + 1.0f, iy + 16.0f, 1.3f, ink);
                            canvas.rect(cx - 5.0f, iy + 8.0f, 2.0f, 2.0f, ink);
                            canvas.rect(cx - 1.0f, iy + 8.0f, 2.0f, 2.0f, ink);
                            canvas.rect(cx + 3.0f, iy + 8.0f, 2.0f, 2.0f, ink);
                        }else{
                            const Treadle::Color axes[3] = {{1.0f, 0.18f, 0.28f, 1.0f},
                                                            {0.20f, 1.0f, 0.42f, 1.0f},
                                                            {0.24f, 0.55f, 1.0f, 1.0f}};
                            canvas.line(cx - 10.0f, iy + 14.0f, cx - 3.0f, iy + 5.0f, 1.8f, axes[0]);
                            canvas.line(cx - 3.0f, iy + 5.0f, cx + 3.0f, iy + 12.0f, 1.8f, axes[1]);
                            canvas.line(cx + 3.0f, iy + 12.0f, cx + 10.0f, iy + 3.0f, 1.8f, axes[2]);
                        }
                        const float labelScale = theme.textScale * 0.58f;
                        canvas.text(cx - Treadle::textWidth(label, labelScale) * 0.5f, box.y + box.height - 19.0f,
                                    label, ink, labelScale);
                    };
                    railItem(RailPane::Media, "MEDIA", 0);
                    railItem(RailPane::Scene, "SCENE", 1);
                    railItem(RailPane::Components, "COMP", 2);
                    railItem(RailPane::Motion, "MOTION", 3);
                    railItem(RailPane::Procedura, "PROC", 4);
                    railItem(RailPane::Timeline, "TIME", 5);
                    railItem(RailPane::Terminal, "TERM", 6);
                    railItem(RailPane::AiChat, "AI CHAT", 7);
                    railItem(RailPane::Compositor, "COMPOSE", 8);
            }else{
                const float handleY = layout.rail.y + layout.rail.height * 0.5f;
                const Treadle::Rect handleBox{2.0f, handleY - 18.0f, 8.0f, 36.0f};
                const Treadle::Ui::Region handle = ui.region("loom-rail-handle", handleBox);
                const float handleAlpha = std::clamp(1.0f - railReveal / 0.72f, 0.0f, 1.0f);
                const Treadle::Color handleFill = handle.hot
                    ? Treadle::Color{theme.accent.r, theme.accent.g, theme.accent.b, 0.36f * handleAlpha}
                    : Treadle::Color{theme.panelEdge.r, theme.panelEdge.g, theme.panelEdge.b, 0.58f * handleAlpha};
                canvas.rect(handleBox, handleFill);
                canvas.outline(handleBox, 1.0f, Treadle::Color{theme.panelEdge.r, theme.panelEdge.g,
                                                               theme.panelEdge.b, 0.9f});
                const float midY = handleY;
                canvas.triangle(3.0f, midY - 5.0f, 3.0f, midY + 5.0f, 8.0f, midY,
                                Treadle::Color{theme.text.r, theme.text.g, theme.text.b, handleAlpha});
            }
        }

        //== MEDIA ================================================================================
        if(activeRailPane == RailPane::Media){
            ui.dock("MEDIA", drawerBox, &mediaScroll);
        {
            ui.label("PROJECT MEDIA");
            if(stage.media.empty()) ui.label("(Add a video below)");
            for(size_t i = 0; i < stage.media.size(); ++i){
                const Warp::Media& media = stage.media[i];
                std::string label = fs::path(media.path).filename().string();
                if(job.running && media.path == jobVideo) label = "* " + label;
                else if(!media.result.empty()) label += "  [SOLVED]";
                const AssetStyle style = assetStyle(fs::path(media.path), "video");
                if(ui.assetRow(label, style.badge, focus == Focus::Media && selectedMedia == int(i), style.colour)){
                    selectedMedia = int(i);
                    focus = Focus::Media;
                }
                if(ui.rightClicked()){
                    selectedMedia = int(i);
                    focus = Focus::Media;
                    menuMedia = int(i);
                    ui.openMenu("Media");
                }
            }
            ui.separator();
            ui.label("FILE BROWSER");
            if(ui.button("Auto Rig from Model")) openAutoRig();
            ui.label(tail(browser.at.string(), size_t(std::max(8.0f, (drawerBox.width - 30.0f) / 12.0f))));
            if(ui.selectable("..  Parent folder", false)){
                browser.at = browser.at.parent_path();
                browser.refresh();
                mediaScroll = 0.0f;
            }
            //Ulazak u mapu tek nakon petlje: refresh() puni browser.folders iznova, pa bi petlja
            //nastavila po oslobodenoj memoriji (i 'folder' bi pokazivao u nju)
            fs::path enter;
            for(const fs::path& folder : browser.folders){
                if(ui.folderRow(folder.filename().string(), false)) enter = folder;
            }
            if(!enter.empty()){
                browser.at = enter;
                browser.refresh();
                mediaScroll = 0.0f;
            }
            for(const fs::path& video : browser.videos){
                const AssetStyle style = assetStyle(video, "video");
                if(ui.assetRow(video.filename().string(), style.badge, false, style.colour)){
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
                const AssetStyle style = assetStyle(project, "project");
                if(ui.assetRow(project.filename().string(), style.badge, project == projectPath, style.colour)){
                    pendingProject = project;
                    ui.openMenu("Project");
                }
            }
            for(const fs::path& motion : browser.motions){
                const AssetStyle style = assetStyle(motion, "motion");
                if(ui.assetRow(motion.filename().string(), style.badge, false, style.colour)){
                    importMotion(motion, motionPanel.targetCharacter);
                }
            }
            for(const fs::path& model : browser.models){
                const AssetStyle style = assetStyle(model, "model");
                if(ui.assetRow(model.filename().string(), style.badge, false, style.colour))
                    importModelAsset(model, layout.viewport);
                if(ui.rightClicked()){
                    menuModelAsset = model;
                    ui.openMenu("Model Asset");
                }
            }
            //Slike samo dok mapa materijala ceka sliku - inace bi popis bio pun tekstura
            if(materialState.armed()){
                for(const fs::path& image : browser.images){
                    const AssetStyle style = assetStyle(image, "image");
                    if(ui.assetRow(image.filename().string(), style.badge, false, style.colour)){
                        if(Loom::assignArmedImage(stage, materialState, image.string())) message = "Texture: " + image.filename().string();
                    }
                }
            }
            for(const fs::path& result : browser.results){
                const AssetStyle style = assetStyle(result, "result");
                if(ui.assetRow(result.filename().string(), style.badge, false, style.colour)){
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
            //Gaussian splat (.ply) sam za sebe: ide u scenu kao splat. Pogled crta prvi vidljivi, pa
            //se ostali sakriju - inace bi klik "ne radio nista"
            for(const fs::path& splatFile : browser.splats){
                if(ui.assetRow(splatFile.filename().string(), "SPLAT", false, {0.95f, 0.55f, 0.95f, 1.0f})) importSplatFile(splatFile);
            }
        }

        }

        if(activeRailPane == RailPane::AiChat){
            ui.dock("AI CHAT", drawerBox);
            ui.label("In Progress");
        }
        Warp::Id hoveredAtlasId = Warp::None;
        Treadle::Rect hoveredAtlasRect;

        //== SCENE ATLAS ======================================================================
        if(outlineVisible && !motionPanel.open && !proceduraPanel.open && !compositor.open){
            ui.dock("SCENE ATLAS", layout.hierarchy, &hierarchyScroll);
            const Treadle::Rect addBox{layout.hierarchy.x + layout.hierarchy.width - 33.0f,
                                        layout.hierarchy.y + 5.0f, 24.0f, 24.0f};
            const Treadle::Ui::Region addHit = ui.region("scene-atlas-add", addBox);
            ui.canvas().rect(addBox, addHit.hot ? theme.hot : theme.control);
            ui.canvas().outline(addBox, 1.0f, theme.accent);
            ui.canvas().text(addBox.x + (addBox.width - Treadle::textWidth("+", theme.textScale)) * 0.5f,
                             addBox.y + (addBox.height - Treadle::textHeight(theme.textScale)) * 0.5f,
                             "+", theme.title, theme.textScale);
            if(addHit.pressed) ui.openMenu("Atlas");
            ui.label("OBJECTS  " + std::to_string(stage.size()));
            if(!selectionTrail.empty()){
                ui.label("RECENT SELECTIONS");
                const size_t count = std::min<size_t>(3, selectionTrail.size());
                const float innerWidth = std::max(0.0f, layout.hierarchy.width - 2.0f * theme.padding);
                const float chipWidth = (innerWidth - theme.spacing * float(count - 1)) / float(count);
                std::vector<Warp::Id> visibleTrail;
                std::vector<std::string> trailLabels;
                for(size_t i = 0; i < count; ++i){
                    const Warp::Entity* recent = stage.get(selectionTrail[i]);
                    if(!recent) continue;
                    visibleTrail.push_back(recent->id);
                    trailLabels.push_back(recent->name);
                }
                const int trailClick = ui.chipRow(trailLabels, theme.accent);
                if(trailClick >= 0 && trailClick < int(visibleTrail.size())){
                    selected = visibleTrail[size_t(trailClick)];
                    focus = Focus::Entity;
                }
            }
            ui.separator();
            if(stage.size() == 0) ui.label("(Empty - import a result)");
            int hiddenBelow = -1;
            stage.walk([&](const Warp::Entity& entity, int depth){
                if(hiddenBelow >= 0 && depth > hiddenBelow) return;
                hiddenBelow = -1;
                const bool expanded = collapsed.count(entity.id) == 0;
                if(!expanded) hiddenBelow = depth;
                const Treadle::Ui::TreeClick click = ui.atlasRow(
                    entity.name, sceneTag(entity), depth, !entity.children.empty(), expanded,
                    focus == Focus::Entity && selected == entity.id, sceneAccent(entity), entity.visible);
                if(ui.lastRowHovered()){
                    hoveredAtlasId = entity.id;
                    hoveredAtlasRect = ui.lastRowRect();
                }
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
                    ui.openMenu("Entity");
                }
            });
            if(stage.size() > 0) ui.label("Click + to import or add");
        }

        //== COMPONENTS =============================================================================
        if((componentsVisible || motionPanel.open || proceduraPanel.open) && !compositor.open){
            ui.dock("INSTRUMENT DECK", layout.properties, &propertiesScroll);
        {
            Warp::Entity* entity = focus == Focus::Entity ? stage.get(selected) : nullptr;
            const Warp::Entity* atlasPreview = hoveredAtlasId != Warp::None ? stage.get(hoveredAtlasId) : nullptr;
            if(atlasPreview) ui.linkedPreview(atlasPreview->name, sceneTag(*atlasPreview), sceneAccent(*atlasPreview));
            else ui.linkedPreview("Hover an item in Scene Atlas", "PREVIEW", theme.accent);
            if(focus == Focus::Media && selectedMedia >= 0 && selectedMedia < int(stage.media.size())){
                const Warp::Media media = stage.media[size_t(selectedMedia)];
                ui.value("Video", Treadle::fitText(fs::path(media.path).filename().string(), 150.0f, theme.textScale));
                char text[64];
                std::snprintf(text, sizeof(text), "%u x %u", media.width, media.height);
                ui.value("Resolution", text);
                std::snprintf(text, sizeof(text), "%u @ %.2f fps", media.frames, media.framesPerSecond);
                ui.value("Frames", text);
                ui.value("Result", media.result.empty() ? "None" : "Available");
                ui.separator();
                std::vector<std::string> stepLabels;
                for(int value : steps) stepLabels.push_back(std::to_string(value));
                ui.choice("Solve every Nth frame", stepLabels, &stepIndex);
                ui.slider("Max frames", &frameCount, 30.0f, 600.0f);
                ui.slider("Training steps", &trainSteps, 1000.0f, 30000.0f);
                if(ui.button("Solve Cameras")) startSolve(selectedMedia, false);
                if(ui.button("Solve + Gaussian splat")) startSolve(selectedMedia, true);
                if(!media.result.empty() && ui.button("Open Result")) importFolder(media.result, media.path);
                if(!media.result.empty() && fs::is_directory(fs::path(media.result) / "images") && !job.running &&
                   ui.button("Train Splat from Result")) startTrain(media.result);
                ui.label("(Also available from right-click)");
            }else if(entity){
                std::vector<Warp::Id> hierarchyIds;
                std::vector<std::string> hierarchyNames;
                for(const Warp::Entity* ancestor = entity; ancestor;){
                    hierarchyIds.push_back(ancestor->id);
                    hierarchyNames.push_back(ancestor->name);
                    ancestor = ancestor->parent == Warp::None ? nullptr : stage.get(ancestor->parent);
                }
                std::reverse(hierarchyIds.begin(), hierarchyIds.end());
                std::reverse(hierarchyNames.begin(), hierarchyNames.end());
                const int breadcrumbClick = ui.breadcrumb(hierarchyNames);
                if(breadcrumbClick >= 0 && breadcrumbClick < int(hierarchyIds.size())){
                    selected = hierarchyIds[size_t(breadcrumbClick)];
                    focus = Focus::Entity;
                }
                ui.selectionCard(entity->name, kindOf(*entity), sceneAccent(*entity), entity->visible);
                //Grupa lika iz pokreta: vidi se odakle je pokret
                if(!entity->children.empty() && stage.get(entity->children.front()) &&
                   stage.get(entity->children.front())->joint && !entity->joint){
                    ui.value("Motion", Engine::WeaverMotion::poweredBy);
                }
                ui.checkbox("Visible", &entity->visible);
                const Warp::Id animatorRig = Loom::motionCharacterForEntity(stage, entity->id);
                Warp::Entity* rigEntity = stage.get(animatorRig);
                if(rigEntity && ui.componentHeader("ANIMATOR", {0.26f, 0.94f, 0.58f, 1.0f}, &animatorExpanded,
                                               rigEntity->animator.has_value())){
                    const Loom::MotionRigRestPose rigRest = Loom::motionRigRestPose(stage, animatorRig);
                    std::array<Warp::Id, 52> uniRigIds;
                    const bool verifiedUniRig = Loom::motionFindVerifiedUniRig52(rigRest, uniRigIds);
                    if(verifiedUniRig){
                        ui.value("Rest pose", rigEntity->animator && rigEntity->animator->relaxedUniRigPose
                            ? "Relaxed hands" : "UniRig pose available");
                        if((!rigEntity->animator || !rigEntity->animator->relaxedUniRigPose) &&
                           ui.button("Apply Natural Pose")){
                            size_t changed = 0;
                            if(Loom::applyUniRigRelaxedRestPose(stage, animatorRig, &changed)){
                                playing = false;
                                eulerFor = Warp::None;
                                message = "Natural stance applied to shoulders, elbows, wrists, and fingers.";
                            }else message = "Could not apply the UniRig natural pose.";
                        }
                    }
                    ui.separator();
                    if(!rigEntity->animator){
                        if(ui.button("Add Animator")){
                            Loom::ensureRigAnimator(stage, animatorRig);
                            message = "Animator added to " + rigEntity->name;
                        }
                    }else{
                        Warp::Animator& animator = *rigEntity->animator;
                        ui.checkbox("Follow character in viewport", &followAnimatorPreview);
                        if(ui.button("Add New Animation")){
                            motionPanel.targetCharacter = animatorRig;
                            motionPanel.rootPathEnabled = false;
                            motionPanel.rootPathAutoEnd = false;
                            motionPanel.rootPathAutoDistance = false;
                            motionPanel.constrainRootHeading = false;
                            motionPanel.constraintsPath.clear();
                            motionPanel.rootWaypoints.assign(1, Loom::MotionRootWaypoint{});
                            motionPanel.selectedRootWaypoint = 0;
                            motionPanel.rootTrackCursorFrame = 0.0f;
                            followAnimatorPreview = true;
                            motionPanel.actions.assign(1, Loom::MotionAction{});
                            motionPanel.activeAction = 0;
                            motionPanel.open = true;
                            motionPanelScroll = 0.0f;
                            ui.focusTextField("action0");
                            message = "Describe the new animation for " + rigEntity->name;
                        }
                        if(ui.button("Animate Along a Path")){
                            if(motionPanel.targetCharacter != animatorRig || motionPanel.actions.empty()){
                                motionPanel.actions.assign(1, Loom::MotionAction{});
                                motionPanel.activeAction = 0;
                            }
                            motionPanel.targetCharacter = animatorRig;
                            followAnimatorPreview = false;
                            motionPanel.open = true;
                            motionPanel.rootPathEnabled = true;
                            motionPanel.rootPathAutoEnd = true;
                            motionPanel.rootPathAutoDistance = true;
                            motionPanel.constraintsPath.clear();
                            const int last = std::max(89, Loom::kimodoMotionLastFrame(motionPanel.actions));
                            motionPanel.rootWaypoints = {Loom::MotionRootWaypoint{},
                                Loom::motionDefaultRootEnd(last, motionPanel.firstHeadingAngle)};
                            motionPanel.rootWaypoints.front().heading = motionPanel.firstHeadingAngle;
                            motionPanel.selectedRootWaypoint = 1;
                            motionPanel.rootTrackCursorFrame = float(last);
                            motionPanelScroll = 0.0f;
                            playing = false;
                            if(!animator.animations.empty())
                                frame = animator.animations[std::min(animator.activeAnimation, animator.animations.size() - 1)].startFrame;
                            motionPathAnchor = motionRootAnchor(animatorRig);
                            view.lookThrough = Warp::None;
                            const glm::vec3 rootAt = glm::vec3(stage.worldMatrix(motionPathAnchor, stage.startFrame)[3]);
                            const Loom::MotionRootWaypoint& end = motionPanel.rootWaypoints.back();
                            view.orbit.target = glm::vec3(rootAt.x + 0.5f * end.x,
                                                          std::max(0.7f, rootAt.y),
                                                          rootAt.z + 0.5f * end.z);
                            view.orbit.distance = std::max(view.orbit.distance,
                                glm::length(glm::vec2(end.x, end.z)) * 1.8f + 2.0f);
                            ui.focusTextField("action0");
                            message = "Drag path points in the viewport, add more points, then generate.";
                        }
                        if(animator.animations.empty()){
                            ui.label("No animations yet.");
                        }else{
                            animator.activeAnimation = std::min(animator.activeAnimation, animator.animations.size() - 1);
                            std::vector<std::string> animationNames;
                            for(const Warp::AnimationClip& clip : animator.animations) animationNames.push_back(clip.name);
                            int previewIndex = int(animator.activeAnimation);
                            if(ui.choice("Preview animation", animationNames, &previewIndex) &&
                               previewIndex >= 0 && previewIndex < int(animator.animations.size())){
                                animator.activeAnimation = size_t(previewIndex);
                                frame = animator.animations[animator.activeAnimation].startFrame;
                                playing = false;
                                followAnimatorPreview = true;
                            }
                            const int navigation = ui.buttonRow({"< Previous", "Next >"});
                            if(navigation >= 0){
                                const int count = int(animator.animations.size());
                                const int direction = navigation == 0 ? -1 : 1;
                                animator.activeAnimation = size_t((int(animator.activeAnimation) + direction + count) % count);
                                frame = animator.animations[animator.activeAnimation].startFrame;
                                playing = false;
                                followAnimatorPreview = true;
                            }
                            Warp::AnimationClip& active = animator.animations[animator.activeAnimation];
                            ui.checkbox("Loop animation", &active.loop);
                            ui.checkbox("In place (keep origin still)", &active.inPlace);
                            char frameRange[64];
                            std::snprintf(frameRange, sizeof(frameRange), "%.0f - %.0f", active.startFrame, active.endFrame);
                            ui.value("Frames", frameRange);

                            ui.separator();
                            ui.caption("POSE KEYS");
                            const double poseFrame = std::round(frame);
                            const bool frameInClip = poseFrame >= active.startFrame && poseFrame <= active.endFrame;
                            //Poza trenutnog kljuca se procita iz scene: uredjuje se zivo u klipu (rig u
                            //pogledu, Transform), pa je ono sto stoji u kadru kljuca upravo uredjena poza
                            auto storeCurrentKey = [&]{
                                Loom::PoseKey key{poseEdit.frame, {}};
                                for(const auto& joint : poseEdit.basePose)
                                    if(stage.get(joint.first)) key.pose.emplace_back(joint.first, stage.localAt(joint.first, poseEdit.frame));
                                auto found = std::find_if(poseEdit.keys.begin(), poseEdit.keys.end(),
                                    [&](const Loom::PoseKey& k){ return k.frame == poseEdit.frame; });
                                if(found != poseEdit.keys.end()) *found = key;
                                else poseEdit.keys.push_back(key);
                                std::sort(poseEdit.keys.begin(), poseEdit.keys.end(),
                                    [](const Loom::PoseKey& a, const Loom::PoseKey& b){ return a.frame < b.frame; });
                            };
                            if(!poseEdit.active){
                                if(frameInClip && ui.button("Edit pose at this frame")){
                                    poseEdit = PoseEditSession{};
                                    poseEdit.active = true;
                                    animatorRigDrag.control = -1;
                                    motionPanel.open = false;
                                    followAnimatorPreview = true;
                                    poseEdit.rig = animatorRig;
                                    poseEdit.animation = animator.activeAnimation;
                                    poseEdit.frame = poseFrame;
                                    frame = poseFrame;
                                    poseEdit.original = active;
                                    selected = animatorRig;
                                    focus = Focus::Entity;
                                    motionPanel.controlRigMode = true;
                                    const Loom::MotionRigRestPose rigPose = Loom::motionRigRestPose(stage, animatorRig);
                                    for(const Loom::MotionRigJointRest& joint : rigPose.joints)
                                        if(stage.get(joint.id)) poseEdit.basePose.emplace_back(joint.id, stage.localAt(joint.id, poseFrame));
                                    playing = false;
                                    message = "Pose key at frame " + std::to_string(int(poseFrame)) +
                                              ". Shape it, then add a second key - the pose blends between keys.";
                                }
                                if(frameInClip) ui.hint("Edit the pose at two or more frames; between them the pose blends key to key.");
                                else ui.hint("Move the playhead inside the clip to edit its pose.");
                            }else{
                                const bool editTarget = poseEdit.rig == animatorRig &&
                                    poseEdit.animation == animator.activeAnimation;
                                if(!editTarget) ui.status("Return to the clip being edited to continue.", theme.warning);
                                //Kljucevi kao pilule; trenutni je oznacen. Klik prebaci uredjivanje na taj kadar
                                std::vector<double> keyFrames;
                                for(const Loom::PoseKey& k : poseEdit.keys) keyFrames.push_back(k.frame);
                                if(std::find(keyFrames.begin(), keyFrames.end(), poseEdit.frame) == keyFrames.end())
                                    keyFrames.push_back(poseEdit.frame);
                                std::sort(keyFrames.begin(), keyFrames.end());
                                std::vector<std::string> keyLabels;
                                int current = -1;
                                for(size_t i = 0; i < keyFrames.size() && i < 8; ++i){
                                    keyLabels.push_back(std::to_string(int(keyFrames[i])));
                                    if(keyFrames[i] == poseEdit.frame) current = int(i);
                                }
                                const int picked = ui.pills(keyLabels, current);
                                if(editTarget && picked >= 0 && keyFrames[size_t(picked)] != poseEdit.frame){
                                    storeCurrentKey();
                                    poseEdit.frame = keyFrames[size_t(picked)];
                                    frame = poseEdit.frame;
                                    playing = false;
                                    animatorRigDrag.control = -1;
                                }
                                const bool isKey = std::find(keyFrames.begin(), keyFrames.end(), poseFrame) != keyFrames.end();
                                if(editTarget && frameInClip && !isKey && keyFrames.size() < 8){
                                    if(ui.button("+ Key at frame " + std::to_string(int(poseFrame)))){
                                        storeCurrentKey();
                                        poseEdit.frame = poseFrame;
                                        playing = false;
                                        animatorRigDrag.control = -1;
                                    }
                                }else if(editTarget && poseFrame != poseEdit.frame && isKey){
                                    ui.hint("Click the key above to edit it.");
                                }
                                if(editTarget && keyFrames.size() > 1 && ui.button("Remove key " + std::to_string(int(poseEdit.frame)))){
                                    poseEdit.keys.erase(std::remove_if(poseEdit.keys.begin(), poseEdit.keys.end(),
                                        [&](const Loom::PoseKey& k){ return k.frame == poseEdit.frame; }), poseEdit.keys.end());
                                    poseEdit.frame = poseEdit.keys.empty() ? poseEdit.frame : poseEdit.keys.front().frame;
                                    frame = poseEdit.frame;
                                }
                                ui.hint("Shape each key in the viewport. Between keys the pose blends key to key; "
                                        "the original motion only leads in before the first key.");
                                ui.slider("Blend in", &poseBlendInFrames, 0.0f, 30.0f, " fr");
                                ui.choice("After last key", {"Return to motion", "Hold pose"}, &poseKeyEnding);
                                if(poseKeyEnding == 0){
                                    ui.slider("Hold", &poseBlendHoldFrames, 0.0f, 180.0f, " fr");
                                    ui.slider("Blend out", &poseBlendOutFrames, 1.0f, 60.0f, " fr");
                                }
                                poseBlendInFrames = std::round(poseBlendInFrames);
                                poseBlendHoldFrames = std::round(poseBlendHoldFrames);
                                poseBlendOutFrames = std::round(poseBlendOutFrames);
                                const int poseAction = ui.buttonRow({"Blend keys", "Keys only", "Cancel"});
                                if(poseAction == 2){
                                    if(Warp::Entity* owner = stage.get(poseEdit.rig); owner && owner->animator &&
                                       poseEdit.animation < owner->animator->animations.size())
                                        owner->animator->animations[poseEdit.animation] = poseEdit.original;
                                    poseEdit = PoseEditSession{};
                                    message = "Pose edit cancelled; original clip restored.";
                                }else if(poseAction >= 0 && editTarget){
                                    storeCurrentKey();
                                    const std::vector<Loom::PoseKey> keys = poseEdit.keys;
                                    //Kljucevi su procitani; klip se vrati na original pa se iz njega racuna
                                    //ulaz prije prvog i povratak iza zadnjeg kljuca (LoomPoseBlend.h)
                                    active = poseEdit.original;
                                    Loom::PoseKeySettings settings;
                                    settings.inFrames = poseBlendInFrames;
                                    settings.holdFrames = poseBlendHoldFrames;
                                    settings.outFrames = poseBlendOutFrames;
                                    settings.ending = poseKeyEnding == 0 ? Loom::PoseKeyEnding::Return : Loom::PoseKeyEnding::Hold;
                                    settings.blendBetween = poseAction == 0;
                                    Loom::applyPoseKeys(stage, keys, active.startFrame, active.endFrame, settings);
                                    frame = keys.front().frame;
                                    playing = false;
                                    poseEdit = PoseEditSession{};
                                    message = std::string(poseAction == 0 ? "Blended " : "Set ") + std::to_string(keys.size()) +
                                              (keys.size() == 1 ? " pose key" : " pose keys") +
                                              (poseAction == 0 ? " key to key." : " (only the key frames changed).");
                                }
                            }

                            std::vector<MotionPlaybackCheckpoint> fallbackCheckpoints;
                            const std::vector<MotionPlaybackCheckpoint>* checkpoints = nullptr;
                            const auto rigChecks = motionPlaybackCheckpoints.find(animatorRig);
                            if(rigChecks != motionPlaybackCheckpoints.end()){
                                const auto clipChecks = rigChecks->second.find(animator.activeAnimation);
                                if(clipChecks != rigChecks->second.end()) checkpoints = &clipChecks->second;
                            }
                            if(!checkpoints || checkpoints->empty()){
                                fallbackCheckpoints.push_back({"Clip start", active.startFrame, active.startFrame});
                                const double middle = (active.startFrame + active.endFrame) * 0.5;
                                fallbackCheckpoints.push_back({"Clip middle", middle, middle});
                                fallbackCheckpoints.push_back({"Clip end", active.endFrame, active.endFrame});
                                checkpoints = &fallbackCheckpoints;
                            }
                            size_t currentCheckpoint = 0;
                            double nearestCheckpointDistance = std::numeric_limits<double>::max();
                            for(size_t i = 0; i < checkpoints->size(); ++i){
                                const MotionPlaybackCheckpoint& check = (*checkpoints)[i];
                                if(frame >= check.first - 0.5 && frame <= check.last + 0.5){
                                    currentCheckpoint = i;
                                    nearestCheckpointDistance = 0.0;
                                    break;
                                }
                                const double midpoint = (check.first + check.last) * 0.5;
                                const double distance = std::fabs(frame - midpoint);
                                if(distance < nearestCheckpointDistance){
                                    nearestCheckpointDistance = distance;
                                    currentCheckpoint = i;
                                }
                            }

                            size_t skinnedMeshes = 0, skinJointLinks = 0;
                            size_t liveSkinnedMeshes = 0, renderedVertices = 0, movedVertices = 0;
                            float maxMeshDisplacement = 0.0f;
                            std::vector<Warp::Id> rigPending{animatorRig};
                            size_t rigVisited = 0;
                            while(!rigPending.empty() && rigVisited++ < stage.size()){
                                const Warp::Entity* item = stage.get(rigPending.back());
                                rigPending.pop_back();
                                if(!item) continue;
                                if(item->model && item->model->skin >= 0){
                                    ++skinnedMeshes;
                                    skinJointLinks += item->model->skinJoints.size();
                                    if(const Loom::ViewportMeshes::SkinningDebug* debug = viewportMeshes.skinningDebug(item->id)){
                                        if(debug->rendered) ++liveSkinnedMeshes;
                                        renderedVertices += debug->vertices;
                                        movedVertices += debug->movedVertices;
                                        maxMeshDisplacement = std::max(maxMeshDisplacement, debug->maxDisplacement);
                                    }
                                }
                                rigPending.insert(rigPending.end(), item->children.begin(), item->children.end());
                            }
                            size_t jointTracks = 0, totalKeys = 0, invalidKeys = 0, movedBones = 0;
                            auto finiteVector = [](const glm::vec3& value){
                                return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
                            };
                            for(const Warp::AnimatorTrack& track : active.tracks){
                                const Warp::Entity* target = stage.get(track.target);
                                if(target && target->joint) ++jointTracks;
                                for(double time : track.translationKeys.times) if(!std::isfinite(time)) ++invalidKeys;
                                for(double time : track.rotationKeys.times) if(!std::isfinite(time)) ++invalidKeys;
                                for(double time : track.scaleKeys.times) if(!std::isfinite(time)) ++invalidKeys;
                                for(const glm::vec3& value : track.translationKeys.values){ ++totalKeys; if(!finiteVector(value)) ++invalidKeys; }
                                for(const glm::vec3& value : track.scaleKeys.values){ ++totalKeys; if(!finiteVector(value)) ++invalidKeys; }
                                for(const glm::quat& value : track.rotationKeys.values){
                                    ++totalKeys;
                                    const float norm = glm::length(value);
                                    if(!std::isfinite(norm) || norm < 0.5f || norm > 1.5f) ++invalidKeys;
                                }
                            }
                            const glm::mat4 rigWorld = stage.worldMatrix(animatorRig, frame);
                            const glm::mat4 rigWorldInverse = glm::inverse(rigWorld);
                            for(const Loom::MotionRigJointRest& joint : rigRest.joints){
                                const glm::mat4 relative = rigWorldInverse * stage.worldMatrix(joint.id, frame);
                                const glm::quat atFrame = Loom::motionRotationOf(relative);
                                const glm::quat atRest = Loom::motionRotationOf(joint.matrix);
                                if(std::fabs(glm::dot(atFrame, atRest)) < 0.9995f) ++movedBones;
                            }
                            ui.separator();
                            ui.label("PLAYBACK CHECK");
                            const size_t expectedBones = verifiedUniRig ? size_t(52) : rigRest.joints.size();
                            const std::string skinAndTracks = skinnedMeshes
                                ? std::to_string(skinnedMeshes) + " mesh " + std::to_string(skinJointLinks) + "J / " +
                                  std::to_string(jointTracks) + "/" + std::to_string(expectedBones)
                                : "NO SKIN · " + std::to_string(jointTracks) + " tracks";
                            ui.value("Skin / tracks", Treadle::fitText(skinAndTracks, 154.0f, theme.textScale));
                            char keyStatusBuffer[64];
                            std::snprintf(keyStatusBuffer, sizeof(keyStatusBuffer), "%zu moved · %.1fk %s",
                                          movedBones, double(totalKeys) / 1000.0,
                                          invalidKeys == 0 ? "OK" : "bad");
                            const std::string keyStatus = keyStatusBuffer;
                            ui.value("Pose / keys", Treadle::fitText(keyStatus, 154.0f, theme.textScale));
                            char meshDeformationBuffer[80];
                            if(skinnedMeshes == 0){
                                std::snprintf(meshDeformationBuffer, sizeof(meshDeformationBuffer), "NO SKINNED MESH");
                            }else if(liveSkinnedMeshes == 0){
                                std::snprintf(meshDeformationBuffer, sizeof(meshDeformationBuffer), "NO LIVE SKIN DEFORMER");
                            }else{
                                std::snprintf(meshDeformationBuffer, sizeof(meshDeformationBuffer),
                                              "%zu live %.1fk/%.1fk d%.3f", liveSkinnedMeshes,
                                              double(movedVertices) / 1000.0, double(renderedVertices) / 1000.0,
                                              double(maxMeshDisplacement));
                            }
                            ui.value("Mesh deformation", Treadle::fitText(meshDeformationBuffer, 154.0f, theme.textScale));
                            const glm::vec3 originAtStart = glm::vec3(stage.worldMatrix(animatorRig, active.startFrame)[3]);
                            const glm::vec3 originAtFrame = glm::vec3(stage.worldMatrix(animatorRig, frame)[3]);
                            const glm::vec3 originTravel = originAtFrame - originAtStart;
                            char rootTravelBuffer[80];
                            std::snprintf(rootTravelBuffer, sizeof(rootTravelBuffer), "%.2f m · X %.2f · Z %.2f",
                                          double(glm::length(glm::vec2(originTravel.x, originTravel.z))),
                                          double(originTravel.x), double(originTravel.z));
                            ui.value("Origin travel", Treadle::fitText(rootTravelBuffer, 154.0f, theme.textScale));
                            std::string poseAxisText = "axis unavailable";
                            if(verifiedUniRig){
                                const glm::vec3 hips(glm::vec3(stage.worldMatrix(uniRigIds[0], frame)[3]));
                                const glm::vec3 head(glm::vec3(stage.worldMatrix(uniRigIds[5], frame)[3]));
                                const glm::vec3 axis = head - hips;
                                const glm::vec3 rigUp = Loom::motionRotationOf(rigWorld) * glm::vec3(0.0f, 1.0f, 0.0f);
                                const float up = glm::length(axis) > 1e-5f
                                    ? glm::dot(glm::normalize(axis), glm::normalize(rigUp)) : 0.0f;
                                const std::string stepName = (*checkpoints)[currentCheckpoint].label;
                                std::string lowerStep = stepName;
                                std::transform(lowerStep.begin(), lowerStep.end(), lowerStep.begin(),
                                    [](unsigned char c){ return char(std::tolower(c)); });
                                const bool rollStep = lowerStep.find("roll") != std::string::npos;
                                const char* orientation = up > 0.45f ? "up" : (up < -0.45f ? "inverted" : "level");
                                char axisText[64];
                                std::snprintf(axisText, sizeof(axisText), "%s %.2f%s", orientation, double(up),
                                              up < -0.45f && rollStep ? " roll" : "");
                                poseAxisText = axisText;
                            }
                            std::string shortAction = (*checkpoints)[currentCheckpoint].label;
                            if(shortAction == "Peace sign") shortAction = "Peace";
                            else if(shortAction == "Forward roll") shortAction = "Fwd roll";
                            else if(shortAction == "Walk + wave") shortAction = "Walk+wave";
                            const std::string actionPose = shortAction + " / " + poseAxisText;
                            ui.value("Action / pose", Treadle::fitText(actionPose, 154.0f, theme.textScale));
                            const int checkpointStep = ui.buttonRow({"< Step", "Next Step >"});
                            if(checkpointStep >= 0 && !checkpoints->empty()){
                                const size_t count = checkpoints->size();
                                const size_t next = checkpointStep == 0
                                    ? (currentCheckpoint + count - 1) % count
                                    : (currentCheckpoint + 1) % count;
                                frame = std::clamp(std::round(((*checkpoints)[next].first +
                                                               (*checkpoints)[next].last) * 0.5),
                                                   active.startFrame, active.endFrame);
                                playing = false;
                                followAnimatorPreview = true;
                            }
                        }

                    }
                }
                if(ui.componentHeader("TRANSFORM", theme.accent, &transformExpanded)){
                //TRANSFORMACIJA U OVOM KADRU. Brzina vucenja je iz velicine scene: solve nema
                //metre, pa bi stalni korak u jednoj snimci bio nevidljiv, a u drugoj golem
                Warp::Transform local = stage.localAt(entity->id, frame);
                const Warp::AnimatorTrack* activeBoneTrack = entity->joint ? stage.activeAnimatorTrack(entity->id) : nullptr;
                const bool editingAnimatorBone = entity->joint && rigEntity && rigEntity->animator && rigEntity->animator->enabled &&
                    rigEntity->animator->activeAnimation < rigEntity->animator->animations.size();
                if(editingAnimatorBone) ui.label("Bone edits key the selected Animator clip at this frame.");
                bool edited = false;
                float translation[3] = {local.translation.x, local.translation.y, local.translation.z};
                if(ui.dragVector("Position", translation, extent.radius * 0.004f)){
                    local.translation = glm::vec3(translation[0], translation[1], translation[2]);
                    edited = true;
                }
                if(eulerFor != entity->id || eulerFrame != frame){
                    eulerCache = glm::degrees(glm::eulerAngles(local.rotation));
                    eulerFor = entity->id;
                    eulerFrame = frame;
                }
                float rotation[3] = {eulerCache.x, eulerCache.y, eulerCache.z};
                if(ui.dragVector("Rotation (deg)", rotation, 0.5f)){
                    eulerCache = glm::vec3(rotation[0], rotation[1], rotation[2]);
                    local.rotation = glm::normalize(glm::quat(glm::radians(eulerCache)));
                    edited = true;
                }
                float scale[3] = {local.scale.x, local.scale.y, local.scale.z};
                const float scaleSpeed = std::max(1e-5f, (std::fabs(scale[0]) + std::fabs(scale[1]) + std::fabs(scale[2])) * 0.0015f);
                if(ui.dragVector("Scale", scale, scaleSpeed)){
                    local.scale = glm::vec3(scale[0], scale[1], scale[2]);
                    edited = true;
                }
                //Jednoliko mjerilo: za kocku, i za grupu solvea kad se scena svodi na metre
                float uniform = 1.0f;
                if(ui.dragFloat("Uniform scale", &uniform, 0.004f) && uniform > 0.0f){
                    local.scale *= uniform;
                    edited = true;
                }
                if(edited) stage.setLocalAt(entity->id, frame, local);
                const Warp::Track<glm::vec3>* translationTrack = activeBoneTrack ? &activeBoneTrack->translationKeys : &entity->translationKeys;
                const Warp::Track<glm::quat>* rotationTrack = activeBoneTrack ? &activeBoneTrack->rotationKeys : &entity->rotationKeys;
                const Warp::Track<glm::vec3>* scaleTrack = activeBoneTrack ? &activeBoneTrack->scaleKeys : &entity->scaleKeys;
                const bool hasTransformKeys = !translationTrack->empty() || !rotationTrack->empty() || !scaleTrack->empty();
                if(hasTransformKeys) ui.label("(Animated properties get a key at this frame)");
                if(hasTransformKeys){
                    ui.value("Keyframes", std::to_string(std::max({translationTrack->size(), rotationTrack->size(), scaleTrack->size()})));
                }
                }
                if(entity->camera && ui.componentHeader("CAMERA", {0.16f, 0.86f, 1.0f, 1.0f}, &cameraExpanded)){
                    char text[64];
                    std::snprintf(text, sizeof(text), "%.0f px", double(entity->camera->focalPixels));
                    ui.value("Focal length", text);
                    std::snprintf(text, sizeof(text), "%u x %u", entity->camera->width, entity->camera->height);
                    ui.value("Frame size", text);
                    if(!entity->camera->plate.empty()){
                        ui.value("Video", Treadle::fitText(fs::path(entity->camera->plate).filename().string(), 150.0f,
                                                            theme.textScale));
                    }
                    if(ui.button(view.lookThrough == entity->id ? "Exit Camera View" : "Look Through Camera")){
                        view.lookThrough = view.lookThrough == entity->id ? Warp::None : entity->id;
                    }
                    if(!entity->camera->plate.empty()){
                        ui.checkbox("Show Video Plate (V)", &showPlate);
                        ui.slider("Plate Brightness", &plateBrightness, 0.0f, 1.0f);
                        char plateText[64];
                        std::snprintf(plateText, sizeof(plateText), "%lld", (long long)plateShown);
                        if(view.lookThrough == entity->id && showPlate) ui.value("Plate Frame", plateText);
                    }
                }
                if(entity->points && ui.componentHeader("POINT CLOUD", {0.43f, 0.92f, 0.54f, 1.0f}, &pointsExpanded)){
                    ui.value("Points", std::to_string(entity->points->positions.size()));
                    ui.value("Colors", entity->points->colours.empty() ? "No" : "Yes");
                }
                if(entity->splat && ui.componentHeader("GAUSSIAN SPLAT", {0.92f, 0.44f, 0.95f, 1.0f}, &splatExpanded)){
                    int splatVisibility = showSplat ? 0 : 1;
                    if(ui.choice("Render", {"Visible", "Not visible"}, &splatVisibility)){
                        showSplat = splatVisibility == 0;
                    }
                    ui.label(Treadle::fitText(entity->splat->path, layout.properties.width - 30.0f, theme.textScale));
                    if(ui.button("Open in Splat Viewer")){
                        char command[1400];
                        std::snprintf(command, sizeof(command), "./SplatViewer \"%s\" 1 16 0 0 pogled.png 3 0 \"%s\" &",
                                      entity->splat->path.c_str(),
                                      fs::path(entity->splat->path).parent_path().string().c_str());
                        if(std::system(command) != 0){ /* preglednik javlja sam */ }
                    }
                    //REZANJE I CISCENJE (LoomSplatCut.h). Rezati se moze samo splat koji pogled
                    //crta - maska zivih je njegova
                    ui.separator();
                    ui.label("CLEAN UP");
                    const bool shownHere = showSplat && entity->visible && viewportSplat.path() == entity->splat->path &&
                                           !viewportSplat.isLoading();
                    const bool cutPending = shownHere && viewportSplat.hasCuts();
                    const bool cleanable = Loom::canCleanFloaters(entity->splat->path);
                    if(ui.button(cleanable ? "Clean Floaters" : "Clean Floaters (needs solve result)") && cleanable &&
                       !job.running && !cutPending){
                        std::string output;
                        const std::string command = Loom::cleanFloatersCommand(LOOM_ROOT_DIR, entity->splat->path, output);
                        splatCleanTarget = entity->id;
                        splatCleanOutput = output;
                        startJob(command, fs::path(entity->splat->path).parent_path().string(), Loom::Task::Clean, 0);
                        message = "Cleaning floaters...";
                    }
                    if(cutPending) ui.label("Save or undo the cut before cleaning.");
                    //PROXY MESH: geometrija za zaklanjanje CG-a (holdout u Blenderu/Nukeu)
                    ui.separator();
                    ui.label("PROXY MESH");
                    if(ui.button(cleanable ? "Make Proxy Mesh (whole scene)" : "Make Proxy Mesh (needs solve result)") &&
                       cleanable && !job.running){
                        proxyTarget = entity->id;
                        proxyOutput = Loom::proxyOutputPath(entity->splat->path, false);
                        startJob(Loom::proxyMeshCommand(LOOM_ROOT_DIR, entity->splat->path, proxyOutput, nullptr),
                                 fs::path(entity->splat->path).parent_path().string(), Loom::Task::Proxy, 0);
                        message = "Building proxy mesh...";
                    }
                    if(ui.button(cleanable ? "Make Scene Blockers (walls, floor)" : "Make Scene Blockers (needs solve result)") &&
                       cleanable && !job.running){
                        proxyTarget = entity->id;
                        proxyOutput = Loom::proxyOutputPath(entity->splat->path, false, true);
                        startJob(Loom::proxyMeshCommand(LOOM_ROOT_DIR, entity->splat->path, proxyOutput, nullptr, true),
                                 fs::path(entity->splat->path).parent_path().string(), Loom::Task::Proxy, 0);
                        message = "Fitting scene blockers...";
                    }
                    ui.label(Treadle::fitText("One object: Cut Box, then Blocker Box", layout.properties.width - 30.0f, theme.textScale));
                    if(ui.button("Add Cut Box") && shownHere){
                        //Velicina iz vecine splata (okvir scene splat ne broji), na mjestu u koje se gleda
                        float size = std::max(1e-3f, extent.radius * 0.25f);
                        glm::vec3 centre;
                        float radius = 0.0f;
                        if(viewportSplat.bounds(centre, radius)){
                            size = 0.3f * radius * glm::length(glm::vec3(stage.worldMatrix(entity->id, frame)[0]));
                        }
                        selected = Loom::addCutBox(stage, view.orbit.target, size);
                        focus = Focus::Entity;
                        message = "Cut box: move/rotate/scale it, then Delete Inside/Outside";
                    }
                    if(shownHere){
                        ui.value("Gaussians", std::to_string(viewportSplat.count()) + " of " + std::to_string(viewportSplat.total()));
                    }
                    if(cutPending){
                        ui.value("Cut (unsaved)", std::to_string(viewportSplat.cutAway()));
                        if(ui.button("Undo Cut")) viewportSplat.undoCut();
                        if(ui.button("Save Cut Splat")){
                            const std::string out = Loom::cutOutputPath(entity->splat->path);
                            const std::string problem = viewportSplat.saveCut(out);
                            if(problem.empty()){
                                const bool same = out == entity->splat->path;
                                entity->splat->path = out;
                                if(same) viewportSplat.reload();
                                browser.refresh();
                                message = "Saved " + fs::path(out).filename().string();
                            }else{
                                message = "Save failed: " + problem;
                            }
                        }
                    }
                }
                //Odabrana kocka reze splat koji se crta (LoomSplatCut.h)
                if(Loom::isCube(entity) && showSplat && splatShownId != Warp::None && !viewportSplat.isLoading() &&
                   ui.componentHeader("SPLAT CUT", {1.0f, 0.30f, 0.42f, 1.0f}, &splatCutExpanded)){
                    const glm::mat4 box = Loom::cubeFromSplat(stage, entity->id, splatShownId, frame);
                    const size_t cuts = viewportSplat.cutAway();
                    if(box != cutBoxSeen || cuts != cutCountSeen || viewportSplat.path() != cutPathSeen){
                        cutInside = viewportSplat.countInBox(box);
                        cutBoxSeen = box;
                        cutCountSeen = cuts;
                        cutPathSeen = viewportSplat.path();
                    }
                    ui.value("Inside", std::to_string(cutInside) + " of " + std::to_string(viewportSplat.count()));
                    const Warp::Entity* proxySplat = stage.get(splatShownId);
                    if(proxySplat && proxySplat->splat && Loom::canCleanFloaters(proxySplat->splat->path) &&
                       ui.button("Make Proxy Mesh from Box") && !job.running){
                        const glm::mat4 splatFromBox = glm::inverse(box);
                        proxyTarget = splatShownId;
                        proxyOutput = Loom::proxyOutputPath(proxySplat->splat->path, true);
                        startJob(Loom::proxyMeshCommand(LOOM_ROOT_DIR, proxySplat->splat->path, proxyOutput, &splatFromBox),
                                 fs::path(proxySplat->splat->path).parent_path().string(), Loom::Task::Proxy, 0);
                        message = "Building proxy mesh inside the box...";
                    }
                    if(proxySplat && proxySplat->splat && Loom::canCleanFloaters(proxySplat->splat->path) &&
                       ui.button("Make Blocker Box") && !job.running){
                        const glm::mat4 splatFromBox = glm::inverse(box);
                        proxyTarget = splatShownId;
                        proxyOutput = Loom::proxyOutputPath(proxySplat->splat->path, true, true);
                        startJob(Loom::proxyMeshCommand(LOOM_ROOT_DIR, proxySplat->splat->path, proxyOutput, &splatFromBox, true),
                                 fs::path(proxySplat->splat->path).parent_path().string(), Loom::Task::Proxy, 0);
                        message = "Fitting a blocker box to what is inside...";
                    }
                    if(ui.button("Delete Inside")) message = "Deleted " + std::to_string(viewportSplat.cutBox(box, true));
                    if(ui.button("Delete Outside")) message = "Deleted " + std::to_string(viewportSplat.cutBox(box, false));
                    if(viewportSplat.hasCuts()){
                        if(ui.button("Undo Cut")) viewportSplat.undoCut();
                        ui.label("Save from the splat's properties.");
                    }
                }
                if((entity->mesh || entity->model) &&
                   ui.componentHeader("MATERIAL", theme.accent, &materialExpanded, true)){
                    Loom::materialPanel(ui, stage, *entity, materialState);
                }
                ui.separator();
                if(ui.button("Delete (Del)")) removeSelected(entity->id);
            }else{
                ui.label("Nothing selected.");
            }
        }

        }

        //== TIMELINE =============================================================================
        if(timelineVisible){
            const Treadle::Rect& area = layout.timeline;
            Treadle::DrawList& canvas = ui.canvas();
            canvas.rect(area, Treadle::Color{0.055f, 0.068f, 0.052f, 1.0f});
            const Warp::Entity* timelineSelection = stage.get(selected);
            const Treadle::Color selectionTint = timelineSelection ? sceneAccent(*timelineSelection) : theme.panelEdge;
            canvas.rect(area.x, area.y, area.width, 1.0f, selectionTint);
            canvas.rect(area.x, area.y + 1.0f, area.width, 1.0f,
                        Treadle::Color{selectionTint.r, selectionTint.g, selectionTint.b, 0.14f});

            const float rowY = area.y + 8.0f, rowH = 26.0f;
            auto [toStart, a1] = toolButton("|<", area.x + 10.0f, rowY, rowH);
            if(toStart) frame = stage.startFrame;
            auto [back, a2] = toolButton("<", a1, rowY, rowH);
            if(back) frame = std::max(stage.startFrame, std::floor(frame) - 1.0);
            auto [play, a3] = toolButton(playing ? "Pause" : "Play", a2, rowY, rowH, playing);
            if(play) togglePlayback();
            auto [forward, a4] = toolButton(">", a3, rowY, rowH);
            if(forward) frame = std::min(stage.endFrame, std::floor(frame) + 1.0);
            auto [toEnd, a5] = toolButton(">|", a4, rowY, rowH);
            if(toEnd) frame = stage.endFrame;

            //Kljucevi odabranog: skok, postavljanje, brisanje
            const bool haveEntity = stage.get(selected) != nullptr;
            auto [previousKey, b1] = toolButton("<K", a5 + 16.0f, rowY, rowH);
            auto [nextKey, b2] = toolButton("K>", b1, rowY, rowH);
            auto [setKey, b3] = toolButton("Key (K)", b2, rowY, rowH);
            auto [dropKey, b4] = toolButton("Delete Key", b3, rowY, rowH);
            double jump = 0.0;
            if(previousKey && haveEntity && stage.neighbourKey(selected, frame, -1, jump)) frame = jump;
            if(nextKey && haveEntity && stage.neighbourKey(selected, frame, +1, jump)) frame = jump;
            if(setKey && haveEntity) stage.keyAll(selected, std::round(frame));
            if(dropKey && haveEntity) stage.eraseKeysAt(selected, std::round(frame));

            //Raspon: od i do glave, ili cijelo - od prvog do zadnjeg kljuca u sceni
            auto [rangeFrom, c1] = toolButton("From", b4 + 16.0f, rowY, rowH);
            auto [rangeTo, c2] = toolButton("To", c1, rowY, rowH);
            auto [rangeAll, c3] = toolButton("All", c2, rowY, rowH);
            if(rangeFrom) stage.startFrame = std::min(std::round(frame), stage.endFrame - 1.0);
            if(rangeTo) stage.endFrame = std::max(std::round(frame), stage.startFrame + 1.0);
            if(rangeAll){
                double low = 1e18, high = -1e18;
                auto includeTimes = [&](const std::vector<double>& times){
                    if(!times.empty()){ low = std::min(low, times.front()); high = std::max(high, times.back()); }
                };
                stage.walk([&](const Warp::Entity& e, int){
                    includeTimes(e.translationKeys.times); includeTimes(e.rotationKeys.times); includeTimes(e.scaleKeys.times);
                    if(e.animator) for(const Warp::AnimationClip& clip : e.animator->animations)
                        for(const Warp::AnimatorTrack& track : clip.tracks){
                            includeTimes(track.translationKeys.times); includeTimes(track.rotationKeys.times); includeTimes(track.scaleKeys.times);
                        }
                });
                if(high > low){ stage.startFrame = low; stage.endFrame = high; }
            }
            frame = std::clamp(frame, stage.startFrame, stage.endFrame);

            char text[96];
            std::snprintf(text, sizeof(text), "Frame %d   (%.0f - %.0f, %.0f fps)", int(std::floor(frame)),
                          stage.startFrame, stage.endFrame, stage.framesPerSecond);
            canvas.text(c3 + 16.0f, rowY + 6.0f, text, theme.text, theme.textScale);

            //Traka: kadrovi, kljucevi odabranog, glava
            const float trackTop = rowY + rowH + 12.0f;
            const float availableTrackHeight = area.y + area.height - trackTop - 12.0f;
            const bool showMotionPathTrack = motionPanel.open && motionPanel.flowMode != 2;
            const float rootTrackHeight = showMotionPathTrack ? (motionPanel.flowMode == 1 ? 68.0f : 52.0f) : 0.0f;
            const float stageTrackHeight = std::max(motionPanel.open ? 18.0f : 24.0f,
                                                    availableTrackHeight - (showMotionPathTrack ? rootTrackHeight + 4.0f : 0.0f));
            const Treadle::Rect track{area.x + 16.0f, trackTop, area.width - 32.0f, stageTrackHeight};
            const Treadle::Rect rootTrack{track.x, track.y + track.height + 4.0f, track.width, rootTrackHeight};
            const float labelGutter = motionPanel.open ? 62.0f : 0.0f;
            const Treadle::Rect contentTrack{track.x + labelGutter, track.y,
                std::max(1.0f, track.width - labelGutter), track.height};

            struct MotionTimelineAction{
                int source = 0;
                int part = 0;
                int partCount = 1;
                int firstFrame = 0;
                int lastFrame = 0;
                int groupFirstFrame = 0;
                int groupFrames = 0;
                std::string prompt;
            };
            std::vector<Loom::MotionAction> timelineSources;
            if(showMotionPathTrack){
                if(motionPanel.flowMode == 1) timelineSources.push_back(motionPanel.directedAction);
                else timelineSources = motionPanel.actions;
            }
            if(motionPanel.flowMode == 0 && !motionPanel.resizingTimelineAction)
                motionPanel.selectedTimelineAction = motionPanel.activeAction;
            std::vector<MotionTimelineAction> timelineActions;
            int motionFrameEnd = 0;
            for(size_t source = 0; source < timelineSources.size(); ++source){
                std::vector<Loom::MotionAction> parts = Loom::filledActions({timelineSources[source]});
                if(parts.empty()) parts.push_back(Loom::MotionAction{"", timelineSources[source].duration});
                std::vector<int> partFrames;
                int groupFrames = 0;
                for(const Loom::MotionAction& part : parts){
                    const int frames = std::max(1, Loom::kimodoMotionFrameCount({Loom::MotionAction{"motion", part.duration}}));
                    partFrames.push_back(frames);
                    groupFrames += frames;
                }
                const int groupFirst = motionFrameEnd;
                for(size_t part = 0; part < parts.size(); ++part){
                    MotionTimelineAction item;
                    item.source = int(source);
                    item.part = int(part);
                    item.partCount = int(parts.size());
                    item.firstFrame = motionFrameEnd;
                    item.lastFrame = motionFrameEnd + partFrames[part];
                    item.groupFirstFrame = groupFirst;
                    item.groupFrames = groupFrames;
                    item.prompt = parts[part].prompt;
                    timelineActions.push_back(std::move(item));
                    motionFrameEnd += partFrames[part];
                }
            }
            const double sceneFps = std::max(1.0, stage.framesPerSecond);
            int maxActionParts = 1;
            for(size_t source = 0; source < timelineSources.size(); ++source)
                maxActionParts = std::max(maxActionParts, int(Loom::filledActions({timelineSources[source]}).size()));
            const int resizeSlackFrames = showMotionPathTrack ? 300 * maxActionParts : 0;
            const double motionEnd = stage.startFrame + double(motionFrameEnd + resizeSlackFrames) *
                                     sceneFps / double(Loom::kimodoMotionFps);
            double timelineEnd = std::max(stage.endFrame, motionEnd);
            if(motionPanel.resizingTimelineAction && motionPanel.timelineDragDisplayEndFrame > stage.startFrame)
                timelineEnd = motionPanel.timelineDragDisplayEndFrame;
            const double span = std::max(1.0, timelineEnd - stage.startFrame);
            auto xOf = [&](double f){ return contentTrack.x + float((f - stage.startFrame) / span) * contentTrack.width; };
            auto sceneFrameFromMotionFrame = [&](double f){
                return stage.startFrame + f * sceneFps / double(Loom::kimodoMotionFps);
            };
            auto motionFrameAtX = [&](float x, int lastFrame){
                const double sceneFrame = stage.startFrame + double(x - contentTrack.x) /
                    std::max(1.0f, contentTrack.width) * span;
                const double sourceFrame = (sceneFrame - stage.startFrame) * double(Loom::kimodoMotionFps) / sceneFps;
                return std::clamp(int(std::lround(sourceFrame)), 0, std::max(0, lastFrame));
            };

            canvas.rect(track, Treadle::Color{0.105f, 0.125f, 0.095f, 1.0f});
            if(motionPanel.open){
                const Warp::Entity* timelineEntity = stage.get(selected);
                const std::string keyLabel = timelineEntity ? "KEYS  /  " + timelineEntity->name : "SCENE KEYS";
                canvas.text(track.x + 7.0f, track.y + 12.0f,
                            Treadle::fitText(keyLabel, labelGutter - 10.0f, 1.0f), theme.dim, 1.0f);
            }
            // Shared ruler: scene-frame units, with Kimodo's 30 Hz keys converted to scene time.
            const double pixelsPerFrame = double(contentTrack.width) / span;
            double tick = 1.0;
            for(double candidate : {1.0, 2.0, 5.0, 10.0, 20.0, 25.0, 50.0, 100.0, 200.0, 250.0, 500.0, 1000.0, 2000.0, 5000.0}){
                tick = candidate;
                if(candidate * pixelsPerFrame >= 70.0) break;
            }
            for(double f = std::ceil(stage.startFrame / tick) * tick; f <= timelineEnd; f += tick){
                const float x = xOf(f);
                canvas.rect(x, track.y, 1.0f, 8.0f, theme.dim);
                canvas.text(x + 3.0f, track.y + 3.0f, std::to_string(int(f)), theme.dim, 1.0f);
            }
            if(const Warp::Entity* entity = stage.get(selected)){
                const Warp::AnimatorTrack* activeTrack = stage.activeAnimatorTrack(selected);
                const Warp::Track<glm::vec3>& translationTrack = activeTrack ? activeTrack->translationKeys : entity->translationKeys;
                const Warp::Track<glm::quat>& rotationTrack = activeTrack ? activeTrack->rotationKeys : entity->rotationKeys;
                const std::vector<double>& times = translationTrack.size() >= rotationTrack.size()
                                                   ? translationTrack.times : rotationTrack.times;
                float lastKey = -10.0f;
                for(double t : times){
                    const float x = xOf(t);
                    if(x - lastKey < 2.0f) continue;
                    canvas.rect(x, track.y + track.height - 12.0f, 1.5f, 10.0f, Treadle::Color{1.0f, 0.78f, 0.25f, 0.8f});
                    lastKey = x;
                }
            }
            const float head = xOf(frame);
            canvas.rect(head - 1.0f, track.y - 4.0f, 2.0f, track.height + 8.0f, Treadle::Color{0.95f, 0.35f, 0.30f, 1.0f});

            // Motion lanes use the same x mapping as scene keys and the frame ruler above.
            if(showMotionPathTrack){
                const int lastFrame = std::max(0, motionFrameEnd - 1);
                const float actionTop = rootTrack.y + 17.0f;
                const float pathTop = rootTrack.y + 34.0f;
                const float poseTop = rootTrack.y + 50.0f;
                const float laneHeight = 12.0f;
                const float markerY = pathTop + laneHeight * 0.5f;
                const float actionBottom = actionTop + laneHeight;
                const float pathBottom = pathTop + laneHeight;
                const float poseBottom = poseTop + laneHeight;
                canvas.rect(rootTrack, Treadle::Color{0.075f, 0.092f, 0.070f, 1.0f});
                canvas.text(rootTrack.x + 7.0f, rootTrack.y + 4.0f,
                            "MOTION TRACKS  /  aligned to scene time", theme.dim, 1.0f);
                canvas.rect(rootTrack.x, actionTop, rootTrack.width, laneHeight, Treadle::Color{0.10f, 0.12f, 0.09f, 1.0f});
                canvas.rect(rootTrack.x, pathTop, rootTrack.width, laneHeight, Treadle::Color{0.09f, 0.11f, 0.085f, 1.0f});
                canvas.text(rootTrack.x + 7.0f, actionTop + 2.0f, "ACTIONS", theme.dim, 1.0f);
                canvas.text(rootTrack.x + 7.0f, pathTop + 2.0f, "ROOT PATH", theme.dim, 1.0f);
                if(motionPanel.flowMode == 1){
                    canvas.rect(rootTrack.x, poseTop, rootTrack.width, laneHeight, Treadle::Color{0.085f, 0.105f, 0.105f, 1.0f});
                    canvas.text(rootTrack.x + 7.0f, poseTop + 2.0f, "POSES", theme.dim, 1.0f);
                }
                const Treadle::Color grid{0.75f, 0.82f, 0.72f, 0.13f};
                for(double f = std::ceil(stage.startFrame / tick) * tick; f <= timelineEnd; f += tick){
                    const float x = xOf(f);
                    canvas.line(x, actionTop, x, motionPanel.flowMode == 1 ? poseBottom : pathBottom, 0.55f, grid);
                }

                for(size_t i = 0; i < timelineActions.size(); ++i){
                    const MotionTimelineAction& item = timelineActions[i];
                    const float x0 = xOf(sceneFrameFromMotionFrame(item.firstFrame));
                    const float x1 = xOf(sceneFrameFromMotionFrame(item.lastFrame));
                    const bool chosen = item.source == motionPanel.selectedTimelineAction;
                    const Treadle::Color fill = chosen ? theme.title :
                        (item.source % 2 == 0 ? Treadle::Color{0.24f, 0.39f, 0.32f, 1.0f}
                                              : Treadle::Color{0.30f, 0.34f, 0.25f, 1.0f});
                    canvas.rect(x0, actionTop + 1.0f, std::max(2.0f, x1 - x0 - 1.0f), laneHeight - 2.0f, fill);
                    if(x1 - x0 > 38.0f){
                        std::string title = motionPanel.flowMode == 1 ? "DIRECT" : "ACTION " + std::to_string(item.source + 1);
                        if(item.partCount > 1) title += "  STEP " + std::to_string(item.part + 1);
                        char durationText[24];
                        std::snprintf(durationText, sizeof(durationText), "  %.2fs", double(timelineSources[size_t(item.source)].duration));
                        canvas.text(x0 + 4.0f, actionTop + 2.0f,
                                    Treadle::fitText(title + durationText, x1 - x0 - 12.0f, 1.0f),
                                    theme.text, 1.0f);
                    }
                    if(item.part + 1 == item.partCount){
                        const Treadle::Color grip = chosen ? theme.text : Treadle::Color{0.9f, 0.91f, 0.78f, 0.72f};
                        canvas.line(x1 - 4.0f, actionTop + 3.0f, x1 - 4.0f, actionBottom - 3.0f, 1.0f, grip);
                        canvas.line(x1 - 2.0f, actionTop + 3.0f, x1 - 2.0f, actionBottom - 3.0f, 1.0f, grip);
                    }
                }
                if(!motionPanel.rootPathEnabled || motionPanel.rootWaypoints.empty()){
                    canvas.text(contentTrack.x + 5.0f, pathTop + 2.0f, "Click to add a waypoint", theme.dim, 1.0f);
                }else{
                    for(size_t i = 1; i < motionPanel.rootWaypoints.size(); ++i){
                        canvas.line(xOf(sceneFrameFromMotionFrame(motionPanel.rootWaypoints[i - 1].frame)), markerY,
                                    xOf(sceneFrameFromMotionFrame(motionPanel.rootWaypoints[i].frame)), markerY,
                                    2.0f, theme.accent);
                    }
                    for(size_t i = 0; i < motionPanel.rootWaypoints.size(); ++i){
                        const Loom::MotionRootWaypoint& key = motionPanel.rootWaypoints[i];
                        const float x = xOf(sceneFrameFromMotionFrame(key.frame));
                        const Treadle::Color colour = int(i) == motionPanel.selectedRootWaypoint
                            ? theme.title : Treadle::Color{1.0f, 0.72f, 0.26f, 1.0f};
                        canvas.rect(x - 5.0f, markerY - 5.0f, 10.0f, 10.0f, colour);
                        if(int(i) == motionPanel.selectedRootWaypoint)
                            canvas.text(x + 7.0f, pathTop + 2.0f, "PATH " + std::to_string(i + 1), theme.text, 1.0f);
                    }
                }
                if(motionPanel.flowMode == 1){
                    for(size_t i = 0; i < motionPanel.poseConstraints.size(); ++i){
                        const float x = xOf(sceneFrameFromMotionFrame(motionPanel.poseConstraints[i].frame));
                        const Treadle::Color tint = int(i) == motionPanel.selectedPose ? theme.title :
                            Treadle::Color{0.39f, 0.86f, 1.0f, 1.0f};
                        canvas.rect(x - 5.0f, poseTop + 1.0f, 10.0f, laneHeight - 2.0f, tint);
                        if(int(i) == motionPanel.selectedPose)
                            canvas.text(x + 7.0f, poseTop + 2.0f, "POSE " + std::to_string(i + 1), theme.text, 1.0f);
                    }
                }

                const Treadle::Ui::Region rootEdit = ui.region("kimodo-root-constraints", rootTrack);
                const bool inTimelineContent = rootEdit.mouseX >= contentTrack.x;
                const bool actionLane = rootEdit.mouseY >= actionTop && rootEdit.mouseY < actionBottom;
                const bool pathLane = rootEdit.mouseY >= pathTop && rootEdit.mouseY < pathBottom;
                const bool poseLane = motionPanel.flowMode == 1 && rootEdit.mouseY >= poseTop && rootEdit.mouseY < poseBottom;
                auto nearestRootWaypoint = [&](float mouseX){
                    int nearest = -1;
                    float distance = 12.0f;
                    for(size_t i = 0; i < motionPanel.rootWaypoints.size(); ++i){
                        const float d = std::fabs(xOf(sceneFrameFromMotionFrame(motionPanel.rootWaypoints[i].frame)) - mouseX);
                        if(d < distance){ distance = d; nearest = int(i); }
                    }
                    return nearest;
                };
                auto nearestPoseConstraint = [&](float mouseX){
                    int nearest = -1;
                    float distance = 12.0f;
                    for(size_t i = 0; i < motionPanel.poseConstraints.size(); ++i){
                        const float d = std::fabs(xOf(sceneFrameFromMotionFrame(motionPanel.poseConstraints[i].frame)) - mouseX);
                        if(d < distance){ distance = d; nearest = int(i); }
                    }
                    return nearest;
                };
                if(rootEdit.pressed){
                    motionPanel.resizingTimelineAction = false;
                    motionPanel.timelineDragLane = 0;
                    motionPanel.draggingPoseLane = false;
                    if(inTimelineContent && actionLane){
                        motionPanel.timelineDragLane = 1;
                        int chosenSegment = -1;
                        float bestDistance = 12.0f;
                        for(size_t i = 0; i < timelineActions.size(); ++i){
                            const MotionTimelineAction& item = timelineActions[i];
                            const float x0 = xOf(sceneFrameFromMotionFrame(item.firstFrame));
                            const float x1 = xOf(sceneFrameFromMotionFrame(item.lastFrame));
                            const float distance = rootEdit.mouseX < x0 ? x0 - rootEdit.mouseX :
                                rootEdit.mouseX > x1 ? rootEdit.mouseX - x1 : 0.0f;
                            if(distance < bestDistance){ bestDistance = distance; chosenSegment = int(i); }
                        }
                        if(chosenSegment >= 0 && bestDistance < 12.0f){
                            const MotionTimelineAction& item = timelineActions[size_t(chosenSegment)];
                            motionPanel.selectedTimelineAction = item.source;
                            if(motionPanel.flowMode == 0)
                                motionPanel.activeAction = std::clamp(item.source, 0, int(motionPanel.actions.size()) - 1);
                            const float endX = xOf(sceneFrameFromMotionFrame(item.lastFrame));
                            if(item.part + 1 == item.partCount && std::fabs(rootEdit.mouseX - endX) <= 8.0f){
                                motionPanel.resizingTimelineAction = true;
                                motionPanel.timelineDragDisplayEndFrame = timelineEnd;
                            }
                            frame = std::clamp(sceneFrameFromMotionFrame(item.firstFrame), stage.startFrame, stage.endFrame);
                            playing = false;
                        }
                    }else if(inTimelineContent && poseLane){
                        motionPanel.timelineDragLane = 3;
                        const int frameAt = motionFrameAtX(rootEdit.mouseX, lastFrame);
                        auto found = std::lower_bound(motionPanel.poseConstraints.begin(), motionPanel.poseConstraints.end(), frameAt,
                            [](const Loom::MotionPoseConstraint& key, int f){ return key.frame < f; });
                        auto nearest = found;
                        if(found != motionPanel.poseConstraints.begin()){
                            auto previous = found - 1;
                            if(found == motionPanel.poseConstraints.end() ||
                               std::fabs(xOf(sceneFrameFromMotionFrame(previous->frame)) - rootEdit.mouseX) <
                               std::fabs(xOf(sceneFrameFromMotionFrame(found->frame)) - rootEdit.mouseX)) nearest = previous;
                        }
                        if(nearest != motionPanel.poseConstraints.end() &&
                           std::fabs(xOf(sceneFrameFromMotionFrame(nearest->frame)) - rootEdit.mouseX) < 10.0f){
                            motionPanel.selectedPose = int(nearest - motionPanel.poseConstraints.begin());
                        }else if(motionPanel.poseConstraints.size() < 20){
                            Loom::MotionPoseConstraint pose;
                            pose.frame = frameAt;
                            if(found != motionPanel.poseConstraints.begin()){
                                pose = *(found - 1);
                                pose.frame = frameAt;
                            }
                            motionPanel.selectedPose = int(motionPanel.poseConstraints.insert(found, pose) -
                                                           motionPanel.poseConstraints.begin());
                        }
                        motionPanel.draggingPoseLane = motionPanel.selectedPose >= 0;
                        frame = std::clamp(sceneFrameFromMotionFrame(frameAt), stage.startFrame, stage.endFrame);
                        playing = false;
                    }else if(inTimelineContent && pathLane){
                        motionPanel.timelineDragLane = 2;
                        motionPanel.rootPathEnabled = true;
                        const int nearest = nearestRootWaypoint(rootEdit.mouseX);
                        if(nearest >= 0){
                            motionPanel.selectedRootWaypoint = nearest;
                            motionPanel.rootTrackCursorFrame = float(motionPanel.rootWaypoints[size_t(nearest)].frame);
                        }else{
                            const int frameAt = motionFrameAtX(rootEdit.mouseX, lastFrame);
                            Loom::MotionRootWaypoint key = Loom::motionRootPathAt(motionPanel.rootWaypoints,
                                float(frameAt), motionPanel.smoothRootPath);
                            Loom::upsertMotionRootWaypoint(motionPanel.rootWaypoints, key, lastFrame);
                            const auto found = std::lower_bound(motionPanel.rootWaypoints.begin(), motionPanel.rootWaypoints.end(), key.frame,
                                [](const Loom::MotionRootWaypoint& item, int f){ return item.frame < f; });
                            motionPanel.selectedRootWaypoint = int(found - motionPanel.rootWaypoints.begin());
                            motionPanel.rootTrackCursorFrame = float(key.frame);
                        }
                        const int frameAt = motionPanel.rootWaypoints.empty() ? 0 :
                            motionPanel.rootWaypoints[size_t(motionPanel.selectedRootWaypoint)].frame;
                        frame = std::clamp(sceneFrameFromMotionFrame(frameAt), stage.startFrame, stage.endFrame);
                        playing = false;
                    }
                }
                if(rootEdit.held && motionPanel.timelineDragLane == 1 && motionPanel.resizingTimelineAction &&
                   motionPanel.selectedTimelineAction >= 0 &&
                   size_t(motionPanel.selectedTimelineAction) < timelineSources.size()){
                    const int sourceIndex = motionPanel.selectedTimelineAction;
                    const MotionTimelineAction* group = nullptr;
                    for(const MotionTimelineAction& item : timelineActions)
                        if(item.source == sourceIndex){ group = &item; break; }
                    if(group && group->partCount > 0){
                        const int targetFrame = motionFrameAtX(rootEdit.mouseX, std::max(lastFrame + resizeSlackFrames, 300));
                        const int pathEnd = motionPanel.rootWaypoints.empty() ? 0 : motionPanel.rootWaypoints.back().frame;
                        const int poseEnd = motionPanel.poseConstraints.empty() ? 0 : motionPanel.poseConstraints.back().frame;
                        const int minimumClipFrames = std::max(pathEnd, poseEnd) + 1;
                        const int otherFrames = std::max(0, motionFrameEnd - group->groupFrames);
                        const float minimumSeconds = std::max(1.0f,
                            float(minimumClipFrames - otherFrames) /
                            (float(Loom::kimodoMotionFps) * float(group->partCount)));
                        const float requestedSeconds = float(targetFrame - group->groupFirstFrame) /
                            (float(Loom::kimodoMotionFps) * float(group->partCount));
                        const float duration = std::clamp(std::round(requestedSeconds * 20.0f) / 20.0f,
                                                          std::min(10.0f, minimumSeconds), 10.0f);
                        if(motionPanel.flowMode == 1) motionPanel.directedAction.duration = duration;
                        else motionPanel.actions[size_t(sourceIndex)].duration = duration;
                    }
                }else if(rootEdit.held && motionPanel.timelineDragLane == 3 && motionPanel.draggingPoseLane &&
                         motionPanel.selectedPose >= 0 &&
                         size_t(motionPanel.selectedPose) < motionPanel.poseConstraints.size()){
                    const int targetFrame = motionFrameAtX(rootEdit.mouseX, lastFrame);
                    Loom::MotionPoseConstraint moving = motionPanel.poseConstraints[size_t(motionPanel.selectedPose)];
                    motionPanel.poseConstraints.erase(motionPanel.poseConstraints.begin() + motionPanel.selectedPose);
                    auto at = std::lower_bound(motionPanel.poseConstraints.begin(), motionPanel.poseConstraints.end(), targetFrame,
                        [](const Loom::MotionPoseConstraint& key, int f){ return key.frame < f; });
                    const int low = at == motionPanel.poseConstraints.begin() ? 0 : (at - 1)->frame + 1;
                    const int high = at == motionPanel.poseConstraints.end() ? lastFrame : at->frame - 1;
                    if(low <= high) moving.frame = std::clamp(targetFrame, low, high);
                    at = std::lower_bound(motionPanel.poseConstraints.begin(), motionPanel.poseConstraints.end(), moving.frame,
                        [](const Loom::MotionPoseConstraint& key, int f){ return key.frame < f; });
                    motionPanel.selectedPose = int(motionPanel.poseConstraints.insert(at, std::move(moving)) -
                                                    motionPanel.poseConstraints.begin());
                }else if(rootEdit.held && motionPanel.timelineDragLane == 2 && !motionPanel.draggingPoseLane &&
                         !motionPanel.resizingTimelineAction && motionPanel.selectedRootWaypoint >= 0 &&
                         size_t(motionPanel.selectedRootWaypoint) < motionPanel.rootWaypoints.size()){
                    const int frameAt = motionFrameAtX(rootEdit.mouseX, lastFrame);
                    Loom::moveMotionRootWaypoint(motionPanel.rootWaypoints,
                        size_t(motionPanel.selectedRootWaypoint), frameAt, lastFrame);
                    motionPanel.rootTrackCursorFrame =
                        float(motionPanel.rootWaypoints[size_t(motionPanel.selectedRootWaypoint)].frame);
                }
                if(rootEdit.rightPressed && inTimelineContent && poseLane){
                    const int nearest = nearestPoseConstraint(rootEdit.mouseX);
                    if(nearest >= 0){
                        motionPanel.poseConstraints.erase(motionPanel.poseConstraints.begin() + nearest);
                        motionPanel.selectedPose = motionPanel.poseConstraints.empty() ? -1 :
                            std::min(nearest, int(motionPanel.poseConstraints.size()) - 1);
                    }
                }else if(rootEdit.rightPressed && inTimelineContent && pathLane){
                    const int nearest = nearestRootWaypoint(rootEdit.mouseX);
                    if(nearest > 0){
                        motionPanel.rootWaypoints.erase(motionPanel.rootWaypoints.begin() + nearest);
                        motionPanel.selectedRootWaypoint = std::clamp(nearest - 1, 0,
                            int(motionPanel.rootWaypoints.size()) - 1);
                    }
                }
                if(!rootEdit.held && !rootEdit.pressed){
                    motionPanel.draggingPoseLane = false;
                    motionPanel.resizingTimelineAction = false;
                    motionPanel.timelineDragLane = 0;
                    motionPanel.timelineDragDisplayEndFrame = 0.0;
                }
            }
            const Treadle::Ui::Region scrub = ui.region("timeline", track);
            if(scrub.held && scrub.mouseX >= contentTrack.x){
                const double f = stage.startFrame + double((scrub.mouseX - contentTrack.x) / contentTrack.width) * span;
                frame = std::clamp(std::round(f), stage.startFrame, stage.endFrame);
                playing = false;
            }
        }

        if(autoRig.open){
            const Treadle::Rect& v = layout.viewport;
            const float width = std::min(560.0f, v.width - 20.0f);
            std::string lastLine;
            {
                std::lock_guard<std::mutex> guard(job.lock);
                if(!job.lines.empty()) lastLine = job.lines.back();
            }
            const fs::path backend = fs::path(LOOM_ROOT_DIR) / "tools/autorig";
            const bool ready = fs::is_regular_file(backend / ".venv/bin/python") &&
                               fs::is_regular_file(backend / "vendor/UniRig/run.py");
            const Loom::AutoRigAction action = Loom::drawAutoRigPanel(ui, autoRig,
                Treadle::Rect{v.x + v.width - width - 10.0f, v.y + 10.0f, width, v.height - 20.0f},
                ready, job.running && job.task == Loom::Task::AutoRig, job.running, lastLine, autoRigScroll);
            if(action.useSelected) openAutoRig();
            if(action.generate) startAutoRig();
            if(action.preview) importAutoRigModel(autoRig.output / "bend_preview.glb");
        }

        //== ANIMATOR WORKSPACE: rig viewport, motion controls and timeline ========================
        tickMotionBricksLive();
        if(motionPanel.open){
            Loom::MotionPanelStatus motionStatus;
            motionStatus.motionBricksReady =
                fs::is_regular_file(fs::path(LOOM_ROOT_DIR) / "tools/motionbricks/.venv/bin/python") &&
                fs::is_regular_file(fs::path(LOOM_ROOT_DIR) / "tools/motionbricks/generate.py") &&
                fs::is_regular_file(fs::path(LOOM_ROOT_DIR) / "tools/motionbricks/realtime.py") &&
                hasMotionBricksCheckpoints(fs::path(LOOM_ROOT_DIR) / "tools/motionbricks/vendor/GR00T-WholeBodyControl/motionbricks");
            motionStatus.liveRecording = motionLive.active;
            motionStatus.liveReady = motionLive.ready;
            motionStatus.liveStopping = motionLive.stopRequested;
            motionStatus.liveFrames = motionLive.recordedFrames;
            motionStatus.liveMessage = motionLive.message;
            motionStatus.motionBricksRunning = job.running && job.task == Loom::Task::WeaverMotion && generatedMotionIsBricks;
            motionStatus.runnerReady = fs::is_regular_file(fs::path(LOOM_ROOT_DIR) / "tools/weavermotion/.venv-clean/bin/python") &&
                                       fs::is_regular_file(fs::path(LOOM_ROOT_DIR) / "tools/weavermotion/.venv-clean/bin/kimodo_gen") &&
                                       fs::is_regular_file(fs::path(LOOM_ROOT_DIR) / "tools/weavermotion/kimodo_cli.py");
            motionStatus.running = job.running && job.task == Loom::Task::WeaverMotion;
            motionStatus.otherJob = job.running && job.task != Loom::Task::WeaverMotion;
            motionStatus.elapsed = std::chrono::duration<double>(now - job.started).count();
            motionStatus.currentFrame = frame;
            motionStatus.timelineStart = stage.startFrame;
            motionStatus.timelineFps = stage.framesPerSecond;
            {
                std::lock_guard<std::mutex> guard(job.lock);
                if(!job.lines.empty()) motionStatus.lastLine = job.lines.back();
            }
            motionStatus.historyDirectory = motionDirectory();
            if(std::chrono::duration<double>(now - sceneMotionCharactersRead).count() > 1.0){
                sceneMotionCharacters = Loom::motionCharactersIn(stage);
                sceneMotionCharactersRead = now;
            }
            motionStatus.characters = sceneMotionCharacters;
            plateWatch.update(stage, frame, Loom::motionCharacterForEntity(stage, motionPanel.targetCharacter),
                              std::chrono::duration<double>(now.time_since_epoch()).count());
            motionStatus.plate = &plateWatch;
            motionStatus.characterNote = "Detected from imported GLTF/GLB mesh + joint hierarchies.";
            const Loom::MotionPanelAction motionAction = Loom::drawMotionPanel(ui, motionPanel,
                layout.motion, motionStatus, motionPanelScroll);
            const Warp::Id selectedMotionRig = Loom::motionCharacterForEntity(stage, motionPanel.targetCharacter);
            if(selectedMotionRig != Warp::None && selected != selectedMotionRig){
                selected = selectedMotionRig;
                focus = Focus::Entity;
            }
            if(motionPanel.flowMode == 1){
                const double neededEnd = stage.startFrame +
                    double(Loom::kimodoMotionLastFrame({motionPanel.directedAction})) *
                    stage.framesPerSecond / Loom::kimodoMotionFps;
                stage.endFrame = std::max(stage.endFrame, neededEnd);
            }
            if(motionAction.jumpPoseFrame >= 0){
                frame = std::clamp(stage.startFrame + double(motionAction.jumpPoseFrame) *
                    stage.framesPerSecond / Loom::kimodoMotionFps, stage.startFrame, stage.endFrame);
                playing = false;
            }
            if(motionAction.standOnPlateFloor){
                std::string problem;
                const Warp::Id rig = Loom::motionCharacterForEntity(stage, motionPanel.targetCharacter);
                if(Loom::standOnPlateFloor(stage, rig, frame, plateWatch.floor, motionPanel.cameraHeightMetres, &problem)){
                    char text[160];
                    std::snprintf(text, sizeof(text), "Character stands on the plate floor at %.2f m camera height (%.3f scene units per metre).",
                                  double(motionPanel.cameraHeightMetres),
                                  double(Loom::plateUnitsPerMetre(plateWatch.floor, motionPanel.cameraHeightMetres)));
                    message = text;
                    plateWatch.checkedAt = -1.0;
                }else message = problem;
            }
            if(motionAction.generate) startMotionGeneration();
            if(motionAction.generateMotionBricks) startMotionBricksGeneration();
            if(motionAction.startLiveRecording) startMotionBricksLive();
            if(motionAction.stopLiveRecording) stopMotionBricksLive();
            if(!motionAction.importPath.empty()) importMotion(motionAction.importPath, motionPanel.targetCharacter);
            if(!motionAction.copyNativeNpz.empty()){
                glfwSetClipboardString(window, fs::absolute(motionAction.copyNativeNpz).string().c_str());
                message = "Native NPZ path copied. Load it in Kimodo Demo > Load/Save > Motion.";
            }
            if(motionAction.compareMode != Loom::MotionCompareMode::None && !motionAction.comparePath.empty())
                importMotion(motionAction.comparePath, motionPanel.targetCharacter, motionAction.compareMode);
            if(motionAction.close) motionPanel.open = false;
        }

        if(proceduraPanel.open){
            const size_t nodesBefore = proceduraPanel.graph.nodes.size();
            Loom::drawWeaverProceduraPanel(ui, proceduraPanel, layout.motion);
            if(nodesBefore == 0 && !proceduraPanel.graph.nodes.empty()) frameProceduraCurve();

            namespace Panel = Loom::WeaverProceduraUi;
            namespace Proc = Engine::WeaverProcedura;
            Proc::Node* curveNode = Panel::activeCurveNode(proceduraPanel);
            if(curveNode){
                const Proc::Curve& curve = std::get<Proc::CurveNode>(curveNode->payload).curve;
                if(proceduraPanel.selectedControlPoint < 0 ||
                   size_t(proceduraPanel.selectedControlPoint) >= curve.points.size())
                    proceduraPanel.selectedControlPoint = curve.points.empty() ? -1 : 0;
                const int pointIndex = proceduraPanel.selectedControlPoint;
                if(pointIndex >= 0){
                    const Loom::ViewCamera hudCamera = Loom::viewCameraFor(stage, frame, layout.viewport, view);
                    glm::vec2 hudPixel;
                    if(Loom::project(hudCamera, curve.points[size_t(pointIndex)], hudPixel) &&
                       layout.viewport.contains(hudPixel.x, hudPixel.y) &&
                       layout.viewport.width > 310.0f && layout.viewport.height > 180.0f){
                        constexpr float hudWidth = 282.0f, hudHeight = 94.0f;
                        float hudX = hudPixel.x + 24.0f;
                        if(hudX + hudWidth > layout.viewport.x + layout.viewport.width - 8.0f)
                            hudX = hudPixel.x - hudWidth - 24.0f;
                        hudX = std::clamp(hudX, layout.viewport.x + 8.0f,
                                          layout.viewport.x + layout.viewport.width - hudWidth - 8.0f);
                        float hudY = hudPixel.y - hudHeight - 18.0f;
                        if(hudY < layout.viewport.y + 8.0f) hudY = hudPixel.y + 20.0f;
                        hudY = std::clamp(hudY, layout.viewport.y + 8.0f,
                                          layout.viewport.y + layout.viewport.height - hudHeight - 8.0f);
                        ui.panel("CURVE POINT / P" + std::to_string(pointIndex + 1), hudX, hudY, hudWidth);
                        ui.value("Position", vectorText(curve.points[size_t(pointIndex)]) + " m");
                        ui.hint("Drag in viewport to move; right-click for point actions.");
                        const int action = ui.buttonRow({"Frame", "Before", "After", "More"});
                        if(action == 0) frameProceduraCurve();
                        else if(action == 1) Panel::insertControlPoint(proceduraPanel, curveNode->id,
                                                                       size_t(pointIndex));
                        else if(action == 2) Panel::insertControlPoint(proceduraPanel, curveNode->id,
                                                                       size_t(pointIndex + 1));
                        if(action == 3 || ui.rightClicked()){
                            proceduraPanel.contextCurveNodeId = curveNode->id;
                            proceduraPanel.contextControlPoint = pointIndex;
                            ui.openMenuAt("Procedura Point", float(cursorX), float(cursorY));
                            menuPixel = glm::vec2(float(cursorX), float(cursorY));
                        }
                    }
                }
            }
        }

        //== PLOHA IZ ODABIRA: sto je odabrano i sto se s tim moze ================================
        if(surfaceTool.active){
            const Treadle::Rect& v = layout.viewport;
            ui.panel("Surface (S)", v.x + 10.0f, v.y + 10.0f, 300.0f);
            if(!surfaceTool.fit.valid){
                ui.label("Drag a rectangle over points");
                ui.label("or the Gaussians on one surface.");
            }else{
                char text[96];
                std::snprintf(text, sizeof(text), "%zu of %zu on plane", surfaceTool.fit.used, surfaceTool.fit.total);
                ui.value("Points", text);
                if(ui.button("Cube on Surface")){
                    selected = Loom::placeOnSurface(stage, surfaceTool, Warp::Shape::Cube);
                    focus = Focus::Entity;
                }
                if(ui.button("Plane on Surface")){
                    selected = Loom::placeOnSurface(stage, surfaceTool, Warp::Shape::Plane);
                    focus = Focus::Entity;
                }
                if(ui.button("Clear Selection")) surfaceTool = Loom::SurfaceTool{true};
            }
            if(ui.button("Close")) surfaceTool.active = false;
        }

        //== OBJECT HUD: contextual actions float beside the selected object ==================
        if(focus == Focus::Entity && selected != Warp::None && !motionPanel.open && !proceduraPanel.open &&
           !autoRig.open && !importer.open &&
           !ui.menuOpen("Entity") && !ui.menuOpen("View") && activeRailPane != RailPane::Media){
            const Warp::Entity* hudEntity = stage.get(selected);
            if(hudEntity && hudEntity->visible && selected != view.lookThrough){
                const Loom::ViewCamera hudCamera = Loom::viewCameraFor(stage, frame, layout.viewport, view);
                glm::vec2 hudPixel;
                const glm::vec3 hudWorld(stage.worldMatrix(selected, frame)[3]);
                if(Loom::project(hudCamera, hudWorld, hudPixel) &&
                   layout.viewport.contains(hudPixel.x, hudPixel.y) &&
                   layout.viewport.width > 300.0f && layout.viewport.height > 220.0f){
                    constexpr float hudWidth = 238.0f, hudHeight = 146.0f;
                    float hudX = hudPixel.x + 22.0f;
                    if(hudX + hudWidth > layout.viewport.x + layout.viewport.width - 8.0f)
                        hudX = hudPixel.x - hudWidth - 22.0f;
                    hudX = std::clamp(hudX, layout.viewport.x + 8.0f,
                                      layout.viewport.x + layout.viewport.width - hudWidth - 8.0f);
                    float hudY = hudPixel.y - hudHeight - 16.0f;
                    if(hudY < layout.viewport.y + 8.0f) hudY = hudPixel.y + 18.0f;
                    hudY = std::clamp(hudY, layout.viewport.y + 8.0f,
                                      layout.viewport.y + layout.viewport.height - hudHeight - 8.0f);
                    const std::string hudType = sceneTag(*hudEntity);
                    const Treadle::Color baseAccent = sceneAccent(*hudEntity);
                    ui.panel("OBJECT HUD / " + hudType, hudX, hudY, hudWidth);
                    ui.value("Selected", Treadle::fitText(hudEntity->name, hudWidth - 42.0f, theme.textScale));
                    const int hudAction = ui.buttonRow({"Focus", hudEntity->camera ? "Camera" : "Motion", "More"});
                    if(hudAction == 0){
                        view.lookThrough = Warp::None;
                        if(hudEntity->mesh || hudEntity->camera){
                            view.orbit.target = glm::vec3(stage.worldMatrix(selected, frame)[3]);
                            view.orbit.distance = extent.radius * 0.8f;
                        }else{
                            Loom::frameAll(stage.size() ? stage : live, frame, view.orbit);
                        }
                    }else if(hudAction == 1){
                        if(hudEntity->camera){
                            view.lookThrough = view.lookThrough == selected ? Warp::None : selected;
                        }else{
                            openMotionWorkflow();
                        }
                    }else if(hudAction == 2){
                        menuEntity = selected;
                        ui.openMenuAt("Entity", hudX + hudWidth, hudY + hudHeight * 0.5f);
                    }
                    const Treadle::Color link{baseAccent.r, baseAccent.g, baseAccent.b, 0.44f};
                    const float edgeX = hudPixel.x < hudX ? hudX : (hudX + hudWidth);
                    const float edgeY = std::clamp(hudPixel.y, hudY + 8.0f, hudY + hudHeight - 8.0f);
                    ui.canvas().line(hudPixel.x, hudPixel.y, edgeX, edgeY, 1.25f, link);
                    ui.canvas().outline(Treadle::Rect{hudPixel.x - 6.0f, hudPixel.y - 6.0f, 12.0f, 12.0f},
                                        1.5f, link);
                }
            }
        }

        //== IMPORTER: preko pogleda, kao preglednik datoteka u Blenderu ========================
        if(importer.open){
            ui.dock("IMPORT", layout.viewport, &importer.scroll);
            ui.label("IMPORT INTO SCENE");
            ui.label(tail(importer.at.string(), size_t(std::max(12.0f, (layout.viewport.width - 30.0f) / 12.0f))));
            const int navigate = ui.buttonRow({"Up", "Home", "Clips", "Project", "Refresh", "Close"});
            if(navigate == 0) importer.enter(importer.at.parent_path());
            else if(navigate == 1){ const char* home = std::getenv("HOME"); if(home) importer.enter(home); }
            else if(navigate == 2){ const char* home = std::getenv("HOME"); if(home) importer.enter(fs::path(home) / "Desktop/loomTestClips"); }
            else if(navigate == 3 && !projectPath.empty()) importer.enter(projectPath.parent_path());
            else if(navigate == 4) importer.refresh();
            else if(navigate == 5) importer.open = false;
            const Treadle::Ui::TextFieldResult typed = ui.textField("importer-path", &importer.pathText);
            if(typed.submitted) importer.enter(fs::path(importer.pathText));
            if(ui.choice("Show", Loom::importFilterNames(), &importer.filter)) importer.chosen.clear();
            ui.separator();

            //-- sto se s odabranim moze ------------------------------------------------------
            const Loom::ImportEntry* one = importer.single();
            const std::vector<fs::path> images = importer.chosenImages();
            const Warp::Entity* target = stage.get(selected);
            const bool canTakeMaterial = focus == Focus::Entity && target && (target->mesh || target->model);
            auto makeMaterial = [&](const std::vector<fs::path>& textures, bool assign){
                const Loom::MaterialImportReport report = Loom::materialFromTextures(stage, textures);
                if(report.material < 0){ importer.status = report.problem; return; }
                std::string text = "Material " + report.name + " (" + std::to_string(report.assigned.size()) + " maps)";
                if(assign && Loom::assignMaterial(stage, selected, report.material)) text += " on " + target->name;
                if(!report.problem.empty()) text += " - " + report.problem;
                importer.status = text;
                message = text;
            };
            bool done = false;
            if(!images.empty()){
                ui.label(std::to_string(images.size()) + " texture(s) selected");
                for(const fs::path& image : images){
                    static const char* roles[] = {"?", "Color", "Normal", "Roughness", "Metallic", "AO", "Emission", "ORM"};
                    ui.value(Treadle::fitText(image.filename().string(), 170.0f, theme.textScale), roles[int(Loom::textureRole(image))]);
                }
                if(ui.button("Create Material")) makeMaterial(images, false);
                if(canTakeMaterial && ui.button("Create Material + Assign to " + target->name)) makeMaterial(images, true);
            }else if(one){
                const fs::path path = one->path;
                ui.value(Loom::importKindName(one->kind), Treadle::fitText(path.filename().string(), 220.0f, theme.textScale));
                switch(one->kind){
                    case Loom::ImportKind::Video:{
                        if(ui.button("Add to Project Media")){ addVideoToProject(path); done = true; }
                        if(ui.button("Add + Solve Cameras (Matchmove)") && !job.running){ startSolve(addVideoToProject(path), false); done = true; }
                        if(ui.button("Add + Solve + Gaussian Splat") && !job.running){ startSolve(addVideoToProject(path), true); done = true; }
                        break;
                    }
                    case Loom::ImportKind::Model:{
                        if(ui.button("Import Model")){ importModelAsset(path, layout.viewport); done = true; }
                        if(ui.button("Import as Humanoid (auto-rig if needed)") && !job.running){ importHumanoidAsset(path); done = true; }
                        break;
                    }
                    case Loom::ImportKind::Splat:
                        if(ui.button("Import Gaussian Splat")){ importSplatFile(path); done = true; }
                        break;
                    case Loom::ImportKind::Result:{
                        if(ui.button("Import Solve Result (camera + points)")){
                            std::string plate, stem = path.filename().string();
                            if(stem.size() > 5) stem.resize(stem.size() - 5);
                            for(const fs::path& video : Loom::videosIn(path.parent_path())) if(video.stem().string() == stem) plate = video.string();
                            importFolder(path, plate);
                            done = true;
                        }
                        break;
                    }
                    case Loom::ImportKind::Motion:
                        if(ui.button("Import Motion")){ importMotion(path, motionPanel.targetCharacter); done = true; }
                        break;
                    case Loom::ImportKind::Project:
                        if(ui.button("Open Project")){ pendingProject = path; ui.openMenu("Project"); done = true; }
                        break;
                    default: break;
                }
            }else{
                ui.label("Pick a file below. Images: pick several textures for one material.");
            }
            const std::vector<fs::path> folderImages = importer.allImages();
            if(images.empty() && !folderImages.empty() &&
               ui.button("Material from all " + std::to_string(folderImages.size()) + " images in this folder")) makeMaterial(folderImages, canTakeMaterial);
            if(!importer.status.empty()) ui.label(importer.status);
            if(done) importer.open = false;
            ui.separator();

            //-- sadrzaj mape ------------------------------------------------------------------
            fs::path enter;
            size_t shown = 0;
            for(const Loom::ImportEntry& entry : importer.entries){
                if(entry.kind == Loom::ImportKind::Folder){
                    if(ui.folderRow(entry.path.filename().string(), false)) enter = entry.path;
                    continue;
                }
                if(!Loom::importFilterAccepts(importer.filter, entry.kind)) continue;
                ++shown;
                AssetStyle style;
                switch(entry.kind){
                    case Loom::ImportKind::Video: style = assetStyle(entry.path, "video"); break;
                    case Loom::ImportKind::Model: style = assetStyle(entry.path, "model"); break;
                    case Loom::ImportKind::Motion: style = assetStyle(entry.path, "motion"); break;
                    case Loom::ImportKind::Project: style = assetStyle(entry.path, "project"); break;
                    case Loom::ImportKind::Image: style = assetStyle(entry.path, "image"); break;
                    case Loom::ImportKind::Splat: style = {"SPLAT", {0.95f, 0.55f, 0.95f, 1.0f}}; break;
                    default: style = assetStyle(entry.path, "result"); break;
                }
                const bool isChosen = importer.chosen.count(entry.path) > 0;
                if(ui.assetRow(entry.path.filename().string(), style.badge, isChosen, style.colour)){
                    //Slike se biraju vise njih (teksture jednog materijala); ostalo jedna
                    if(entry.kind == Loom::ImportKind::Image){
                        if(!importer.chosenImages().size() && !importer.chosen.empty()) importer.chosen.clear();
                        if(isChosen) importer.chosen.erase(entry.path); else importer.chosen.insert(entry.path);
                    }else{
                        importer.chosen.clear();
                        importer.chosen.insert(entry.path);
                    }
                    importer.status.clear();
                }
            }
            if(shown == 0) ui.label("(nothing importable here - open a folder)");
            if(!enter.empty()) importer.enter(enter);
        }

        //== IZBORNICI ============================================================================
        //Desni klik uz putanju otvara alat za dodavanje poze na tocnoj tocki putanje.
        //Uzorkujemo glatku krivulju po pod-kadrovima kako bi se pogodila i izmedu waypointa.
        int motionPathContextFrame = -1;
        const bool canContextMotionPath = motionPanel.open && motionPanel.flowMode != 2 &&
            motionPanel.rootPathEnabled &&
            stage.contains(motionPanel.targetCharacter) && !motionPanel.rootWaypoints.empty();
        if(rightPressed && canContextMotionPath &&
           layout.viewport.contains(float(rightClickX), float(rightClickY)) && !ui.wantsMouse() && !compositor.open){
            const Loom::ViewCamera contextCamera = Loom::viewCameraFor(stage, frame, layout.viewport, view);
            const Warp::Id rootBone = motionRootAnchor(motionPanel.targetCharacter);
            glm::vec3 pathOrigin = glm::vec3(stage.worldMatrix(rootBone, stage.startFrame)[3]);
            pathOrigin.y = 0.0f;
            const int lastMotionFrame = motionPanel.flowMode == 1
                ? Loom::kimodoMotionLastFrame({motionPanel.directedAction})
                : Loom::kimodoMotionLastFrame(motionPanel.actions);
            const float pathStartFrame = float(motionPanel.rootWaypoints.front().frame);
            const float pathEndFrame = std::min(float(lastMotionFrame),
                float(motionPanel.rootWaypoints.back().frame));
            const int sampleCount = std::max(1, int(std::ceil(std::max(0.0f,
                pathEndFrame - pathStartFrame) * 2.0f)));
            const glm::vec2 pointer{float(rightClickX), float(rightClickY)};
            float nearestDistance = 15.0f;
            float nearestFrame = -1.0f;
            glm::vec2 previousPixel;
            bool previousVisible = false;
            float previousFrame = 0.0f;
            for(int sample = 0; sample <= sampleCount; ++sample){
                const float sampleFrame = pathStartFrame +
                    (pathEndFrame - pathStartFrame) * float(sample) / float(sampleCount);
                const Loom::MotionRootWaypoint point = Loom::motionRootPathAt(
                    motionPanel.rootWaypoints, sampleFrame, motionPanel.smoothRootPath);
                glm::vec2 pixel;
                const bool visible = Loom::project(contextCamera,
                    pathOrigin + glm::vec3(point.x, 0.025f, point.z), pixel) &&
                    layout.viewport.contains(pixel.x, pixel.y);
                if(visible && previousVisible){
                    const glm::vec2 edge = pixel - previousPixel;
                    const float edgeLength2 = glm::dot(edge, edge);
                    const float t = edgeLength2 > 0.001f
                        ? std::clamp(glm::dot(pointer - previousPixel, edge) / edgeLength2, 0.0f, 1.0f)
                        : 0.0f;
                    const float distance = glm::length(pointer - (previousPixel + edge * t));
                    if(distance < nearestDistance){
                        nearestDistance = distance;
                        nearestFrame = previousFrame + (sampleFrame - previousFrame) * t;
                    }
                }
                previousPixel = pixel;
                previousVisible = visible;
                previousFrame = sampleFrame;
            }
            if(nearestFrame >= 0.0f) motionPathContextFrame = std::clamp(
                int(std::lround(nearestFrame)), 0, lastMotionFrame);
        }
        bool proceduraPointContextHit = ui.menuOpen("Procedura Point");
        if(rightPressed && proceduraPanel.open && layout.viewport.contains(float(rightClickX), float(rightClickY)) &&
           !ui.wantsMouse() && !compositor.open){
            namespace Panel = Loom::WeaverProceduraUi;
            namespace Proc = Engine::WeaverProcedura;
            Proc::Node* curveNode = Panel::activeCurveNode(proceduraPanel);
            if(curveNode){
                const Proc::Curve& curve = std::get<Proc::CurveNode>(curveNode->payload).curve;
                const Loom::ViewCamera contextCamera = Loom::viewCameraFor(stage, frame, layout.viewport, view);
                const glm::vec2 pointer{float(rightClickX), float(rightClickY)};
                float closest = 18.0f;
                int pointIndex = -1;
                for(size_t i = 0; i < curve.points.size(); ++i){
                    glm::vec2 pixel;
                    if(!Loom::project(contextCamera, curve.points[i], pixel) ||
                       !layout.viewport.contains(pixel.x, pixel.y)) continue;
                    const float distance = glm::length(pixel - pointer);
                    if(distance < closest){ closest = distance; pointIndex = int(i); }
                }
                if(pointIndex >= 0){
                    proceduraPanel.selectedCurveNodeId = curveNode->id;
                    proceduraPanel.selectedControlPoint = pointIndex;
                    proceduraPanel.contextCurveNodeId = curveNode->id;
                    proceduraPanel.contextControlPoint = pointIndex;
                    ui.openMenuAt("Procedura Point", float(rightClickX), float(rightClickY));
                    menuPixel = glm::vec2(float(rightClickX), float(rightClickY));
                    proceduraPointContextHit = true;
                }
            }
        }
        //Desni klik izvan putanje ili control pointa zadrzava postojeci izbornik pogleda.
        if(rightPressed && layout.viewport.contains(float(rightClickX), float(rightClickY)) &&
           !ui.wantsMouse() && !compositor.open && !proceduraPointContextHit &&
           !ui.menuOpen("Procedura Point") && !ui.menuOpen("Media") && !ui.menuOpen("Entity") && !ui.menuOpen("Atlas")){
            if(motionPathContextFrame >= 0){
                motionPathMenuFrame = motionPathContextFrame;
                ui.openMenuAt("Motion Path", float(rightClickX), float(rightClickY));
            }else{
                ui.openMenu("View");
            }
            menuPixel = glm::vec2(float(rightClickX), float(rightClickY));
        }
        auto openContextSubmenu = [&](const std::string& id){
            const float popupWidth = 220.0f;
            float x = float(cursorX) + 20.0f;
            if(x + popupWidth > float(windowWidth)) x = float(cursorX) - popupWidth - 20.0f;
            const float y = std::clamp(float(cursorY) - 22.0f, 4.0f, std::max(4.0f, float(windowHeight) - 150.0f));
            ui.openMenuAt(id, x, y);
        };
        if(ui.beginMenu("Motion Path")){
            ui.menuItem("ROOT PATH  /  FRAME " + std::to_string(motionPathMenuFrame), false);
            ui.menuSeparator();
            const int requestedFrame = std::max(0, motionPathMenuFrame);
            const auto requestedPose = std::lower_bound(motionPanel.poseConstraints.begin(),
                motionPanel.poseConstraints.end(), requestedFrame,
                [](const Loom::MotionPoseConstraint& pose, int value){ return pose.frame < value; });
            const bool requestedPoseExists = requestedPose != motionPanel.poseConstraints.end() &&
                                             requestedPose->frame == requestedFrame;
            const bool canAddPose = requestedPoseExists || motionPanel.poseConstraints.size() < 20;
            if(ui.menuItem(requestedPoseExists ? "Select Skeleton Pose" : "Add Skeleton", canAddPose)){
                int frameAt = requestedFrame;
                if(motionPanel.flowMode != 1){
                    const int sourceLast = std::max(1, Loom::kimodoMotionLastFrame(motionPanel.actions));
                    Loom::MotionPathDraft currentPath;
                    currentPath.enabled = motionPanel.rootPathEnabled;
                    currentPath.autoEnd = motionPanel.rootPathAutoEnd;
                    currentPath.autoDistance = motionPanel.rootPathAutoDistance;
                    currentPath.smooth = motionPanel.smoothRootPath;
                    currentPath.pinHeading = motionPanel.constrainRootHeading;
                    currentPath.selected = motionPanel.selectedRootWaypoint;
                    currentPath.waypoints = motionPanel.rootWaypoints;
                    motionPanel.recipePath = currentPath;
                    motionPanel.directedPath = currentPath;

                    std::string combinedPrompt;
                    for(const Loom::MotionAction& step : Loom::filledActions(motionPanel.actions)){
                        if(step.prompt.empty()) continue;
                        if(!combinedPrompt.empty()) combinedPrompt += "; then ";
                        if(combinedPrompt.size() < 600)
                            combinedPrompt += step.prompt.substr(0, 600 - combinedPrompt.size());
                    }
                    if(!combinedPrompt.empty()) motionPanel.directedAction.prompt = combinedPrompt;
                    // Keep the path's authored frame numbers exactly. A single directed action
                    // needs the same total clip length as the recipe sequence it replaces.
                    motionPanel.directedAction.duration = float(double(sourceLast + 1) /
                                                                 Loom::kimodoMotionFps);
                    motionPanel.flowMode = 1;
                }
                const auto poseAtFrame = std::lower_bound(motionPanel.poseConstraints.begin(),
                    motionPanel.poseConstraints.end(), frameAt,
                    [](const Loom::MotionPoseConstraint& pose, int value){ return pose.frame < value; });
                if(poseAtFrame != motionPanel.poseConstraints.end() && poseAtFrame->frame == frameAt){
                    motionPanel.selectedPose = int(poseAtFrame - motionPanel.poseConstraints.begin());
                }else if(motionPanel.poseConstraints.size() < 20){
                    Loom::MotionPoseConstraint pose;
                    pose.frame = frameAt;
                    if(poseAtFrame != motionPanel.poseConstraints.begin()){
                        pose = *(poseAtFrame - 1);
                        pose.frame = frameAt;
                    }
                    motionPanel.selectedPose = int(motionPanel.poseConstraints.insert(
                        poseAtFrame, pose) - motionPanel.poseConstraints.begin());
                }
                motionPanel.rootPathEnabled = true;
                motionPanel.selectedBone = 0;
                motionPanel.rootTrackCursorFrame = float(frameAt);
                frame = std::clamp(stage.startFrame + double(frameAt) * stage.framesPerSecond /
                    Loom::kimodoMotionFps, stage.startFrame, stage.endFrame);
                playing = false;
            }
            if(motionPanel.poseConstraints.size() >= 20 && !requestedPoseExists)
                ui.menuItem("Pose limit reached (20)", false);
            ui.menuItem("Cancel");
            ui.endMenu();
        }
        if(ui.beginMenu("Procedura Point")){
            namespace Panel = Loom::WeaverProceduraUi;
            namespace Proc = Engine::WeaverProcedura;
            Proc::Node* curveNode = Panel::findCurveNode(proceduraPanel,
                                                          proceduraPanel.contextCurveNodeId);
            const int index = proceduraPanel.contextControlPoint;
            const bool pointValid = curveNode && index >= 0 &&
                size_t(index) < std::get<Proc::CurveNode>(curveNode->payload).curve.points.size();
            size_t pointCount = pointValid
                ? std::get<Proc::CurveNode>(curveNode->payload).curve.points.size() : 0;
            const bool canInsert = pointValid && pointCount < 64;
            const bool canDelete = pointValid && pointCount >
                (std::get<Proc::CurveNode>(curveNode->payload).curve.closed ? 3u : 2u);
            if(pointValid) ui.menuItem("CURVE POINT  /  P" + std::to_string(index + 1), false);
            else ui.menuItem("CURVE POINT", false);
            ui.menuSeparator();
            if(ui.menuItem("Insert before", canInsert))
                Panel::insertControlPoint(proceduraPanel, curveNode->id, size_t(index));
            if(ui.menuItem("Insert after", canInsert))
                Panel::insertControlPoint(proceduraPanel, curveNode->id, size_t(index + 1));
            if(ui.menuItem("Raise by 0.25 m", pointValid)){
                auto& point = std::get<Proc::CurveNode>(curveNode->payload).curve.points[size_t(index)];
                point.y += 0.25f;
                Panel::markGraphChanged(proceduraPanel);
            }
            if(ui.menuItem("Lower by 0.25 m", pointValid)){
                auto& point = std::get<Proc::CurveNode>(curveNode->payload).curve.points[size_t(index)];
                point.y -= 0.25f;
                Panel::markGraphChanged(proceduraPanel);
            }
            if(ui.menuItem("Set height to ground", pointValid)){
                auto& point = std::get<Proc::CurveNode>(curveNode->payload).curve.points[size_t(index)];
                point.y = 0.0f;
                Panel::markGraphChanged(proceduraPanel);
            }
            if(ui.menuItem("Frame curve", pointValid)) frameProceduraCurve();
            ui.menuSeparator();
            if(ui.menuItem("Delete point", canDelete))
                Panel::eraseControlPoint(proceduraPanel, curveNode->id, size_t(index));
            ui.endMenu();
        }
        //Otvaranje, nova scena i izlaz PITAJU kad ima nespremljenog - i nude spremanje prvo
        if(ui.beginMenu("Project")){
            const std::string name = pendingProject.filename().string();
            if(dirty){
                if(ui.menuItem("Save and Open " + name) && saveProjectNow()) openProject(pendingProject);
                if(ui.menuItem("Open " + name + " Without Saving")) openProject(pendingProject);
            }else if(ui.menuItem("Open " + name)){
                openProject(pendingProject);
            }
            ui.menuItem("Cancel");
            ui.endMenu();
        }
        if(ui.beginMenu("New")){
            if(dirty){
                if(ui.menuItem("Save and Create New Scene") && saveProjectNow()) newScene();
                if(ui.menuItem("New Scene Without Saving")) newScene();
            }else if(ui.menuItem("New Empty Scene")){
                newScene();
            }
            ui.menuItem("Cancel");
            ui.endMenu();
        }
        if(!offeredAutosave.empty() && !autosaveAsked){
            autosaveAsked = true;
            int w = 0, h = 0;
            glfwGetWindowSize(window, &w, &h);
            ui.openMenuAt("Autosave", float(w) * 0.5f - 200.0f, float(h) * 0.4f);
        }
        if(ui.beginMenu("Autosave")){
            std::error_code error;
            const auto age = fs::last_write_time(offeredAutosave, error);
            const double minutes = error ? 0.0 : std::chrono::duration<double>(fs::file_time_type::clock::now() - age).count() / 60.0;
            char text[160];
            std::snprintf(text, sizeof(text), "An autosave newer than the saved scene exists (%.0f min old).", minutes);
            ui.menuItem(text, false);
            ui.menuSeparator();
            if(ui.menuItem("Restore Autosave")){
                std::string problem;
                Warp::Stage restored;
                if(Warp::loadProject(offeredAutosave.string(), restored, problem)){
                    stage = std::move(restored);          //projectPath stays: Save writes the real project
                    selected = Warp::None;
                    extentDirty = true;
                    history.reset(stage);
                    message = "Autosave restored - save to keep it";
                }else{
                    message = "Could not restore autosave: " + problem;
                }
                offeredAutosave.clear();
            }
            if(ui.menuItem("Discard Autosave")){
                autosave.discard(projectPath);
                offeredAutosave.clear();
            }
            ui.endMenu();
        }else if(autosaveAsked){
            offeredAutosave.clear();              //answered, or closed by clicking elsewhere: keep the file, stop asking
            autosaveAsked = false;
        }
        if(ui.beginMenu("Exit")){
            ui.menuItem(projectPath.empty() ? "Scene is not saved." : projectPath.filename().string() + " has unsaved changes.", false);
            if(job.running) ui.menuItem("(Solve is still running - exit will wait for it)", false);
            if(viewportSplat.hasCuts()) ui.menuItem("Splat has an unsaved cut (Save Cut Splat in its properties).", false);
            ui.menuSeparator();
            if(ui.menuItem("Save and Exit") && saveProjectNow()) quitting = true;
            if(ui.menuItem("Exit Without Saving")) quitting = true;
            ui.menuItem("Cancel");
            ui.endMenu();
        }
        if(ui.beginMenu("Media")){
            const bool valid = menuMedia >= 0 && menuMedia < int(stage.media.size());
            if(ui.menuItem("Solve Cameras (Matchmove)", valid && !job.running)) startSolve(menuMedia, false);
            if(ui.menuItem("Solve + Gaussian splat", valid && !job.running)) startSolve(menuMedia, true);
            const bool hasResult = valid && !stage.media[size_t(menuMedia)].result.empty();
            if(ui.menuItem("Open Result", hasResult)){
                importFolder(stage.media[size_t(menuMedia)].result, stage.media[size_t(menuMedia)].path);
            }
            //Trening trazi slike kadrova, a solve za matchmove ih ne pise
            const bool canTrain = hasResult && fs::is_directory(fs::path(stage.media[size_t(menuMedia)].result) / "images");
            if(ui.menuItem(canTrain || !hasResult ? "Train Splat from Result" : "Train Splat (Solve Without Images)",
                           canTrain && !job.running)){
                jobVideo = stage.media[size_t(menuMedia)].path;
                startTrain(stage.media[size_t(menuMedia)].result);
            }
            ui.menuSeparator();
            if(ui.menuItem("Remove from Project", valid)){
                stage.media.erase(stage.media.begin() + menuMedia);
                selectedMedia = -1;
            }
            ui.endMenu();
        }
        {
            if(ui.menuOpen("Entity")){
                const Warp::Entity* entity = stage.get(menuEntity);
                const int action = ui.orbitMenu("Entity",
                    {"Camera View", "Add", "Text to Motion", "Delete"},
                    {entity && entity->camera, entity != nullptr, entity != nullptr, entity != nullptr},
                    {"0", "", "", "Del"}, &entityOrbitFavorites);
                if(action == 0 && entity && entity->camera) view.lookThrough = menuEntity;
                else if(action == 1 && entity) openContextSubmenu("Entity Add");
                else if(action == 2 && entity) openMotionWorkflow();
                else if(action == 3 && entity) removeSelected(menuEntity);
            }
            if(ui.beginMenu("Entity Add")){
                ui.menuItem("ADD OBJECT", false);
                ui.menuSeparator();
                if(ui.menuItem("Cube Here")) addMesh(Warp::Shape::Cube, menuEntity);
                if(ui.menuItem("Plane Here")) addMesh(Warp::Shape::Plane, menuEntity);
                if(ui.menuItem("Camera from View")) addCameraHere(menuEntity);
                if(ui.menuItem("Empty")) addEmpty(menuEntity);
                ui.menuSeparator();
                if(ui.menuItem("Import...  (Ctrl+I)")) openImporter();
                ui.endMenu();
            }
            if(ui.beginMenu("Atlas")){
                ui.menuItem("SCENE ATLAS", false);
                ui.menuSeparator();
                if(ui.menuItem("Import...  (Ctrl+I)")) openImporter();
                ui.menuSeparator();
                if(ui.menuItem("Add Camera from View")) addCameraHere(Warp::None);
                if(ui.menuItem("Add Cube")) addMesh(Warp::Shape::Cube, Warp::None);
                if(ui.menuItem("Add Plane")) addMesh(Warp::Shape::Plane, Warp::None);
                if(ui.menuItem("Add Empty")) addEmpty(Warp::None);
                ui.endMenu();
            }
        }
        if(ui.beginMenu("Model Asset")){
            std::string modelExtension = menuModelAsset.extension().string();
            std::transform(modelExtension.begin(), modelExtension.end(), modelExtension.begin(), [](unsigned char c){ return char(std::tolower(c)); });
            const bool supportedModel = modelExtension == ".glb" || modelExtension == ".gltf";
            const bool canStartAutoRig = supportedModel && !job.running;
            if(ui.menuItem("Import Model")) importModelAsset(menuModelAsset, layout.viewport);
            if(ui.menuItem("Import as Humanoid (auto-rig if needed)", canStartAutoRig))
                importHumanoidAsset(menuModelAsset);
            ui.menuSeparator();
            if(ui.menuItem("Open Auto Rig Setup", !job.running)) openAutoRig(menuModelAsset);
            ui.endMenu();
        }
        {
            if(ui.menuOpen("View")){
                const int action = ui.orbitMenu("View",
                    {"Add", "HumanoidMascott", "Text to Motion", "Frame All",
                     view.showPoints ? "Hide Points" : "Show Points",
                     view.showPaths ? "Hide Paths" : "Show Paths", view.showGrid ? "Hide Grid" : "Show Grid"},
                    {}, {"", "", "", "F", "", "", ""}, &viewOrbitFavorites);
                if(action == 0) openContextSubmenu("View Add");
                else if(action == 1) addHumanoidMascott();
                else if(action == 2) openMotionWorkflow();
                else if(action == 3){
                    view.lookThrough = Warp::None;
                    Loom::frameAll(stage.size() ? stage : live, frame, view.orbit);
                }else if(action == 4) view.showPoints = !view.showPoints;
                else if(action == 5) view.showPaths = !view.showPaths;
                else if(action == 6) view.showGrid = !view.showGrid;
            }
            if(ui.beginMenu("View Add")){
                ui.menuItem("ADD TO SCENE", false);
                ui.menuSeparator();
                if(ui.menuItem("Cube Here")) addMeshAt(Warp::Shape::Cube, Warp::None, menuPixel, true);
                if(ui.menuItem("Plane Here")) addMeshAt(Warp::Shape::Plane, Warp::None, menuPixel, true);
                if(ui.menuItem("Camera from View")) addCameraHere(Warp::None);
                ui.menuSeparator();
                if(ui.menuItem("Import...  (Ctrl+I)")) openImporter();
                ui.endMenu();
            }
        }

        //== VIEWPORT HUD: STAGE COMPASS + CAMERA POSTCARDS =====================================
        {
            const Treadle::Rect& hudViewport = layout.viewport;
            //Importer i Compositor prekrivaju pogled; kompas i razglednice bi se crtali preko njih
            if(!importer.open && !compositor.open && hudViewport.width > 300.0f && hudViewport.height > 190.0f){
                static int cameraPostcardOffset = 0;
                static bool cameraCompareMode = false;
                static Warp::Id cameraCompareA = Warp::None;
                static Warp::Id cameraCompareB = Warp::None;
                static float cameraCompareSplit = 0.5f;
                Treadle::DrawList& canvas = ui.canvas();
                const Treadle::Color panelFill{0.035f, 0.055f, 0.043f, 0.92f};
                const Treadle::Color panelEdge{0.38f, 0.48f, 0.34f, 0.88f};
                const Treadle::Color softText{0.74f, 0.82f, 0.70f, 0.92f};
                const Treadle::Color gold{1.00f, 0.78f, 0.27f, 1.00f};
                const Treadle::Color neonBlue{0.22f, 0.72f, 1.00f, 1.00f};
                const float hudTextScale = std::max(1.8f, ui.style().textScale * 0.72f);

                struct CompassMarker{
                    Warp::Id id = Warp::None;
                    glm::vec3 world{0.0f};
                    glm::vec3 forward{0.0f, 0.0f, -1.0f};
                    Treadle::Color colour;
                    bool camera = false;
                };
                std::vector<CompassMarker> compassMarkers;
                float compassRadius = std::max(0.5f, extent.radius);
                stage.walk([&](const Warp::Entity& entity, int){
                    if(!entity.visible || !(entity.camera || entity.mesh || entity.model || entity.points || entity.splat || entity.joint))
                        return;
                    const glm::mat4 world = stage.worldMatrix(entity.id, frame);
                    const glm::vec3 position(world[3]);
                    CompassMarker marker;
                    marker.id = entity.id;
                    marker.world = position;
                    marker.camera = bool(entity.camera);
                    if(entity.camera){
                        marker.forward = glm::normalize(glm::vec3(world * glm::vec4(0.0f, 0.0f, -1.0f, 0.0f)));
                        marker.colour = neonBlue;
                    }else if(entity.mesh || entity.model){
                        marker.colour = {1.00f, 0.32f, 0.38f, 1.00f};
                    }else if(entity.points){
                        marker.colour = {0.30f, 0.96f, 0.58f, 1.00f};
                    }else if(entity.splat){
                        marker.colour = {0.92f, 0.42f, 1.00f, 1.00f};
                    }else{
                        marker.colour = {0.90f, 0.82f, 0.42f, 1.00f};
                    }
                    compassRadius = std::max(compassRadius,
                        glm::length(glm::vec2(position.x - extent.centre.x, position.z - extent.centre.z)));
                    compassMarkers.push_back(marker);
                });

                const float compassW = std::min(164.0f, hudViewport.width * 0.31f);
                const float compassHitH = compassMinimized ? 34.0f : 166.0f;
                const Treadle::Rect compassHitBox{hudViewport.x + 14.0f, hudViewport.y + 14.0f, compassW, compassHitH};
                const auto compass = ui.region("stage-compass", compassHitBox);
                const Treadle::Rect toggleBox{compassHitBox.x + compassW - 27.0f, compassHitBox.y + 5.0f, 21.0f, 21.0f};
                const auto compassToggle = ui.region("stage-compass-toggle", toggleBox);
                if(compassToggle.pressed) compassMinimized = !compassMinimized;
                const float compassH = compassMinimized ? 34.0f : 166.0f;
                const Treadle::Rect compassBox{compassHitBox.x, compassHitBox.y, compassW, compassH};
                canvas.rect(compassBox, panelFill);
                canvas.outline(compassBox, 1.0f, panelEdge);
                const std::string compassTitle = Treadle::fitText("STAGE COMPASS", compassW - 43.0f, hudTextScale);
                canvas.text(compassBox.x + 9.0f, compassBox.y + 7.0f, compassTitle, gold, hudTextScale);
                canvas.rect(toggleBox, compassToggle.hot ? panelEdge : Treadle::Color{0.10f, 0.14f, 0.11f, 0.88f});
                canvas.outline(toggleBox, 1.0f, panelEdge);
                const float toggleMidX = toggleBox.x + toggleBox.width * 0.5f;
                const float toggleMidY = toggleBox.y + toggleBox.height * 0.5f;
                canvas.line(toggleBox.x + 5.0f, toggleMidY, toggleBox.x + toggleBox.width - 5.0f,
                            toggleMidY, 1.5f, gold);
                if(compassMinimized)
                    canvas.line(toggleMidX, toggleBox.y + 5.0f, toggleMidX, toggleBox.y + toggleBox.height - 5.0f,
                                1.5f, gold);
                if(!compassMinimized){
                    const Treadle::Rect mapBox{compassBox.x + 9.0f, compassBox.y + 27.0f,
                                               compassBox.width - 18.0f, compassBox.height - 37.0f};
                    canvas.rect(mapBox, {0.055f, 0.078f, 0.063f, 0.78f});
                    canvas.outline(mapBox, 1.0f, {0.28f, 0.39f, 0.29f, 0.75f});
                    const float mapCx = mapBox.x + mapBox.width * 0.5f;
                    const float mapCy = mapBox.y + mapBox.height * 0.5f;
                    const float mapR = std::min(mapBox.width, mapBox.height) * 0.39f;
                    for(int grid = -1; grid <= 1; ++grid){
                        const float offset = float(grid) * mapR * 0.5f;
                        canvas.line(mapCx + offset, mapCy - mapR, mapCx + offset, mapCy + mapR,
                                    1.0f, {0.32f, 0.44f, 0.34f, 0.24f});
                        canvas.line(mapCx - mapR, mapCy + offset, mapCx + mapR, mapCy + offset,
                                    1.0f, {0.32f, 0.44f, 0.34f, 0.24f});
                    }
                    canvas.line(mapCx - mapR, mapCy, mapCx + mapR, mapCy, 1.0f, {0.58f, 0.66f, 0.51f, 0.34f});
                    canvas.line(mapCx, mapCy - mapR, mapCx, mapCy + mapR, 1.0f, {0.58f, 0.66f, 0.51f, 0.34f});
                    canvas.text(mapCx - Treadle::textWidth("-Z", hudTextScale) * 0.5f, mapBox.y + 1.0f,
                                "-Z", softText, hudTextScale);
                    canvas.text(mapCx - Treadle::textWidth("+Z", hudTextScale) * 0.5f, mapBox.y + mapBox.height - 12.0f,
                                "+Z", softText, hudTextScale);
                    canvas.text(mapBox.x + 1.0f, mapCy - 5.0f, "-X", softText, hudTextScale);
                    canvas.text(mapBox.x + mapBox.width - 19.0f, mapCy - 5.0f, "+X", softText, hudTextScale);

                    auto markerPixel = [&](const CompassMarker& marker){
                        const float scale = mapR / (std::max(0.5f, compassRadius) * 1.12f);
                        return glm::vec2(mapCx + (marker.world.x - extent.centre.x) * scale,
                                         mapCy + (marker.world.z - extent.centre.z) * scale);
                    };
                    for(const CompassMarker& marker : compassMarkers){
                        const glm::vec2 at = markerPixel(marker);
                        if(!mapBox.contains(at.x, at.y)) continue;
                        const Treadle::Color colour = marker.id == selected ? gold : marker.colour;
                        if(marker.camera){
                            const glm::vec2 heading = glm::normalize(glm::vec2(marker.forward.x, marker.forward.z));
                            const glm::vec2 tip = at + heading * 11.0f;
                            canvas.line(at.x, at.y, tip.x, tip.y, 1.5f, colour);
                            const glm::vec2 side(-heading.y, heading.x);
                            canvas.line(tip.x, tip.y, tip.x - heading.x * 4.0f + side.x * 3.0f,
                                        tip.y - heading.y * 4.0f + side.y * 3.0f, 1.2f, colour);
                            canvas.line(tip.x, tip.y, tip.x - heading.x * 4.0f - side.x * 3.0f,
                                        tip.y - heading.y * 4.0f - side.y * 3.0f, 1.2f, colour);
                            canvas.triangle(at.x, at.y - 4.0f, at.x + 4.0f, at.y, at.x, at.y + 4.0f, colour);
                            canvas.triangle(at.x, at.y - 4.0f, at.x - 4.0f, at.y, at.x, at.y + 4.0f, colour);
                        }else{
                            canvas.triangle(at.x, at.y - 4.0f, at.x + 4.0f, at.y, at.x, at.y + 4.0f, colour);
                            canvas.triangle(at.x, at.y - 4.0f, at.x - 4.0f, at.y, at.x, at.y + 4.0f, colour);
                        }
                        if(marker.id == selected)
                            canvas.outline(Treadle::Rect{at.x - 6.0f, at.y - 6.0f, 12.0f, 12.0f}, 1.0f, gold);
                    }
                    if(compass.pressed && !compassToggle.pressed && mapBox.contains(compass.mouseX, compass.mouseY)){
                        float nearest = 9.0f;
                        Warp::Id hit = Warp::None;
                        for(const CompassMarker& marker : compassMarkers){
                            const glm::vec2 at = markerPixel(marker);
                            const float distance = glm::length(at - glm::vec2(compass.mouseX, compass.mouseY));
                            if(distance < nearest){ nearest = distance; hit = marker.id; }
                        }
                        if(hit != Warp::None){
                            selected = hit;
                            focus = Focus::Entity;
                            view.lookThrough = Warp::None;
                            view.orbit.target = glm::vec3(stage.worldMatrix(hit, frame)[3]);
                            view.orbit.distance = std::max(0.35f, extent.radius * 0.8f);
                        }
                    }
                }

                std::vector<Warp::Id> postcardCameras;
                stage.walk([&](const Warp::Entity& entity, int){
                    if(entity.camera) postcardCameras.push_back(entity.id);
                });
                auto appendDrawList = [](Treadle::DrawList& target, const Treadle::DrawList& source){
                    const uint32_t base = uint32_t(target.vertices.size());
                    target.vertices.insert(target.vertices.end(), source.vertices.begin(), source.vertices.end());
                    target.indices.reserve(target.indices.size() + source.indices.size());
                    for(uint32_t index : source.indices) target.indices.push_back(base + index);
                };
                auto isPostcardCamera = [&](Warp::Id id){
                    return std::find(postcardCameras.begin(), postcardCameras.end(), id) != postcardCameras.end();
                };
                if(!postcardCameras.empty()){
                    if(!isPostcardCamera(cameraCompareA)) cameraCompareA = postcardCameras.front();
                    if(!isPostcardCamera(cameraCompareB)){
                        cameraCompareB = cameraPostcardOffset < int(postcardCameras.size()) ?
                            postcardCameras[size_t(cameraPostcardOffset)] : postcardCameras.front();
                        for(Warp::Id id : postcardCameras) if(id != cameraCompareA){ cameraCompareB = id; break; }
                    }
                    if(cameraCompareMode){
                        ui.region("camera-compare-canvas", hudViewport);
                        const float initialDividerX = hudViewport.x + hudViewport.width * cameraCompareSplit;
                        const auto divider = ui.region("camera-compare-divider",
                            Treadle::Rect{initialDividerX - 9.0f, hudViewport.y, 18.0f, hudViewport.height});
                        if(divider.held)
                            cameraCompareSplit = std::clamp((divider.mouseX - hudViewport.x) /
                                                            std::max(1.0f, hudViewport.width), 0.20f, 0.80f);
                        const float dividerX = hudViewport.x + hudViewport.width * cameraCompareSplit;
                        const Treadle::Rect paneA{hudViewport.x, hudViewport.y,
                                                  dividerX - hudViewport.x, hudViewport.height};
                        const Treadle::Rect paneB{dividerX, hudViewport.y,
                                                  hudViewport.x + hudViewport.width - dividerX, hudViewport.height};
                        const auto drawPortal = [&](const Treadle::Rect& pane, Warp::Id cameraId,
                                                    const char* slot, const Treadle::Color& accent){
                            const Warp::Entity* cameraEntity = stage.get(cameraId);
                            if(!cameraEntity || !cameraEntity->camera) return;
                            canvas.rect(pane, {0.018f, 0.028f, 0.023f, 0.98f});
                            Loom::ViewportState previewState = view;
                            previewState.lookThrough = cameraId;
                            previewState.showGrid = false;
                            previewState.showPaths = false;
                            previewState.showCameras = false;
                            previewState.gpuMeshes = false;
                            const Loom::ViewCamera previewCamera = Loom::viewCameraFor(stage, frame, pane, previewState);
                            Treadle::DrawList preview;
                            Loom::paintStage(stage, frame, previewCamera, previewState, extent, selected, preview, 2400);
                            appendDrawList(canvas, preview);
                            canvas.rect(pane.x, pane.y, pane.width, 27.0f, {0.025f, 0.040f, 0.031f, 0.94f});
                            std::string title = Treadle::fitText(std::string(slot) + "   /   " + cameraEntity->name,
                                                                 pane.width - 20.0f, hudTextScale);
                            canvas.text(pane.x + 9.0f, pane.y + 6.0f, title, accent, hudTextScale);
                            const std::string footer = "FRAME " + std::to_string(int(std::lround(frame))) +
                                "   /   " + std::to_string(cameraEntity->camera->width) + " x " +
                                std::to_string(cameraEntity->camera->height);
                            const float footerY = pane.y + pane.height - 24.0f;
                            canvas.rect(pane.x, footerY - 4.0f, pane.width, 28.0f, {0.025f, 0.040f, 0.031f, 0.88f});
                            canvas.text(pane.x + 9.0f, footerY + 2.0f,
                                Treadle::fitText(footer, pane.width - 18.0f, hudTextScale),
                                {0.76f, 0.83f, 0.71f, 0.92f}, hudTextScale);
                            canvas.outline(pane, 1.0f, {accent.r, accent.g, accent.b, 0.84f});
                        };
                        drawPortal(paneA, cameraCompareA, "A", gold);
                        drawPortal(paneB, cameraCompareB, "B", neonBlue);
                        canvas.rect(dividerX - 1.0f, hudViewport.y, 2.0f, hudViewport.height,
                                    {0.96f, 0.78f, 0.30f, 0.96f});
                        const Treadle::Rect handle{dividerX - 13.0f,
                                                   hudViewport.y + hudViewport.height * 0.5f - 24.0f,
                                                   26.0f, 48.0f};
                        canvas.rect(handle, {0.055f, 0.078f, 0.063f, 0.98f});
                        canvas.outline(handle, 1.2f, {1.0f, 0.80f, 0.32f, 0.98f});
                        canvas.text(handle.x + 4.0f, handle.y + 18.0f, "A/B", gold,
                                    std::max(1.7f, hudTextScale * 0.78f));
                    }
                }
                if(!postcardCameras.empty()){
                    const float cardW = std::min(184.0f, std::max(138.0f, hudViewport.width * 0.29f));
                    const float cardH = 106.0f;
                    const int cardsVisible = std::clamp(int((hudViewport.height - 56.0f) / (cardH + 8.0f)), 1, 3);
                    const int maxOffset = std::max(0, int(postcardCameras.size()) - cardsVisible);
                    cameraPostcardOffset = std::clamp(cameraPostcardOffset, 0, maxOffset);
                    const float railX = hudViewport.x + hudViewport.width - cardW - 14.0f;
                    const float railY = hudViewport.y + 14.0f;
                    const float railH = 46.0f + float(cardsVisible) * (cardH + 8.0f);
                    const Treadle::Rect railBox{railX - 7.0f, railY - 5.0f, cardW + 14.0f, railH};
                    const auto rail = ui.region("camera-postcard-rail", railBox);
                    if(rail.wheel != 0.0f)
                        cameraPostcardOffset = std::clamp(cameraPostcardOffset - int(std::lround(rail.wheel)), 0, maxOffset);
                    canvas.rect(railBox, {0.035f, 0.055f, 0.043f, 0.90f});
                    canvas.outline(railBox, 1.0f, panelEdge);
                    canvas.text(railX, railY, "CAMERA POSTCARDS", gold, std::max(1.7f, hudTextScale * 0.82f));
                    const std::string pageText = std::to_string(cameraPostcardOffset + 1) + "/" +
                                                 std::to_string(postcardCameras.size());
                    canvas.text(railX + cardW - Treadle::textWidth(pageText, hudTextScale), railY,
                                pageText, softText, hudTextScale);
                    const bool canCompare = postcardCameras.size() >= 2;
                    const Treadle::Rect compareButton{railX, railY + 19.0f, 74.0f, 18.0f};
                    const auto compareToggle = ui.region("camera-postcard-compare", compareButton);
                    if(compareToggle.pressed && canCompare){
                        cameraCompareMode = !cameraCompareMode;
                        if(cameraCompareMode){
                            cameraCompareA = isPostcardCamera(view.lookThrough) ? view.lookThrough : postcardCameras.front();
                            cameraCompareB = cameraCompareA;
                            for(Warp::Id id : postcardCameras) if(id != cameraCompareA){ cameraCompareB = id; break; }
                            cameraCompareSplit = 0.5f;
                        }
                    }else if(compareToggle.pressed){
                        message = "Add a second camera to compare A/B views.";
                    }
                    canvas.rect(compareButton, cameraCompareMode ? Treadle::Color{0.78f, 0.61f, 0.24f, 1.0f}
                                                                 : Treadle::Color{0.10f, 0.15f, 0.11f, 1.0f});
                    canvas.outline(compareButton, 1.0f, cameraCompareMode ? gold : panelEdge);
                    const std::string compareLabel = cameraCompareMode ? "EXIT A/B" : (canCompare ? "COMPARE" : "NEED 2");
                    canvas.text(compareButton.x + 5.0f, compareButton.y + 3.0f,
                                Treadle::fitText(compareLabel, compareButton.width - 10.0f,
                                                 std::max(1.5f, hudTextScale * 0.72f)),
                                cameraCompareMode ? Treadle::Color{0.08f, 0.11f, 0.07f, 1.0f} : softText,
                                std::max(1.5f, hudTextScale * 0.72f));
                    canvas.text(railX + 82.0f, railY + 22.0f,
                                cameraCompareMode ? "PICK A AND B" : "SAME FRAME",
                                softText, std::max(1.5f, hudTextScale * 0.64f));
                    for(int slot = 0; slot < cardsVisible; ++slot){
                        const int cameraIndex = cameraPostcardOffset + slot;
                        if(cameraIndex >= int(postcardCameras.size())) break;
                        const Warp::Id cameraId = postcardCameras[size_t(cameraIndex)];
                        const Warp::Entity* cameraEntity = stage.get(cameraId);
                        if(!cameraEntity || !cameraEntity->camera) continue;
                        const Treadle::Rect card{railX, railY + 40.0f + float(slot) * (cardH + 8.0f), cardW, cardH};
                        const auto cardRegion = ui.region("camera-postcard-" + std::to_string(cameraId), card);
                        if(cardRegion.pressed && !cameraCompareMode) view.lookThrough = cameraId;
                        const Treadle::Rect assignABox{card.x + card.width - 43.0f, card.y + 4.0f, 17.0f, 17.0f};
                        const Treadle::Rect assignBBox{card.x + card.width - 22.0f, card.y + 4.0f, 17.0f, 17.0f};
                        const auto assignA = cameraCompareMode ? ui.region("camera-postcard-a-" + std::to_string(cameraId), assignABox) : Treadle::Ui::Region{};
                        const auto assignB = cameraCompareMode ? ui.region("camera-postcard-b-" + std::to_string(cameraId), assignBBox) : Treadle::Ui::Region{};
                        if(assignA.pressed) cameraCompareA = cameraId;
                        if(assignB.pressed) cameraCompareB = cameraId;
                        const bool liveCard = view.lookThrough == cameraId;
                        canvas.rect(card, {0.055f, 0.078f, 0.063f, 0.96f});
                        const Treadle::Rect thumb{card.x + 7.0f, card.y + 23.0f, card.width - 14.0f, 58.0f};
                        canvas.rect(thumb, {0.012f, 0.020f, 0.016f, 1.00f});
                        Loom::ViewportState previewState = view;
                        previewState.lookThrough = cameraId;
                        previewState.showGrid = false;
                        previewState.showPaths = false;
                        previewState.showCameras = false;
                        previewState.gpuMeshes = false;
                        const Loom::ViewCamera previewCamera = Loom::viewCameraFor(stage, frame, thumb, previewState);
                        Treadle::DrawList preview;
                        Loom::paintStage(stage, frame, previewCamera, previewState, extent, selected, preview, 1600);
                        appendDrawList(canvas, preview);
                        canvas.outline(thumb, 1.0f, liveCard ? neonBlue : Treadle::Color{0.40f, 0.50f, 0.37f, 0.85f});
                        canvas.outline(card, 1.0f, cameraCompareMode && (cameraCompareA == cameraId || cameraCompareB == cameraId) ? neonBlue : (liveCard ? neonBlue : panelEdge));
                        std::string cameraName = cameraEntity->name;
                        if(cameraName.size() > (cameraCompareMode ? 12u : 17u)) cameraName = cameraName.substr(0, cameraCompareMode ? 11u : 16u) + "...";
                        canvas.text(card.x + 7.0f, card.y + 5.0f,
                                    cameraName, liveCard ? neonBlue : softText, hudTextScale);
                        if(cameraCompareMode){
                            canvas.rect(assignABox, cameraCompareA == cameraId ? gold : Treadle::Color{0.10f, 0.15f, 0.11f, 1.0f});
                            canvas.outline(assignABox, 1.0f, cameraCompareA == cameraId ? gold : panelEdge);
                            canvas.text(assignABox.x + 5.0f, assignABox.y + 4.0f, "A",
                                cameraCompareA == cameraId ? Treadle::Color{0.08f, 0.11f, 0.07f, 1.0f} : softText, std::max(1.5f, hudTextScale * 0.75f));
                            canvas.rect(assignBBox, cameraCompareB == cameraId ? neonBlue : Treadle::Color{0.10f, 0.15f, 0.11f, 1.0f});
                            canvas.outline(assignBBox, 1.0f, cameraCompareB == cameraId ? neonBlue : panelEdge);
                            canvas.text(assignBBox.x + 5.0f, assignBBox.y + 4.0f, "B",
                                cameraCompareB == cameraId ? Treadle::Color{0.04f, 0.08f, 0.11f, 1.0f} : softText, std::max(1.5f, hudTextScale * 0.75f));
                        }
                        canvas.text(card.x + 7.0f, card.y + 86.0f,
                            cameraCompareMode ? (cameraCompareA == cameraId ? "PORTAL A" : cameraCompareB == cameraId ? "PORTAL B" : "TAP A OR B") : (liveCard ? "LIVE IN VIEWPORT" : "CLICK TO PREVIEW"),
                            cameraCompareMode && cameraCompareA == cameraId ? gold : cameraCompareMode && cameraCompareB == cameraId ? neonBlue : (liveCard ? gold : softText), hudTextScale);
                    }
                }
            }
        }

        //== QUICK LENS: HOLD I OVER AN OBJECT ===================================================
        {
            const Treadle::Rect& lensViewport = layout.viewport;
            const bool lensHeld = glfwGetKey(window, GLFW_KEY_I) == GLFW_PRESS;
            const bool lensCanOpen = lensHeld && !ui.wantsKeyboard() && !ui.wantsMouse() &&
                !importer.open && !motionPanel.open && !autoRig.open &&
                lensViewport.contains(float(cursorX), float(cursorY));
            if(lensCanOpen){
                const Loom::ViewCamera lensCamera = Loom::viewCameraFor(stage, frame, lensViewport, view);
                Warp::Id hoveredId = Loom::pickEntity(stage, frame, lensCamera, glm::vec2(float(cursorX), float(cursorY)));
                if(hoveredId == Warp::None){
                    float bestRatio = 1.0f;
                    stage.walk([&](const Warp::Entity& entity, int){
                        if(!entity.visible || !(entity.camera || entity.mesh || entity.model || entity.joint ||
                                               entity.points || entity.splat)) return;
                        const glm::mat4 world = stage.worldMatrix(entity.id, frame);
                        glm::vec2 projected;
                        float depth = 0.0f;
                        if(!Loom::project(lensCamera, glm::vec3(world[3]), projected, &depth) ||
                           !lensViewport.contains(projected.x, projected.y)) return;
                        float radius = 15.0f;
                        if(entity.mesh || entity.model){
                            const float scale = std::max({glm::length(glm::vec3(world[0])),
                                                          glm::length(glm::vec3(world[1])),
                                                          glm::length(glm::vec3(world[2]))});
                            radius = std::clamp(lensCamera.focal * scale * 0.62f / std::max(1e-3f, depth),
                                                14.0f, 110.0f);
                        }
                        const float ratio = glm::length(projected - glm::vec2(float(cursorX), float(cursorY))) / radius;
                        if(ratio < bestRatio){ bestRatio = ratio; hoveredId = entity.id; }
                    });
                }
                const Warp::Entity* lensEntity = stage.get(hoveredId);
                if(lensEntity){
                    const float lensW = std::min(264.0f, lensViewport.width - 24.0f);
                    const float lensH = 98.0f;
                    float lensX = float(cursorX) + 18.0f;
                    if(lensX + lensW > lensViewport.x + lensViewport.width - 8.0f)
                        lensX = float(cursorX) - lensW - 18.0f;
                    lensX = std::clamp(lensX, lensViewport.x + 8.0f,
                                       lensViewport.x + lensViewport.width - lensW - 8.0f);
                    const float lensY = std::clamp(float(cursorY) + 18.0f, lensViewport.y + 8.0f,
                                                   lensViewport.y + lensViewport.height - lensH - 8.0f);
                    const Treadle::Rect lensBox{lensX, lensY, lensW, lensH};
                    const Treadle::Color accent = sceneAccent(*lensEntity);
                    Treadle::DrawList& canvas = ui.canvas();
                    canvas.rect(lensBox, {0.035f, 0.055f, 0.043f, 0.97f});
                    canvas.outline(lensBox, 1.2f, {accent.r, accent.g, accent.b, 0.94f});
                    canvas.rect(lensBox.x, lensBox.y, 3.0f, lensBox.height, accent);
                    const float lensScale = std::max(1.8f, theme.textScale * 0.70f);
                    const std::string title = Treadle::fitText(sceneTag(*lensEntity) + " / " + lensEntity->name,
                                                               lensW - 24.0f, lensScale);
                    canvas.text(lensBox.x + 11.0f, lensBox.y + 7.0f, title, accent, lensScale);
                    const glm::mat4 world = stage.worldMatrix(lensEntity->id, frame);
                    const glm::vec3 position(world[3]);
                    char positionText[128];
                    std::snprintf(positionText, sizeof(positionText), "POSITION   X %+.2f   Y %+.2f   Z %+.2f",
                                  position.x, position.y, position.z);
                    canvas.text(lensBox.x + 11.0f, lensBox.y + 29.0f,
                                Treadle::fitText(positionText, lensW - 24.0f, lensScale),
                                {0.84f, 0.89f, 0.80f, 1.0f}, lensScale);
                    std::string componentInfo = "SCENE OBJECT";
                    if(lensEntity->camera){
                        char cameraText[96];
                        std::snprintf(cameraText, sizeof(cameraText), "CAMERA   %u x %u   /   %.0f px",
                                      lensEntity->camera->width, lensEntity->camera->height,
                                      double(lensEntity->camera->focalPixels));
                        componentInfo = cameraText;
                    }else if(lensEntity->splat){
                        componentInfo = "SPLAT   " + fs::path(lensEntity->splat->path).filename().string();
                    }else if(lensEntity->points){
                        componentInfo = "POINT CLOUD   " + std::to_string(lensEntity->points->positions.size()) + " POINTS";
                    }else if(lensEntity->mesh){
                        componentInfo = std::string("MESH   ") +
                            (lensEntity->mesh->shape == Warp::Shape::Cube ? "CUBE" : "PLANE");
                    }else if(lensEntity->model){
                        componentInfo = "MODEL COMPONENT";
                    }else if(lensEntity->joint){
                        componentInfo = "JOINT   " + lensEntity->name;
                    }
                    canvas.text(lensBox.x + 11.0f, lensBox.y + 50.0f,
                                Treadle::fitText(componentInfo, lensW - 24.0f, lensScale),
                                {0.70f, 0.80f, 0.68f, 1.0f}, lensScale);
                    canvas.text(lensBox.x + 11.0f, lensBox.y + 73.0f,
                                "CLICK TO SELECT   /   F TO FOCUS", {0.95f, 0.78f, 0.34f, 0.96f},
                                std::max(1.7f, lensScale * 0.88f));
                }
            }
        }

        //== SCENE PULSE: LIVE VIEWPORT ACTIVITY =================================================
        {
            const Treadle::Rect& pulseViewport = layout.viewport;
            if(!importer.open && !compositor.open && pulseViewport.width > 300.0f && pulseViewport.height > 190.0f){
                static glm::vec3 previousOrbitTarget = view.orbit.target;
                static float previousOrbitYaw = view.orbit.yaw;
                static float previousOrbitPitch = view.orbit.pitch;
                static float previousOrbitDistance = view.orbit.distance;
                static double previousPulseFrame = frame;
                static Warp::Id previousPulseCamera = view.lookThrough;
                static double cameraPulseUntil = 0.0;
                const double pulseTime = std::chrono::duration<double>(now.time_since_epoch()).count();
                const bool orbitChanged = glm::length(view.orbit.target - previousOrbitTarget) > 1e-4f ||
                    std::fabs(view.orbit.yaw - previousOrbitYaw) > 1e-4f ||
                    std::fabs(view.orbit.pitch - previousOrbitPitch) > 1e-4f ||
                    std::fabs(view.orbit.distance - previousOrbitDistance) > 1e-4f;
                if(orbitChanged || previousPulseCamera != view.lookThrough ||
                   (view.lookThrough != Warp::None && std::fabs(frame - previousPulseFrame) > 1e-4))
                    cameraPulseUntil = pulseTime + 0.75;
                previousOrbitTarget = view.orbit.target;
                previousOrbitYaw = view.orbit.yaw;
                previousOrbitPitch = view.orbit.pitch;
                previousOrbitDistance = view.orbit.distance;
                previousPulseFrame = frame;
                previousPulseCamera = view.lookThrough;

                struct PulseState{ std::string label; Treadle::Color colour; };
                std::vector<PulseState> states;
                if(job.running){
                    const char* label = job.task == Loom::Task::Solve ? "SOLVING" :
                                        job.task == Loom::Task::Train ? "TRAINING" :
                                        job.task == Loom::Task::AutoRig ? "AUTO RIG" : "PROCESSING";
                    states.push_back({label, {1.0f, 0.72f, 0.25f, 1.0f}});
                }
                if(viewportSplat.isLoading())
                    states.push_back({"SPLAT LOADING", {0.94f, 0.43f, 1.0f, 1.0f}});
                if(playing)
                    states.push_back({"PLAYING", {0.36f, 1.0f, 0.60f, 1.0f}});
                if(transformGhostActive || (leftDown && gizmoAxisHeld >= 0))
                    states.push_back({"TRANSFORMING", {1.0f, 0.34f, 0.42f, 1.0f}});
                if(view.lookThrough != Warp::None)
                    states.push_back({"CAMERA LIVE", {0.28f, 0.75f, 1.0f, 1.0f}});
                else if(pulseTime < cameraPulseUntil)
                    states.push_back({"VIEW MOVING", {0.28f, 0.75f, 1.0f, 1.0f}});

                std::string pulseLabel = "SCENE PULSE";
                for(const PulseState& state : states) pulseLabel += "   /   " + state.label;
                if(states.empty()) pulseLabel += "   /   READY";
                const float pulseW = std::min(510.0f, pulseViewport.width - 24.0f);
                const Treadle::Rect pulseBox{pulseViewport.x + (pulseViewport.width - pulseW) * 0.5f,
                                             pulseViewport.y + pulseViewport.height - 34.0f,
                                             pulseW, 26.0f};
                ui.region("scene-pulse", pulseBox);
                Treadle::DrawList& canvas = ui.canvas();
                canvas.rect(pulseBox, {0.035f, 0.055f, 0.043f, 0.91f});
                canvas.outline(pulseBox, 1.0f, {0.34f, 0.44f, 0.32f, 0.82f});
                const bool activePulse = !states.empty();
                const float beat = activePulse ? 0.70f + 0.30f * float(0.5 + 0.5 * std::sin(pulseTime * 6.0)) : 0.5f;
                canvas.rect(pulseBox.x + 8.0f, pulseBox.y + 9.0f, 8.0f, 8.0f,
                            {0.35f, 0.95f, 0.56f, activePulse ? beat : 0.45f});
                const Treadle::Color labelColour = states.empty() ? Treadle::Color{0.72f, 0.80f, 0.68f, 1.0f}
                                                                  : states.front().colour;
                const float pulseScale = std::max(1.8f, theme.textScale * 0.69f);
                const std::string fittedPulse = Treadle::fitText(pulseLabel, pulseW - 27.0f, pulseScale);
                canvas.text(pulseBox.x + 22.0f, pulseBox.y + 6.0f, fittedPulse, labelColour, pulseScale);
            }
        }

        const Treadle::Theme moodboardBaseTheme = ui.style();
        if(moodboard.open){
            const Treadle::Rect boardArea{
                layout.viewport.x + 5.0f, layout.viewport.y + 5.0f,
                std::max(0.0f, layout.viewport.width - 10.0f),
                std::max(0.0f, layout.viewport.height - 10.0f)
            };
            if(Loom::drawMoodboard(ui, moodboard, browser.images, browser.at, boardArea))
                setMoodboardOpen(false);
        }
        // Collect process output and app status after actions in this frame.
        {
            std::lock_guard<std::mutex> guard(job.lock);
            if(terminal.jobNextLineIndex < job.firstLineIndex)
                terminal.jobNextLineIndex = job.firstLineIndex;
            while(terminal.jobNextLineIndex < job.nextLineIndex){
                const size_t index = size_t(terminal.jobNextLineIndex - job.firstLineIndex);
                terminal.add(job.lines[index], Loom::TerminalState::classify(job.lines[index]));
                ++terminal.jobNextLineIndex;
            }
        }
        if(motionLive.logFile != terminal.liveLogPath){
            terminal.liveLogPath = motionLive.logFile;
            terminal.liveLogOffset = 0;
        }
        if(!terminal.liveLogPath.empty()){
            std::error_code logError;
            const auto logSize = fs::file_size(terminal.liveLogPath, logError);
            if(!logError){
                if(logSize < terminal.liveLogOffset) terminal.liveLogOffset = 0;
                if(logSize > terminal.liveLogOffset){
                    std::ifstream log(terminal.liveLogPath);
                    log.seekg(std::streamoff(terminal.liveLogOffset));
                    std::string line;
                    while(std::getline(log, line)) terminal.add("MotionBricks: " + line, Loom::TerminalState::classify(line));
                    terminal.liveLogOffset = logSize;
                }
            }
        }
        if(message != terminal.lastMessage){
            terminal.lastMessage = message;
            if(!message.empty()) terminal.add(message, Loom::TerminalState::classify(message));
        }
        if(motionLive.message != terminal.lastLiveMessage){
            terminal.lastLiveMessage = motionLive.message;
            if(!motionLive.message.empty()) terminal.add("MotionBricks: " + motionLive.message,
                Loom::TerminalState::classify(motionLive.message));
        }
        if(terminal.visible && layout.terminal.height > 0.0f){
            const Treadle::Rect& area = layout.terminal;
            Treadle::DrawList& canvas = ui.canvas();
            canvas.rect(area, Treadle::Color{0.035f, 0.050f, 0.039f, 1.0f});
            canvas.rect(area.x, area.y, area.width, 1.0f, theme.panelEdge);
            canvas.text(area.x + 13.0f, area.y + 8.0f, "TERMINAL", theme.title, theme.textScale * 0.76f);
            const bool runningNow = job.running || motionLive.active;
            canvas.text(area.x + 135.0f, area.y + 9.0f, runningNow ? "RUNNING" : "READY",
                        runningNow ? theme.accent : theme.dim, theme.textScale * 0.64f);
            auto terminalButton = [&](const std::string& id, const std::string& label, float x){
                const Treadle::Rect button{x, area.y + 5.0f, 58.0f, 25.0f};
                const auto hit = ui.region(id, button);
                canvas.rect(button, hit.hot ? theme.hot : theme.control);
                canvas.outline(button, 1.0f, theme.panelEdge);
                canvas.text(x + 8.0f, area.y + 9.0f, label, theme.text, theme.textScale * 0.62f);
                return hit.pressed;
            };
            if(terminalButton("terminal-clear", "Clear", area.x + area.width - 198.0f)){
                terminal.entries.clear();
                terminal.scrollRows = 0;
            }
            if(terminalButton("terminal-follow", terminal.follow ? "Follow" : "Paused", area.x + area.width - 134.0f)){
                terminal.follow = !terminal.follow;
                if(terminal.follow) terminal.scrollRows = 0;
            }
            if(terminalButton("terminal-close", "Close", area.x + area.width - 70.0f)) terminal.visible = false;
            canvas.line(area.x + 1.0f, area.y + 34.0f, area.x + area.width - 1.0f,
                        area.y + 34.0f, 1.0f, theme.panelEdge);
            const float lineHeight = 20.0f;
            const int fits = std::max(0, int((area.height - 42.0f) / lineHeight));
            const Treadle::Rect logArea{area.x, area.y + 35.0f, area.width, area.height - 35.0f};
            const auto logHit = ui.region("terminal-lines", logArea);
            if(logHit.wheel != 0.0f){
                terminal.scrollRows = std::clamp(terminal.scrollRows + int(std::round(logHit.wheel * 3.0f)),
                                                 0, std::max(0, int(terminal.entries.size()) - fits));
                terminal.follow = terminal.scrollRows == 0;
            }
            const int end = std::max(0, int(terminal.entries.size()) - terminal.scrollRows);
            const int begin = std::max(0, end - fits);
            const float textScale = theme.textScale * 0.65f;
            for(int i = begin; i < end; ++i){
                const auto& entry = terminal.entries[size_t(i)];
                const std::time_t stamp = std::chrono::system_clock::to_time_t(entry.at);
                std::tm localTime{};
                localtime_r(&stamp, &localTime);
                char timeText[16];
                std::strftime(timeText, sizeof(timeText), "%H:%M:%S", &localTime);
                const char* tag = entry.level == Loom::TerminalLevel::Error ? "ERR" :
                                  entry.level == Loom::TerminalLevel::Debug ? "DBG" :
                                  entry.level == Loom::TerminalLevel::Running ? "RUN" : "INF";
                const Treadle::Color ink = entry.level == Loom::TerminalLevel::Error ? theme.warning :
                                           entry.level == Loom::TerminalLevel::Debug ? theme.dim : theme.text;
                const std::string rendered = std::string(timeText) + "  [" + tag + "]  " + entry.text;
                canvas.text(area.x + 13.0f, area.y + 40.0f + float(i - begin) * lineHeight,
                            Treadle::fitText(rendered, area.width - 24.0f, textScale), ink, textScale);
            }
        }
        if(motionPanel.open && motionPanel.flowMode == 1 && motionPanel.selectedPose >= 0 &&
           size_t(motionPanel.selectedPose) < motionPanel.poseConstraints.size()){
            Loom::MotionPoseConstraint& pose = motionPanel.poseConstraints[size_t(motionPanel.selectedPose)];
            auto joints = Loom::motionDirectJointPositions(pose);
            const auto& bones = Loom::motionDirectBones();
            const auto& controls = Loom::motionDirectControls();
            glm::vec3 characterOrigin(0.0f), anchor(0.0f);
            float scale = 1.0f;
            if(stage.contains(motionPanel.targetCharacter)){
                const Warp::Id rootBone = motionRootAnchor(motionPanel.targetCharacter);
                characterOrigin = glm::vec3(stage.worldMatrix(rootBone, stage.startFrame)[3]);
                characterOrigin.y = 0.0f;
                anchor = characterOrigin;
                const Loom::MotionRigRestPose rig = Loom::motionRigRestPose(stage, motionPanel.targetCharacter);
                scale = std::clamp(rig.bounds.height() / 1.7f, 0.5f, 2.0f);
            }else{
                characterOrigin = view.orbit.target;
                characterOrigin.y = 0.0f;
                anchor = characterOrigin;
            }
            Loom::MotionRootWaypoint pathPoint{};
            glm::quat pathRotation(1.0f, 0.0f, 0.0f, 0.0f);
            if(motionPanel.rootPathEnabled && !motionPanel.rootWaypoints.empty()){
                pathPoint = Loom::motionRootPathAt(
                    motionPanel.rootWaypoints, float(pose.frame), motionPanel.smoothRootPath);
                anchor += glm::vec3(pathPoint.x, 0.0f, pathPoint.z);
                pathRotation = glm::angleAxis(pathPoint.heading, glm::vec3(0.0f, 1.0f, 0.0f));
            }
            const Loom::ViewCamera skeletonCamera = Loom::viewCameraFor(stage, frame, layout.viewport, view);
            const Treadle::Rect& v = layout.viewport;
            const Treadle::Rect motionBox{};
            auto toWorld = [&](const glm::vec3& local){ return anchor + pathRotation * (local * scale); };
            auto toRigLocal = [&](const glm::vec3& world){
                return glm::inverse(pathRotation) * (world - anchor) / std::max(1e-4f, scale);
            };
            auto intersectPlane = [&](const glm::vec2& pixel, const glm::vec3& point,
                                      const glm::vec3& normal, glm::vec3& hitPoint){
                const Loom::Ray ray = Loom::rayThrough(skeletonCamera, pixel);
                const float denominator = glm::dot(ray.direction, normal);
                if(std::fabs(denominator) < 1e-5f) return false;
                const float distance = glm::dot(point - ray.origin, normal) / denominator;
                if(distance < 0.0f) return false;
                hitPoint = ray.origin + distance * ray.direction;
                return true;
            };
            auto controlWorld = [&](const Loom::MotionDirectControl& control){
                if(control.kind == Loom::MotionDirectControlKind::PathRoot)
                    return characterOrigin + glm::vec3(pathPoint.x, 0.035f, pathPoint.z);
                return toWorld(joints[size_t(control.joint)]);
            };

            //Compact mode switch: the default rig edits intent through IK targets; joint mode
            //keeps the precise SOMA30 rotation handles available for technical adjustments.
            const float modeWidth = 206.0f, modeHeight = 30.0f;
            const Treadle::Rect modeBox{v.x + v.width - modeWidth - 12.0f, v.y + 12.0f,
                                        modeWidth, modeHeight};
            const float modeGap = 4.0f, modeButtonWidth = (modeWidth - modeGap) * 0.5f;
            const Treadle::Rect rigModeBox{modeBox.x, modeBox.y, modeButtonWidth, modeHeight};
            const Treadle::Rect jointModeBox{modeBox.x + modeButtonWidth + modeGap, modeBox.y,
                                             modeButtonWidth, modeHeight};
            const auto rigModeHit = ui.region("motion-control-rig-mode", rigModeBox);
            const auto jointModeHit = ui.region("motion-control-joint-mode", jointModeBox);
            if(rigModeHit.pressed) motionPanel.controlRigMode = true;
            if(jointModeHit.pressed) motionPanel.controlRigMode = false;
            Treadle::DrawList& canvas = ui.canvas();
            const Treadle::Color rigActive{0.12f, 0.38f, 0.33f, 0.98f};
            const Treadle::Color modeIdle{0.055f, 0.075f, 0.065f, 0.96f};
            canvas.rect(rigModeBox, motionPanel.controlRigMode ? rigActive : modeIdle);
            canvas.outline(rigModeBox, 1.0f, motionPanel.controlRigMode ? theme.accent : theme.panelEdge);
            canvas.text(rigModeBox.x + 11.0f, rigModeBox.y + 8.0f, "RIG CONTROLS",
                        motionPanel.controlRigMode ? theme.title : theme.dim, theme.textScale * 0.68f);
            canvas.rect(jointModeBox, !motionPanel.controlRigMode ? rigActive : modeIdle);
            canvas.outline(jointModeBox, 1.0f, !motionPanel.controlRigMode ? theme.accent : theme.panelEdge);
            canvas.text(jointModeBox.x + 15.0f, jointModeBox.y + 8.0f, "JOINTS",
                        !motionPanel.controlRigMode ? theme.title : theme.dim, theme.textScale * 0.68f);

            std::array<bool, 11> controlHot{};
            std::array<glm::vec2, 11> controlPixels{};
            std::array<bool, 11> controlVisible{};
            auto projectControls = [&]{
                joints = Loom::motionDirectJointPositions(pose);
                if(motionPanel.rootPathEnabled && !motionPanel.rootWaypoints.empty()){
                    pathPoint = Loom::motionRootPathAt(
                        motionPanel.rootWaypoints, float(pose.frame), motionPanel.smoothRootPath);
                    anchor = characterOrigin + glm::vec3(pathPoint.x, 0.0f, pathPoint.z);
                    pathRotation = glm::angleAxis(pathPoint.heading, glm::vec3(0.0f, 1.0f, 0.0f));
                }
                for(size_t i = 0; i < controls.size(); ++i){
                    const glm::vec3 world = controlWorld(controls[i]);
                    controlVisible[i] = Loom::project(skeletonCamera, world, controlPixels[i]) &&
                        layout.viewport.contains(controlPixels[i].x, controlPixels[i].y) &&
                        !motionBox.contains(controlPixels[i].x, controlPixels[i].y);
                }
            };
            projectControls();

            if(motionPanel.controlRigMode){
                if(!leftDown) motionRigDrag.control = -1;
                for(size_t i = 0; i < controls.size(); ++i){
                    if(!controlVisible[i]) continue;
                    const Loom::MotionDirectControl& control = controls[i];
                    const glm::vec3 world = controlWorld(control);
                    const Treadle::Rect hitBox{controlPixels[i].x - 14.0f, controlPixels[i].y - 14.0f, 28.0f, 28.0f};
                    const auto hit = ui.region("motion-rig-control-" + std::to_string(i), hitBox);
                    controlHot[i] = hit.hot || hit.held;
                    if(hit.pressed){
                        motionPanel.selectedRigControl = int(i);
                        motionRigDrag.control = int(i);
                        motionRigDrag.pose = motionPanel.selectedPose;
                        motionRigDrag.startMouse = glm::vec2(hit.mouseX, hit.mouseY);
                        motionRigDrag.startWorld = world;
                        motionRigDrag.startPose = pose;
                        if(control.kind == Loom::MotionDirectControlKind::PathRoot){
                            motionRigDrag.planeNormal = glm::vec3(0.0f, 1.0f, 0.0f);
                            const glm::vec3 floorPoint(world.x, 0.035f, world.z);
                            if(!intersectPlane(motionRigDrag.startMouse, floorPoint,
                                               motionRigDrag.planeNormal, motionRigDrag.planeStartHit))
                                motionRigDrag.planeStartHit = world;
                        }else if(control.kind != Loom::MotionDirectControlKind::RotateJoint){
                            motionRigDrag.planeNormal = glm::normalize(world - skeletonCamera.eye);
                            if(!intersectPlane(motionRigDrag.startMouse, world, motionRigDrag.planeNormal,
                                               motionRigDrag.planeStartHit))
                                motionRigDrag.planeStartHit = world;
                        }
                    }
                    if(!hit.held || motionRigDrag.control != int(i) ||
                       motionRigDrag.pose != motionPanel.selectedPose) continue;
                    if(control.kind == Loom::MotionDirectControlKind::PathRoot){
                        if(pose.frame <= 0 || motionPanel.rootWaypoints.empty()) continue;
                        glm::vec3 currentHit;
                        if(!intersectPlane(glm::vec2(hit.mouseX, hit.mouseY),
                                           motionRigDrag.planeStartHit,
                                           motionRigDrag.planeNormal, currentHit)) continue;
                        const glm::vec3 targetWorld = motionRigDrag.startWorld +
                            (currentHit - motionRigDrag.planeStartHit);
                        Loom::MotionRootWaypoint key = Loom::motionRootPathAt(
                            motionPanel.rootWaypoints, float(pose.frame), motionPanel.smoothRootPath);
                        key.frame = pose.frame;
                        key.x = targetWorld.x - characterOrigin.x;
                        key.z = targetWorld.z - characterOrigin.z;
                        const int lastFrame = Loom::kimodoMotionLastFrame({motionPanel.directedAction});
                        Loom::upsertMotionRootWaypoint(motionPanel.rootWaypoints, key, lastFrame);
                        const auto found = std::lower_bound(motionPanel.rootWaypoints.begin(),
                            motionPanel.rootWaypoints.end(), pose.frame,
                            [](const Loom::MotionRootWaypoint& item, int value){ return item.frame < value; });
                        if(found != motionPanel.rootWaypoints.end())
                            motionPanel.selectedRootWaypoint = int(found - motionPanel.rootWaypoints.begin());
                        motionPanel.rootTrackCursorFrame = float(pose.frame);
                    }else if(control.kind == Loom::MotionDirectControlKind::RotateJoint){
                        Loom::MotionPoseConstraint& edited = motionPanel.poseConstraints[size_t(motionPanel.selectedPose)];
                        edited = motionRigDrag.startPose;
                        const glm::vec2 delta(hit.mouseX, hit.mouseY);
                        const glm::vec2 moved = delta - motionRigDrag.startMouse;
                        glm::vec3& angles = edited.rotationDegrees[size_t(control.joint)];
                        angles = motionRigDrag.startPose.rotationDegrees[size_t(control.joint)] +
                            glm::vec3(moved.y * 0.45f, moved.x * 0.45f, 0.0f);
                        angles = glm::clamp(angles, glm::vec3(-180.0f), glm::vec3(180.0f));
                    }else{
                        glm::vec3 currentHit;
                        if(!intersectPlane(glm::vec2(hit.mouseX, hit.mouseY),
                                           motionRigDrag.planeStartHit,
                                           motionRigDrag.planeNormal, currentHit)) continue;
                        const glm::vec3 targetWorld = motionRigDrag.startWorld +
                            (currentHit - motionRigDrag.planeStartHit);
                        const glm::vec3 targetLocal = toRigLocal(targetWorld);
                        Loom::MotionPoseConstraint& edited = motionPanel.poseConstraints[size_t(motionPanel.selectedPose)];
                        edited = motionRigDrag.startPose;
                        const auto startJoints = Loom::motionDirectJointPositions(motionRigDrag.startPose);
                        const bool rotateEnd = control.kind == Loom::MotionDirectControlKind::LimbTarget &&
                            (glfwGetKey(window, GLFW_KEY_LEFT_ALT) == GLFW_PRESS ||
                             glfwGetKey(window, GLFW_KEY_RIGHT_ALT) == GLFW_PRESS);
                        if(rotateEnd){
                            const glm::vec2 moved = glm::vec2(hit.mouseX, hit.mouseY) - motionRigDrag.startMouse;
                            glm::vec3& angles = edited.rotationDegrees[size_t(control.end)];
                            angles = motionRigDrag.startPose.rotationDegrees[size_t(control.end)] +
                                glm::vec3(moved.y * 0.45f, moved.x * 0.45f, 0.0f);
                            angles = glm::clamp(angles, glm::vec3(-180.0f), glm::vec3(180.0f));
                            if(motionPanel.mirrorBone){
                                const int other = Loom::motionDirectMirrorBone(control.end);
                                edited.rotationDegrees[size_t(other)] = glm::vec3(angles.x, -angles.y, -angles.z);
                            }
                        }else{
                            const glm::vec3 endTarget = control.kind == Loom::MotionDirectControlKind::LimbTarget
                                ? targetLocal : startJoints[size_t(control.end)];
                            const glm::vec3 poleTarget = control.kind == Loom::MotionDirectControlKind::Pole
                                ? targetLocal : startJoints[size_t(control.middle)];
                            Loom::motionDirectSolveTwoBoneIK(edited, control.upper, control.middle,
                                                             control.end, endTarget, poleTarget);
                            if(motionPanel.mirrorBone){
                                const int mirroredUpper = Loom::motionDirectMirrorBone(control.upper);
                                const int mirroredMiddle = Loom::motionDirectMirrorBone(control.middle);
                                const int mirroredEnd = Loom::motionDirectMirrorBone(control.end);
                                Loom::motionDirectSolveTwoBoneIK(edited, mirroredUpper, mirroredMiddle,
                                    mirroredEnd, glm::vec3(-endTarget.x, endTarget.y, endTarget.z),
                                    glm::vec3(-poleTarget.x, poleTarget.y, poleTarget.z));
                            }
                        }
                    }
                }
            }else{
                for(size_t i = 0; i < joints.size(); ++i){
                    if(!Loom::project(skeletonCamera, toWorld(joints[i]), controlPixels[0]) ||
                       !layout.viewport.contains(controlPixels[0].x, controlPixels[0].y) ||
                       motionBox.contains(controlPixels[0].x, controlPixels[0].y)) continue;
                    const Treadle::Rect hitBox{controlPixels[0].x - 8.0f, controlPixels[0].y - 8.0f, 16.0f, 16.0f};
                    const auto hit = ui.region("directed-bone-" + std::to_string(i), hitBox);
                    if(hit.pressed){
                        motionPanel.selectedBone = int(i);
                        motionPanel.lastBoneDragMouse = glm::vec2(hit.mouseX, hit.mouseY);
                    }
                    if(hit.held && motionPanel.selectedBone == int(i)){
                        const glm::vec2 current(hit.mouseX, hit.mouseY);
                        const glm::vec2 delta = current - motionPanel.lastBoneDragMouse;
                        if(glm::length(delta) > 0.0f){
                            glm::vec3& angles = motionPanel.poseConstraints[size_t(motionPanel.selectedPose)].rotationDegrees[i];
                            angles.x = std::clamp(angles.x + delta.y * 0.5f, -180.0f, 180.0f);
                            angles.y = std::clamp(angles.y + delta.x * 0.5f, -180.0f, 180.0f);
                            if(motionPanel.mirrorBone){
                                const int other = Loom::motionDirectMirrorBone(int(i));
                                if(other != int(i)) motionPanel.poseConstraints[size_t(motionPanel.selectedPose)]
                                    .rotationDegrees[size_t(other)] = glm::vec3(angles.x, -angles.y, -angles.z);
                            }
                        }
                        motionPanel.lastBoneDragMouse = current;
                    }
                }
            }
            projectControls();
            for(size_t i = 0; i < joints.size(); ++i){
                const int parent = bones[i].parent;
                if(parent < 0) continue;
                glm::vec2 childPixel, parentPixel;
                if(!Loom::project(skeletonCamera, toWorld(joints[i]), childPixel) ||
                   !Loom::project(skeletonCamera, toWorld(joints[size_t(parent)]), parentPixel) ||
                   !layout.viewport.contains(childPixel.x, childPixel.y) ||
                   !layout.viewport.contains(parentPixel.x, parentPixel.y) ||
                   motionBox.contains(childPixel.x, childPixel.y) || motionBox.contains(parentPixel.x, parentPixel.y)) continue;
                canvas.line(parentPixel.x, parentPixel.y, childPixel.x, childPixel.y, 2.5f,
                            Treadle::Color{0.23f, 0.85f, 0.97f, 0.9f});
            }
            if(motionPanel.controlRigMode){
                for(size_t i = 0; i < controls.size(); ++i){
                    if(!controlVisible[i]) continue;
                    const Loom::MotionDirectControl& control = controls[i];
                    const bool active = int(i) == motionPanel.selectedRigControl;
                    const bool rootLocked = control.kind == Loom::MotionDirectControlKind::PathRoot && pose.frame <= 0;
                    Treadle::Color tint = control.kind == Loom::MotionDirectControlKind::PathRoot
                        ? Treadle::Color{1.0f, 0.76f, 0.28f, 1.0f}
                        : control.kind == Loom::MotionDirectControlKind::RotateJoint
                            ? Treadle::Color{0.81f, 0.54f, 1.0f, 1.0f}
                            : control.kind == Loom::MotionDirectControlKind::Pole
                                ? Treadle::Color{1.0f, 0.57f, 0.24f, 1.0f}
                                : Treadle::Color{0.24f, 0.92f, 0.98f, 1.0f};
                    if(rootLocked) tint = theme.dim;
                    const float radius = active || controlHot[i] ? 10.0f : 8.0f;
                    const Treadle::Rect box{controlPixels[i].x - radius, controlPixels[i].y - radius,
                                            radius * 2.0f, radius * 2.0f};
                    canvas.rect(box, active ? Treadle::Color{tint.r, tint.g, tint.b, 0.62f}
                                            : Treadle::Color{0.02f, 0.04f, 0.04f, 0.92f});
                    canvas.outline(box, active ? 2.0f : 1.3f, tint);
                    canvas.line(controlPixels[i].x - 3.0f, controlPixels[i].y,
                                controlPixels[i].x + 3.0f, controlPixels[i].y, 1.1f, tint);
                    canvas.line(controlPixels[i].x, controlPixels[i].y - 3.0f,
                                controlPixels[i].x, controlPixels[i].y + 3.0f, 1.1f, tint);
                    if(active || controlHot[i]){
                        const float labelScale = std::max(1.0f, theme.textScale * 0.62f);
                        const float labelWidth = std::clamp(float(std::char_traits<char>::length(control.label)) *
                            labelScale * 0.64f + 16.0f, 42.0f, 92.0f);
                        float labelX = controlPixels[i].x + 14.0f;
                        if(labelX + labelWidth > v.x + v.width - 6.0f) labelX = controlPixels[i].x - labelWidth - 14.0f;
                        const float labelY = std::clamp(controlPixels[i].y - 10.0f,
                            v.y + 5.0f, v.y + v.height - 23.0f);
                        const Treadle::Rect labelBox{labelX, labelY, labelWidth, 19.0f};
                        canvas.rect(labelBox, Treadle::Color{0.015f, 0.025f, 0.025f, 0.94f});
                        canvas.outline(labelBox, 1.0f, tint);
                        canvas.text(labelX + 7.0f, labelY + 3.0f, control.label, tint, labelScale);
                    }
                }
                const float hintScale = std::max(1.0f, theme.textScale * 0.58f);
                const float hintX = std::max(motionBox.x + motionBox.width + 10.0f, modeBox.x - 310.0f);
                const float hintWidth = std::max(0.0f, modeBox.x - hintX - 8.0f);
                if(hintWidth > 90.0f){
                    const Treadle::Rect hintBox{hintX, modeBox.y + 2.0f, hintWidth, 26.0f};
                    canvas.rect(hintBox, Treadle::Color{0.015f, 0.025f, 0.025f, 0.82f});
                    canvas.outline(hintBox, 1.0f, Treadle::Color{theme.accent.r, theme.accent.g, theme.accent.b, 0.28f});
                    const std::string hint = Treadle::fitText(
                        "Drag a target to pose  ·  Alt-drag to rotate", hintWidth - 12.0f, hintScale);
                    canvas.text(hintX + 6.0f, modeBox.y + 9.0f, hint, theme.dim, hintScale);
                }
            }else{
                for(size_t i = 0; i < joints.size(); ++i){
                    glm::vec2 pixel;
                    if(!Loom::project(skeletonCamera, toWorld(joints[i]), pixel) ||
                       !layout.viewport.contains(pixel.x, pixel.y) || motionBox.contains(pixel.x, pixel.y)) continue;
                    const bool selectedBone = int(i) == motionPanel.selectedBone;
                    canvas.rect(pixel.x - (selectedBone ? 5.0f : 3.5f),
                                pixel.y - (selectedBone ? 5.0f : 3.5f),
                                selectedBone ? 10.0f : 7.0f, selectedBone ? 10.0f : 7.0f,
                                selectedBone ? theme.title : theme.accent);
                    if(selectedBone)
                        canvas.text(pixel.x + 11.0f, pixel.y - 9.0f, bones[i].name,
                                    theme.title, theme.textScale * 0.65f);
                }
            }
        }
        if(motionPanel.open && motionPanel.helpOpen){
            const Treadle::Rect& v = layout.viewport;
            const float helpWidth = std::min(380.0f, std::max(240.0f, v.width - 20.0f));
            const float helpHeight = std::min(590.0f, std::max(180.0f, v.height - 20.0f));
            const float helpX = motionPanel.rootPathEnabled ? v.x + v.width - helpWidth - 10.0f : v.x + 10.0f;
            Loom::drawMotionHelp(ui, motionPanel, Treadle::Rect{helpX, v.y + 10.0f, helpWidth, helpHeight});
        }
        const Warp::Entity* poseRigEntity = stage.get(poseEdit.rig);
        const bool showAnimatorPoseRig = poseEdit.active && poseRigEntity && poseRigEntity->animator &&
            poseEdit.animation == poseRigEntity->animator->activeAnimation &&
            std::round(frame) == poseEdit.frame && layout.viewport.width > 40.0f &&
            layout.viewport.height > 40.0f && !compositor.open;
        if(!showAnimatorPoseRig) animatorRigDrag.control = -1;
        if(showAnimatorPoseRig){
            const auto jointIds = motionDirectRigJointIds(poseEdit.rig);
            const auto& directBones = Loom::motionDirectBones();
            const auto& controls = Loom::motionDirectControls();
            const Loom::ViewCamera poseCamera = Loom::viewCameraFor(stage, poseEdit.frame, layout.viewport, view);
            const Treadle::Rect& viewport = layout.viewport;
            Treadle::DrawList& canvas = ui.canvas();
            auto controlJointIndex = [](const Loom::MotionDirectControl& control){
                if(control.kind == Loom::MotionDirectControlKind::PathRoot) return 0;
                if(control.kind == Loom::MotionDirectControlKind::RotateJoint) return control.joint;
                if(control.kind == Loom::MotionDirectControlKind::LimbTarget) return control.end;
                return control.middle;
            };
            auto jointWorld = [&](int index){
                return index >= 0 && index < int(jointIds.size()) && jointIds[size_t(index)] != Warp::None
                    ? glm::vec3(stage.worldMatrix(jointIds[size_t(index)], poseEdit.frame)[3])
                    : glm::vec3(0.0f);
            };
            auto hasJoint = [&](int index){
                return index >= 0 && index < int(jointIds.size()) &&
                    jointIds[size_t(index)] != Warp::None && stage.get(jointIds[size_t(index)]);
            };
            auto controlAvailable = [&](const Loom::MotionDirectControl& control){
                if(control.kind == Loom::MotionDirectControlKind::LimbTarget ||
                   control.kind == Loom::MotionDirectControlKind::Pole){
                    if(!hasJoint(control.upper) || !hasJoint(control.middle) || !hasJoint(control.end)) return false;
                    const Warp::Entity* middle = stage.get(jointIds[size_t(control.middle)]);
                    const Warp::Entity* end = stage.get(jointIds[size_t(control.end)]);
                    return middle && end && middle->parent == jointIds[size_t(control.upper)] &&
                        end->parent == jointIds[size_t(control.middle)];
                }
                return hasJoint(controlJointIndex(control));
            };
            auto intersectPosePlane = [&](const glm::vec2& pixel, const glm::vec3& point,
                                          const glm::vec3& normal, glm::vec3& hitPoint){
                const Loom::Ray ray = Loom::rayThrough(poseCamera, pixel);
                const float denominator = glm::dot(ray.direction, normal);
                if(std::fabs(denominator) < 1e-5f) return false;
                const float distance = glm::dot(point - ray.origin, normal) / denominator;
                if(distance < 0.0f) return false;
                hitPoint = ray.origin + distance * ray.direction;
                return true;
            };
            auto setJointLocal = [&](int index, const Warp::Transform& local){
                if(hasJoint(index)) stage.setLocalAt(jointIds[size_t(index)], poseEdit.frame, local);
            };
            auto parentWorldFor = [&](int index){
                const Warp::Entity* entity = hasJoint(index) ? stage.get(jointIds[size_t(index)]) : nullptr;
                return !entity || entity->parent == Warp::None
                    ? glm::mat4(1.0f) : stage.worldMatrix(entity->parent, poseEdit.frame);
            };
            auto setJointWorldRotation = [&](int index, const glm::quat& worldRotation,
                                             Warp::Transform local){
                if(!hasJoint(index)) return;
                local.rotation = glm::normalize(glm::inverse(rotationOf(parentWorldFor(index))) * worldRotation);
                setJointLocal(index, local);
            };
            auto solveLimb = [&](const Loom::MotionDirectControl& control,
                                 const glm::vec3& endTarget, const glm::vec3& poleTarget){
                const glm::vec3 root = jointWorld(control.upper);
                const glm::vec3 middle = jointWorld(control.middle);
                const glm::vec3 end = jointWorld(control.end);
                const float upperLength = glm::length(middle - root);
                const float lowerLength = glm::length(end - middle);
                glm::vec3 toTarget = endTarget - root;
                float distance = glm::length(toTarget);
                if(upperLength < 1e-5f || lowerLength < 1e-5f || distance < 1e-5f) return;
                const glm::vec3 direction = toTarget / distance;
                distance = std::clamp(distance, std::fabs(upperLength - lowerLength) + 1e-4f,
                                      upperLength + lowerLength - 1e-4f);
                glm::vec3 bend = poleTarget - root;
                bend -= direction * glm::dot(bend, direction);
                if(glm::dot(bend, bend) < 1e-8f){
                    bend = middle - root;
                    bend -= direction * glm::dot(bend, direction);
                }
                if(glm::dot(bend, bend) < 1e-8f)
                    bend = glm::cross(direction, std::fabs(direction.y) < 0.9f
                        ? glm::vec3(0, 1, 0) : glm::vec3(0, 0, 1));
                bend = glm::normalize(bend);
                const float along = (upperLength * upperLength - lowerLength * lowerLength + distance * distance) /
                                    (2.0f * distance);
                const float height = std::sqrt(std::max(0.0f, upperLength * upperLength - along * along));
                const glm::vec3 middleTarget = root + direction * along + bend * height;

                const Warp::Id upperId = jointIds[size_t(control.upper)];
                const Warp::Id middleId = jointIds[size_t(control.middle)];
                const glm::quat upperWorldRotation = rotationOf(stage.worldMatrix(upperId, poseEdit.frame));
                const glm::quat upperDelta = Loom::motionDirectRotationBetween(middle - root, middleTarget - root);
                setJointWorldRotation(control.upper, glm::normalize(upperDelta * upperWorldRotation),
                                      stage.localAt(upperId, poseEdit.frame));

                const glm::vec3 updatedMiddle = jointWorld(control.middle);
                const glm::vec3 updatedEnd = jointWorld(control.end);
                const glm::quat middleWorldRotation = rotationOf(stage.worldMatrix(middleId, poseEdit.frame));
                const glm::quat middleDelta = Loom::motionDirectRotationBetween(
                    updatedEnd - updatedMiddle, endTarget - updatedMiddle);
                setJointWorldRotation(control.middle, glm::normalize(middleDelta * middleWorldRotation),
                                      stage.localAt(middleId, poseEdit.frame));
            };

            std::array<glm::vec2, 11> controlPixels{};
            std::array<bool, 11> controlVisible{};
            std::array<bool, 11> controlHot{};
            auto projectPoseRig = [&]{
                for(size_t i = 0; i < controls.size(); ++i){
                    controlVisible[i] = false;
                    if(!controlAvailable(controls[i])) continue;
                    const int joint = controlJointIndex(controls[i]);
                    controlVisible[i] = Loom::project(poseCamera, jointWorld(joint), controlPixels[i]) &&
                        viewport.contains(controlPixels[i].x, controlPixels[i].y);
                }
            };
            projectPoseRig();

            for(size_t i = 0; i < directBones.size(); ++i){
                const int parent = directBones[i].parent;
                if(parent < 0 || !hasJoint(int(i)) || !hasJoint(parent)) continue;
                glm::vec2 childPixel, parentPixel;
                if(!Loom::project(poseCamera, jointWorld(int(i)), childPixel) ||
                   !Loom::project(poseCamera, jointWorld(parent), parentPixel) ||
                   !viewport.contains(childPixel.x, childPixel.y) ||
                   !viewport.contains(parentPixel.x, parentPixel.y)) continue;
                canvas.line(parentPixel.x, parentPixel.y, childPixel.x, childPixel.y, 2.2f,
                            Treadle::Color{0.23f, 0.85f, 0.97f, 0.82f});
            }

            if(!leftDown) animatorRigDrag.control = -1;
            for(size_t i = 0; i < controls.size(); ++i){
                if(!controlVisible[i]) continue;
                const Loom::MotionDirectControl& control = controls[i];
                const int targetJoint = controlJointIndex(control);
                const float hitRadius = control.kind == Loom::MotionDirectControlKind::PathRoot ? 15.0f : 13.0f;
                const auto hit = ui.region("animator-pose-rig-control-" + std::to_string(i),
                    Treadle::Rect{controlPixels[i].x - hitRadius, controlPixels[i].y - hitRadius,
                                  hitRadius * 2.0f, hitRadius * 2.0f});
                controlHot[i] = hit.hot || hit.held;
                if(hit.pressed){
                    animatorRigDrag = AnimatorRigDragState{};
                    animatorRigDrag.control = int(i);
                    animatorRigDrag.rig = poseEdit.rig;
                    animatorRigDrag.frame = poseEdit.frame;
                    animatorRigDrag.startMouse = glm::vec2(hit.mouseX, hit.mouseY);
                    animatorRigDrag.startWorld = jointWorld(targetJoint);
                    const Warp::Id selectedJoint = jointIds[size_t(targetJoint)];
                    selected = selectedJoint;
                    focus = Focus::Entity;
                    motionPanel.selectedRigControl = int(i);
                    auto captureJoint = [&](int joint){
                        if(animatorRigDrag.jointCount >= animatorRigDrag.jointIndices.size() || !hasJoint(joint)) return;
                        const size_t slot = animatorRigDrag.jointCount++;
                        animatorRigDrag.jointIndices[slot] = joint;
                        animatorRigDrag.startLocal[slot] = stage.localAt(jointIds[size_t(joint)], poseEdit.frame);
                    };
                    if(control.kind == Loom::MotionDirectControlKind::PathRoot){
                        captureJoint(0);
                        animatorRigDrag.planeNormal = glm::vec3(0.0f, 1.0f, 0.0f);
                    }else if(control.kind == Loom::MotionDirectControlKind::RotateJoint){
                        captureJoint(control.joint);
                    }else{
                        captureJoint(control.upper);
                        captureJoint(control.middle);
                        if(control.kind == Loom::MotionDirectControlKind::LimbTarget) captureJoint(control.end);
                        animatorRigDrag.planeNormal = glm::normalize(animatorRigDrag.startWorld - poseCamera.eye);
                    }
                    if(glm::dot(animatorRigDrag.planeNormal, animatorRigDrag.planeNormal) < 1e-8f)
                        animatorRigDrag.planeNormal = glm::vec3(0.0f, 0.0f, 1.0f);
                    if(!intersectPosePlane(animatorRigDrag.startMouse, animatorRigDrag.startWorld,
                                           animatorRigDrag.planeNormal, animatorRigDrag.planeStartHit))
                        animatorRigDrag.planeStartHit = animatorRigDrag.startWorld;
                }
                if(!hit.held || animatorRigDrag.control != int(i) || animatorRigDrag.rig != poseEdit.rig ||
                   animatorRigDrag.frame != poseEdit.frame || !leftDown) continue;

                const glm::vec2 mouse(hit.mouseX, hit.mouseY);
                if(glm::length(mouse - animatorRigDrag.startMouse) < 0.25f) continue;
                for(size_t joint = 0; joint < animatorRigDrag.jointCount; ++joint)
                    setJointLocal(animatorRigDrag.jointIndices[joint], animatorRigDrag.startLocal[joint]);

                if(control.kind == Loom::MotionDirectControlKind::PathRoot){
                    glm::vec3 currentHit;
                    if(!intersectPosePlane(mouse, animatorRigDrag.planeStartHit,
                                           animatorRigDrag.planeNormal, currentHit)) continue;
                    const glm::vec3 targetWorld = animatorRigDrag.startWorld +
                        (currentHit - animatorRigDrag.planeStartHit);
                    Warp::Transform local = animatorRigDrag.startLocal[0];
                    const Warp::Entity* rootEntity = stage.get(jointIds[0]);
                    const glm::mat4 parentWorld = !rootEntity || rootEntity->parent == Warp::None
                        ? glm::mat4(1.0f) : stage.worldMatrix(rootEntity->parent, poseEdit.frame);
                    local.translation = glm::vec3(glm::inverse(parentWorld) * glm::vec4(targetWorld, 1.0f));
                    setJointLocal(0, local);
                }else if(control.kind == Loom::MotionDirectControlKind::RotateJoint){
                    const int joint = control.joint;
                    const Warp::Id id = jointIds[size_t(joint)];
                    const glm::mat4 inverseView = glm::inverse(poseCamera.view);
                    const glm::vec3 cameraRight = glm::normalize(glm::vec3(inverseView[0]));
                    const glm::vec3 cameraUp = glm::normalize(glm::vec3(inverseView[1]));
                    const glm::vec2 delta = mouse - animatorRigDrag.startMouse;
                    const glm::quat worldDelta = glm::normalize(
                        glm::angleAxis(glm::radians(-delta.x * 0.45f), cameraUp) *
                        glm::angleAxis(glm::radians(-delta.y * 0.45f), cameraRight));
                    const glm::quat startWorldRotation = rotationOf(
                        stage.worldMatrix(id, poseEdit.frame));
                    setJointWorldRotation(joint, glm::normalize(worldDelta * startWorldRotation),
                                          animatorRigDrag.startLocal[0]);
                }else{
                    glm::vec3 currentHit;
                    if(!intersectPosePlane(mouse, animatorRigDrag.planeStartHit,
                                           animatorRigDrag.planeNormal, currentHit)) continue;
                    const glm::vec3 targetWorld = animatorRigDrag.startWorld +
                        (currentHit - animatorRigDrag.planeStartHit);
                    const bool rotateEnd = control.kind == Loom::MotionDirectControlKind::LimbTarget &&
                        (glfwGetKey(window, GLFW_KEY_LEFT_ALT) == GLFW_PRESS ||
                         glfwGetKey(window, GLFW_KEY_RIGHT_ALT) == GLFW_PRESS);
                    if(rotateEnd){
                        const int end = control.end;
                        const Warp::Id endId = jointIds[size_t(end)];
                        const glm::mat4 inverseView = glm::inverse(poseCamera.view);
                        const glm::vec3 cameraRight = glm::normalize(glm::vec3(inverseView[0]));
                        const glm::vec3 cameraUp = glm::normalize(glm::vec3(inverseView[1]));
                        const glm::vec2 delta = mouse - animatorRigDrag.startMouse;
                        const glm::quat worldDelta = glm::normalize(
                            glm::angleAxis(glm::radians(-delta.x * 0.45f), cameraUp) *
                            glm::angleAxis(glm::radians(-delta.y * 0.45f), cameraRight));
                        setJointWorldRotation(end, glm::normalize(worldDelta *
                            rotationOf(stage.worldMatrix(endId, poseEdit.frame))), animatorRigDrag.startLocal[2]);
                    }else{
                        const glm::vec3 endTarget = control.kind == Loom::MotionDirectControlKind::LimbTarget
                            ? targetWorld : jointWorld(control.end);
                        const glm::vec3 poleTarget = control.kind == Loom::MotionDirectControlKind::Pole
                            ? targetWorld : jointWorld(control.middle);
                        solveLimb(control, endTarget, poleTarget);
                    }
                }
                projectPoseRig();
            }

            projectPoseRig();
            for(size_t i = 0; i < controls.size(); ++i){
                if(!controlVisible[i]) continue;
                const Loom::MotionDirectControl& control = controls[i];
                const bool active = animatorRigDrag.control == int(i) || motionPanel.selectedRigControl == int(i);
                const Treadle::Color tint = control.kind == Loom::MotionDirectControlKind::PathRoot
                    ? Treadle::Color{1.0f, 0.76f, 0.28f, 1.0f}
                    : control.kind == Loom::MotionDirectControlKind::RotateJoint
                        ? Treadle::Color{0.81f, 0.54f, 1.0f, 1.0f}
                        : control.kind == Loom::MotionDirectControlKind::Pole
                            ? Treadle::Color{1.0f, 0.57f, 0.24f, 1.0f}
                            : Treadle::Color{0.24f, 0.92f, 0.98f, 1.0f};
                const float radius = active || controlHot[i] ? 10.0f : 8.0f;
                const Treadle::Rect box{controlPixels[i].x - radius, controlPixels[i].y - radius,
                                        radius * 2.0f, radius * 2.0f};
                canvas.rect(box, active ? Treadle::Color{tint.r, tint.g, tint.b, 0.62f}
                                        : Treadle::Color{0.02f, 0.04f, 0.04f, 0.92f});
                canvas.outline(box, active ? 2.0f : 1.3f, tint);
                canvas.line(controlPixels[i].x - 3.0f, controlPixels[i].y,
                            controlPixels[i].x + 3.0f, controlPixels[i].y, 1.1f, tint);
                canvas.line(controlPixels[i].x, controlPixels[i].y - 3.0f,
                            controlPixels[i].x, controlPixels[i].y + 3.0f, 1.1f, tint);
                if(active || controlHot[i]){
                    const float labelScale = std::max(1.0f, theme.textScale * 0.62f);
                    const float labelWidth = std::clamp(float(std::char_traits<char>::length(control.label)) *
                        labelScale * 0.64f + 16.0f, 42.0f, 92.0f);
                    float labelX = controlPixels[i].x + 14.0f;
                    if(labelX + labelWidth > viewport.x + viewport.width - 6.0f)
                        labelX = controlPixels[i].x - labelWidth - 14.0f;
                    const float labelY = std::clamp(controlPixels[i].y - 10.0f,
                        viewport.y + 5.0f, viewport.y + viewport.height - 23.0f);
                    const Treadle::Rect labelBox{labelX, labelY, labelWidth, 19.0f};
                    canvas.rect(labelBox, Treadle::Color{0.015f, 0.025f, 0.025f, 0.94f});
                    canvas.outline(labelBox, 1.0f, tint);
                    canvas.text(labelX + 7.0f, labelY + 3.0f, control.label, tint, labelScale);
                }
            }
            const Treadle::Rect badge{viewport.x + 12.0f, viewport.y + 12.0f, 250.0f, 28.0f};
            canvas.rect(badge, Treadle::Color{0.015f, 0.025f, 0.025f, 0.90f});
            canvas.outline(badge, 1.0f, Treadle::Color{theme.accent.r, theme.accent.g, theme.accent.b, 0.56f});
            canvas.text(badge.x + 9.0f, badge.y + 7.0f, "POSE CONTROL RIG  ·  DRAG TARGETS",
                        theme.title, std::max(1.0f, theme.textScale * 0.65f));
        }
        //== COMPOSITOR ===========================================================================
        //Preko srednjeg dijela prozora: pregled, graf i svojstva. Pregled ostaje neoslikan - ondje
        //ide slika iz compositorTexture u prolazu crtanja, prije suicelja
        if(compositor.open){
            const float left = layout.rail.x + layout.rail.width + 4.0f;
            const float top = layout.toolbar.y + layout.toolbar.height + 4.0f;
            const float bottom = (timelineVisible ? layout.timeline.y : float(windowHeight)) - 4.0f;
            const Treadle::Rect middle{left, top, float(windowWidth) - left - 4.0f, std::max(0.0f, bottom - top)};
            compositorLayout = Loom::layoutCompositor(middle);
            Loom::compositorTick(compositor);
            Loom::compositorUpdate(compositor, std::max<int64_t>(0, int64_t(std::llround(frame)) - 1));
            Treadle::DrawList& canvas = ui.canvas();
            const Treadle::Rect& v = compositorLayout.viewer;
            const Treadle::Rect image = Loom::compositorImageRect(compositor, v);
            const Treadle::Color shade{0.025f, 0.03f, 0.028f, 1.0f};
            canvas.rect(Treadle::Rect{layout.rail.x + layout.rail.width, layout.toolbar.y + layout.toolbar.height,
                                      float(windowWidth) - layout.rail.x - layout.rail.width, top - layout.toolbar.y - layout.toolbar.height}, shade);
            if(image.width > 0.0f){
                canvas.rect(Treadle::Rect{v.x, v.y, v.width, image.y - v.y}, shade);
                canvas.rect(Treadle::Rect{v.x, image.y + image.height, v.width, v.y + v.height - image.y - image.height}, shade);
                canvas.rect(Treadle::Rect{v.x, image.y, image.x - v.x, image.height}, shade);
                canvas.rect(Treadle::Rect{image.x + image.width, image.y, v.x + v.width - image.x - image.width, image.height}, shade);
            }else{
                canvas.rect(v, shade);
                canvas.text(v.x + 16.0f, v.y + v.height * 0.5f, "Choose a video on the Read node.", theme.dim, theme.textScale);
            }
            canvas.outline(v, 1.0f, theme.panelEdge);
            canvas.text(v.x + 8.0f, v.y + 5.0f, "VIEWER   frame " + std::to_string(std::max<int64_t>(0, int64_t(std::llround(frame)) - 1)),
                        theme.dim, theme.textScale * 0.7f);
            canvas.rect(Treadle::Rect{v.x, v.y + v.height, v.width, compositorLayout.graph.y - v.y - v.height}, shade);
            canvas.rect(Treadle::Rect{compositorLayout.graph.x + compositorLayout.graph.width, top,
                                      compositorLayout.properties.x - compositorLayout.graph.x - compositorLayout.graph.width, middle.height}, shade);
            canvas.rect(Treadle::Rect{left - 4.0f, top, 4.0f, middle.height}, shade);
            canvas.rect(Treadle::Rect{middle.x + middle.width, top, 4.0f, middle.height}, shade);
            ui.region("comp-viewer", v);
            Loom::compositorGraph(ui, compositor, compositorLayout.graph, input);
            std::string cameraPlate;
            if(const Warp::Entity* through = stage.get(view.lookThrough); through && through->camera) cameraPlate = through->camera->plate;
            Loom::compositorProperties(ui, compositor, compositorLayout.properties, LOOM_ROOT_DIR, browser.videos, cameraPlate);
            if(compositor.timelineFrames > 0){
                stage.startFrame = 1.0;
                stage.endFrame = double(compositor.timelineFrames);
                if(compositor.timelineFps > 0.0) stage.framesPerSecond = compositor.timelineFps;
                frame = 1.0;
                compositor.timelineFrames = 0;
            }
        }
        ui.end();
        ui.style() = moodboardBaseTheme;

        //== POGLED: mis i tipke, tek kad suicelje nije uzelo mis ==================================
        const Treadle::Rect& viewportRect = layout.viewport;
        const bool overViewport = viewportRect.contains(float(cursorX), float(cursorY)) && !ui.wantsMouse() && !compositor.open;
        if(followAnimatorPreview && !(motionPanel.open && motionPanel.flowMode != 2 && motionPanel.rootPathEnabled)){
            const Warp::Id followRoot = Loom::motionCharacterForEntity(stage, selected);
            const Warp::Entity* followEntity = stage.get(followRoot);
            if(followEntity && followEntity->animator && !followEntity->animator->animations.empty()){
                const Loom::MotionRigRestPose rest = Loom::motionRigRestPose(stage, followRoot);
                std::array<Warp::Id, 52> uniRig;
                Warp::Id anchor = Warp::None;
                if(Loom::motionFindVerifiedUniRig52(rest, uniRig)) anchor = uniRig[0];
                else for(const Loom::MotionRigJointRest& joint : rest.joints){
                    const std::string key = Loom::motionJointKey(joint.name);
                    if(key == "hips" || key == "hip" || key == "pelvis"){ anchor = joint.id; break; }
                }
                if(anchor == Warp::None) anchor = followRoot;
                glm::vec3 target = glm::vec3(stage.worldMatrix(anchor, frame)[3]);
                target.y += std::max(0.2f, 0.25f * rest.bounds.height());
                view.orbit.target = target;
            }
        }
        const Loom::ViewCamera pickCamera = Loom::viewCameraFor(stage, frame, viewportRect, view);

        //Strelice odabranog: vide se i hvataju prije okretanja pogleda
        const Warp::Entity* chosen = stage.get(selected);
        Loom::Gizmo gizmo;
        if(chosen && chosen->visible && selected != view.lookThrough && focus == Focus::Entity &&
           !proceduraPanel.open){
            gizmo = Loom::gizmoFor(pickCamera, glm::vec3(stage.worldMatrix(selected, frame)[3]));
        }
        const glm::vec2 mouse{float(cursorX), float(cursorY)};
        gizmoAxisHot = gizmoAxisHeld >= 0 ? gizmoAxisHeld
                     : !overViewport ? -1
                     : tool == Tool::Move ? Loom::gizmoAxisAt(pickCamera, gizmo, mouse) : Loom::ringAxisAt(pickCamera, gizmo, mouse);

        bool proceduraPointGesture = false;
        bool proceduraPointClicked = false;
        int proceduraPointHot = -1;
        if(proceduraPanel.open && !surfaceTool.active){
            namespace Panel = Loom::WeaverProceduraUi;
            namespace Proc = Engine::WeaverProcedura;
            Proc::Node* curveNode = Panel::activeCurveNode(proceduraPanel);
            if(curveNode){
                Proc::Curve& curve = std::get<Proc::CurveNode>(curveNode->payload).curve;
                float closest = 17.0f;
                for(size_t i = 0; i < curve.points.size(); ++i){
                    glm::vec2 pixel;
                    if(!Loom::project(pickCamera, curve.points[i], pixel) ||
                       !viewportRect.contains(pixel.x, pixel.y)) continue;
                    const float distance = glm::length(pixel - mouse);
                    if(distance < closest){ closest = distance; proceduraPointHot = int(i); }
                }

                if(leftPressed && overViewport && proceduraPointHot >= 0){
                    proceduraPanel.selectedCurveNodeId = curveNode->id;
                    proceduraPanel.selectedControlPoint = proceduraPointHot;
                    proceduraCurveDragPoint = proceduraPointHot;
                    proceduraDragPlanePoint = curve.points[size_t(proceduraPointHot)];
                    const glm::vec3 toEye = pickCamera.eye - proceduraDragPlanePoint;
                    const float toEyeLength2 = glm::dot(toEye, toEye);
                    proceduraDragPlaneNormal = toEyeLength2 > 1e-8f
                        ? toEye / std::sqrt(toEyeLength2) : glm::vec3(0.0f, 0.0f, 1.0f);
                    proceduraPointClicked = true;
                }

                if(proceduraCurveDragPoint >= 0 && leftDown){
                    proceduraPointGesture = true;
                    if(size_t(proceduraCurveDragPoint) < curve.points.size()){
                        const Loom::Ray ray = Loom::rayThrough(pickCamera, mouse);
                        const float denominator = glm::dot(ray.direction, proceduraDragPlaneNormal);
                        if(std::fabs(denominator) > 1e-5f){
                            const float distance = glm::dot(proceduraDragPlanePoint - ray.origin,
                                                           proceduraDragPlaneNormal) / denominator;
                            if(distance > 0.0f){
                                const glm::vec3 position = ray.origin + distance * ray.direction;
                                glm::vec3& point = curve.points[size_t(proceduraCurveDragPoint)];
                                const glm::vec3 delta = position - point;
                                if(glm::dot(delta, delta) > 1e-10f){
                                    point = position;
                                    Panel::markGraphChanged(proceduraPanel);
                                }
                            }
                        }
                    }
                }
                if(leftReleased || !leftDown) proceduraCurveDragPoint = -1;
            }
        }else{
            proceduraCurveDragPoint = -1;
        }
        if(proceduraPointHot >= 0 || proceduraPointGesture || proceduraPointClicked)
            gizmoAxisHot = -1;

        const bool editingPath = motionPanel.open && motionPanel.flowMode != 2 && motionPanel.rootPathEnabled &&
                                 stage.contains(motionPanel.targetCharacter) &&
                                 !motionPanel.rootWaypoints.empty();
        glm::vec3 pathAnchor(0.0f);
        if(editingPath){
            if(!stage.contains(motionPathAnchor)) motionPathAnchor = motionRootAnchor(motionPanel.targetCharacter);
            pathAnchor = glm::vec3(stage.worldMatrix(motionPathAnchor, stage.startFrame)[3]);
            pathAnchor.y = 0.0f;
        }
        auto pathWorld = [&](const Loom::MotionRootWaypoint& key){
            return pathAnchor + glm::vec3(key.x, 0.025f, key.z);
        };
        int pathHot = -1;
        bool pathGesture = false;
        if(editingPath){
            float closest = 14.0f;
            for(size_t i = 0; i < motionPanel.rootWaypoints.size(); ++i){
                glm::vec2 pixel;
                if(!Loom::project(pickCamera, pathWorld(motionPanel.rootWaypoints[i]), pixel)) continue;
                if(!viewportRect.contains(pixel.x, pixel.y)) continue;
                const float distance = glm::length(pixel - mouse);
                if(distance < closest){ closest = distance; pathHot = int(i); }
            }
            auto moveWaypoint = [&](int index){
                if(index <= 0 || size_t(index) >= motionPanel.rootWaypoints.size()) return;
                const Loom::Ray ray = Loom::rayThrough(pickCamera, mouse);
                if(std::fabs(ray.direction.y) < 1e-5f) return;
                const float distance = -ray.origin.y / ray.direction.y;
                if(distance <= 0.0f) return;
                const glm::vec3 ground = ray.origin + distance * ray.direction;
                Loom::MotionRootWaypoint& key = motionPanel.rootWaypoints[size_t(index)];
                key.x = ground.x - pathAnchor.x;
                key.z = ground.z - pathAnchor.z;
                if(motionPanel.rootPathAutoEnd && index == int(motionPanel.rootWaypoints.size()) - 1)
                    motionPanel.rootPathAutoDistance = false;
            };
            if(leftPressed && overViewport && !surfaceTool.active){
                if(pathHot >= 0){
                    pathDragIndex = pathHot;
                    motionPanel.selectedRootWaypoint = pathHot;
                    motionPanel.rootTrackCursorFrame = float(motionPanel.rootWaypoints[size_t(pathHot)].frame);
                }else if(shift){
                    int segment = motionPanel.selectedRootWaypoint;
                    float nearestPath = 24.0f;
                    for(size_t i = 0; i + 1 < motionPanel.rootWaypoints.size(); ++i){
                        const int first = motionPanel.rootWaypoints[i].frame;
                        const int last = motionPanel.rootWaypoints[i + 1].frame;
                        const int steps = motionPanel.smoothRootPath ? std::max(1, (last - first + 2) / 3) : 1;
                        glm::vec2 previous;
                        bool hasPrevious = false;
                        for(int j = 0; j <= steps; ++j){
                            const float sampleFrame = float(first) + float(last - first) * float(j) / float(steps);
                            glm::vec2 pixel;
                            const bool visible = Loom::project(pickCamera,
                                pathWorld(Loom::motionRootPathAt(motionPanel.rootWaypoints,
                                    sampleFrame, motionPanel.smoothRootPath)), pixel);
                            if(visible && hasPrevious){
                                const glm::vec2 edge = pixel - previous;
                                const float length2 = glm::dot(edge, edge);
                                const float t = length2 > 0.001f
                                    ? std::clamp(glm::dot(mouse - previous, edge) / length2, 0.0f, 1.0f) : 0.0f;
                                const float distance = glm::length(mouse - (previous + t * edge));
                                if(distance < nearestPath){ nearestPath = distance; segment = int(i); }
                            }
                            previous = pixel;
                            hasPrevious = visible;
                        }
                    }
                    int atFrame = Loom::motionRootInsertionFrame(motionPanel.rootWaypoints, segment);
                    if(atFrame > 0 && atFrame < Loom::kimodoMotionLastFrame(motionPanel.actions)){
                        Loom::MotionRootWaypoint key = Loom::motionRootPathAt(
                            motionPanel.rootWaypoints, float(atFrame), motionPanel.smoothRootPath);
                        Loom::upsertMotionRootWaypoint(motionPanel.rootWaypoints, key,
                            Loom::kimodoMotionLastFrame(motionPanel.actions));
                        const auto found = std::lower_bound(motionPanel.rootWaypoints.begin(),
                            motionPanel.rootWaypoints.end(), atFrame,
                            [](const Loom::MotionRootWaypoint& item, int value){ return item.frame < value; });
                        pathDragIndex = int(found - motionPanel.rootWaypoints.begin());
                        motionPanel.selectedRootWaypoint = pathDragIndex;
                        motionPanel.rootTrackCursorFrame = float(atFrame);
                    }
                }
            }
            if(leftDown && pathDragIndex >= 0){
                pathGesture = true;
                moveWaypoint(pathDragIndex);
            }
        }
        if(!leftDown || !editingPath) pathDragIndex = -1;
        if(pathHot >= 0 || pathGesture) gizmoAxisHot = -1;

        //Alat plohe uzima lijevi mis u pogledu: pravokutnik umjesto odabira i okretanja
        Loom::surfaceToolMouse(surfaceTool, leftDown, leftWasDown, mouse, overViewport, stage, frame, pickCamera,
            [&](const std::function<void(const glm::vec3&)>& visit){
                //Sredista gaussiana prvog vidljivog splata, u svijet kroz njegovu grupu
                Warp::Id splatId = Warp::None;
                stage.walk([&](const Warp::Entity& e, int){ if(splatId == Warp::None && e.visible && e.splat) splatId = e.id; });
                if(splatId == Warp::None || !showSplat) return;
                const glm::mat4 world = stage.worldMatrix(splatId, frame);
                viewportSplat.forEachCentre(0.3f, [&](const glm::vec3& p){ visit(glm::vec3(world * glm::vec4(p, 1.0f))); });
            }, leftPressed, leftReleased);

        //Lijevi: strelica pomice, klik bira, vucenje okrece
        if(leftPressed && !surfaceTool.active){
            leftInViewport = overViewport && !pathGesture &&
                             !proceduraPointGesture && !proceduraPointClicked;
            dragging = false;
            pressX = leftClickX; pressY = leftClickY;
            gizmoAxisHeld = leftInViewport ? gizmoAxisHot : -1;
            transformGhostActive = gizmoAxisHeld >= 0 && chosen && tool == Tool::Move;
            transformGhostId = transformGhostActive ? selected : Warp::None;
            if(transformGhostActive) transformGhostStart = stage.worldMatrix(selected, frame);
        }
        if(!leftDown){
            gizmoAxisHeld = -1;
            transformGhostActive = false;
            transformGhostId = Warp::None;
        }
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
        if(leftReleased && leftInViewport && !dragging){
            selected = Loom::pickEntity(stage, frame, pickCamera, glm::vec2(float(cursorX), float(cursorY)));
            focus = Focus::Entity;
        }
        if(!leftDown) leftInViewport = false;

        //Srednji gumb kao u Blenderu: okretanje, sa shiftom pomicanje
        if(middlePressed) middleDragging = overViewport;
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

        //A step is taken when no gesture is in progress: a whole drag is one undo
        history.track(stage, leftDown || middleDown || rightDown || ui.wantsKeyboard());

        if(ui.wantsKeyboard()){
            //Tipke pripadaju polju za tekst: precaci se ne okidaju (W u opisu ne mijenja alat), ali se
            //stanje tipki ipak procita da se ne okinu kasnije, kad polje izgubi fokus
            for(int key : {GLFW_KEY_SPACE, GLFW_KEY_K, GLFW_KEY_V, GLFW_KEY_B, GLFW_KEY_W, GLFW_KEY_E, GLFW_KEY_S,
                           GLFW_KEY_RIGHT, GLFW_KEY_LEFT, GLFW_KEY_HOME, GLFW_KEY_END, GLFW_KEY_F, GLFW_KEY_0,
                           GLFW_KEY_KP_0, GLFW_KEY_DELETE, GLFW_KEY_ENTER, GLFW_KEY_KP_ENTER, GLFW_KEY_ESCAPE,
                           GLFW_KEY_Z, GLFW_KEY_Y, GLFW_KEY_I, GLFW_KEY_M}) keys.pressed(window, key);
        }else{
        if(moodboard.open && keys.pressed(window, GLFW_KEY_ESCAPE)) setMoodboardOpen(false);
        if(keys.pressed(window, GLFW_KEY_M)) setMoodboardOpen(!moodboard.open);
        if(keys.pressed(window, GLFW_KEY_SPACE)) togglePlayback();
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
        //Ctrl+Z undo, Ctrl+Shift+Z or Ctrl+Y redo (LoomUndo.h)
        if(keys.pressed(window, GLFW_KEY_I) && control) openImporter();
        const bool zKey = keys.pressed(window, GLFW_KEY_Z);
        const bool yKey = keys.pressed(window, GLFW_KEY_Y);
        if(control && (zKey || yKey)){
            const bool redo = yKey || shift;
            if(redo ? history.redo(stage) : history.undo(stage)){
                if(!stage.get(selected)) selected = Warp::None;
                if(view.lookThrough != Warp::None && !stage.get(view.lookThrough)) view.lookThrough = Warp::None;
                extentDirty = true;
                message = std::string(redo ? "Redo" : "Undo") + " (" + std::to_string(history.undoSteps()) + " undo, " +
                          std::to_string(history.redoSteps()) + " redo left)";
            }else{
                message = redo ? "Nothing to redo" : "Nothing to undo";
            }
        }
        else if(sKey) surfaceTool.active = !surfaceTool.active;
        if(keys.pressed(window, GLFW_KEY_RIGHT)) frame = std::min(stage.endFrame, std::floor(frame) + 1.0);
        if(keys.pressed(window, GLFW_KEY_LEFT)) frame = std::max(stage.startFrame, std::floor(frame) - 1.0);
        if(keys.pressed(window, GLFW_KEY_HOME)) frame = stage.startFrame;
        if(keys.pressed(window, GLFW_KEY_END)) frame = stage.endFrame;
        if(keys.pressed(window, GLFW_KEY_F)){
            if(!(proceduraPanel.open && frameProceduraCurve())){
                view.lookThrough = Warp::None;
                const Warp::Entity* entity = stage.get(selected);
                if(entity && (entity->mesh || entity->camera)){
                    view.orbit.target = glm::vec3(stage.worldMatrix(selected, frame)[3]);
                    view.orbit.distance = extent.radius * 0.8f;
                }else{
                    Loom::frameAll(stage.size() ? stage : live, frame, view.orbit);
                }
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
        if(keys.pressed(window, GLFW_KEY_DELETE)){
            namespace Panel = Loom::WeaverProceduraUi;
            namespace Proc = Engine::WeaverProcedura;
            if(proceduraPanel.open && proceduraPanel.selectedControlPoint >= 0){
                Proc::Node* curveNode = Panel::activeCurveNode(proceduraPanel);
                if(curveNode) Panel::eraseControlPoint(proceduraPanel, curveNode->id,
                    size_t(proceduraPanel.selectedControlPoint));
            }else if(selected != Warp::None && focus == Focus::Entity){
                removeSelected(selected);
            }
        }
        if(keys.pressed(window, GLFW_KEY_ESCAPE)){
            if(ui.menuOpen("View") || ui.menuOpen("Media") || ui.menuOpen("Entity") || ui.menuOpen("Procedura Point") ||
               ui.menuOpen("Project") || ui.menuOpen("New") || ui.menuOpen("Autosave") ||
               ui.menuOpen("Exit") || ui.menuOpen("Atlas") ||
               ui.menuOpen("Entity Add") || ui.menuOpen("View Add")) ui.closeMenu();
            else if(importer.open) importer.open = false;
            else if(autoRig.open) autoRig.open = false;
            else if(motionPanel.open) motionPanel.open = false;
            else if(proceduraPanel.open){ proceduraPanel.open = false; activeRailPane = RailPane::None; }
            else if(view.lookThrough != Warp::None) view.lookThrough = Warp::None;
        }

        }

        //== CRTANJE =================================================================================
        Treadle::DrawList scene, overlay;
        {
            const Loom::ViewCamera camera = Loom::viewCameraFor(stage, frame, viewportRect, view);
            Loom::paintStage(stage, frame, camera, view, extent, selected, scene);
            static Warp::Id bloomSelection = Warp::None;
            static std::chrono::steady_clock::time_point bloomStarted = now;
            if(selected != bloomSelection){
                bloomSelection = selected;
                bloomStarted = now;
            }
            const Warp::Entity* bloomEntity = stage.get(bloomSelection);
            const float bloomAge = std::chrono::duration<float>(now - bloomStarted).count();
            if(bloomEntity && bloomEntity->visible && focus == Focus::Entity && bloomAge < 0.82f){
                int hierarchyDepth = 0;
                for(const Warp::Entity* ancestor = bloomEntity;
                    ancestor && ancestor->parent != Warp::None;
                    ancestor = stage.get(ancestor->parent)) ++hierarchyDepth;
                const float t = std::clamp(bloomAge / 0.82f, 0.0f, 1.0f);
                const float envelope = (1.0f - t) * (1.0f - t);
                const float dimAlpha = std::min(0.15f, 0.045f + 0.012f * float(hierarchyDepth)) * envelope;
                float minX = std::numeric_limits<float>::max();
                float minY = std::numeric_limits<float>::max();
                float maxX = std::numeric_limits<float>::lowest();
                float maxY = std::numeric_limits<float>::lowest();
                auto includeFocusPoint = [&](const glm::vec3& point){
                    glm::vec2 pixel;
                    if(!Loom::project(camera, point, pixel) || !viewportRect.contains(pixel.x, pixel.y)) return;
                    minX = std::min(minX, pixel.x);
                    minY = std::min(minY, pixel.y);
                    maxX = std::max(maxX, pixel.x);
                    maxY = std::max(maxY, pixel.y);
                };
                std::vector<Warp::Id> focusStack{bloomSelection};
                std::set<Warp::Id> focusVisited;
                while(!focusStack.empty()){
                    const Warp::Id id = focusStack.back();
                    focusStack.pop_back();
                    if(!focusVisited.insert(id).second) continue;
                    const Warp::Entity* item = stage.get(id);
                    if(!item || !item->visible) continue;
                    const glm::mat4 world = stage.worldMatrix(id, frame);
                    includeFocusPoint(glm::vec3(world[3]));
                    focusStack.insert(focusStack.end(), item->children.begin(), item->children.end());
                    if(item->mesh){
                        if(item->mesh->shape == Warp::Shape::Cube){
                            for(const glm::vec3& corner : Loom::cubeCorners())
                                includeFocusPoint(glm::vec3(world * glm::vec4(corner, 1.0f)));
                        }else{
                            const glm::vec3 corners[4] = {
                                {-0.5f, 0.0f, -0.5f}, {0.5f, 0.0f, -0.5f},
                                {0.5f, 0.0f, 0.5f}, {-0.5f, 0.0f, 0.5f}
                            };
                            for(const glm::vec3& corner : corners)
                                includeFocusPoint(glm::vec3(world * glm::vec4(corner, 1.0f)));
                        }
                    }else if(item->model){
                        const glm::vec3 corners[8] = {
                            {-0.5f,-0.5f,-0.5f},{0.5f,-0.5f,-0.5f},
                            {-0.5f,0.5f,-0.5f},{0.5f,0.5f,-0.5f},
                            {-0.5f,-0.5f,0.5f},{0.5f,-0.5f,0.5f},
                            {-0.5f,0.5f,0.5f},{0.5f,0.5f,0.5f}
                        };
                        for(const glm::vec3& corner : corners)
                            includeFocusPoint(glm::vec3(world * glm::vec4(corner, 1.0f)));
                    }
                }
                if(minX == std::numeric_limits<float>::max()){
                    glm::vec2 centre;
                    float depth = 0.0f;
                    if(Loom::project(camera, glm::vec3(stage.worldMatrix(bloomSelection, frame)[3]), centre, &depth) &&
                       viewportRect.contains(centre.x, centre.y)){
                        const glm::mat4 world = stage.worldMatrix(bloomSelection, frame);
                        const float worldScale = std::max({glm::length(glm::vec3(world[0])),
                                                           glm::length(glm::vec3(world[1])),
                                                           glm::length(glm::vec3(world[2]))});
                        const float radius = std::clamp(camera.focal * worldScale * 0.55f /
                                                        std::max(1e-3f, depth), 20.0f, 110.0f);
                        minX = centre.x - radius; maxX = centre.x + radius;
                        minY = centre.y - radius; maxY = centre.y + radius;
                    }
                }
                if(minX != std::numeric_limits<float>::max()){
                    const float padding = std::clamp(std::max(maxX - minX, maxY - minY) * 0.10f + 16.0f,
                                                     22.0f, 54.0f);
                    Treadle::Rect focusBox{
                        std::clamp(minX - padding, viewportRect.x + 2.0f, viewportRect.x + viewportRect.width - 30.0f),
                        std::clamp(minY - padding, viewportRect.y + 2.0f, viewportRect.y + viewportRect.height - 30.0f),
                        0.0f, 0.0f
                    };
                    const float right = std::clamp(maxX + padding, focusBox.x + 28.0f,
                                                   viewportRect.x + viewportRect.width - 2.0f);
                    const float bottom = std::clamp(maxY + padding, focusBox.y + 28.0f,
                                                    viewportRect.y + viewportRect.height - 2.0f);
                    focusBox.width = right - focusBox.x;
                    focusBox.height = bottom - focusBox.y;
                    const Treadle::Color shade{0.008f, 0.014f, 0.010f, dimAlpha};
                    const float topHeight = std::max(0.0f, focusBox.y - viewportRect.y);
                    const float bottomY = focusBox.y + focusBox.height;
                    const float bottomHeight = std::max(0.0f, viewportRect.y + viewportRect.height - bottomY);
                    const float leftWidth = std::max(0.0f, focusBox.x - viewportRect.x);
                    const float rightX = focusBox.x + focusBox.width;
                    const float rightWidth = std::max(0.0f, viewportRect.x + viewportRect.width - rightX);
                    if(topHeight > 0.0f) overlay.rect(viewportRect.x, viewportRect.y, viewportRect.width, topHeight, shade);
                    if(bottomHeight > 0.0f) overlay.rect(viewportRect.x, bottomY, viewportRect.width, bottomHeight, shade);
                    if(leftWidth > 0.0f) overlay.rect(viewportRect.x, focusBox.y, leftWidth, focusBox.height, shade);
                    if(rightWidth > 0.0f) overlay.rect(rightX, focusBox.y, rightWidth, focusBox.height, shade);
                    const Treadle::Color accent = sceneAccent(*bloomEntity);
                    const float glowAlpha = (0.18f + 0.035f * float(std::min(hierarchyDepth, 5))) * envelope;
                    overlay.outline(focusBox, 1.5f, {accent.r, accent.g, accent.b, glowAlpha});
                    const Treadle::Rect outerFocus{
                        std::max(viewportRect.x + 1.0f, focusBox.x - 5.0f),
                        std::max(viewportRect.y + 1.0f, focusBox.y - 5.0f),
                        std::min(viewportRect.x + viewportRect.width - 1.0f, focusBox.x + focusBox.width + 5.0f) -
                            std::max(viewportRect.x + 1.0f, focusBox.x - 5.0f),
                        std::min(viewportRect.y + viewportRect.height - 1.0f, focusBox.y + focusBox.height + 5.0f) -
                            std::max(viewportRect.y + 1.0f, focusBox.y - 5.0f)
                    };
                    overlay.outline(outerFocus, 1.0f, {1.0f, 0.80f, 0.32f, 0.22f * envelope});
                }
            }
            if(hoveredAtlasId != Warp::None && hoveredAtlasId != selected){
                const Warp::Entity* hovered = stage.get(hoveredAtlasId);
                if(hovered && hovered->visible){
                    glm::vec2 pixel;
                    const glm::vec3 world(stage.worldMatrix(hoveredAtlasId, frame)[3]);
                    if(Loom::project(camera, world, pixel) && viewportRect.contains(pixel.x, pixel.y)){
                        const Treadle::Color base = sceneAccent(*hovered);
                        const Treadle::Color guide{base.r, base.g, base.b, 0.26f};
                        const Treadle::Color marker{base.r, base.g, base.b, 0.86f};
                        const float atlasY = std::clamp(hoveredAtlasRect.y + hoveredAtlasRect.height * 0.5f,
                                                        viewportRect.y + 8.0f,
                                                        viewportRect.y + viewportRect.height - 8.0f);
                        overlay.line(viewportRect.x + 5.0f, atlasY, pixel.x, pixel.y, 1.0f, guide);
                        overlay.outline(Treadle::Rect{pixel.x - 8.0f, pixel.y - 8.0f, 16.0f, 16.0f},
                                        1.4f, marker);
                        overlay.line(pixel.x - 11.0f, pixel.y, pixel.x - 5.0f, pixel.y, 1.3f, marker);
                        overlay.line(pixel.x + 5.0f, pixel.y, pixel.x + 11.0f, pixel.y, 1.3f, marker);
                        overlay.line(pixel.x, pixel.y - 11.0f, pixel.x, pixel.y - 5.0f, 1.3f, marker);
                        overlay.line(pixel.x, pixel.y + 5.0f, pixel.x, pixel.y + 11.0f, 1.3f, marker);
                    }
                }
            }
            if(editingPath){
                const Treadle::Color pathColour{0.34f, 0.88f, 0.64f, 0.95f};
                for(size_t i = 1; i < motionPanel.rootWaypoints.size(); ++i){
                    const int first = motionPanel.rootWaypoints[i - 1].frame;
                    const int last = motionPanel.rootWaypoints[i].frame;
                    const int steps = motionPanel.smoothRootPath ? std::max(1, (last - first + 2) / 3) : 1;
                    for(int j = 1; j <= steps; ++j){
                        const float f0 = float(first) + float(last - first) * float(j - 1) / float(steps);
                        const float f1 = float(first) + float(last - first) * float(j) / float(steps);
                        Loom::segment(overlay, camera,
                            pathWorld(Loom::motionRootPathAt(motionPanel.rootWaypoints, f0, motionPanel.smoothRootPath)),
                            pathWorld(Loom::motionRootPathAt(motionPanel.rootWaypoints, f1, motionPanel.smoothRootPath)),
                            3.0f, pathColour);
                    }
                }
                const Loom::MotionRootWaypoint cursorPoint = Loom::motionRootPathAt(
                    motionPanel.rootWaypoints, motionPanel.rootTrackCursorFrame, motionPanel.smoothRootPath);
                glm::vec2 cursorPixel;
                if(Loom::project(camera, pathWorld(cursorPoint), cursorPixel) &&
                   viewportRect.contains(cursorPixel.x, cursorPixel.y))
                    overlay.outline(Treadle::Rect{cursorPixel.x - 4.0f, cursorPixel.y - 4.0f, 8.0f, 8.0f},
                                    2.0f, Treadle::Color{1.0f, 1.0f, 1.0f, 0.9f});
                for(size_t i = 0; i < motionPanel.rootWaypoints.size(); ++i){
                    glm::vec2 pixel;
                    if(!Loom::project(camera, pathWorld(motionPanel.rootWaypoints[i]), pixel) ||
                       !viewportRect.contains(pixel.x, pixel.y)) continue;
                    const bool active = int(i) == motionPanel.selectedRootWaypoint;
                    const Treadle::Color colour = active ? Treadle::Color{1.0f, 0.82f, 0.25f, 1.0f}
                                                          : pathColour;
                    const float radius = active || int(i) == pathHot ? 7.0f : 5.0f;
                    overlay.rect(pixel.x - radius, pixel.y - radius, radius * 2.0f, radius * 2.0f, colour);
                    overlay.text(pixel.x + 10.0f, pixel.y - 8.0f,
                                 std::to_string(i + 1), colour, theme.textScale);
                }
                const std::string pathHint = "PATH: Shift-click to add; drag to shape";
                overlay.text(viewportRect.x + viewportRect.width - Treadle::textWidth(pathHint, theme.textScale) - 18.0f,
                             viewportRect.y + 24.0f, pathHint, pathColour, theme.textScale);
            }
            if(surfaceTool.active) Loom::paintSurfaceTool(surfaceTool, camera, overlay);
            //Kocka koja reze splat: zicani obrub, da se vidi i kad je prozirna
            if(splatShownId != Warp::None && Loom::isCube(stage.get(selected))){
                Loom::paintBoxWire(overlay, camera, stage.worldMatrix(selected, frame), {1.0f, 0.82f, 0.30f, 0.95f});
            }
            if(transformGhostActive && transformGhostId == selected && gizmoAxisHeld >= 0 && tool == Tool::Move){
                const Warp::Entity* ghostEntity = stage.get(transformGhostId);
                if(ghostEntity){
                    const glm::vec3 oldPosition(transformGhostStart[3]);
                    const glm::mat4 currentWorld = stage.worldMatrix(transformGhostId, frame);
                    const glm::vec3 currentPosition(currentWorld[3]);
                    const glm::vec3 delta = currentPosition - oldPosition;
                    Loom::paintBoxWire(overlay, camera, transformGhostStart, {0.28f, 0.96f, 0.62f, 0.55f});
                    const Treadle::Color axisColours[3] = {
                        {1.00f, 0.24f, 0.34f, 0.96f},
                        {0.34f, 1.00f, 0.42f, 0.96f},
                        {0.30f, 0.58f, 1.00f, 0.96f}
                    };
                    const glm::vec3 axisDeltas[3] = {
                        {delta.x, 0.0f, 0.0f},
                        {0.0f, delta.y, 0.0f},
                        {0.0f, 0.0f, delta.z}
                    };
                    for(int axis = 0; axis < 3; ++axis)
                        Loom::segment(overlay, camera, oldPosition, oldPosition + axisDeltas[axis], 2.2f, axisColours[axis]);
                    glm::vec2 currentPixel;
                    if(Loom::project(camera, currentPosition, currentPixel) &&
                       viewportRect.contains(currentPixel.x, currentPixel.y)){
                        const float hudW = std::min(180.0f, viewportRect.width - 24.0f);
                        const float hudH = 42.0f;
                        const float hudX = std::clamp(currentPixel.x + 16.0f, viewportRect.x + 8.0f,
                                                      viewportRect.x + viewportRect.width - hudW - 8.0f);
                        const float hudY = std::clamp(currentPixel.y + 14.0f, viewportRect.y + 8.0f,
                                                      viewportRect.y + viewportRect.height - hudH - 8.0f);
                        overlay.rect(Treadle::Rect{hudX, hudY, hudW, hudH}, {0.035f, 0.055f, 0.043f, 0.94f});
                        overlay.outline(Treadle::Rect{hudX, hudY, hudW, hudH}, 1.0f, {0.74f, 0.60f, 0.27f, 0.96f});
                        char deltaText[128];
                        std::snprintf(deltaText, sizeof(deltaText), "X %+.3f   Y %+.3f   Z %+.3f",
                                      delta.x, delta.y, delta.z);
                        overlay.text(hudX + 8.0f, hudY + 6.0f, "TRANSFORM DELTA", {0.96f, 0.83f, 0.44f, 1.0f},
                                     std::max(1.7f, theme.textScale * 0.68f));
                        overlay.text(hudX + 8.0f, hudY + 23.0f, deltaText, {0.84f, 0.91f, 0.81f, 1.0f},
                                     std::max(1.7f, theme.textScale * 0.64f));
                    }
                }
            }
            const Warp::Entity* chosenNow = stage.get(selected);
            if(!editingPath && !proceduraPanel.open && chosenNow && chosenNow->visible &&
               selected != view.lookThrough && focus == Focus::Entity){
                const Loom::Gizmo shown = Loom::gizmoFor(camera, glm::vec3(stage.worldMatrix(selected, frame)[3]));
                if(tool == Tool::Move) Loom::paintGizmo(overlay, camera, shown, gizmoAxisHot);
                else Loom::paintRings(overlay, camera, shown, gizmoAxisHot);
            }
            if(proceduraPanel.open){
                namespace Panel = Loom::WeaverProceduraUi;
                namespace Proc = Engine::WeaverProcedura;
                Proc::Node* curveNode = Panel::activeCurveNode(proceduraPanel);
                if(curveNode){
                    const Proc::Curve& curve = std::get<Proc::CurveNode>(curveNode->payload).curve;
                    const Treadle::Color line{0.20f, 0.88f, 0.67f, 0.95f};
                    for(size_t i = 1; i < curve.points.size(); ++i)
                        Loom::segment(overlay, camera, curve.points[i - 1], curve.points[i], 2.6f, line);
                    if(curve.closed && curve.points.size() > 2)
                        Loom::segment(overlay, camera, curve.points.back(), curve.points.front(), 2.6f, line);
                    for(size_t i = 0; i < curve.points.size(); ++i){
                        glm::vec2 pixel;
                        if(!Loom::project(camera, curve.points[i], pixel) ||
                           !viewportRect.contains(pixel.x, pixel.y)) continue;
                        const bool isSelected = int(i) == proceduraPanel.selectedControlPoint;
                        const bool isHot = int(i) == proceduraPointHot;
                        const Treadle::Color fill = isSelected ? Treadle::Color{1.0f, 0.72f, 0.26f, 1.0f}
                            : isHot ? Treadle::Color{0.58f, 1.0f, 0.68f, 1.0f}
                                    : Treadle::Color{0.22f, 0.94f, 0.72f, 0.96f};
                        const float size = isSelected || isHot ? 15.0f : 11.0f;
                        const Treadle::Rect marker{pixel.x - size * 0.5f, pixel.y - size * 0.5f, size, size};
                        overlay.rect(marker, Treadle::Color{0.035f, 0.075f, 0.06f, 0.98f});
                        overlay.outline(marker, isSelected ? 2.2f : 1.5f, fill);
                        overlay.line(pixel.x - 3.0f, pixel.y, pixel.x + 3.0f, pixel.y, 1.2f, fill);
                        overlay.line(pixel.x, pixel.y - 3.0f, pixel.x, pixel.y + 3.0f, 1.2f, fill);
                        overlay.text(pixel.x + size * 0.5f + 4.0f, pixel.y - 5.0f,
                                     "P" + std::to_string(i + 1), fill, 1.8f);
                    }
                }
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
            splatShownId = showSplat ? splatId : Warp::None;
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
            if(loadingNow && !splatWasLoading) message = "Loading splat...";
            if(!loadingNow && splatWasLoading){
                glm::vec3 centre;
                float radius = 0.0f;
                if(splatFrameWhenLoaded != Warp::None && splatFrameWhenLoaded == splatShownId && viewportSplat.bounds(centre, radius)){
                    const glm::mat4 world = stage.worldMatrix(splatShownId, frame);
                    view.lookThrough = Warp::None;
                    view.orbit.target = glm::vec3(world * glm::vec4(centre, 1.0f));
                    view.orbit.distance = 2.2f * radius * glm::length(glm::vec3(world[0]));
                }
                splatFrameWhenLoaded = Warp::None;
                const std::string problem = viewportSplat.error();
                message = problem.empty() ? "Splat in viewport: " + std::to_string(viewportSplat.count()) + " gaussians"
                                          : "Could not read splat: " + problem;
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
            const Engine::WeaverProcedura::MeshData* proceduralPreview = proceduraPanel.open && proceduraPanel.previewReady
                ? &proceduraPanel.previewMesh : nullptr;
            meshesActive = viewportMeshes.prepare(stage, frame, camera, pixelScaleX, nearPlane,
                std::max(100.0f, extent.radius * 500.0f), proceduralPreview, proceduraPanel.previewRevision);
            for(const std::string& problem : viewportMeshes.takeErrors()) message = "Could not read model: " + problem;
        }

        //COMPOSITOR: tekstura pregleda se (ponovno) stvara kad se velicina promijeni, kao ploca
        if(compositor.open && compositor.outputChanged && compositor.outputWidth > 0 &&
           (!compositorTexture || compositorTexture->getExtent().width != compositor.outputWidth ||
            compositorTexture->getExtent().height != compositor.outputHeight)){
            loom.waitIdle();
            compositorMaterial.reset();
            StreamingTextureConfig textureConfig;
            textureConfig.format = vk::Format::eR8G8B8A8Srgb;
            compositorTexture = std::make_unique<StreamingTexture>(loom.device, loom.command,
                vk::Extent2D{compositor.outputWidth, compositor.outputHeight}, textureConfig);
            compositorMaterial = std::make_unique<Material>(loom.device, loom.command, loom.getDescriptorPool(),
                                                            platePipeline, compositorTexture->getSampled());
            compositorReady = false;
        }
        if(!loom.renderer.beginFrame()) continue;
        if(compositor.open && compositor.outputChanged && compositorTexture &&
           compositor.output.size() == size_t(compositor.outputWidth) * compositor.outputHeight * 4){
            compositorTexture->update(compositor.output.data(), compositor.output.size());
            compositorMaterial->setSampledImage(compositorTexture->getSampled());
            compositorReady = true;
            compositor.outputChanged = false;
        }
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
        //COMPOSITOR: slika pregleda u svoj pravokutnik, preko svega sto je pogled nacrtao ispod
        if(compositor.open && compositorReady && compositorMaterial){
            const Treadle::Rect image = Loom::compositorImageRect(compositor, compositorLayout.viewer);
            if(image.width > 1.0f && image.height > 1.0f){
                const float sx = float(framebufferWidth) / float(std::max(1, windowWidth));
                const float sy = float(framebufferHeight) / float(std::max(1, windowHeight));
                compositorMaterial->setBaseColor(glm::vec4(1.0f));
                const vk::raii::CommandBuffer& commands = loom.renderer.borrowCommands();
                commands.setViewport(0, vk::Viewport{image.x * sx, image.y * sy, image.width * sx, image.height * sy, 0.0f, 1.0f});
                commands.setScissor(0, vk::Rect2D{{int32_t(image.x * sx), int32_t(image.y * sy)},
                                                  {uint32_t(image.width * sx), uint32_t(image.height * sy)}});
                loom.renderer.drawFullscreen(*compositorMaterial);
                commands.setViewport(0, vk::Viewport{0.0f, 0.0f, float(framebufferWidth), float(framebufferHeight), 0.0f, 1.0f});
                commands.setScissor(0, vk::Rect2D{{0, 0}, {uint32_t(framebufferWidth), uint32_t(framebufferHeight)}});
            }
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
            std::printf("Screenshot saved: %s (%ux%u)\n", shotPath.c_str(), image.width, image.height);
            break;
        }
    }

    if(motionLive.active){
        stopMotionBricksLive();
        const auto stopDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(8);
        while(std::chrono::steady_clock::now() < stopDeadline){
            Loom::refreshMotionBricksLiveStatus(motionLive);
            if(motionLive.complete || motionLive.failed) break;
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }
    if(worker.joinable()) worker.join();
    plateStream.close();
    loom.waitIdle();
    return 0;
}
