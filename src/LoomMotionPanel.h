#pragma once
//=============================================================================================
// POKRET IZ TEKSTA: panel u pogledu za Kimodo (WeaverMotion).
//
// Prva verzija je bila jedno polje u stupcu svojstava - redak koji se reze na sirinu stupca,
// kursor samo na kraju, bez odabira i lijepljenja. Kimodo moze vise nego sto je to pokazivalo:
//
//   NIZ RADNJI   vise opisa zaredom ("hoda naprijed", pa "sjedne"), svaki sa svojim trajanjem;
//                Kimodo ih spoji s prijelazima. Na naredbenom retku su opisi odvojeni tockom, a
//                trajanja razmakom - pa se tocka unutar opisa pretvori u zarez
//   SJEME        isti opis s istim sjemenom daje isti pokret; bez njega svaki put drukciji
//   KVALITETA    koraci difuzije: 100 je zadano, manje je brze i grublje
//   STOPALA      Kimodovo cistenje klizanja stopala; iskljuceno ostavlja sirovi izlaz modela
//
// POVIJEST: uz svaki BVH se zapise .txt s opisima i trajanjima, pa se prosli pokreti vide po
// onome sto su bili, ne po broju u imenu - i opis se da vratiti u panel i promijeniti
//=============================================================================================
#include "LoomJob.h"
#include "LoomWeaverMotion.h"

#include <Treadle/Ui.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <string>
#include <vector>

