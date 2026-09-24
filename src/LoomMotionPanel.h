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
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace Loom{

struct MotionAction{
    std::string prompt;
    float duration = 4.0f;          //sekunde
};

struct MotionRequest{
    std::vector<MotionAction> actions;
    int seed = -1;                  //-1: bez sjemena, svaki put drukcije
    int diffusionSteps = 100;
    bool footCleanup = true;
};

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

//Naredba za kimodo_gen: opisi spojeni tockom, trajanja razmakom (Kimodo tako zna vise radnji)
inline std::string buildMotionCommand(const std::filesystem::path& executable, const MotionRequest& request,
                                      const std::filesystem::path& outputStem){
    const std::vector<MotionAction> actions = filledActions(request.actions);
    std::string prompts, durations;
    for(size_t i = 0; i < actions.size(); ++i){
        if(i){ prompts += ". "; durations += " "; }
        prompts += actions[i].prompt;
        char text[16];
        std::snprintf(text, sizeof(text), "%.2f", double(actions[i].duration));
        durations += text;
    }
    std::string command = "TEXT_ENCODER_MODE=local TEXT_ENCODER_DEVICE=cpu " + shellQuoteArgument(executable.string()) + " " +
                          shellQuoteArgument(prompts) + " --model 'Kimodo-SOMA-RP-v1.1' --duration " + shellQuoteArgument(durations) +
                          " --num_samples 1 --diffusion_steps " + std::to_string(std::clamp(request.diffusionSteps, 10, 500)) +
                          " --output " + shellQuoteArgument(outputStem.string()) + " --bvh --bvh_standard_tpose";
    if(request.seed >= 0) command += " --seed " + std::to_string(request.seed);
    if(!request.footCleanup) command += " --no-postprocess";
    return command;
}

//Uz BVH: opis i trajanje po retku, odvojeni tabom
inline void writeMotionSidecar(const std::filesystem::path& outputStem, const MotionRequest& request){
    std::ofstream file(outputStem.string() + ".txt");
    for(const MotionAction& a : filledActions(request.actions)) file << a.prompt << '\t' << a.duration << '\n';
}

inline std::vector<MotionHistoryEntry> motionHistory(const std::filesystem::path& directory, size_t limit = 12){
    std::vector<std::pair<std::filesystem::file_time_type, std::filesystem::path>> found;
    std::error_code error;
    for(const auto& entry : std::filesystem::directory_iterator(directory, error)){
        if(error) break;
        if(entry.path().extension() == ".bvh") found.push_back({entry.last_write_time(error), entry.path()});
    }
    std::sort(found.begin(), found.end(), [](const auto& a, const auto& b){ return a.first > b.first; });
    std::vector<MotionHistoryEntry> out;
    for(const auto& [time, path] : found){
        if(out.size() >= limit) break;
        MotionHistoryEntry item;
        item.bvh = path;
        std::ifstream sidecar(path.parent_path() / (path.stem().string() + ".txt"));
        std::string line;
        while(std::getline(sidecar, line)){
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
        }
        out.push_back(item);
    }
    return out;
}

//Gotovi opisi: engleski, jer ga Kimodov enkoder teksta razumije
struct MotionPreset{ const char* label; const char* prompt; };
inline const MotionPreset motionPresets[] = {
    {"hod", "a person walks forward"},
    {"trcanje", "a person jogs forward"},
    {"skok", "a person jumps up in place"},
    {"sjedne", "a person sits down on a chair"},
    {"mase", "a person waves with the right hand"},
    {"ples", "a person dances happily"},
    {"okret", "a person turns around to the left"},
    {"pad", "a person stumbles and falls down"},
};

struct MotionPanelState{
    bool open = false;
    std::vector<MotionAction> actions{MotionAction{}};
    int activeAction = 0;                   //u koju radnju ide gotovi opis
    bool fixedSeed = false;
    float seed = 42.0f;
    float quality = 100.0f;                 //koraci difuzije
    bool footCleanup = true;
    std::vector<MotionHistoryEntry> history;
    std::filesystem::path historyFrom;
    std::chrono::steady_clock::time_point historyRead{};

    MotionRequest request() const{
        MotionRequest r;
        r.actions = actions;
        r.seed = fixedSeed ? std::max(0, int(std::lround(seed))) : -1;
        r.diffusionSteps = int(std::lround(quality));
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

    ui.dock("Pokret iz teksta  -  NVIDIA Kimodo", area, &scroll);
    if(!status.runnerReady){
        ui.label("Kimodo nije instaliran:");
        ui.label("./tools/weavermotion/setup.sh");
        ui.separator();
    }

    //-- radnje ---------------------------------------------------------------------------------
    Treadle::Ui::TextFieldConfig field;
    field.lines = 3;
    field.maxLength = 600;
    field.placeholder = "opisi pokret na engleskom, npr. a person walks forward and waves";
    float total = 0.0f;
    int removeAt = -1, moveUp = -1;
    for(size_t i = 0; i < state.actions.size(); ++i){
        MotionAction& a = state.actions[i];
        const bool active = int(i) == state.activeAction;
        ui.label(state.actions.size() > 1 ? "RADNJA " + std::to_string(i + 1) + (active ? "  <" : "") : "OPIS POKRETA");
        const Treadle::Ui::TextFieldResult result = ui.textField("radnja" + std::to_string(i), &a.prompt, field);
        if(result.focused) state.activeAction = int(i);
        if(result.submitted) action.generate = true;
        ui.slider(std::string("trajanje ") + std::to_string(i + 1), &a.duration, 1.0f, 10.0f, " s");
        total += a.duration;
        if(state.actions.size() > 1){
            //Prva radnja ne moze gore, pa ima samo "ukloni"
            if(i == 0){
                if(ui.buttonRow({"ukloni"}) == 0) removeAt = 0;
            }else{
                const int clicked = ui.buttonRow({"gore", "ukloni"});
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
    if(state.actions.size() < 6 && ui.button("+ dodaj radnju (pa zatim...)")){
        state.actions.push_back(MotionAction{"", 3.0f});
        state.activeAction = int(state.actions.size()) - 1;
        ui.focusTextField("radnja" + std::to_string(state.actions.size() - 1));
    }

    //-- gotovi opisi: u aktivnu radnju --------------------------------------------------------
    ui.label("BRZI OPISI");
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
            ui.focusTextField("radnja" + std::to_string(state.activeAction));
        }
    }

    //-- postavke -------------------------------------------------------------------------------
    ui.separator();
    char text[96];
    std::snprintf(text, sizeof(text), "%.1f s, %zu %s", double(total), filledActions(state.actions).size(),
                  filledActions(state.actions).size() == 1 ? "radnja" : "radnje");
    ui.value("ukupno", text);
    ui.slider("kvaliteta (koraci)", &state.quality, 20.0f, 200.0f);
    ui.checkbox("isto sjeme = isti pokret", &state.fixedSeed);
    if(state.fixedSeed) ui.dragFloat("sjeme", &state.seed, 0.2f);
    ui.checkbox("cisti klizanje stopala", &state.footCleanup);

    //-- generiranje ----------------------------------------------------------------------------
    ui.separator();
    const bool ready = status.runnerReady && !status.running && !status.otherJob && !filledActions(state.actions).empty();
    if(status.running){
        ui.value("Kimodo radi", Loom::humanTime(status.elapsed));
        ui.label(Treadle::fitText(status.lastLine.empty() ? "pokrece se..." : status.lastLine, area.width - 30.0f, theme.textScale));
        ui.label("(prvo pokretanje skida model, ~17 GB)");
    }else if(status.otherJob){
        ui.label("Ceka: tece drugi posao (solve ili trening).");
    }
    if(ui.button(ready ? "GENERIRAJ  (Enter)" : status.running ? "generira se..." : "GENERIRAJ  (upisi opis)")){
        if(ready) action.generate = true;
    }
    if(!ready) action.generate = false;

    //-- povijest ---------------------------------------------------------------------------------
    ui.separator();
    ui.label("PROSLI POKRETI");
    ui.label("klik uvozi, desni klik vraca opis");
    if(state.history.empty()) ui.label("(jos nista u " + status.historyDirectory.filename().string() + ")");
    for(size_t i = 0; i < state.history.size(); ++i){
        const MotionHistoryEntry& item = state.history[i];
        if(ui.selectable(Treadle::fitText(item.summary, area.width - 40.0f, theme.textScale), false)) action.importPath = item.bvh;
        if(ui.rightClicked() && !item.actions.empty()){
            state.actions = item.actions;
            state.activeAction = 0;
            ui.focusTextField("radnja0");
        }
    }
    ui.separator();
    ui.label("Lik je zasad Kimodo kostur (77 zglobova).");
    if(!status.characterNote.empty()) ui.label(status.characterNote + " - jos se ne deformira.");
    if(ui.button("Zatvori (Esc)")) action.close = true;
    return action;
}

}
