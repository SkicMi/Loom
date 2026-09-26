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
#include "LoomMotionDirect.h"
#include "LoomMotionQuality.h"
#include "LoomPlateFloor.h"
#include "LoomWeaverMotion.h"

#include <Treadle/Ui.h>

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iterator>
#include <sstream>
#include <string>
#include <vector>

namespace Loom{

struct MotionAction{
    std::string prompt;
    float duration = 4.0f;          //sekunde
};
enum class MotionPreset : int{ Idle, Walk, Run, Jump, Crawl, Crouch };
struct MotionPresetInfo{
    const char* label;
    const char* prompt;
    const char* motionBricksStyle;
    float seconds;
    float defaultPathSpeed;
    bool realtime;
    bool footContactIK;
    bool kimodoPostprocess;
};
inline constexpr MotionPresetInfo motionPresetInfo[] = {
    {"Idle", "A person stands relaxed, shifts weight subtly, and breathes naturally", "idle", 4.0f, 0.0f, true, true, true},
    {"Walk", "A person walks forward at a relaxed pace with a natural arm swing", "walk", 4.0f, 0.8f, true, true, true},
    {"Run", "A person runs forward with an athletic stride and coordinated arm swing", "walk", 3.0f, 2.2f, false, true, true},
    {"Jump", "A person jumps straight up, lands softly with bent knees, and regains balance", "walk", 2.5f, 0.0f, false, true, true},
    {"Crawl", "A person crawls forward on hands and knees close to the floor with steady contacts", "hand_crawling", 4.0f, 0.55f, true, false, true},
    {"Crouch", "A person walks forward in a low crouch while keeping the torso balanced", "walk_stealth", 4.0f, 0.7f, true, true, true},
};

inline const MotionPresetInfo& motionPresetSettings(int index){
    return motionPresetInfo[std::clamp(index, 0, int(sizeof(motionPresetInfo) / sizeof(motionPresetInfo[0])) - 1)];
}

// Map the movement word in a prompt to the closest editable recipe preset.
// Searching the original text keeps compound prompts such as "walk, then jump" predictable.
inline int motionPresetMentionedInPrompt(const std::string& prompt){
    std::string lower = prompt;
    std::transform(lower.begin(), lower.end(), lower.begin(),
                   [](unsigned char c){ return char(std::tolower(c)); });
    if(lower.find("low crouch") != std::string::npos || lower.find("crouch walk") != std::string::npos ||
       lower.find("crouch-walk") != std::string::npos) return int(MotionPreset::Crouch);
    struct Keyword{ const char* word; MotionPreset preset; };
    static constexpr Keyword keywords[] = {
        {"crawling", MotionPreset::Crawl}, {"crawls", MotionPreset::Crawl}, {"crawl", MotionPreset::Crawl},
        {"crouching", MotionPreset::Crouch}, {"crouches", MotionPreset::Crouch}, {"crouch", MotionPreset::Crouch},
        {"running", MotionPreset::Run}, {"runs", MotionPreset::Run}, {"run", MotionPreset::Run},
        {"walking", MotionPreset::Walk}, {"walks", MotionPreset::Walk}, {"walk", MotionPreset::Walk},
        {"jumping", MotionPreset::Jump}, {"jumps", MotionPreset::Jump}, {"jump", MotionPreset::Jump},
        {"idle", MotionPreset::Idle}, {"standing", MotionPreset::Idle}, {"stands", MotionPreset::Idle},
        {"stand", MotionPreset::Idle}
    };
    size_t earliest = std::string::npos;
    int preset = -1;
    for(const Keyword& keyword : keywords){
        size_t at = lower.find(keyword.word);
        while(at != std::string::npos){
            const bool leftBoundary = at == 0 || !std::isalpha(static_cast<unsigned char>(lower[at - 1]));
            const size_t end = at + std::char_traits<char>::length(keyword.word);
            const bool rightBoundary = end == lower.size() || !std::isalpha(static_cast<unsigned char>(lower[end]));
            const size_t contextStart = lower.rfind(' ', at);
            const std::string context = lower.substr(contextStart == std::string::npos ? 0 : contextStart + 1,
                                                     at - (contextStart == std::string::npos ? 0 : contextStart + 1));
            const bool negated = context == "not" || context == "no" || context == "without" ||
                                 context == "never" || context == "dont" || context == "don't";
            if(leftBoundary && rightBoundary && !negated){
                if(at < earliest){
                    earliest = at;
                    preset = int(keyword.preset);
                }
                break;
            }
            at = lower.find(keyword.word, at + 1);
        }
    }
    return preset;
}

inline bool motionPromptRequestsNoIk(const std::string& prompt){
    std::string lower = prompt;
    std::transform(lower.begin(), lower.end(), lower.begin(),
                   [](unsigned char c){ return char(std::tolower(c)); });
    static constexpr const char* phrases[] = {
        "no ik", "no foot ik", "without ik", "without foot ik", "disable ik", "disable foot ik",
        "no inverse kinematics", "without inverse kinematics", "don't use ik", "dont use ik",
        "do not use ik", "don't use foot ik"
    };
    for(const char* phrase : phrases) if(lower.find(phrase) != std::string::npos) return true;
    return false;
}

struct MotionRootWaypoint{
    int frame = 0;                 //Kimodo clip frame, zero-based
    float x = 0.0f, z = 0.0f;      //meters on Kimodo's Y-up ground plane
    float heading = 0.0f;          //radians; only serialized when heading constraints are enabled
};

inline constexpr float kimodoMotionFps = 30.0f;

struct MotionRequest{
    std::vector<MotionAction> actions;
    int movementPreset = int(MotionPreset::Walk);
    bool automaticContactSettings = true;
    std::string model = "Kimodo-SOMA-RP-v1.1";
    int seed = -1;                  //-1: bez sjemena, svaki put drukcije
    int diffusionSteps = 200;
    int numSamples = 4;
    int transitionFrames = 5;
    std::string cfgType;            //empty: model default; nocfg, regular, separated
    float textGuidance = 2.0f;
    float constraintGuidance = 2.0f;
    float firstHeadingAngle = 0.0f;
    float rootMargin = 0.04f;
    std::filesystem::path constraints;
    std::vector<MotionRootWaypoint> rootWaypoints;
    std::vector<MotionPoseConstraint> poseConstraints;
    bool directedFlow = false;
    bool constrainRootHeading = false;
    bool smoothRootPath = true;
    bool allowFastPath = false;
    Warp::Id targetCharacter = Warp::None;
    bool kimodoPostprocess = true;
    bool footContactIK = true;
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

inline Warp::Id motionCharacterForEntity(const Warp::Stage& stage, Warp::Id selected){
    if(!stage.contains(selected)) return Warp::None;
    //A selected mesh, joint or rig group resolves to the nearest character root above it.
    for(Warp::Id current = selected; current != Warp::None;){
        const Warp::Entity* entity = stage.get(current);
        if(!entity) break;
        if(motionSubtreeHasRig(stage, current)){
            //Resolve selected scene wrappers to the smallest containing mesh + skeleton subtree.
            while(true){
                Warp::Id childRig = Warp::None;
                bool ambiguous = false;
                for(Warp::Id child : entity->children){
                    if(!motionSubtreeHasRig(stage, child)) continue;
                    if(childRig != Warp::None){ ambiguous = true; break; }
                    childRig = child;
                }
                if(ambiguous) return Warp::None;
                if(childRig == Warp::None) break;
                current = childRig;
                entity = stage.get(current);
                if(!entity) break;
            }
            return current;
        }
        current = entity->parent;
    }
    //A selected scene group can contain one character without being part of its rig hierarchy.
    Warp::Id only = Warp::None;
    for(const MotionCharacter& character : motionCharactersIn(stage)){
        Warp::Id current = character.id;
        while(current != Warp::None && current != selected){
            const Warp::Entity* entity = stage.get(current);
            current = entity ? entity->parent : Warp::None;
        }
        if(current == selected){
            if(only != Warp::None) return Warp::None;
            only = character.id;
        }
    }
    return only;
}

inline std::string motionClipName(const std::filesystem::path& bvh){
    std::ifstream sidecar(bvh.parent_path() / (bvh.stem().string() + ".txt"));
    std::string line;
    while(std::getline(sidecar, line)){
        if(line.empty() || line.front() == '#') continue;
        const size_t tab = line.find('\t');
        std::string name = line.substr(0, tab);
        while(!name.empty() && std::isspace(static_cast<unsigned char>(name.back()))) name.pop_back();
        const size_t first = name.find_first_not_of(" \t");
        if(first == std::string::npos) continue;
        name.erase(0, first);
        if(name.size() > 48) name.resize(48);
        return name;
    }
    std::string fallback = bvh.stem().string();
    if(fallback.size() > 48) fallback.resize(48);
    return fallback.empty() ? "Imported animation" : fallback;
}

inline const std::vector<std::string>& motionBricksStyles(){
    static const std::vector<std::string> styles{
        "Idle", "Walk", "Slow walk", "Hand crawl", "Elbow crawl", "Crouch walk",
        "Walk boxing", "Stealth walk", "Happy dance", "Zombie walk", "Injured walk",
        "Scared walk", "Gun walk"
    };
    return styles;
}

inline const std::vector<std::string>& motionBricksStyleIds(){
    static const std::vector<std::string> ids{
        "idle", "walk", "slow_walk", "hand_crawling", "elbow_crawling", "walk_stealth",
        "walk_boxing", "stealth_walk", "walk_happy_dance", "walk_zombie", "injured_walk",
        "walk_scared", "walk_gun"
    };
    return ids;
}

inline int motionBricksStyleIndex(const std::string& style){
    const std::vector<std::string>& names = motionBricksStyleIds();
    const auto found = std::find(names.begin(), names.end(), style);
    return found == names.end() ? 1 : int(found - names.begin());
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

inline std::string motionPromptSubject(const std::string& prompt){
    std::string lower = prompt;
    std::transform(lower.begin(), lower.end(), lower.begin(),
                   [](unsigned char c){ return char(std::tolower(c)); });
    static const char* actors[] = {"person", "woman", "man", "child", "teenager", "adult", "human", "humanoid", "robot", "character"};
    size_t first = std::string::npos, end = 0;
    for(const char* actor : actors){
        const size_t at = lower.find(actor);
        if(at != std::string::npos && at < first && at <= 48){
            const size_t after = at + std::char_traits<char>::length(actor);
            const bool leftWord = at == 0 || !std::isalnum(static_cast<unsigned char>(lower[at - 1]));
            const bool rightWord = after == lower.size() || !std::isalnum(static_cast<unsigned char>(lower[after]));
            if(leftWord && rightWord){ first = at; end = after; }
        }
    }
    if(first == std::string::npos) return "A person";
    const size_t firstText = lower.find_first_not_of(" \t");
    const bool hasSubjectLead = firstText != std::string::npos &&
        (lower.compare(firstText, 2, "a ") == 0 || lower.compare(firstText, 3, "an ") == 0 ||
         lower.compare(firstText, 4, "the ") == 0 || first == firstText);
    if(!hasSubjectLead) return "A person";
    std::string subject = prompt.substr(0, end);
    while(!subject.empty() && std::isspace(static_cast<unsigned char>(subject.back()))) subject.pop_back();
    return subject.empty() ? "A person" : subject;
}

inline bool motionPromptStartsWithSubject(const std::string& prompt){
    std::string lower = prompt;
    std::transform(lower.begin(), lower.end(), lower.begin(),
                   [](unsigned char c){ return char(std::tolower(c)); });
    static const char* actors[] = {"person", "woman", "man", "child", "teenager", "adult", "human", "humanoid", "robot", "character"};
    for(const char* actor : actors){
        const size_t at = lower.find(actor);
        if(at != std::string::npos && at <= 12){
            const size_t after = at + std::char_traits<char>::length(actor);
            const bool leftWord = at == 0 || !std::isalnum(static_cast<unsigned char>(lower[at - 1]));
            const bool rightWord = after == lower.size() || !std::isalnum(static_cast<unsigned char>(lower[after]));
            if(leftWord && rightWord) return true;
        }
    }
    return false;
}

//A sentence such as "walk and wave, then show a peace sign, then roll forward" is a
//sequence. Give each step its own Kimodo time segment, just like separate Action rows.
inline std::vector<MotionAction> filledActions(const std::vector<MotionAction>& actions){
    std::vector<MotionAction> out;
    for(const MotionAction& a : actions){
        const std::string subject = motionPromptSubject(a.prompt);
        std::string lower = a.prompt;
        std::transform(lower.begin(), lower.end(), lower.begin(),
                       [](unsigned char c){ return char(std::tolower(c)); });
        size_t start = 0;
        while(start < a.prompt.size()){
            const size_t separator = lower.find(" then ", start);
            std::string part = a.prompt.substr(start, separator == std::string::npos ? separator : separator - start);
            std::string clean = cleanPrompt(part);
            if(clean.size() >= 4 && clean.compare(clean.size() - 4, 4, " and") == 0)
                clean.resize(clean.size() - 4);
            while(!clean.empty() && (clean.back() == ' ' || clean.back() == ',')) clean.pop_back();
            if(!clean.empty()){
                if(!motionPromptStartsWithSubject(clean)) clean = subject + " " + clean;
                out.push_back({clean, std::clamp(a.duration, 1.0f, 10.0f)});
            }
            if(separator == std::string::npos) break;
            start = separator + 6;
        }
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

//A usable starting path for a locomotion clip. Kimodo faces +Z at heading zero.
inline MotionRootWaypoint motionDefaultRootEnd(int lastFrame, float heading, float speed = 0.8f){
    MotionRootWaypoint end;
    end.frame = std::max(1, lastFrame);
    const float seconds = float(end.frame + 1) / kimodoMotionFps;
    //Keep the preview compact even when a clip contains long stationary gestures after walking.
    const float distance = speed <= 1e-4f ? 0.0f : std::clamp(speed * seconds, 0.75f, 12.0f);
    end.x = std::sin(heading) * distance;
    end.z = std::cos(heading) * distance;
    end.heading = heading;
    return end;
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

inline MotionRootWaypoint motionRootPathAt(const std::vector<MotionRootWaypoint>& keys, float frame, bool smooth){
    MotionRootWaypoint result;
    result.frame = int(std::lround(frame));
    if(keys.empty()) return result;
    if(frame <= keys.front().frame) return keys.front();
    if(frame >= keys.back().frame) return keys.back();
    const auto right = std::upper_bound(keys.begin(), keys.end(), frame,
        [](float f, const MotionRootWaypoint& key){ return f < key.frame; });
    const size_t i = size_t(right - keys.begin()) - 1;
    const MotionRootWaypoint& a = keys[i];
    const MotionRootWaypoint& b = keys[i + 1];
    const float span = float(b.frame - a.frame);
    const float t = (frame - a.frame) / span;
    result.x = a.x + (b.x - a.x) * t;
    result.z = a.z + (b.z - a.z) * t;
    // Heading always takes the shortest turn. The position curve passes through every authored point.
    result.heading = a.heading + std::remainder(b.heading - a.heading, 6.28318530718f) * t;
    if(!smooth || keys.size() < 3) return result;
    auto coordinate = [&](float av, float bv, float previous, float next){
        const float current = (bv - av) / span;
        const float in = i > 0 ? (av - previous) / float(a.frame - keys[i - 1].frame) : current;
        const float out = i + 2 < keys.size() ? (next - bv) / float(keys[i + 2].frame - b.frame) : current;
        const float tangentA = i > 0 ? (in + current) * 0.5f : current;
        const float tangentB = i + 2 < keys.size() ? (current + out) * 0.5f : current;
        const float t2 = t * t, t3 = t2 * t;
        return (2*t3 - 3*t2 + 1)*av + (t3 - 2*t2 + t)*span*tangentA +
               (-2*t3 + 3*t2)*bv + (t3 - t2)*span*tangentB;
    };
    result.x = coordinate(a.x, b.x, i > 0 ? keys[i - 1].x : a.x,
                          i + 2 < keys.size() ? keys[i + 2].x : b.x);
    result.z = coordinate(a.z, b.z, i > 0 ? keys[i - 1].z : a.z,
                          i + 2 < keys.size() ? keys[i + 2].z : b.z);
    return result;
}

struct MotionPathSpeed{
    float distance = 0.0f;
    float peakMetersPerSecond = 0.0f;
};
inline MotionPathSpeed motionPathSpeed(const std::vector<MotionRootWaypoint>& keys, bool smooth){
    MotionPathSpeed result;
    if(keys.size() < 2) return result;
    MotionRootWaypoint previous = motionRootPathAt(keys, float(keys.front().frame), smooth);
    for(int frame = keys.front().frame + 1; frame <= keys.back().frame; ++frame){
        const MotionRootWaypoint current = motionRootPathAt(keys, float(frame), smooth);
        const float step = glm::length(glm::vec2(current.x - previous.x, current.z - previous.z));
        result.distance += step;
        result.peakMetersPerSecond = std::max(result.peakMetersPerSecond, step * kimodoMotionFps);
        previous = current;
    }
    return result;
}
inline float motionPromptPathSpeedLimit(const std::string& prompt){
    switch(motionPresetMentionedInPrompt(prompt)){
        case int(MotionPreset::Idle): return 0.25f;
        case int(MotionPreset::Walk): return 1.8f;
        case int(MotionPreset::Run): return 5.0f;
        case int(MotionPreset::Jump): return 3.0f;
        case int(MotionPreset::Crawl): return 0.9f;
        case int(MotionPreset::Crouch): return 1.4f;
        default: return 0.0f; // Unclassified action: show speed without imposing a gait.
    }
}
inline std::string motionPathSpeedProblem(const std::vector<MotionRootWaypoint>& keys,
                                          bool smooth, const std::string& prompt){
    const float limit = motionPromptPathSpeedLimit(prompt);
    const MotionPathSpeed speed = motionPathSpeed(keys, smooth);
    if(limit <= 0.0f || speed.peakMetersPerSecond <= limit) return {};
    char message[160];
    std::snprintf(message, sizeof(message), "Path peaks at %.1f m/s; this prompt suggests at most %.1f m/s. Slow or reshape the path.",
                  double(speed.peakMetersPerSecond), double(limit));
    return message;
}

inline int motionRootInsertionFrame(const std::vector<MotionRootWaypoint>& keys, int selected){
    if(keys.size() < 2) return -1;
    const int from = std::clamp(selected, 0, int(keys.size()) - 2);
    // Prefer the selected segment, then find the widest available gap.
    int gapIndex = from;
    if(keys[size_t(from + 1)].frame - keys[size_t(from)].frame < 2){
        int widest = 1;
        for(size_t i = 0; i + 1 < keys.size(); ++i){
            const int gap = keys[i + 1].frame - keys[i].frame;
            if(gap > widest){ widest = gap; gapIndex = int(i); }
        }
        if(widest < 2) return -1;
    }
    return keys[size_t(gapIndex)].frame +
           (keys[size_t(gapIndex + 1)].frame - keys[size_t(gapIndex)].frame) / 2;
}

inline bool writeMotionRootConstraints(const std::filesystem::path& path,
                                       const std::vector<MotionRootWaypoint>& waypoints,
                                       bool constrainHeading, int lastFrame,
                                       std::string& problem, bool smoothPath = false){
    problem = motionRootPathProblem(waypoints, lastFrame);
    if(!problem.empty()) return false;
    std::vector<MotionRootWaypoint> samples;
    if(smoothPath && waypoints.size() > 2){
        for(size_t i = 0; i + 1 < waypoints.size(); ++i){
            samples.push_back(waypoints[i]);
            for(int f = waypoints[i].frame + 3; f < waypoints[i + 1].frame; f += 3)
                samples.push_back(motionRootPathAt(waypoints, float(f), true));
        }
        samples.push_back(waypoints.back());
    }else samples = waypoints;
    std::ofstream file(path);
    if(!file){ problem = "Could not create Kimodo constraints file: " + path.string(); return false; }
    file << "[\n  {\n    \"type\": \"root2d\",\n    \"frame_indices\": [";
    for(size_t i = 0; i < samples.size(); ++i){ if(i) file << ", "; file << samples[i].frame; }
    file << "],\n    \"smooth_root_2d\": [";
    file << std::fixed << std::setprecision(6);
    for(size_t i = 0; i < samples.size(); ++i){
        if(i) file << ", ";
        file << "[" << samples[i].x << ", " << samples[i].z << "]";
    }
    file << "]";
    if(constrainHeading){
        file << ",\n    \"global_root_heading\": [";
        for(size_t i = 0; i < samples.size(); ++i){
            if(i) file << ", ";
            const double cosine = std::cos(samples[i].heading);
            const double sine = std::sin(samples[i].heading);
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

inline bool writeMotionDirectedConstraints(const std::filesystem::path& path,
                                           const std::vector<MotionRootWaypoint>& waypoints,
                                           const std::vector<MotionPoseConstraint>& poses,
                                           bool constrainHeading, int lastFrame,
                                           std::string& problem, bool smoothPath = false){
    problem = motionDirectPoseProblem(poses, lastFrame);
    if(!problem.empty()) return false;
    if(!waypoints.empty()){
        problem = motionRootPathProblem(waypoints, lastFrame);
        if(!problem.empty()) return false;
    }
    if(waypoints.empty() && poses.empty()){
        problem = "Add a route or at least one pose key.";
        return false;
    }
    std::vector<MotionRootWaypoint> samples;
    if(smoothPath && waypoints.size() > 2){
        for(size_t i = 0; i + 1 < waypoints.size(); ++i){
            samples.push_back(waypoints[i]);
            for(int f = waypoints[i].frame + 3; f < waypoints[i + 1].frame; f += 3)
                samples.push_back(motionRootPathAt(waypoints, float(f), true));
        }
        samples.push_back(waypoints.back());
    }else samples = waypoints;
    std::ofstream file(path);
    if(!file){ problem = "Could not create Kimodo constraints file: " + path.string(); return false; }
    file << std::fixed << std::setprecision(7) << "[\n";
    if(!samples.empty()){
        file << "  {\n    \"type\": \"root2d\",\n    \"frame_indices\": [";
        for(size_t i = 0; i < samples.size(); ++i){ if(i) file << ", "; file << samples[i].frame; }
        file << "],\n    \"smooth_root_2d\": [";
        for(size_t i = 0; i < samples.size(); ++i){
            if(i) file << ", ";
            file << '[' << samples[i].x << ", " << samples[i].z << ']';
        }
        file << ']';
        if(constrainHeading){
            file << ",\n    \"global_root_heading\": [";
            for(size_t i = 0; i < samples.size(); ++i){
                if(i) file << ", ";
                file << '[' << std::cos(samples[i].heading) << ", " << std::sin(samples[i].heading) << ']';
            }
            file << ']';
        }
        file << "\n  }";
    }
    if(!poses.empty()){
        if(!samples.empty()) file << ",\n";
        file << "  {\n    \"type\": \"fullbody\",\n    \"frame_indices\": [";
        for(size_t i = 0; i < poses.size(); ++i){ if(i) file << ", "; file << poses[i].frame; }
        file << "],\n    \"local_joints_rot\": [\n";
        for(size_t i = 0; i < poses.size(); ++i){
            file << "      [";
            for(size_t j = 0; j < poses[i].rotationDegrees.size(); ++j){
                if(j) file << ", ";
                const glm::quat q = glm::normalize(glm::quat(glm::radians(poses[i].rotationDegrees[j])));
                const double angle = 2.0 * std::acos(std::clamp(double(q.w), -1.0, 1.0));
                const double divisor = std::sqrt(std::max(0.0, 1.0 - double(q.w) * double(q.w)));
                const double scale = divisor > 1e-7 ? angle / divisor : 0.0;
                file << '[' << q.x * scale << ", " << q.y * scale << ", " << q.z * scale << ']';
            }
            file << ']' << (i + 1 < poses.size() ? ",\n" : "\n");
        }
        file << "    ],\n    \"root_positions\": [";
        for(size_t i = 0; i < poses.size(); ++i){
            if(i) file << ", ";
            const MotionRootWaypoint root = motionRootPathAt(waypoints, float(poses[i].frame), smoothPath);
            file << '[' << root.x << ", " << poses[i].rootHeight << ", " << root.z << ']';
        }
        file << "],\n    \"smooth_root_2d\": [";
        for(size_t i = 0; i < poses.size(); ++i){
            if(i) file << ", ";
            const MotionRootWaypoint root = motionRootPathAt(waypoints, float(poses[i].frame), smoothPath);
            file << '[' << root.x << ", " << root.z << ']';
        }
        file << "]\n  }";
    }
    file << "\n]\n";
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
    if(!adapterScript.empty()){
        command += shellQuoteArgument(adapterScript.string()) + " ";
        //kimodo_service.py prima iste argumente kao kimodo_cli.py iza naredbe "run"
        if(adapterScript.filename() == "kimodo_service.py") command += "run ";
    }
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
    if(!request.kimodoPostprocess) command += " --no-postprocess";
    return command;
}

//Uz BVH: opis i trajanje po retku, odvojeni tabom
inline void writeMotionSidecar(const std::filesystem::path& outputStem, const MotionRequest& request){
    std::ofstream file(outputStem.string() + ".txt");
    file << "# model\t" << request.model << '\n';
    file << "# target\t" << request.targetCharacter << '\n';
    file << "# first_heading_angle\t" << request.firstHeadingAngle << '\n';
    file << "# root_margin\t" << request.rootMargin << '\n';
    file << "# transition_frames\t" << request.transitionFrames << '\n';
    file << "# movement_preset\t" << motionPresetSettings(request.movementPreset).label << '\n';
    file << "# contact_settings\t" << (request.automaticContactSettings ? "automatic" : "manual") << '\n';
    file << "# foot_contact_ik\t" << (request.footContactIK ? "on" : "off") << '\n';
    file << "# kimodo_postprocess\t" << (request.kimodoPostprocess ? "on" : "off") << '\n';
    const char* rootPathKind = !request.rootWaypoints.empty() ? "authored"
                             : !request.constraints.empty() ? "external constraints" : "none";
    file << "# root_path\t" << rootPathKind << '\n';
    file << "# workflow\t" << (request.directedFlow ? "path + pose keys" : "recipe + live") << '\n';
    file << "# pose_keys\t" << request.poseConstraints.size() << '\n';
    file << "# fast_path_override\t" << (request.allowFastPath ? "on" : "off") << '\n';
    for(const MotionAction& a : filledActions(request.actions)) file << a.prompt << '\t' << a.duration << '\n';
}

inline std::vector<MotionAction> motionActionsForClip(const std::filesystem::path& bvh){
    std::filesystem::path sidecarPath = bvh.parent_path() / (bvh.stem().string() + ".txt");
    if(!std::filesystem::exists(sidecarPath))
        sidecarPath = bvh.parent_path().parent_path() / (bvh.parent_path().filename().string() + ".txt");
    std::ifstream sidecar(sidecarPath);
    std::vector<MotionAction> actions;
    std::string line;
    while(std::getline(sidecar, line)){
        if(line.empty() || line.front() == '#') continue;
        const size_t tab = line.find('\t');
        MotionAction action;
        action.prompt = line.substr(0, tab);
        if(tab != std::string::npos) action.duration = std::strtof(line.c_str() + tab + 1, nullptr);
        if(!action.prompt.empty()) actions.push_back(std::move(action));
    }
    return filledActions(actions);
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
struct MotionQuickPrompt{ const char* label; const char* prompt; };
inline const MotionQuickPrompt motionPresets[] = {
    {"Walk", "A person walks forward"},
    {"Jog", "A person jogs forward"},
    {"Jump", "A person jumps up in place"},
    {"Sit", "A person sits down on a chair"},
    {"Wave", "A person waves with the right hand"},
    {"Dance", "A person dances happily"},
    {"Turn", "A person turns around to the left"},
    {"Fall", "A person stumbles and falls down"},
    {"Peace", "A person shows a peace sign with the right hand"},
    {"Forward Roll", "A person performs a forward roll on the ground"},
};

//Advance a clip without reversing: one-shots hold the last frame, loops wrap only when requested.
inline bool advanceClipPlaybackFrame(double& frame, double deltaFrames,
                                    double firstFrame, double lastFrame, bool loop){
    if(!std::isfinite(frame) || !std::isfinite(deltaFrames) || !std::isfinite(firstFrame) ||
       !std::isfinite(lastFrame) || deltaFrames < 0.0 || lastFrame <= firstFrame){
        if(std::isfinite(firstFrame)) frame = firstFrame;
        return false;
    }
    const double nextFrame = std::clamp(frame, firstFrame, lastFrame) + deltaFrames;
    if(nextFrame <= lastFrame){
        frame = nextFrame;
        return true;
    }
    if(!loop){
        frame = lastFrame;
        return false;
    }
    const double duration = lastFrame - firstFrame;
    frame = firstFrame + std::fmod(nextFrame - firstFrame, duration);
    return true;
}

struct MotionPathDraft{
    bool enabled = true;
    bool autoEnd = true;
    bool autoDistance = true;
    bool smooth = true;
    bool pinHeading = false;
    int selected = 0;
    std::vector<MotionRootWaypoint> waypoints;
};

struct MotionPanelState{
    bool open = false;
    int flowMode = 0;              //0: create, 1: direct, 2: review
    int selectedTimelineAction = 0;
    bool resizingTimelineAction = false;
    int timelineDragLane = 0;       //0: none, 1: action, 2: path, 3: pose
    double timelineDragDisplayEndFrame = 0.0;
    MotionPathDraft recipePath;
    MotionPathDraft directedPath;
    std::vector<MotionPoseConstraint> poseConstraints;
    int selectedPose = -1;
    int selectedBone = 0;
    bool controlRigMode = true;
    int selectedRigControl = 0;
    int boneGroup = 0;
    bool mirrorBone = false;
    bool draggingPoseLane = false;
    glm::vec2 lastBoneDragMouse{0.0f};
    MotionPoseConstraint copiedPose;
    bool hasCopiedPose = false;
    int qualityCompareIndex = 0;
    bool qualityCompareOpen = false;
    MotionQualityCache qualityCache;        //ocjena varijanti, po datoteci i vremenu zapisa
    bool plateFloorOpen = false;
    bool redoOpen = false;                  //Review: ponovno generiranje dijela takea
    float redoFirst = -1.0f, redoLast = -1.0f;   //kadrovi takea (30 Hz), -1 dok nije odabrano
    std::string redoPrompt;                 //prazno: isti opisi kao take
    float cameraHeightMetres = 1.5f;        //visina snimatelja; iz nje metar scene (LoomPlateFloor.h)
    bool targetListOpen = false;
    bool gesturesOpen = false;
    bool recipeOptionsOpen = false;
    bool recipeRouteOpen = false;
    bool directedRouteOpen = false;
    bool directedQualityOpen = false;
    bool poseKeyListOpen = false;
    bool poseJointDetailsOpen = false;
    bool liveToolsOpen = false;
    bool historyOpen = false;
    bool helpOpen = false;
    float helpScroll = 0.0f;
    bool promptPresetDetection = true;
    int contactSettingsMode = 0;           //0: derive from recipe/prompt, 1: manual IK switches
    std::vector<MotionAction> actions{
        MotionAction{"A person walks forward at a relaxed pace with a natural arm swing", 4.0f}
    };
    MotionAction directedAction{"A person walks along the path", 4.0f};
    int activeAction = 0;                   //u koju radnju ide gotovi opis
    int locomotionPreset = int(MotionPreset::Walk);
    bool advancedExpanded = false;
    bool fixedSeed = false;
    float seed = 42.0f;
    int modelIndex = 0;
    float quality = 200.0f;                 //high-quality Kimodo denoising default
    float samples = 4.0f;
    float transitionFrames = 5.0f;
    int cfgIndex = 3;                       //separate text/constraint guidance
    float textGuidance = 2.0f, constraintGuidance = 2.0f;
    float firstHeadingAngle = 0.0f;
    float rootMargin = 0.04f;
    float defaultPathSpeed = 0.8f;
    std::string constraintsPath;
    bool rootPathEnabled = true;
    bool rootPathAutoEnd = true;
    bool rootPathAutoDistance = true;
    bool constrainRootHeading = false;
    bool smoothRootPath = true;
    bool allowFastPath = false;
    std::vector<MotionRootWaypoint> rootWaypoints{MotionRootWaypoint{}};
    int selectedRootWaypoint = 0;
    float rootTrackCursorFrame = 0.0f;
    Warp::Id targetCharacter = Warp::None;
    bool kimodoPostprocess = true;
    bool footContactIK = true;
    bool saveExample = false;
    int motionBricksStyle = 1;
    std::vector<MotionHistoryEntry> history;
    std::filesystem::path historyFrom;
    std::chrono::steady_clock::time_point historyRead{};

    const std::string& activePrompt() const{
        static const std::string empty;
        if(flowMode == 1) return directedAction.prompt;
        if(actions.empty()) return empty;
        return actions[size_t(std::clamp(activeAction, 0, int(actions.size()) - 1))].prompt;
    }
    bool automaticFootContactIK() const{
        const int mentioned = motionPresetMentionedInPrompt(activePrompt());
        if(motionPromptRequestsNoIk(activePrompt()) || mentioned == int(MotionPreset::Crawl)) return false;
        if(flowMode == 1) return true;
        if(locomotionPreset == int(MotionPreset::Crawl)) return false;
        return motionPresetSettings(locomotionPreset).footContactIK;
    }
    bool automaticKimodoPostprocess() const{
        // Keep Kimodo's constraint enforcement on for floor poses; Loom rig IK is separate.
        return flowMode == 1 ? true : motionPresetSettings(locomotionPreset).kimodoPostprocess;
    }
    bool effectiveFootContactIK() const{
        return contactSettingsMode == 0 ? automaticFootContactIK() : footContactIK;
    }
    bool effectiveKimodoPostprocess() const{
        return contactSettingsMode == 0 ? automaticKimodoPostprocess() : kimodoPostprocess;
    }
    std::string automaticContactReason() const{
        if(motionPromptRequestsNoIk(activePrompt())) return "Prompt says no IK";
        if(motionPresetMentionedInPrompt(activePrompt()) == int(MotionPreset::Crawl) ||
           (flowMode == 0 && locomotionPreset == int(MotionPreset::Crawl))) return "Crawl floor contacts";
        return "Preset defaults";
    }

    MotionRequest request() const{
        MotionRequest r;
        r.actions = actions;
        r.movementPreset = locomotionPreset;
        r.automaticContactSettings = contactSettingsMode == 0;
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
        r.constraints = flowMode == 1 ? std::filesystem::path{} : std::filesystem::path(constraintsPath);
        r.directedFlow = flowMode == 1;
        if(r.directedFlow) r.actions = {directedAction};
        if(r.directedFlow){ r.model = "Kimodo-SOMA-RP-v1.1"; r.poseConstraints = poseConstraints; }
        //Vanjski constraints.json ima prednost: dvije putanje u istom zahtjevu Kimodo ne zna
        //pomiriti, a panel je dosad slao obje (test_motion_panel to brani)
        if(rootPathEnabled && r.constraints.empty()){
            r.rootWaypoints = rootWaypoints; r.constrainRootHeading = constrainRootHeading; r.smoothRootPath = smoothRootPath;
        }
        r.allowFastPath = allowFastPath;
        r.targetCharacter = targetCharacter;
        r.kimodoPostprocess = effectiveKimodoPostprocess();
        r.footContactIK = effectiveFootContactIK();
        return r;
    }
};

inline void configureMotionPreset(MotionPanelState& state, int presetIndex, bool replacePrompt){
    presetIndex = std::clamp(presetIndex, 0, int(sizeof(motionPresetInfo) / sizeof(motionPresetInfo[0])) - 1);
    state.locomotionPreset = presetIndex;
    const MotionPresetInfo& preset = motionPresetSettings(presetIndex);
    const int actionIndex = std::clamp(state.activeAction, 0, int(state.actions.size()) - 1);
    if(replacePrompt){
        state.actions[size_t(actionIndex)].prompt = preset.prompt;
        state.actions[size_t(actionIndex)].duration = preset.seconds;
    }
    state.motionBricksStyle = motionBricksStyleIndex(preset.motionBricksStyle);
    state.defaultPathSpeed = preset.defaultPathSpeed;
    if(state.contactSettingsMode == 0){
        state.footContactIK = preset.footContactIK;
        state.kimodoPostprocess = preset.kimodoPostprocess;
    }
    state.rootPathEnabled = presetIndex != int(MotionPreset::Idle) && presetIndex != int(MotionPreset::Jump);
    state.rootPathAutoEnd = state.rootPathEnabled;
    state.rootPathAutoDistance = state.rootPathEnabled;
    const int last = std::max(1, kimodoMotionLastFrame(state.actions));
    state.rootWaypoints.assign(1, MotionRootWaypoint{});
    if(state.rootPathEnabled)
        state.rootWaypoints.push_back(motionDefaultRootEnd(last, state.firstHeadingAngle, state.defaultPathSpeed));
    state.rootWaypoints.front().heading = state.firstHeadingAngle;
    state.selectedRootWaypoint = int(state.rootWaypoints.size()) - 1;
    state.rootTrackCursorFrame = float(last);
}

//Sto je panel trazio u ovom kadru; aplikacija to izvrsi (posao, uvoz)
enum class MotionCompareMode{ None, SourceSkeleton, RigNoIk, RigWithIk };
struct MotionPanelAction{
    MotionCompareMode compareMode = MotionCompareMode::None;
    std::filesystem::path comparePath;
    std::filesystem::path copyNativeNpz;
    bool generate = false;
    bool generateMotionBricks = false;
    bool startLiveRecording = false;
    bool stopLiveRecording = false;
    int jumpPoseFrame = -1;
    bool standOnPlateFloor = false;
    std::filesystem::path regenerateTake;   //take ciji se dio generira iznova (BVH)
    int regenerateFirst = -1, regenerateLast = -1;
    std::string regeneratePrompt;
    std::filesystem::path importPath;
    bool close = false;
};

struct MotionPanelStatus{
    bool runnerReady = false;
    bool motionBricksReady = false;
    bool motionBricksRunning = false;
    bool liveRecording = false;
    bool liveReady = false;
    bool liveStopping = false;
    size_t liveFrames = 0;
    std::string liveMessage;
    double currentFrame = 1.0;
    double timelineStart = 1.0;
    double timelineFps = 30.0;
    bool running = false;                   //Kimodo posao tece
    bool otherJob = false;                  //tece neki drugi posao (solve, trening)
    double elapsed = 0.0;
    std::string lastLine;                   //zadnji redak ispisa posla
    std::filesystem::path historyDirectory; //gdje su generirani BVH-ovi
    std::string characterNote;              //sto je s rigged likom (WeaverMascott)
    std::vector<MotionCharacter> characters;
    const PlateFloorWatch* plate = nullptr;  //pod snimke i zakljucanost lika; null izvan scene solvea
    std::filesystem::path activeTake;       //BVH aktivnog klipa na liku (prazno: klip nije iz takea)
    double activeTakeStart = 1.0;           //kadar scene na kojem take pocinje
    int activeTakeFrames = 0;               //duljina takea u kadrovima takea (30 Hz)
};

inline Treadle::Color motionStatusOk(){ return {0.36f, 0.95f, 0.61f, 0.95f}; }

//Lik na podu snimke: pod, metar i koliko stopala klize po snimci. Sekcija postoji samo u sceni sa
//riješenom kamerom i oblakom - bez njih nema ni poda ni mjerila
inline void drawMotionPlateFloor(Treadle::Ui& ui, MotionPanelState& state, const MotionPanelStatus& status,
                                 MotionPanelAction& action){
    if(!status.plate || !status.plate->floor.valid || status.plate->floor.camera == Warp::None) return;
    const PlateFloorWatch& plate = *status.plate;
    const Treadle::Theme& theme = ui.style();
    char text[128];
    std::string summary = "floor found";
    if(plate.lock.valid){
        std::snprintf(text, sizeof(text), "feet slide %.1f px", double(plate.lock.medianPixels));
        summary = text;
    }
    if(!ui.disclosure("Plate floor", summary, &state.plateFloorOpen)) return;
    ui.slider("Camera height", &state.cameraHeightMetres, 0.3f, 3.0f, " m");
    ui.hint("Handheld is about 1.5 m. The solve has no metres; this height sets them.");
    const float metre = plateUnitsPerMetre(plate.floor, state.cameraHeightMetres);
    std::snprintf(text, sizeof(text), "%zu points, %.1f cm thick", plate.floor.support,
                  double(metre > 0.0f ? plate.floor.spread / metre * 100.0f : 0.0f));
    ui.value("Floor", text);
    if(ui.button("Stand character on plate floor")) action.standOnPlateFloor = true;
    if(plate.lock.valid){
        //1 px je granica koju oko na kompozitu jos ne vidi kao klizanje
        const bool locked = plate.lock.medianPixels <= 1.0f;
        std::snprintf(text, sizeof(text), "Feet on plate: median %.1f px, worst %.1f px over %zu contacts",
                      double(plate.lock.medianPixels), double(plate.lock.worstPixels), plate.lock.contacts);
        ui.status(text, locked ? motionStatusOk() : theme.warning);
        if(!locked) ui.hint("Planted feet slide on the plate. Rig foot IK (Contacts) pins them; the variant score shows how much the source skates.");
    }else if(!plate.lock.problem.empty()) ui.hint(plate.lock.problem);
}

//=============================================================================================
// IZGLED PANELA: tri razine vaznosti, ne jedna.
//
// Prva verzija je sve crtala istom tezinom - zlatni gumb nacina rada, zlatni klizac trajanja,
// zlatni gumbi pokreta, deset sekcija istog okvira - pa se nije vidjelo sto je glavno, a gumb
// GENERATE je bio ispod ruba plohe, dostupan tek pomicanjem. Sada:
//
//   1  GLAVNO      nacin rada (kartice), pokret, opis, trajanje - i GENERATE u podnozju koje se
//                  ne pomice, sa stanjem u jednom retku iznad sebe (zasto se ne moze, sto tece)
//   2  PODESAVANJE putanja, geste, ponasanje, kvaliteta, snimanje uzivo - tihe sekcije sa
//                  sazetkom desno, zatvorene dok ih se ne otvori
//   3  OBJASNJENJA sitan sivi tekst koji se prelama, a ne redak koji se reze na rubu
//=============================================================================================


//Sto podnozje kaze i nudi. Tekst stanja je jedan redak - ono sto korisnik treba prije klika
struct MotionFooterState{
    std::string status;
    Treadle::Color dot;
    std::string button;
    bool enabled = false;
    bool fastPathToggle = false;    //putanja prebrza: izbor "ipak dopusti" stoji uz gumb koji blokira
};

inline float motionFooterHeight(const Treadle::Theme& theme, bool extraRow){
    const float status = Treadle::textHeight(theme.textScale * 0.78f) + theme.spacing;
    const float primary = theme.rowHeight * 1.45f + theme.spacing;
    const float extra = extraRow ? theme.rowHeight + theme.spacing : 0.0f;
    return theme.padding * 0.8f + status + extra + primary + theme.padding * 0.7f;
}

inline bool drawMotionFooter(Treadle::Ui& ui, const Treadle::Rect& box, const MotionFooterState& footer,
                             bool* allowFastPath){
    const Treadle::Theme& theme = ui.style();
    ui.footer(box);
    const float room = box.width - 2.0f * theme.padding - 12.0f;
    ui.status(Treadle::fitText(footer.status, room, theme.textScale * 0.78f), footer.dot);
    if(footer.fastPathToggle && allowFastPath) ui.checkbox("Allow fast path anyway", allowFastPath);
    return ui.primaryButton(footer.button, footer.enabled);
}

inline std::string motionSecondsText(float seconds){
    char text[32];
    std::snprintf(text, sizeof(text), "%.1f s", double(seconds));
    return text;
}

inline std::string motionRouteSummary(const MotionPanelState& state){
    if(!state.rootPathEnabled) return "In place";
    const MotionPathSpeed speed = motionPathSpeed(state.rootWaypoints, state.smoothRootPath);
    char text[64];
    std::snprintf(text, sizeof(text), "%zu points  /  %.1f m", state.rootWaypoints.size(), double(speed.distance));
    return text;
}

inline std::string motionContactSummary(const MotionPanelState& state){
    std::string text = state.contactSettingsMode == 0 ? "Auto" : "Manual";
    text += state.effectiveFootContactIK() ? "  /  foot IK" : "  /  no foot IK";
    return text;
}

//Obrazlozenje zasto je nesto blokirano ima prednost pred "spremno": gumb koji ne radi mora
//odmah reci zasto, na istom mjestu gdje ga se klikne
inline void motionBlockedStatus(const Treadle::Theme& defaults, MotionFooterState& footer, const MotionPanelStatus& status,
                                const std::string& problem, bool hasPrompt){
    if(status.running){
        footer.status = "Generating  /  " + Loom::humanTime(status.elapsed) +
                        (status.lastLine.empty() ? std::string() : "  /  " + status.lastLine);
        footer.dot = defaults.accent;
    }else if(status.otherJob){
        footer.status = "Waiting for the solve or training job to finish";
        footer.dot = defaults.accent;
    }else if(!status.runnerReady){
        footer.status = "Kimodo is not installed  /  tools/weavermotion/setup.sh";
        footer.dot = defaults.warning;
    }else if(!problem.empty()){
        footer.status = problem;
        footer.dot = defaults.warning;
    }else if(!hasPrompt){
        footer.status = "Describe the motion to generate it";
        footer.dot = defaults.dim;
    }
}

//Kad je putanja ukljucena, zadnja tocka prati trajanje klipa; tocke izmedju se razmjerno pomaknu
inline void keepMotionRouteInClip(MotionPanelState& state){
    if(!state.rootPathEnabled) return;
    const int lastFrame = kimodoMotionLastFrame(state.actions);
    if(state.rootPathAutoEnd && lastFrame > 0){
        if(state.rootWaypoints.empty()) state.rootWaypoints.push_back(MotionRootWaypoint{});
        if(state.rootWaypoints.front().frame != 0){
            MotionRootWaypoint origin = motionRootWaypointAt(state.rootWaypoints, 0);
            origin.frame = 0; origin.x = 0.0f; origin.z = 0.0f;
            state.rootWaypoints.insert(state.rootWaypoints.begin(), origin);
        }
        if(state.rootWaypoints.size() == 1)
            state.rootWaypoints.push_back(motionDefaultRootEnd(lastFrame, state.firstHeadingAngle, state.defaultPathSpeed));
        else{
            // Keep authored points in order when action durations change.
            while(state.rootWaypoints.size() > size_t(lastFrame + 1))
                state.rootWaypoints.erase(state.rootWaypoints.end() - 2);
            const int oldEnd = std::max(1, state.rootWaypoints.back().frame);
            if(oldEnd != lastFrame){
                int previousFrame = 0;
                for(size_t i = 1; i + 1 < state.rootWaypoints.size(); ++i){
                    const int ideal = int(std::lround(double(state.rootWaypoints[i].frame) *
                                                      double(lastFrame) / double(oldEnd)));
                    const int latest = lastFrame - int(state.rootWaypoints.size() - 1 - i);
                    state.rootWaypoints[i].frame = std::clamp(ideal, previousFrame + 1, latest);
                    previousFrame = state.rootWaypoints[i].frame;
                }
            }
            if(state.rootPathAutoDistance)
                state.rootWaypoints.back() = motionDefaultRootEnd(lastFrame, state.firstHeadingAngle, state.defaultPathSpeed);
            else state.rootWaypoints.back().frame = lastFrame;
        }
    }
    state.rootTrackCursorFrame = std::clamp(state.rootTrackCursorFrame, 0.0f, float(std::max(0, lastFrame)));
}

//-- DIRECT: putanja i poze --------------------------------------------------------------------

inline MotionPanelAction drawMotionDirectedFlow(Treadle::Ui& ui, MotionPanelState& state,
                                                 const Treadle::Rect& area, const MotionPanelStatus& status,
                                                 MotionFooterState& footer){
    MotionPanelAction action;
    MotionAction& step = state.directedAction;

    ui.caption("PROMPT");
    Treadle::Ui::TextFieldConfig promptField;
    promptField.lines = 2;
    promptField.maxLength = 600;
    promptField.placeholder = "Walk along the path, then reach toward the camera";
    ui.textField("directed-motion-prompt", &step.prompt, promptField);

    const int oldLastFrame = std::max(1, kimodoMotionLastFrame({step}));
    if(ui.slider("Duration", &step.duration, 1.0f, 12.0f, " s")){
        const int newLastFrame = std::max(1, kimodoMotionLastFrame({step}));
        if(state.rootPathAutoEnd && state.rootWaypoints.size() > 1 &&
           state.rootWaypoints.back().frame == oldLastFrame){
            state.rootWaypoints.back().frame = newLastFrame;
            if(state.rootPathAutoDistance)
                state.rootWaypoints.back() = motionDefaultRootEnd(newLastFrame, state.firstHeadingAngle, state.defaultPathSpeed);
        }
    }
    const int lastFrame = std::max(1, kimodoMotionLastFrame({step}));
    const int playheadFrame = std::clamp(int(std::lround((status.currentFrame - status.timelineStart) *
        kimodoMotionFps / std::max(1.0, status.timelineFps))), 0, lastFrame);

    //-- poze: ono zbog cega se ovaj nacin bira, pa ide prvo ------------------------------------
    ui.caption("POSE KEYS   /   PLAYHEAD " + std::to_string(playheadFrame) + " / " + std::to_string(lastFrame));
    if(!state.poseConstraints.empty()){
        std::vector<std::string> keyLabels;
        const size_t shown = std::min<size_t>(state.poseConstraints.size(), 6);
        for(size_t i = 0; i < shown; ++i) keyLabels.push_back(std::to_string(state.poseConstraints[i].frame));
        const int clicked = ui.pills(keyLabels, state.selectedPose < int(shown) ? state.selectedPose : -1);
        if(clicked >= 0){
            state.selectedPose = clicked;
            action.jumpPoseFrame = state.poseConstraints[size_t(clicked)].frame;
        }
        if(state.poseConstraints.size() > shown){
            const std::string more = "All " + std::to_string(state.poseConstraints.size()) + " keys";
            if(ui.disclosure(more, "", &state.poseKeyListOpen)){
                for(size_t i = 0; i < state.poseConstraints.size(); ++i){
                    const std::string name = "Pose " + std::to_string(i + 1) + "   Frame " +
                                             std::to_string(state.poseConstraints[i].frame);
                    if(ui.selectable(name, state.selectedPose == int(i))){
                        state.selectedPose = int(i);
                        action.jumpPoseFrame = state.poseConstraints[i].frame;
                    }
                }
            }
        }
    }
    const bool poseLimit = state.poseConstraints.size() >= 20;
    if(ui.button(poseLimit ? "Pose limit reached (20)" : "+ Pose at playhead") && !poseLimit){
        const auto found = std::lower_bound(state.poseConstraints.begin(), state.poseConstraints.end(), playheadFrame,
            [](const MotionPoseConstraint& key, int f){ return key.frame < f; });
        if(found != state.poseConstraints.end() && found->frame == playheadFrame){
            state.selectedPose = int(found - state.poseConstraints.begin());
        }else{
            MotionPoseConstraint pose;
            pose.frame = playheadFrame;
            if(found != state.poseConstraints.begin()){
                pose = *(found - 1);
                pose.frame = playheadFrame;
            }
            state.selectedPose = int(state.poseConstraints.insert(found, pose) - state.poseConstraints.begin());
        }
    }
    if(state.selectedPose >= 0 && size_t(state.selectedPose) < state.poseConstraints.size()){
        MotionPoseConstraint& pose = state.poseConstraints[size_t(state.selectedPose)];
        ui.caption("EDITING POSE " + std::to_string(state.selectedPose + 1) + "   /   FRAME " + std::to_string(pose.frame));
        const int operation = ui.buttonRow({"Copy", "Paste", "Delete"});
        if(operation == 0){ state.copiedPose = pose; state.hasCopiedPose = true; }
        if(operation == 1 && state.hasCopiedPose){
            const int keepFrame = pose.frame;
            pose = state.copiedPose;
            pose.frame = keepFrame;
        }
        if(operation == 2){
            state.poseConstraints.erase(state.poseConstraints.begin() + state.selectedPose);
            state.selectedPose = state.poseConstraints.empty() ? -1 :
                std::min(state.selectedPose, int(state.poseConstraints.size()) - 1);
        }else{
            const int posePreset = ui.buttonRow({"Neutral", "Arms up", "Reach L", "Reach R"});
            if(posePreset >= 0){
                pose.rotationDegrees.fill(glm::vec3(0.0f));
                if(posePreset == 1){ pose.rotationDegrees[11].z = 75.0f; pose.rotationDegrees[17].z = -75.0f; }
                else if(posePreset == 2) pose.rotationDegrees[11].y = -75.0f;
                else if(posePreset == 3) pose.rotationDegrees[17].y = 75.0f;
            }
            ui.slider("Body height", &pose.rootHeight, 0.25f, 1.8f, " m");
            ui.hint("Drag cyan hand/foot targets in the viewport; Alt-drag rotates them. Orange sets "
                    "elbow/knee bend, violet turns chest and head.");
            if(ui.disclosure("Joint rotations", "", &state.poseJointDetailsOpen)){
                ui.choice("Bone group", {"Torso", "Arms", "Legs", "Face + Hands"}, &state.boneGroup);
                static constexpr int groups[4][9] = {
                    {0,1,2,3,4,5,6,-1,-1}, {10,11,12,13,16,17,18,19,-1},
                    {22,23,24,25,26,27,28,29,-1}, {7,8,9,14,15,20,21,-1,-1}
                };
                static constexpr const char* labels[30] = {
                    "Hips","Spine 1","Spine 2","Chest","Neck 1","Neck 2","Head","Jaw","Eye L","Eye R",
                    "Shoulder L","Arm L","Forearm L","Hand L","Thumb L","Finger L",
                    "Shoulder R","Arm R","Forearm R","Hand R","Thumb R","Finger R",
                    "Leg L","Shin L","Foot L","Toe L","Leg R","Shin R","Foot R","Toe R"
                };
                const int group = std::clamp(state.boneGroup, 0, 3);
                const int bone = std::clamp(state.selectedBone, 0, 29);
                for(int row = 0; row < 3; ++row){
                    std::vector<std::string> names;
                    std::vector<int> indices;
                    int chosen = -1;
                    for(int column = 0; column < 3; ++column){
                        const int index = groups[group][row * 3 + column];
                        if(index < 0) continue;
                        if(index == bone) chosen = int(names.size());
                        names.emplace_back(labels[index]);
                        indices.push_back(index);
                    }
                    if(!names.empty()){
                        const int clicked = ui.pills(names, chosen);
                        if(clicked >= 0 && clicked < int(indices.size())) state.selectedBone = indices[size_t(clicked)];
                    }
                }
                glm::vec3& angles = pose.rotationDegrees[size_t(std::clamp(state.selectedBone, 0, 29))];
                bool changed = false;
                changed |= ui.slider("Bend X", &angles.x, -180.0f, 180.0f, " deg");
                changed |= ui.slider("Turn Y", &angles.y, -180.0f, 180.0f, " deg");
                changed |= ui.slider("Twist Z", &angles.z, -180.0f, 180.0f, " deg");
                ui.checkbox("Mirror left / right", &state.mirrorBone);
                const int selectedBone = std::clamp(state.selectedBone, 0, 29);
                if(changed && state.mirrorBone){
                    const int other = motionDirectMirrorBone(selectedBone);
                    if(other != selectedBone) pose.rotationDegrees[size_t(other)] = glm::vec3(angles.x, -angles.y, -angles.z);
                }
                const int reset = ui.buttonRow({"Reset joint", "Reset pose"});
                if(reset == 0){
                    angles = glm::vec3(0.0f);
                    if(state.mirrorBone){
                        const int other = motionDirectMirrorBone(selectedBone);
                        if(other != selectedBone) pose.rotationDegrees[size_t(other)] = glm::vec3(0.0f);
                    }
                }
                if(reset == 1) pose.rotationDegrees.fill(glm::vec3(0.0f));
            }
        }
    }else if(state.poseConstraints.empty()){
        ui.hint("Move the playhead, add a pose, then shape it with the rig controls in the viewport.");
    }

    //-- podesavanje ----------------------------------------------------------------------------
    ui.caption("OPTIONS");
    if(ui.disclosure("Path", motionRouteSummary(state), &state.directedRouteOpen)){
        if(ui.checkbox("Move the root along a path", &state.rootPathEnabled) && state.rootPathEnabled &&
           state.rootWaypoints.size() < 2){
            state.rootWaypoints = {MotionRootWaypoint{},
                motionDefaultRootEnd(lastFrame, state.firstHeadingAngle, state.defaultPathSpeed)};
            state.selectedRootWaypoint = 1;
        }
        if(state.rootPathEnabled){
            if(state.rootWaypoints.empty()) state.rootWaypoints.push_back(MotionRootWaypoint{});
            ui.hint("Drag the amber ROOT handle in the viewport, or pick a point below.");
            ui.checkbox("Smooth route", &state.smoothRootPath);
            ui.checkbox("Pin facing at points", &state.constrainRootHeading);
            if(ui.button("+ Point at playhead") && state.rootWaypoints.size() < size_t(lastFrame + 1)){
                MotionRootWaypoint point = motionRootPathAt(state.rootWaypoints, float(playheadFrame), state.smoothRootPath);
                upsertMotionRootWaypoint(state.rootWaypoints, point, lastFrame);
                const auto found = std::lower_bound(state.rootWaypoints.begin(), state.rootWaypoints.end(), playheadFrame,
                    [](const MotionRootWaypoint& key, int f){ return key.frame < f; });
                state.selectedRootWaypoint = int(found - state.rootWaypoints.begin());
            }
            for(size_t i = 0; i < state.rootWaypoints.size(); ++i){
                const MotionRootWaypoint& point = state.rootWaypoints[i];
                char name[96];
                std::snprintf(name, sizeof(name), "Frame %d   X %.2f   Z %.2f m", point.frame,
                              double(point.x), double(point.z));
                if(ui.selectable(name, state.selectedRootWaypoint == int(i))) state.selectedRootWaypoint = int(i);
            }
            if(state.selectedRootWaypoint >= 0 && size_t(state.selectedRootWaypoint) < state.rootWaypoints.size()){
                MotionRootWaypoint& point = state.rootWaypoints[size_t(state.selectedRootWaypoint)];
                if(point.frame == 0) ui.hint("The start point stays at the origin.");
                else{
                    ui.dragFloat("X (m)", &point.x, 0.01f);
                    ui.dragFloat("Z (m)", &point.z, 0.01f);
                    if(state.constrainRootHeading) ui.slider("Facing (rad)", &point.heading, -3.14159f, 3.14159f);
                    if(ui.button("Delete point")){
                        state.rootWaypoints.erase(state.rootWaypoints.begin() + state.selectedRootWaypoint);
                        state.selectedRootWaypoint = std::max(0, state.selectedRootWaypoint - 1);
                        state.rootPathAutoEnd = false;
                    }
                }
            }
        }
    }
    drawMotionPlateFloor(ui, state, status, action);
    const std::string qualitySummary = std::to_string(int(std::lround(state.quality))) + " steps  /  " +
                                       std::to_string(int(std::lround(state.samples))) + " variants";
    if(ui.disclosure("Quality & contact", qualitySummary, &state.directedQualityOpen)){
        ui.slider("Denoising steps", &state.quality, 100.0f, 300.0f);
        ui.slider("Variants", &state.samples, 1.0f, 8.0f);
        const int previousContactMode = state.contactSettingsMode;
        if(ui.choice("Contact rules", {"Automatic", "Manual"}, &state.contactSettingsMode) &&
           previousContactMode == 0 && state.contactSettingsMode == 1){
            state.footContactIK = state.automaticFootContactIK();
            state.kimodoPostprocess = state.automaticKimodoPostprocess();
        }
        if(state.contactSettingsMode == 1){
            ui.checkbox("Rig foot IK", &state.footContactIK);
            ui.checkbox("Kimodo cleanup", &state.kimodoPostprocess);
        }else{
            ui.value("Foot IK", state.effectiveFootContactIK() ? "On" : "Off");
            ui.value("Kimodo cleanup", state.effectiveKimodoPostprocess() ? "On" : "Off");
        }
    }

    //-- podnozje -------------------------------------------------------------------------------
    const std::string poseProblem = motionDirectPoseProblem(state.poseConstraints, lastFrame);
    const std::string pathSpeedProblem = state.rootPathEnabled
        ? motionPathSpeedProblem(state.rootWaypoints, state.smoothRootPath, step.prompt) : std::string{};
    const std::string routeProblem = state.rootPathEnabled
        ? motionRootPathProblem(state.rootWaypoints, lastFrame) : std::string{};
    std::string problem = !poseProblem.empty() ? poseProblem : routeProblem;
    if(problem.empty() && !state.rootPathEnabled && state.poseConstraints.empty())
        problem = "Add a pose key or turn on the path";
    if(problem.empty() && !pathSpeedProblem.empty() && !state.allowFastPath) problem = pathSpeedProblem;
    const bool hasPrompt = !filledActions({step}).empty();
    const bool ready = status.runnerReady && !status.running && !status.otherJob && hasPrompt && problem.empty();

    footer.fastPathToggle = !pathSpeedProblem.empty();
    footer.button = status.running ? "GENERATING..." : "GENERATE PATH + POSES";
    footer.enabled = ready;
    footer.status = std::to_string(state.poseConstraints.size()) + " pose keys  /  " + motionRouteSummary(state) +
                    "  /  " + motionSecondsText(step.duration);
    footer.dot = motionStatusOk();
    motionBlockedStatus(ui.style(), footer, status, problem, hasPrompt);
    (void)area;
    return action;
}

//-- REVIEW: spremljeni pokreti ----------------------------------------------------------------

//Ponovno generiranje DIJELA takea (tools/weavermotion/kimodo_range.py): raspon se odabere na
//ucitanom takeu playheadom, sve izvan njega ostaje kakvo jest, Kimodo generira samo sredinu
inline void drawMotionRedoRange(Treadle::Ui& ui, MotionPanelState& state, const MotionPanelStatus& status,
                                MotionPanelAction& action){
    const Treadle::Theme& theme = ui.style();
    std::string summary = status.activeTake.empty() ? "load a take first" : "frames " +
        (state.redoFirst < 0.0f ? std::string("?") : std::to_string(int(state.redoFirst))) + " - " +
        (state.redoLast < 0.0f ? std::string("?") : std::to_string(int(state.redoLast)));
    if(!ui.disclosure("Redo part of the take", summary, &state.redoOpen)) return;
    if(status.activeTake.empty()){
        ui.hint("Click a saved take above to load it on the character; then pick the part to redo here.");
        return;
    }
    std::filesystem::path npz = status.activeTake;
    npz.replace_extension(".npz");
    if(!std::filesystem::is_regular_file(npz)){
        ui.hint("This take has no native Kimodo NPZ (e.g. a MotionBricks recording), so it cannot be partly regenerated.");
        return;
    }
    //Playhead u kadar takea: take je 30 Hz, scena ima svoj fps
    const int last = std::max(0, status.activeTakeFrames - 1);
    const int here = std::clamp(int(std::lround((status.currentFrame - status.activeTakeStart) *
                                                kimodoMotionFps / std::max(1.0, status.timelineFps))), 0, last);
    ui.value("Take", Treadle::fitText(status.activeTake.stem().string(), 190.0f, theme.textScale));
    const int picked = ui.buttonRow({"Start here (" + std::to_string(here) + ")", "End here (" + std::to_string(here) + ")"});
    if(picked == 0) state.redoFirst = float(here);
    if(picked == 1) state.redoLast = float(here);
    if(state.redoFirst >= 0.0f && state.redoLast >= 0.0f && state.redoFirst > state.redoLast) std::swap(state.redoFirst, state.redoLast);
    Treadle::Ui::TextFieldConfig field;
    field.lines = 2;
    field.maxLength = 400;
    field.placeholder = "New prompt for this part (empty: same as the take)";
    ui.textField("redo-prompt", &state.redoPrompt, field);
    const bool ranged = state.redoFirst >= 0.0f && state.redoLast >= 0.0f;
    const bool whole = ranged && int(state.redoFirst) == 0 && int(state.redoLast) >= last;
    const bool ready = ranged && !whole && status.runnerReady && !status.running && !status.otherJob;
    if(!ranged) ui.hint("Move the playhead to where the bad part starts and ends.");
    else if(whole) ui.hint("That is the whole take - use Create instead.");
    else ui.hint("Everything outside frames " + std::to_string(int(state.redoFirst)) + "-" + std::to_string(int(state.redoLast)) +
                 " stays exactly as it is; Kimodo makes new variants of this part and the best one loads.");
    if(ui.button(ready ? "REGENERATE THIS PART" : status.running ? "Generating..." : "Pick a part to regenerate") && ready){
        action.regenerateTake = status.activeTake;
        action.regenerateFirst = int(state.redoFirst);
        action.regenerateLast = int(state.redoLast);
        action.regeneratePrompt = state.redoPrompt;
    }
}

inline void drawMotionReview(Treadle::Ui& ui, MotionPanelState& state, const Treadle::Rect& area,
                             const MotionPanelStatus& status, MotionPanelAction& action){
    const Treadle::Theme& theme = ui.style();
    ui.caption("SAVED TAKES   /   " + std::to_string(state.history.size()));
    if(state.history.empty()){
        ui.hint("Nothing generated yet. Takes from Create and Direct appear here.");
    }else{
        ui.hint("Click a take to load it on the character; the numbers are its variants. "
                "Right-click to reuse its prompt in Create.");
        //Jedno generiranje daje vise varijanti istog opisa (mapa s _00, _01 ...). Kao zasebni redovi
        //bile su cetiri jednaka retka jedan ispod drugog; ovdje su jedan opis i brojevi varijanti
        size_t i = 0;
        while(i < state.history.size()){
            const MotionHistoryEntry& first = state.history[i];
            const std::filesystem::path group = first.bvh.parent_path();
            const bool grouped = group != state.historyFrom;
            size_t end = i + 1;
            while(grouped && end < state.history.size() && state.history[end].bvh.parent_path() == group) ++end;
            std::vector<const MotionHistoryEntry*> variants;
            for(size_t k = i; k < end; ++k) variants.push_back(&state.history[k]);
            std::sort(variants.begin(), variants.end(), [](const MotionHistoryEntry* a, const MotionHistoryEntry* b){
                return a->bvh.filename() < b->bvh.filename();
            });
            //Najbolja varijanta je oznacena, a ispod stoji zasto: korisnik vidi i izbor i razlog,
            //i moze ga odbiti - mjera ne vidi stil ni to je li pokret ono sto je opis trazio
            std::vector<std::filesystem::path> paths;
            for(const MotionHistoryEntry* variant : variants) paths.push_back(variant->bvh);
            const int best = bestMotionVariant(paths, state.qualityCache);
            std::string title = first.actions.empty() ? first.bvh.stem().string() : first.actions.front().prompt;
            if(first.actions.size() > 1) title += "  (+" + std::to_string(first.actions.size() - 1) + ")";
            if(ui.selectable(Treadle::fitText(title, area.width - 40.0f, theme.textScale), false))
                action.importPath = variants[size_t(std::max(0, best))]->bvh;
            if(ui.rightClicked() && !first.actions.empty()){
                state.actions = first.actions;
                state.activeAction = 0;
                state.flowMode = 0;
                if(state.promptPresetDetection){
                    const int detected = motionPresetMentionedInPrompt(state.activePrompt());
                    if(detected >= 0) configureMotionPreset(state, detected, false);
                }
                ui.focusTextField("action0");
            }
            if(variants.size() > 1){
                std::vector<std::string> labels;
                for(size_t k = 0; k < variants.size() && k < 8; ++k)
                    labels.push_back(int(k) == best ? std::to_string(k + 1) + " best" : std::to_string(k + 1));
                const int clicked = ui.pills(labels, best);
                if(clicked >= 0) action.importPath = variants[size_t(clicked)]->bvh;
            }
            if(best >= 0){
                const Engine::MotionQuality::Report& report = state.qualityCache.get(paths[size_t(best)]);
                ui.hint(Engine::MotionQuality::summary(report) +
                        (report.score > 4.0f ? "  -  weakest: " + report.worst : std::string()));
            }
            i = end;
        }
    }

    ui.caption("FIX");
    drawMotionRedoRange(ui, state, status, action);

    ui.caption("DIAGNOSTICS");
    if(ui.disclosure("Compare import stages", "", &state.qualityCompareOpen)){
        if(state.history.empty()) ui.hint("Generate a take to compare.");
        else{
            std::vector<std::string> takeNames;
            takeNames.reserve(state.history.size());
            for(const MotionHistoryEntry& item : state.history)
                takeNames.push_back(Treadle::fitText(item.summary, area.width - 66.0f, theme.textScale));
            state.qualityCompareIndex = std::clamp(state.qualityCompareIndex, 0, int(state.history.size()) - 1);
            ui.choice("Take", takeNames, &state.qualityCompareIndex);
            const std::filesystem::path bvh = state.history[size_t(state.qualityCompareIndex)].bvh;
            ui.hint("Each rig option creates a separate Animator clip, so the stages can be scrubbed side by side.");
            const int clicked = ui.buttonRow({"Source BVH", "Rig no IK", "Rig with IK"});
            if(clicked >= 0){
                action.comparePath = bvh;
                action.compareMode = clicked == 0 ? MotionCompareMode::SourceSkeleton :
                    clicked == 1 ? MotionCompareMode::RigNoIk : MotionCompareMode::RigWithIk;
            }
            std::filesystem::path npz = bvh;
            npz.replace_extension(".npz");
            if(std::filesystem::is_regular_file(npz)){
                if(ui.button("Copy native NPZ path")) action.copyNativeNpz = npz;
                ui.hint("Open it in kimodo_demo under Load/Save > Motion.");
            }
        }
    }
}

//-- CREATE: pokret iz opisa -------------------------------------------------------------------

inline void drawMotionCreate(Treadle::Ui& ui, MotionPanelState& state, const Treadle::Rect& area,
                             const MotionPanelStatus& status, MotionPanelAction& action,
                             MotionFooterState& footer){
    const Treadle::Theme& theme = ui.style();
    if(state.actions.empty()) state.actions.push_back(MotionAction{"", 3.0f});
    state.activeAction = std::clamp(state.activeAction, 0, int(state.actions.size()) - 1);

    //-- 1 pokret: odabrani se vidi, ne pise se jos jednom ispod kao "Movement: Walk" --------------
    ui.caption("MOVEMENT");
    for(int row = 0; row < 2; ++row){
        const int first = row * 3;
        const int selected = state.locomotionPreset >= first && state.locomotionPreset < first + 3
                           ? state.locomotionPreset - first : -1;
        const int clicked = ui.pills({motionPresetInfo[first].label, motionPresetInfo[first + 1].label,
                                      motionPresetInfo[first + 2].label}, selected);
        if(clicked >= 0){
            configureMotionPreset(state, first + clicked, true);
            ui.focusTextField("action" + std::to_string(state.activeAction));
        }
    }

    //-- 2 opis i trajanje ------------------------------------------------------------------------
    const bool multiStep = state.actions.size() > 1;
    ui.caption(multiStep ? "PROMPT   /   STEP " + std::to_string(state.activeAction + 1) + " OF " +
                           std::to_string(state.actions.size()) : "PROMPT");
    if(multiStep){
        std::vector<std::string> stepLabels;
        for(size_t i = 0; i < state.actions.size(); ++i) stepLabels.push_back(std::to_string(i + 1));
        const int clicked = ui.pills(stepLabels, state.activeAction);
        if(clicked >= 0) state.activeAction = clicked;
    }
    MotionAction& activeAction = state.actions[size_t(state.activeAction)];
    Treadle::Ui::TextFieldConfig field;
    field.lines = 3;
    field.maxLength = 600;
    field.placeholder = "A person walks forward with a relaxed arm swing";
    const Treadle::Ui::TextFieldResult promptResult = ui.textField(
        "action" + std::to_string(state.activeAction), &activeAction.prompt, field);
    if(state.promptPresetDetection && promptResult.changed){
        const int detected = motionPresetMentionedInPrompt(activeAction.prompt);
        if(detected >= 0 && detected != state.locomotionPreset) configureMotionPreset(state, detected, false);
    }
    if(promptResult.submitted) action.generate = true;
    ui.slider("Duration", &activeAction.duration, 1.0f, 10.0f, " s");

    //Koraci: dodavanje je cesto, preslagivanje rijetko - pa preslagivanje postoji tek kad ima sto
    int stepAction = -1;
    const bool canAdd = state.actions.size() < 6;
    if(multiStep) stepAction = ui.buttonRow(canAdd ? std::vector<std::string>{"+ Step", "Earlier", "Later", "Remove"}
                                                   : std::vector<std::string>{"Earlier", "Later", "Remove"});
    else stepAction = ui.buttonRow({"+ Add a step after this"});
    if(canAdd && stepAction == 0){
        state.actions.push_back(MotionAction{"", 3.0f});
        state.activeAction = int(state.actions.size()) - 1;
        ui.focusTextField("action" + std::to_string(state.activeAction));
    }else if(multiStep && stepAction >= 0){
        const int op = stepAction - (canAdd ? 1 : 0);
        if(op == 0 && state.activeAction > 0){
            std::swap(state.actions[size_t(state.activeAction)], state.actions[size_t(state.activeAction - 1)]);
            --state.activeAction;
        }else if(op == 1 && state.activeAction + 1 < int(state.actions.size())){
            std::swap(state.actions[size_t(state.activeAction)], state.actions[size_t(state.activeAction + 1)]);
            ++state.activeAction;
        }else if(op == 2){
            state.actions.erase(state.actions.begin() + state.activeAction);
            state.activeAction = std::clamp(state.activeAction, 0, int(state.actions.size()) - 1);
        }
    }
    keepMotionRouteInClip(state);

    //-- 3 podesavanje: zatvoreno, sa sazetkom da se ne mora otvarati da bi se znalo ------------
    const MotionPresetInfo& activePreset = motionPresetSettings(state.locomotionPreset);
    ui.caption("OPTIONS");
    const int lastFrame = kimodoMotionLastFrame(state.actions);
    if(ui.disclosure("Path", motionRouteSummary(state), &state.recipeRouteOpen)){
        if(ui.checkbox("Travel along a route", &state.rootPathEnabled) && state.rootPathEnabled){
            state.rootPathAutoEnd = true;
            state.rootPathAutoDistance = true;
            if(state.rootWaypoints.size() < 2){
                state.rootWaypoints = {MotionRootWaypoint{},
                    motionDefaultRootEnd(std::max(1, lastFrame), state.firstHeadingAngle, state.defaultPathSpeed)};
                state.selectedRootWaypoint = 1;
            }
            keepMotionRouteInClip(state);
        }
        if(state.rootPathEnabled){
            ui.hint("Shift-click in the viewport adds a point; drag points to shape the route.");
            ui.checkbox("Smooth path through points", &state.smoothRootPath);
            if(ui.checkbox("Pin heading at points", &state.constrainRootHeading)){
                if(state.constrainRootHeading && !state.rootWaypoints.empty() && state.rootWaypoints.front().frame == 0)
                    state.rootWaypoints.front().heading = state.firstHeadingAngle;
            }
            const int keyAction = ui.buttonRow({"+ Point after selected", "Delete"});
            if(keyAction == 0){
                const int atFrame = motionRootInsertionFrame(state.rootWaypoints, state.selectedRootWaypoint);
                if(atFrame >= 0){
                    MotionRootWaypoint key = motionRootPathAt(state.rootWaypoints, float(atFrame), state.smoothRootPath);
                    upsertMotionRootWaypoint(state.rootWaypoints, key, lastFrame);
                    const auto found = std::lower_bound(state.rootWaypoints.begin(), state.rootWaypoints.end(), atFrame,
                        [](const MotionRootWaypoint& item, int f){ return item.frame < f; });
                    state.selectedRootWaypoint = int(found - state.rootWaypoints.begin());
                    state.rootTrackCursorFrame = float(atFrame);
                }
            }else if(keyAction == 1 && state.selectedRootWaypoint >= 0 &&
                     size_t(state.selectedRootWaypoint) < state.rootWaypoints.size() &&
                     state.rootWaypoints[size_t(state.selectedRootWaypoint)].frame != 0){
                if(state.rootPathAutoEnd && state.selectedRootWaypoint == int(state.rootWaypoints.size()) - 1)
                    state.rootPathAutoEnd = false;
                state.rootWaypoints.erase(state.rootWaypoints.begin() + state.selectedRootWaypoint);
                state.selectedRootWaypoint = std::clamp(state.selectedRootWaypoint, 0, int(state.rootWaypoints.size()) - 1);
            }
            for(size_t i = 0; i < state.rootWaypoints.size(); ++i){
                const MotionRootWaypoint& key = state.rootWaypoints[i];
                char label[112];
                std::snprintf(label, sizeof(label), "%zu   %.2f s   X %.2f  Z %.2f%s", i + 1,
                              double(key.frame) / kimodoMotionFps, double(key.x), double(key.z),
                              key.frame == 0 ? "  start" : "");
                if(ui.selectable(label, int(i) == state.selectedRootWaypoint)){
                    state.selectedRootWaypoint = int(i);
                    state.rootTrackCursorFrame = float(key.frame);
                }
            }
            if(state.selectedRootWaypoint >= 0 && size_t(state.selectedRootWaypoint) < state.rootWaypoints.size()){
                MotionRootWaypoint& key = state.rootWaypoints[size_t(state.selectedRootWaypoint)];
                if(key.frame != 0){
                    if(!(state.rootPathAutoEnd && state.selectedRootWaypoint == int(state.rootWaypoints.size()) - 1)){
                        float seconds = float(key.frame) / kimodoMotionFps;
                        if(ui.dragFloat("Time (s)", &seconds, 0.01f)){
                            moveMotionRootWaypoint(state.rootWaypoints, size_t(state.selectedRootWaypoint),
                                                   int(std::lround(seconds * kimodoMotionFps)), lastFrame);
                            state.rootTrackCursorFrame = float(key.frame);
                        }
                    }
                    float position[3]{key.x, 0.0f, key.z};
                    if(ui.dragVector("Position X / Z (m)", position, 0.002f)){
                        key.x = position[0];
                        key.z = position[2];
                        if(state.rootPathAutoEnd && state.selectedRootWaypoint == int(state.rootWaypoints.size()) - 1)
                            state.rootPathAutoDistance = false;
                    }
                }
                if(state.constrainRootHeading){
                    float degrees = key.heading * (180.0f / 3.14159265359f);
                    if(ui.dragFloat("Facing (deg)", &degrees, 0.2f))
                        key.heading = std::clamp(degrees, -180.0f, 180.0f) * (3.14159265359f / 180.0f);
                }
            }
        }
    }

    drawMotionPlateFloor(ui, state, status, action);

    constexpr int presetCount = int(sizeof(motionPresets) / sizeof(motionPresets[0]));
    if(ui.disclosure("Gestures", "append to prompt", &state.gesturesOpen)){
        for(int row = 0; row < (presetCount + 3) / 4; ++row){
            std::vector<std::string> labels;
            for(int k = 0; k < 4 && row * 4 + k < presetCount; ++k)
                labels.push_back(motionPresets[row * 4 + k].label);
            const int clicked = ui.chipRow(labels, theme.accent);
            if(clicked >= 0){
                MotionAction& target = state.actions[size_t(state.activeAction)];
                const std::string text = motionPresets[row * 4 + clicked].prompt;
                std::string lowerText = text;
                std::transform(lowerText.begin(), lowerText.end(), lowerText.begin(),
                               [](unsigned char c){ return char(std::tolower(c)); });
                const std::string subject = "a person ";
                const std::string tail = lowerText.rfind(subject, 0) == 0 ? text.substr(subject.size()) : text;
                target.prompt = target.prompt.empty() ? text : target.prompt + ", then " + tail;
                if(state.promptPresetDetection){
                    const int detected = motionPresetMentionedInPrompt(target.prompt);
                    if(detected >= 0 && detected != state.locomotionPreset) configureMotionPreset(state, detected, false);
                }
                ui.focusTextField("action" + std::to_string(state.activeAction));
            }
        }
    }

    if(ui.disclosure("Contacts & blending", motionContactSummary(state), &state.recipeOptionsOpen)){
        if(filledActions(state.actions).size() > 1)
            ui.slider("Blend between steps", &state.transitionFrames, 0.0f, 30.0f, " fr");
        ui.checkbox("Pick movement from prompt words", &state.promptPresetDetection);
        const int previousContactMode = state.contactSettingsMode;
        if(ui.choice("Contact rules", {"Automatic", "Manual"}, &state.contactSettingsMode) &&
           previousContactMode == 0 && state.contactSettingsMode != 0){
            state.footContactIK = state.automaticFootContactIK();
            state.kimodoPostprocess = state.automaticKimodoPostprocess();
        }
        if(state.contactSettingsMode == 0){
            ui.value("Foot-contact IK", state.effectiveFootContactIK() ? "On" : "Off");
            ui.value("Kimodo cleanup", state.effectiveKimodoPostprocess() ? "On" : "Off");
            ui.hint("Why: " + state.automaticContactReason() + ".");
        }else{
            ui.checkbox("Rig foot-contact IK", &state.footContactIK);
            ui.checkbox("Kimodo postprocess", &state.kimodoPostprocess);
        }
    }

    const std::string qualitySummary = std::to_string(int(std::lround(state.quality))) + " steps  /  " +
                                       std::to_string(int(std::lround(state.samples))) + " variants";
    if(ui.disclosure("Quality", qualitySummary, &state.advancedExpanded)){
        ui.slider("Variants to compare", &state.samples, 1.0f, 8.0f);
        ui.slider("Denoising steps", &state.quality, 100.0f, 300.0f);
        ui.checkbox("Same seed = same motion", &state.fixedSeed);
        if(state.fixedSeed) ui.dragFloat("Seed", &state.seed, 0.2f);
        if(ui.button("Restore best-quality defaults")){
            state.modelIndex = 0;
            state.quality = 200.0f;
            state.samples = 4.0f;
            state.transitionFrames = 5.0f;
            state.cfgIndex = 3;
            state.textGuidance = 2.0f;
            state.constraintGuidance = 2.0f;
            state.kimodoPostprocess = true;
        }
        ui.caption("EXPERT");
        ui.choice("Model", kimodoModels(), &state.modelIndex);
        const std::string& model = kimodoModels()[size_t(std::clamp(state.modelIndex, 0, int(kimodoModels().size()) - 1))];
        if(model.find("G1") != std::string::npos || model.find("SMPLX") != std::string::npos)
            ui.hint("This model writes native NPZ, not SOMA BVH - the Animator cannot preview it yet.");
        ui.choice("Guidance", {"Default", "None", "Regular", "Text + path"}, &state.cfgIndex);
        if(state.cfgIndex == 2 || state.cfgIndex == 3) ui.slider("Text guidance", &state.textGuidance, 0.0f, 10.0f);
        if(state.cfgIndex == 3) ui.slider("Constraint guidance", &state.constraintGuidance, 0.0f, 10.0f);
        if(ui.slider("Initial heading", &state.firstHeadingAngle, -3.14159f, 3.14159f, " rad")){
            if(state.rootPathEnabled && state.constrainRootHeading && !state.rootWaypoints.empty() &&
               state.rootWaypoints.front().frame == 0) state.rootWaypoints.front().heading = state.firstHeadingAngle;
        }
        ui.slider("Root correction margin", &state.rootMargin, 0.0f, 0.25f, " m");
        Treadle::Ui::TextFieldConfig pathField;
        pathField.lines = 1;
        pathField.maxLength = 1024;
        pathField.placeholder = "External constraints.json (optional)";
        ui.textField("kimodo-constraints", &state.constraintsPath, pathField);
        ui.checkbox("Save reusable Kimodo example", &state.saveExample);
    }

    if(activePreset.realtime || status.liveRecording){
        if(status.liveRecording) state.liveToolsOpen = true;
        const std::string liveSummary = status.liveRecording ? "recording" :
            status.motionBricksReady ? motionBricksStyles()[size_t(std::clamp(state.motionBricksStyle, 0,
                int(motionBricksStyles().size()) - 1))] : "setup required";
        if(ui.disclosure("Live recording", liveSummary, &state.liveToolsOpen)){
            if(!status.motionBricksReady){
                ui.hint("Install MotionBricks once: tools/motionbricks/setup.sh");
            }else if(status.liveRecording){
                ui.value("Session", status.liveStopping ? "Saving..." : status.liveReady ? "Recording" : "Loading...");
                ui.value("Recorded frames", std::to_string(status.liveFrames));
                if(!status.liveMessage.empty()) ui.hint(status.liveMessage);
            }else{
                ui.choice("Style", motionBricksStyles(), &state.motionBricksStyle);
                ui.hint("Streams MotionBricks onto the rig along the route; stopping keeps the whole take.");
                const bool idle = !status.running && !status.otherJob;
                const int live = ui.buttonRow({"Start live + record", "Render clip"});
                if(live == 0 && idle) action.startLiveRecording = true;
                if(live == 1 && idle) action.generateMotionBricks = true;
                if(status.motionBricksRunning) ui.hint("MotionBricks is rendering a clip...");
            }
        }
    }

    //-- podnozje -------------------------------------------------------------------------------
    const std::string rootProblem = state.rootPathEnabled ? motionRootPathProblem(state.rootWaypoints, lastFrame) : std::string{};
    const bool constraintConflict = state.rootPathEnabled && !state.constraintsPath.empty();
    const std::string pathSpeedProblem = state.rootPathEnabled
        ? motionPathSpeedProblem(state.rootWaypoints, state.smoothRootPath,
            state.actions.empty() ? std::string{} : state.actions.front().prompt) : std::string{};
    std::string problem = !rootProblem.empty() ? "Path: " + rootProblem
                        : constraintConflict ? "Clear the external constraints.json or turn off the path"
                        : std::string{};
    if(problem.empty() && !pathSpeedProblem.empty() && !state.allowFastPath) problem = pathSpeedProblem;
    const std::vector<MotionAction> generatedActions = filledActions(state.actions);
    const bool hasPrompt = !generatedActions.empty();
    const bool ready = status.runnerReady && !status.running && !status.otherJob && hasPrompt && problem.empty();
    if(!ready) action.generate = false;

    float total = 0.0f;
    for(const MotionAction& step : generatedActions) total += step.duration;
    footer.fastPathToggle = !pathSpeedProblem.empty();
    footer.enabled = ready;
    footer.button = status.running ? "GENERATING..." : "GENERATE";
    footer.status = std::string(activePreset.label) + "  /  " + motionRouteSummary(state) + "  /  " +
                    motionSecondsText(total) + (generatedActions.size() > 1
                        ? " in " + std::to_string(generatedActions.size()) + " steps" : std::string());
    footer.dot = motionStatusOk();
    motionBlockedStatus(ui.style(), footer, status, problem, hasPrompt);
    if(status.liveRecording){
        footer.status = "Recording live  /  " + std::to_string(status.liveFrames) + " frames";
        footer.dot = theme.warning;
        footer.button = status.liveStopping ? "SAVING..." : "STOP & KEEP ANIMATION";
        footer.enabled = !status.liveStopping;
    }
}

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

    //Review nema glavnu radnju, pa ni podnozje: aplikacija ispod njega dopisuje svoje sekcije
    //(obrada pokreta) u istu plohu
    const bool hasFooter = state.flowMode != 2;
    const bool extraRow = hasFooter && state.rootPathEnabled && !state.rootWaypoints.empty() &&
        !motionPathSpeedProblem(state.rootWaypoints, state.smoothRootPath, state.activePrompt()).empty();
    const float footerHeight = hasFooter ? motionFooterHeight(theme, extraRow) : 0.0f;
    const Treadle::Rect body{area.x, area.y, area.width, area.height - footerHeight};
    ui.dock("ANIMATOR", body, &scroll);

    const Treadle::Rect closeButton{area.x + area.width - 65.0f, area.y + 4.0f, 25.0f, 25.0f};
    const auto closeHit = ui.region("motion-window-close", closeButton);
    ui.canvas().rect(closeButton, closeHit.hot ? theme.hot : theme.control);
    ui.canvas().outline(closeButton, 1.0f, closeHit.hot ? theme.warning : theme.panelEdge);
    ui.canvas().text(closeButton.x + 8.0f, closeButton.y + 4.0f, "X", theme.title, theme.textScale * 0.68f);
    if(closeHit.pressed) action.close = true;
    const Treadle::Rect helpButton{area.x + area.width - 34.0f, area.y + 4.0f, 25.0f, 25.0f};
    const auto helpHit = ui.region("motion-help-button", helpButton);
    ui.canvas().rect(helpButton, helpHit.hot ? theme.hot : theme.control);
    ui.canvas().outline(helpButton, 1.0f, state.helpOpen ? theme.accent : theme.panelEdge);
    ui.canvas().text(helpButton.x + 8.0f, helpButton.y + 3.0f, "?", theme.title, theme.textScale * 0.78f);
    if(helpHit.pressed) state.helpOpen = !state.helpOpen;

    //-- lik: jedan je gotovo uvijek, pa popis postoji tek kad ima izbora -------------------------
    if(status.characters.empty()){
        ui.status("No rigged character in the scene - import one to preview motion.", theme.warning);
    }else{
        bool selectedExists = false;
        for(const MotionCharacter& character : status.characters)
            selectedExists = selectedExists || state.targetCharacter == character.id;
        if(!selectedExists) state.targetCharacter = status.characters.front().id;
        const MotionCharacter* selectedCharacter = &status.characters.front();
        for(const MotionCharacter& character : status.characters)
            if(character.id == state.targetCharacter){ selectedCharacter = &character; break; }
        if(status.characters.size() == 1) ui.value("Character", selectedCharacter->name);
        else if(ui.disclosure("Character", selectedCharacter->name, &state.targetListOpen)){
            for(const MotionCharacter& character : status.characters){
                const std::string label = character.name + "   /   " + character.path;
                if(ui.selectable(Treadle::fitText(label, area.width - 36.0f, theme.textScale),
                                 state.targetCharacter == character.id)) state.targetCharacter = character.id;
            }
        }
    }

    //-- nacin rada -------------------------------------------------------------------------------
    const int previousFlow = state.flowMode;
    if(ui.tabs({"Create", "Direct", "Review"}, &state.flowMode) && state.flowMode != previousFlow){
        auto savePath = [&](MotionPathDraft& draft){
            draft.enabled = state.rootPathEnabled;
            draft.autoEnd = state.rootPathAutoEnd;
            draft.autoDistance = state.rootPathAutoDistance;
            draft.smooth = state.smoothRootPath;
            draft.pinHeading = state.constrainRootHeading;
            draft.selected = state.selectedRootWaypoint;
            draft.waypoints = state.rootWaypoints;
        };
        auto loadPath = [&](const MotionPathDraft& draft){
            state.rootPathEnabled = draft.enabled;
            state.rootPathAutoEnd = draft.autoEnd;
            state.rootPathAutoDistance = draft.autoDistance;
            state.smoothRootPath = draft.smooth;
            state.constrainRootHeading = draft.pinHeading;
            state.rootWaypoints = draft.waypoints;
            state.selectedRootWaypoint = draft.selected;
        };
        if(previousFlow != 1 && state.flowMode == 1){
            savePath(state.recipePath);
            if(state.directedPath.waypoints.empty()){
                state.directedPath.enabled = true;
                state.directedPath.waypoints.push_back(MotionRootWaypoint{});
                const int last = std::max(1, kimodoMotionLastFrame({state.directedAction}));
                state.directedPath.waypoints.push_back(
                    motionDefaultRootEnd(last, state.firstHeadingAngle, state.defaultPathSpeed));
                state.directedPath.selected = 1;
            }
            loadPath(state.directedPath);
        }else if(previousFlow == 1 && state.flowMode != 1){
            savePath(state.directedPath);
            loadPath(state.recipePath);
        }
    }

    MotionFooterState footer;
    if(state.flowMode == 2){
        drawMotionReview(ui, state, area, status, action);
        return action;
    }
    if(state.flowMode == 1){
        MotionPanelAction directed = drawMotionDirectedFlow(ui, state, area, status, footer);
        directed.compareMode = action.compareMode;
        directed.comparePath = action.comparePath;
        directed.copyNativeNpz = action.copyNativeNpz;
        directed.close = action.close;
        action = directed;
    }else{
        drawMotionCreate(ui, state, area, status, action, footer);
    }

    const Treadle::Rect footerBox{area.x, area.y + area.height - footerHeight, area.width, footerHeight};
    if(drawMotionFooter(ui, footerBox, footer, &state.allowFastPath)){
        if(status.liveRecording) action.stopLiveRecording = true;
        else if(footer.enabled) action.generate = true;
    }
    return action;
}

inline void drawMotionHelp(Treadle::Ui& ui, MotionPanelState& state, const Treadle::Rect& area){
    ui.dock("MOTION HELP", area, &state.helpScroll);
    ui.caption("CREATE");
    ui.hint("Pick a movement, edit the prompt and duration, then Generate (or Enter). Add steps for "
            "a sequence - Kimodo blends them. Idle, walk, crawl and crouch can also be recorded live "
            "with MotionBricks under Options.");
    ui.caption("DIRECT");
    ui.hint("Move the playhead, add a pose key and shape it with the rig in the viewport: drag cyan "
            "hand/foot targets (Alt-drag rotates), orange sets elbow/knee bend, violet turns chest and "
            "head. Drag ROOT to shape the path. Kimodo fills in the motion between keys.");
    ui.caption("REVIEW");
    ui.hint("Click a saved take to load it on the character; right-click to reuse its prompt. "
            "Compare import stages shows where quality is lost: source BVH, rig without IK, rig with IK.");
    ui.caption("CONTACTS");
    ui.hint("Automatic rules follow the movement and prompt: crawl turns off rig foot IK, Kimodo "
            "cleanup stays on. Manual lets you set both.");
    ui.caption("DIAGNOSTICS");
    ui.hint("TERM on the left rail shows the running job and its errors.");
    ui.space(ui.style().spacing);
    if(ui.button("Close help")) state.helpOpen = false;
}

}