namespace Loom{

struct MotionAction{
    std::string prompt;
    float duration = 4.0f;          //sekunde
};
struct MotionRootWaypoint{
    int frame = 0;                 //Kimodo clip frame, zero-based
    float x = 0.0f, z = 0.0f;      //meters on Kimodo's Y-up ground plane
    float heading = 0.0f;          //radians; only serialized when heading constraints are enabled
};

inline constexpr float kimodoMotionFps = 30.0f;

struct MotionRequest{
    std::vector<MotionAction> actions;
    std::string model = "Kimodo-SOMA-RP-v1.1";
    int seed = -1;                  //-1: bez sjemena, svaki put drukcije
    int diffusionSteps = 100;
    int numSamples = 1;
    int transitionFrames = 5;
    std::string cfgType;            //empty: model default; nocfg, regular, separated
    float textGuidance = 2.0f;
    float constraintGuidance = 2.0f;
    float firstHeadingAngle = 0.0f;
    float rootMargin = 0.04f;
    std::filesystem::path constraints;
    std::vector<MotionRootWaypoint> rootWaypoints;
    bool constrainRootHeading = false;
    Warp::Id targetCharacter = Warp::None;
    bool footCleanup = true;
    bool saveExample = false;
};

struct MotionCharacter{
    Warp::Id id = Warp::None;
    std::string name;
    std::string path;
};

inline bool motionSubtreeHasRig(const Warp::Stage& stage, Warp::Id root){
    bool hasModel = false, hasJoint = false;
    std::vector<Warp::Id> pending{root};
    size_t visited = 0;
    while(!pending.empty() && visited++ < stage.size()){
        const Warp::Entity* entity = stage.get(pending.back());
        pending.pop_back();
        if(!entity) continue;
        hasModel = hasModel || entity->model.has_value();
        hasJoint = hasJoint || entity->joint.has_value();
        if(hasModel && hasJoint) return true;
        pending.insert(pending.end(), entity->children.begin(), entity->children.end());
    }
    return false;
}

inline std::vector<MotionCharacter> motionCharactersIn(const Warp::Stage& stage){
    std::vector<MotionCharacter> candidates;
    std::vector<Warp::Id> pending(stage.roots().rbegin(), stage.roots().rend());
    while(!pending.empty()){
        const Warp::Id id = pending.back();
        pending.pop_back();
        const Warp::Entity* entity = stage.get(id);
        if(!entity) continue;
        if(motionSubtreeHasRig(stage, id)){
            bool childCandidate = false;
            for(Warp::Id child : entity->children){
                if(motionSubtreeHasRig(stage, child)){ childCandidate = true; break; }
            }
            if(!childCandidate){
                candidates.push_back({id, entity->name, stage.path(id)});
                continue;
            }
        }
        for(auto child = entity->children.rbegin(); child != entity->children.rend(); ++child) pending.push_back(*child);
    }
    return candidates;
}

inline const std::vector<std::string>& kimodoModels(){
    static const std::vector<std::string> models{
        "Kimodo-SOMA-RP-v1.1", "Kimodo-SOMA-RP-v1", "Kimodo-SOMA-SEED-v1.1",
        "Kimodo-SOMA-SEED-v1", "Kimodo-G1-RP-v1", "Kimodo-G1-SEED-v1", "Kimodo-SMPLX-RP-v1"
    };
    return models;
}

struct MotionHistoryEntry{
    std::filesystem::path bvh;
    std::string summary;
    std::vector<MotionAction> actions;
};

//Opis za naredbeni redak: bez tocke (ona dijeli radnje) i bez znakova koji nisu tekst
inline std::string cleanPrompt(const std::string& prompt){
    std::string out;
    for(char c : prompt){
        if(c == '.' ) c = ',';
        if(c == '\n' || c == '\r' || c == '\t') c = ' ';
        if(c == ' ' && (out.empty() || out.back() == ' ')) continue;
        out += c;
    }
    while(!out.empty() && (out.back() == ' ' || out.back() == ',')) out.pop_back();
    return out;
}

//Radnje koje imaju opis; prazne se preskacu
inline std::vector<MotionAction> filledActions(const std::vector<MotionAction>& actions){
    std::vector<MotionAction> out;
    for(const MotionAction& a : actions){
        const std::string clean = cleanPrompt(a.prompt);
        if(!clean.empty()) out.push_back({clean, std::clamp(a.duration, 1.0f, 10.0f)});
    }
    return out;
}
// The official generator turns CLI durations into int(seconds * model.fps); Kimodo runs at 30 Hz.
// Keep the same two-decimal duration rounding as our CLI.
inline int kimodoMotionFrameCount(const std::vector<MotionAction>& actions){
    int frames = 0;
    for(const MotionAction& action : filledActions(actions)){
        char seconds[16];
        std::snprintf(seconds, sizeof(seconds), "%.2f", double(action.duration));
        const int centiseconds = int(std::lround(std::strtod(seconds, nullptr) * 100.0));
        frames += centiseconds * int(kimodoMotionFps) / 100;
    }
    return frames;
}

inline int kimodoMotionLastFrame(const std::vector<MotionAction>& actions){
    return std::max(0, kimodoMotionFrameCount(actions) - 1);
}

inline std::string motionRootPathProblem(const std::vector<MotionRootWaypoint>& waypoints, int lastFrame){
    if(waypoints.empty()) return "Add at least one root-path waypoint.";
    int previous = -1;
    for(const MotionRootWaypoint& waypoint : waypoints){
        if(waypoint.frame < 0 || waypoint.frame > lastFrame)
            return "Root-path frame " + std::to_string(waypoint.frame) + " is outside this clip (0-" + std::to_string(lastFrame) + ").";
        if(waypoint.frame <= previous) return "Root-path keyframes must have unique, increasing frame numbers.";
        if(!std::isfinite(waypoint.x) || !std::isfinite(waypoint.z) || !std::isfinite(waypoint.heading))
            return "Root-path values must be finite numbers.";
        previous = waypoint.frame;
    }
    if(waypoints.front().frame == 0 && (std::fabs(waypoints.front().x) > 1e-5f || std::fabs(waypoints.front().z) > 1e-5f))
        return "Kimodo frame 0 is the canonical origin and must stay at X=0, Z=0.";
    return {};
}

inline void upsertMotionRootWaypoint(std::vector<MotionRootWaypoint>& waypoints, MotionRootWaypoint waypoint, int lastFrame){
    waypoint.frame = std::clamp(waypoint.frame, 0, std::max(0, lastFrame));
    auto at = std::lower_bound(waypoints.begin(), waypoints.end(), waypoint.frame,
                               [](const MotionRootWaypoint& key, int frame){ return key.frame < frame; });
    if(at != waypoints.end() && at->frame == waypoint.frame) *at = waypoint;
    else waypoints.insert(at, waypoint);
    if(!waypoints.empty() && waypoints.front().frame == 0){
        waypoints.front().x = 0.0f;
        waypoints.front().z = 0.0f;
    }
}

inline int moveMotionRootWaypoint(std::vector<MotionRootWaypoint>& waypoints, size_t selected, int frame, int lastFrame){
    if(selected >= waypoints.size()) return -1;
    if(selected == 0 && waypoints.front().frame == 0) return 0;
    const int low = selected > 0 ? waypoints[selected - 1].frame + 1 : 0;
    const int high = selected + 1 < waypoints.size() ? waypoints[selected + 1].frame - 1 : lastFrame;
    if(low > high) return int(selected);
    waypoints[selected].frame = std::clamp(frame, low, high);
    return int(selected);
}

inline bool writeMotionRootConstraints(const std::filesystem::path& path,
                                       const std::vector<MotionRootWaypoint>& waypoints,
                                       bool constrainHeading, int lastFrame,
                                       std::string& problem){
    problem = motionRootPathProblem(waypoints, lastFrame);
    if(!problem.empty()) return false;
    std::ofstream file(path);
    if(!file){ problem = "Could not create Kimodo constraints file: " + path.string(); return false; }
    file << "[\n  {\n    \"type\": \"root2d\",\n    \"frame_indices\": [";
    for(size_t i = 0; i < waypoints.size(); ++i){ if(i) file << ", "; file << waypoints[i].frame; }
    file << "],\n    \"smooth_root_2d\": [";
    file << std::fixed << std::setprecision(6);
    for(size_t i = 0; i < waypoints.size(); ++i){
        if(i) file << ", ";
        file << "[" << waypoints[i].x << ", " << waypoints[i].z << "]";
    }
    file << "]";
    if(constrainHeading){
        file << ",\n    \"global_root_heading\": [";
        for(size_t i = 0; i < waypoints.size(); ++i){
            if(i) file << ", ";
            const double cosine = std::cos(waypoints[i].heading);
            const double sine = std::sin(waypoints[i].heading);
            file << "[" << (std::fabs(cosine) < 0.0000005 ? 0.0 : cosine) << ", "
                 << (std::fabs(sine) < 0.0000005 ? 0.0 : sine) << "]";

        }
        file << "]";
    }
    file << "\n  }\n]\n";
    file.close();
    if(!file){ problem = "Could not finish writing Kimodo constraints file: " + path.string(); return false; }
    return true;
}

inline MotionRootWaypoint motionRootWaypointAt(const std::vector<MotionRootWaypoint>& waypoints, int frame){
    MotionRootWaypoint value;
    value.frame = frame;
    if(waypoints.empty()) return value;
    const auto right = std::lower_bound(waypoints.begin(), waypoints.end(), frame,
                                        [](const MotionRootWaypoint& key, int f){ return key.frame < f; });
    if(right != waypoints.end() && right->frame == frame) return *right;
    if(right == waypoints.begin()){
        value.x = right->x;
        value.z = right->z;
        value.heading = right->heading;
        return value;
    }
    if(right == waypoints.end()){
        value.x = waypoints.back().x;
        value.z = waypoints.back().z;
        value.heading = waypoints.back().heading;
        return value;
    }
    const MotionRootWaypoint& a = *(right - 1);
    const MotionRootWaypoint& b = *right;
    const float amount = float(frame - a.frame) / float(std::max(1, b.frame - a.frame));
    value.x = a.x + (b.x - a.x) * amount;
    value.z = a.z + (b.z - a.z) * amount;
    value.heading = a.heading + (b.heading - a.heading) * amount;
    return value;
}

//Naredba za sluzbeni Kimodo CLI (izravno ili kroz Loom adapter za API-only postavke)
inline std::string buildMotionCommand(const std::filesystem::path& executable, const MotionRequest& request,
                                      const std::filesystem::path& outputStem,
                                      const std::filesystem::path& adapterScript = {}){
    const std::vector<MotionAction> actions = filledActions(request.actions);
    std::string prompts, durations;
    for(size_t i = 0; i < actions.size(); ++i){
        if(i){ prompts += ". "; durations += " "; }
        prompts += actions[i].prompt;
        char text[16];
        std::snprintf(text, sizeof(text), "%.2f", double(actions[i].duration));
        durations += text;
    }
    std::string command = "TEXT_ENCODER_MODE=local TEXT_ENCODER_DEVICE=cpu " + shellQuoteArgument(executable.string()) + " ";
    if(!adapterScript.empty()) command += shellQuoteArgument(adapterScript.string()) + " ";
    command += shellQuoteArgument(prompts) + " --model " + shellQuoteArgument(request.model) +
                          " --duration " + shellQuoteArgument(durations) +
                          " --num_samples " + std::to_string(std::clamp(request.numSamples, 1, 8)) +
                          " --diffusion_steps " + std::to_string(std::clamp(request.diffusionSteps, 10, 500)) +
                          " --num_transition_frames " + std::to_string(std::clamp(request.transitionFrames, 0, 30)) +
                          " --output " + shellQuoteArgument(outputStem.string());
    if(!adapterScript.empty()) command += " --first_heading_angle " + std::to_string(request.firstHeadingAngle) +
                                          " --root_margin " + std::to_string(request.rootMargin);
    const bool soma = request.model.find("SOMA") != std::string::npos;
    if(soma) command += " --bvh --bvh_standard_tpose";
    if(!request.constraints.empty()) command += " --constraints " + shellQuoteArgument(request.constraints.string());
    if(request.saveExample) command += " --save_example_dir";
    if(!request.cfgType.empty()){
        command += " --cfg_type " + shellQuoteArgument(request.cfgType);
        if(request.cfgType == "regular") command += " --cfg_weight " + std::to_string(request.textGuidance);
        else if(request.cfgType == "separated") command += " --cfg_weight " + std::to_string(request.textGuidance) + " " + std::to_string(request.constraintGuidance);
    }
    if(request.seed >= 0) command += " --seed " + std::to_string(request.seed);
    if(!request.footCleanup) command += " --no-postprocess";
    return command;
}

//Uz BVH: opis i trajanje po retku, odvojeni tabom
inline void writeMotionSidecar(const std::filesystem::path& outputStem, const MotionRequest& request){
    std::ofstream file(outputStem.string() + ".txt");
    file << "# model\t" << request.model << '\n';
    file << "# target\t" << request.targetCharacter << '\n';
    file << "# first_heading_angle\t" << request.firstHeadingAngle << '\n';
    file << "# root_margin\t" << request.rootMargin << '\n';
    for(const MotionAction& a : filledActions(request.actions)) file << a.prompt << '\t' << a.duration << '\n';
}

inline std::vector<MotionHistoryEntry> motionHistory(const std::filesystem::path& directory, size_t limit = 12){
    std::vector<std::pair<std::filesystem::file_time_type, std::filesystem::path>> found;
    std::error_code error;
    for(const auto& entry : std::filesystem::recursive_directory_iterator(directory, error)){
        if(error) break;
        if(entry.path().extension() == ".bvh") found.push_back({entry.last_write_time(error), entry.path()});
    }
    std::sort(found.begin(), found.end(), [](const auto& a, const auto& b){ return a.first > b.first; });
    std::vector<MotionHistoryEntry> out;
    for(const auto& [time, path] : found){
        if(out.size() >= limit) break;
        MotionHistoryEntry item;
        item.bvh = path;
        std::filesystem::path sidecarPath = path.parent_path() / (path.stem().string() + ".txt");
        if(!std::filesystem::exists(sidecarPath) && path.parent_path() != directory)
            sidecarPath = path.parent_path().parent_path() / (path.parent_path().filename().string() + ".txt");
        std::ifstream sidecar(sidecarPath);
        std::string line;
        while(std::getline(sidecar, line)){
            if(!line.empty() && line.front() == '#') continue;
            const size_t tab = line.find('\t');
            MotionAction a;
            a.prompt = line.substr(0, tab);
            if(tab != std::string::npos) a.duration = std::strtof(line.c_str() + tab + 1, nullptr);
            if(!a.prompt.empty()) item.actions.push_back(a);
        }
        if(item.actions.empty()) item.summary = path.filename().string();
        else{
            item.summary = item.actions.front().prompt;
            if(item.actions.size() > 1) item.summary += "  (+" + std::to_string(item.actions.size() - 1) + ")";
        if(path.parent_path() != directory) item.summary += " [" + path.filename().string() + "]";
        }
        out.push_back(item);
    }
    return out;
}

//Gotovi opisi: engleski, jer ga Kimodov enkoder teksta razumije
struct MotionPreset{ const char* label; const char* prompt; };
inline const MotionPreset motionPresets[] = {
    {"Walk", "a person walks forward"},
    {"Jog", "a person jogs forward"},
    {"Jump", "a person jumps up in place"},
    {"Sit", "a person sits down on a chair"},
    {"Wave", "a person waves with the right hand"},
    {"Dance", "a person dances happily"},
    {"Turn", "a person turns around to the left"},
    {"Fall", "a person stumbles and falls down"},
};

struct MotionPanelState{
    bool open = false;
    std::vector<MotionAction> actions{MotionAction{}};
    int activeAction = 0;                   //u koju radnju ide gotovi opis
    bool fixedSeed = false;
    float seed = 42.0f;
    int modelIndex = 0;
    float quality = 100.0f;                 //koraci difuzije
    float samples = 1.0f;
    float transitionFrames = 5.0f;
    int cfgIndex = 0;
    float textGuidance = 2.0f, constraintGuidance = 2.0f;
    float firstHeadingAngle = 0.0f;
    float rootMargin = 0.04f;
    std::string constraintsPath;
    bool rootPathEnabled = false;
    bool constrainRootHeading = false;
    std::vector<MotionRootWaypoint> rootWaypoints{MotionRootWaypoint{}};
    int selectedRootWaypoint = 0;
    float rootTrackCursorFrame = 0.0f;
    Warp::Id targetCharacter = Warp::None;
    bool footCleanup = true;
    bool saveExample = false;
    std::vector<MotionHistoryEntry> history;
    std::filesystem::path historyFrom;
    std::chrono::steady_clock::time_point historyRead{};

    MotionRequest request() const{
        MotionRequest r;
        r.actions = actions;
        r.model = kimodoModels()[size_t(std::clamp(modelIndex, 0, int(kimodoModels().size()) - 1))];
        r.seed = fixedSeed ? std::max(0, int(std::lround(seed))) : -1;
        r.diffusionSteps = int(std::lround(quality));
        r.saveExample = saveExample;
        r.numSamples = int(std::lround(samples));
        r.transitionFrames = int(std::lround(transitionFrames));
        static const char* cfgModes[] = {"", "nocfg", "regular", "separated"};
        r.cfgType = cfgModes[std::clamp(cfgIndex, 0, 3)];
        r.textGuidance = textGuidance;
        r.constraintGuidance = constraintGuidance;
        r.firstHeadingAngle = firstHeadingAngle;
        r.rootMargin = rootMargin;
        r.constraints = constraintsPath;
        if(rootPathEnabled){ r.rootWaypoints = rootWaypoints; r.constrainRootHeading = constrainRootHeading; }
        r.targetCharacter = targetCharacter;
        r.footCleanup = footCleanup;
        return r;
    }
};

//Sto je panel trazio u ovom kadru; aplikacija to izvrsi (posao, uvoz)
struct MotionPanelAction{
    bool generate = false;
    std::filesystem::path importPath;
    bool close = false;
};

struct MotionPanelStatus{
    bool runnerReady = false;
    bool running = false;                   //Kimodo posao tece
    bool otherJob = false;                  //tece neki drugi posao (solve, trening)
    double elapsed = 0.0;
    std::string lastLine;                   //zadnji redak ispisa posla
    std::filesystem::path historyDirectory; //gdje su generirani BVH-ovi
    std::string characterNote;              //sto je s rigged likom (WeaverMascott)
    std::vector<MotionCharacter> characters;
};

inline MotionPanelAction drawMotionPanel(Treadle::Ui& ui, MotionPanelState& state, const Treadle::Rect& area,
                                         const MotionPanelStatus& status, float& scroll){
    MotionPanelAction action;
    const Treadle::Theme& theme = ui.style();

    //Povijest se cita kad se mapa promijeni ili svake dvije sekunde (novi BVH iz posla)
    const auto now = std::chrono::steady_clock::now();
    if(state.historyFrom != status.historyDirectory || std::chrono::duration<double>(now - state.historyRead).count() > 2.0){
        state.history = motionHistory(status.historyDirectory);
        state.historyFrom = status.historyDirectory;
        state.historyRead = now;
    }

    ui.dock("TEXT-TO-MOTION  /  NVIDIA KIMODO", area, &scroll);
    if(!status.runnerReady){
        ui.label("Kimodo is not installed:");
        ui.label("./tools/weavermotion/setup.sh");
        ui.separator();
    }

    //-- radnje ---------------------------------------------------------------------------------
    Treadle::Ui::TextFieldConfig field;
    field.lines = 3;
    field.maxLength = 600;
    field.placeholder = "Describe a motion, e.g. a person walks forward and waves";
    float total = 0.0f;
    int removeAt = -1, moveUp = -1;
    for(size_t i = 0; i < state.actions.size(); ++i){
        MotionAction& a = state.actions[i];
        const bool active = int(i) == state.activeAction;
        ui.label(state.actions.size() > 1 ? "ACTION " + std::to_string(i + 1) + (active ? "  <" : "") : "MOTION PROMPT");
        const Treadle::Ui::TextFieldResult result = ui.textField("action" + std::to_string(i), &a.prompt, field);
        if(result.focused) state.activeAction = int(i);
        if(result.submitted) action.generate = true;
        ui.slider(std::string("Duration ") + std::to_string(i + 1), &a.duration, 1.0f, 10.0f, " s");
        total += a.duration;
        if(state.actions.size() > 1){
            //Prva radnja ne moze gore, pa ima samo "ukloni"
            if(i == 0){
                if(ui.buttonRow({"Remove"}) == 0) removeAt = 0;
            }else{
                const int clicked = ui.buttonRow({"Move Up", "Remove"});
                if(clicked == 0) moveUp = int(i);
                if(clicked == 1) removeAt = int(i);
            }
        }
    }
    if(moveUp > 0){
        std::swap(state.actions[size_t(moveUp)], state.actions[size_t(moveUp - 1)]);
        state.activeAction = moveUp - 1;
    }
    if(removeAt >= 0){
        state.actions.erase(state.actions.begin() + removeAt);
        state.activeAction = std::clamp(state.activeAction, 0, int(state.actions.size()) - 1);
    }
    if(state.actions.size() < 6 && ui.button("+ Add Action (then...)")){
        state.actions.push_back(MotionAction{"", 3.0f});
        state.activeAction = int(state.actions.size()) - 1;
        ui.focusTextField("action" + std::to_string(state.actions.size() - 1));
    }

    //-- gotovi opisi: u aktivnu radnju --------------------------------------------------------
    ui.label("QUICK PROMPTS");
    for(int row = 0; row < 2; ++row){
        std::vector<std::string> labels;
        for(int k = 0; k < 4; ++k) labels.push_back(motionPresets[row * 4 + k].label);
        const int clicked = ui.buttonRow(labels);
        if(clicked >= 0){
            MotionAction& target = state.actions[size_t(std::clamp(state.activeAction, 0, int(state.actions.size()) - 1))];
            const std::string text = motionPresets[row * 4 + clicked].prompt;
            //U praznu radnju cijeli opis; u zapocetu se nastavi ("..., then jumps up in place")
            const std::string subject = "a person ";
            const std::string tail = text.rfind(subject, 0) == 0 ? text.substr(subject.size()) : text;
            target.prompt = target.prompt.empty() ? text : target.prompt + ", then " + tail;
            ui.focusTextField("action" + std::to_string(state.activeAction));
        }
    }


    ui.separator();
    ui.label("KIMODO MODEL");
    ui.choice("Skeleton / dataset", kimodoModels(), &state.modelIndex);
    ui.slider("Samples to compare", &state.samples, 1.0f, 8.0f);
    const std::string& model = kimodoModels()[size_t(std::clamp(state.modelIndex, 0, int(kimodoModels().size()) - 1))];
    if(model.find("G1") != std::string::npos) ui.label("G1 exports native NPZ/CSV; Loom timeline preview currently imports SOMA BVH only.");
    else if(model.find("SMPLX") != std::string::npos) ui.label("SMPL-X exports native NPZ/AMASS; Loom timeline preview currently imports SOMA BVH only.");
    ui.slider("Transition frames", &state.transitionFrames, 0.0f, 30.0f);
    ui.choice("Classifier-free guidance", {"Model default", "No CFG", "Regular", "Text + constraints"}, &state.cfgIndex);
    if(state.cfgIndex == 2) ui.slider("Text guidance", &state.textGuidance, 0.0f, 10.0f);
    if(state.cfgIndex == 3){
        ui.slider("Text guidance", &state.textGuidance, 0.0f, 10.0f);
        ui.slider("Constraint guidance", &state.constraintGuidance, 0.0f, 10.0f);
    }
    Treadle::Ui::TextFieldConfig pathField;
    pathField.lines = 1;
    pathField.maxLength = 1024;
    pathField.placeholder = "Optional Kimodo constraints.json path";
    ui.textField("kimodo-constraints", &state.constraintsPath, pathField);
    ui.checkbox("Save reusable Kimodo example bundle", &state.saveExample);
    ui.checkbox("Constrain Kimodo root path (XZ metres)", &state.rootPathEnabled);
    if(state.rootPathEnabled){
        const int lastFrame = kimodoMotionLastFrame(state.actions);
        state.rootTrackCursorFrame = std::clamp(state.rootTrackCursorFrame, 0.0f, float(lastFrame));
        float cursor = state.rootTrackCursorFrame;
        if(ui.dragFloat("Kimodo waypoint frame", &cursor, 0.35f))
            state.rootTrackCursorFrame = std::clamp(std::round(cursor), 0.0f, float(lastFrame));
        ui.label("Click the Kimodo track below to add/select a key. Frame 0 is the fixed origin.");
        if(ui.checkbox("Constrain heading at every root waypoint", &state.constrainRootHeading)){
            if(state.constrainRootHeading && !state.rootWaypoints.empty() && state.rootWaypoints.front().frame == 0)
                state.rootWaypoints.front().heading = state.firstHeadingAngle;
        }
        const int keyAction = ui.buttonRow({"Add / update at cursor", "Delete selected"});
        if(keyAction == 0){
            MotionRootWaypoint key = motionRootWaypointAt(state.rootWaypoints, int(std::lround(state.rootTrackCursorFrame)));
            upsertMotionRootWaypoint(state.rootWaypoints, key, lastFrame);
            const auto found = std::lower_bound(state.rootWaypoints.begin(), state.rootWaypoints.end(), key.frame,
                                                [](const MotionRootWaypoint& item, int f){ return item.frame < f; });
            state.selectedRootWaypoint = int(found - state.rootWaypoints.begin());
        }else if(keyAction == 1 && state.selectedRootWaypoint >= 0 &&
                 size_t(state.selectedRootWaypoint) < state.rootWaypoints.size() &&
                 !(state.rootWaypoints[size_t(state.selectedRootWaypoint)].frame == 0)){
            state.rootWaypoints.erase(state.rootWaypoints.begin() + state.selectedRootWaypoint);
            state.selectedRootWaypoint = std::clamp(state.selectedRootWaypoint, 0, int(state.rootWaypoints.size()) - 1);
        }
        for(size_t i = 0; i < state.rootWaypoints.size(); ++i){
            const MotionRootWaypoint& key = state.rootWaypoints[i];
            char label[112];
            std::snprintf(label, sizeof(label), "Frame %d    X %.2f m    Z %.2f m%s", key.frame,
                          double(key.x), double(key.z), key.frame == 0 ? "    (origin)" : "");
            if(ui.selectable(label, int(i) == state.selectedRootWaypoint)){
                state.selectedRootWaypoint = int(i);
                state.rootTrackCursorFrame = float(key.frame);
            }
        }
        if(state.selectedRootWaypoint >= 0 && size_t(state.selectedRootWaypoint) < state.rootWaypoints.size()){
            MotionRootWaypoint& key = state.rootWaypoints[size_t(state.selectedRootWaypoint)];
            if(key.frame == 0){
                ui.value("Frame 0 root", "fixed at canonical XZ origin");
            }else{
                float position[3]{key.x, 0.0f, key.z};
                if(ui.dragVector("Waypoint X / Z (m)", position, 0.002f)){
                    key.x = position[0];
                    key.z = position[2];
                }
            }
            if(state.constrainRootHeading){
                if(ui.dragFloat("Waypoint heading (rad)", &key.heading, 0.002f))
                    key.heading = std::clamp(key.heading, -3.14159f, 3.14159f);
            }
        }
        const std::string pathProblem = motionRootPathProblem(state.rootWaypoints, lastFrame);
        if(!pathProblem.empty()) ui.label("Root path needs attention: " + pathProblem);
        if(!state.constraintsPath.empty())
            ui.label("Use either the authored root path or the external JSON, not both.");
        ui.value("Path frame rate", "30 fps (Kimodo)");
    }

    ui.separator();
    ui.label("RIGGED CHARACTERS IN THIS SCENE");
    if(status.characters.empty()){
        ui.label("No imported model with both mesh and joint hierarchy.");
        state.targetCharacter = Warp::None;
    }else{
        bool selectedExists = false;
        for(const MotionCharacter& character : status.characters){
            const bool selected = state.targetCharacter == character.id;
            selectedExists = selectedExists || selected;
            const std::string label = character.name + "  " + character.path;
            if(ui.selectable(Treadle::fitText(label, area.width - 36.0f, theme.textScale), selected))
                state.targetCharacter = character.id;
        }
        if(!selectedExists) state.targetCharacter = status.characters.front().id;
        if(!status.characterNote.empty()) ui.label(status.characterNote);
        ui.label("Target anchors the generated skeleton preview; Loom does not yet skin/retarget the source mesh.");
    }
    //-- postavke -------------------------------------------------------------------------------
    ui.separator();
    char text[96];
    std::snprintf(text, sizeof(text), "%.1f s, %zu %s", double(total), filledActions(state.actions).size(),
                  filledActions(state.actions).size() == 1 ? "action" : "actions");
    ui.value("Total", text);
    ui.slider("Quality (steps)", &state.quality, 10.0f, 500.0f);
    ui.checkbox("Same seed = same motion", &state.fixedSeed);
    if(state.fixedSeed) ui.dragFloat("Seed", &state.seed, 0.2f);
    ui.checkbox("Clean foot sliding", &state.footCleanup);
    if(ui.slider("Initial heading (rad)", &state.firstHeadingAngle, -3.14159f, 3.14159f)){
        if(state.rootPathEnabled && state.constrainRootHeading && !state.rootWaypoints.empty() &&
           state.rootWaypoints.front().frame == 0) state.rootWaypoints.front().heading = state.firstHeadingAngle;
    }
    ui.slider("Root correction margin (m)", &state.rootMargin, 0.0f, 0.25f);

    //-- generiranje ----------------------------------------------------------------------------
    ui.separator();
    const std::string rootProblem = state.rootPathEnabled
        ? motionRootPathProblem(state.rootWaypoints, kimodoMotionLastFrame(state.actions)) : std::string{};
    const bool constraintConflict = state.rootPathEnabled && !state.constraintsPath.empty();
    const bool constraintsReady = rootProblem.empty() && !constraintConflict;
    const bool ready = status.runnerReady && !status.running && !status.otherJob &&
                       !filledActions(state.actions).empty() && constraintsReady;
    if(status.running){
        ui.value("Kimodo running", Loom::humanTime(status.elapsed));
        ui.label(Treadle::fitText(status.lastLine.empty() ? "Starting..." : status.lastLine, area.width - 30.0f, theme.textScale));
        ui.label("(First launch downloads the model, ~17 GB)");
    }else if(status.otherJob){
        ui.label("Waiting: another solve or training job is running.");
    }
    if(ui.button(ready ? "GENERATE  (Enter)" : status.running ? "Generating..." : "GENERATE  (enter a prompt)")){
        if(ready) action.generate = true;
    }
    if(!ready) action.generate = false;

    //-- povijest ---------------------------------------------------------------------------------
    ui.separator();
    ui.label("RECENT MOTIONS");
    ui.label("Click to import; right-click to restore prompt");
    if(state.history.empty()) ui.label("(Nothing here yet: " + status.historyDirectory.filename().string() + ")");
    for(size_t i = 0; i < state.history.size(); ++i){
        const MotionHistoryEntry& item = state.history[i];
        if(ui.selectable(Treadle::fitText(item.summary, area.width - 40.0f, theme.textScale), false)) action.importPath = item.bvh;
        if(ui.rightClicked() && !item.actions.empty()){
            state.actions = item.actions;
            state.activeAction = 0;
            ui.focusTextField("action0");
        }
    }
    ui.separator();
    if(!state.rootPathEnabled) ui.label("Load a Kimodo constraints.json above or enable root-path editing here.");
    else if(constraintConflict) ui.label("Clear the external JSON path to generate from this authored root path.");
    ui.label("SOMA exports importable BVH; the selected source mesh stays static until skinning/retargeting is implemented.");
    if(ui.button("Close (Esc)")) action.close = true;
    return action;
}

}
