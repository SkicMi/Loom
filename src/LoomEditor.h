#pragma once
//=============================================================================================
// EDITOR: Scene Atlas, centralni viewport, Instrument Deck i vremenska traka.
//
//   +--------------------------------------------------------------+
//   | alatna traka                                                 |
//   +-----+--------------------------------------------------------+
//   | rail | Scene Atlas |             viewport | Instrument Deck |
//   +-----+--------------------------------------------------------+
//   | timeline                                                     |
//   +--------------------------------------------------------------+
//
// RASPORED JE CISTI RACUN: dva side panela ostaju uz viewport, neovisno jedan o drugome.
//=============================================================================================
#include <Treadle/Draw.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <string>
#include <vector>

namespace Loom{

struct EditorLayout{
    Treadle::Rect toolbar;
    Treadle::Rect rail;
    Treadle::Rect media;        //legacy alias for the tool rail
    Treadle::Rect viewport;
    Treadle::Rect hierarchy;    //Scene Atlas dock
    Treadle::Rect properties;   //Instrument Deck dock
    Treadle::Rect timeline;
    Treadle::Rect terminal;
};

inline EditorLayout layoutEditor(float width, float height, bool outlineVisible = true, bool componentsVisible = true,
                                 bool timelineVisible = true, bool terminalVisible = false){
    EditorLayout layout;
    const float toolbarHeight = 40.0f;
    const float timelineHeight = timelineVisible ? std::clamp(height * 0.2f, 110.0f, 190.0f) : 0.0f;
    const float terminalHeight = terminalVisible ? std::min(std::clamp(height * 0.23f, 150.0f, 250.0f),
                                                        std::max(0.0f, height - toolbarHeight - timelineHeight - 170.0f)) : 0.0f;
    const float railWidth = std::min(56.0f, std::max(46.0f, width * 0.045f));
    const float middleHeight = std::max(0.0f, height - toolbarHeight - timelineHeight - terminalHeight);
    const float desiredOutline = std::clamp(width * 0.16f, 214.0f, 264.0f);
    const float desiredComponents = std::clamp(width * 0.205f, 288.0f, 336.0f);
    const float panelGap = 6.0f;
    const int visiblePanels = int(outlineVisible) + int(componentsVisible);
    const float availablePanels = std::max(0.0f, width - railWidth - 360.0f - panelGap * float(visiblePanels));
    const float requestedPanels = (outlineVisible ? desiredOutline : 0.0f) +
                                  (componentsVisible ? desiredComponents : 0.0f);
    const float panelScale = requestedPanels > 0.0f ? std::min(1.0f, availablePanels / requestedPanels) : 1.0f;
    const float outlineWidth = outlineVisible ? desiredOutline * panelScale : 0.0f;
    const float componentsWidth = componentsVisible ? desiredComponents * panelScale : 0.0f;

    layout.toolbar = Treadle::Rect{0.0f, 0.0f, width, toolbarHeight};
    layout.rail = Treadle::Rect{0.0f, toolbarHeight, railWidth, middleHeight};
    layout.media = layout.rail;
    layout.hierarchy = Treadle::Rect{railWidth, toolbarHeight, outlineWidth, middleHeight};
    layout.properties = Treadle::Rect{width - componentsWidth, toolbarHeight, componentsWidth, middleHeight};
    const float viewportX = railWidth + outlineWidth + (outlineVisible ? panelGap : 0.0f);
    const float viewportRight = width - componentsWidth - (componentsVisible ? panelGap : 0.0f);
    layout.viewport = Treadle::Rect{viewportX, toolbarHeight, std::max(0.0f, viewportRight - viewportX), middleHeight};
    layout.terminal = Treadle::Rect{0.0f, toolbarHeight + middleHeight, width, terminalHeight};
    layout.timeline = Treadle::Rect{0.0f, toolbarHeight + middleHeight + terminalHeight, width, timelineHeight};
    return layout;
}

//Snimke u mapi, po imenu
inline std::vector<std::filesystem::path> videosIn(const std::filesystem::path& directory){
    std::vector<std::filesystem::path> found;
    std::error_code error;
    for(const auto& entry : std::filesystem::directory_iterator(directory, error)){
        if(error) break;
        if(!entry.is_regular_file(error)) continue;
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

//Mapa rezultata koju VideoSolve pravi za snimku: uz nju, s nastavkom _loom
inline std::filesystem::path resultFolderFor(const std::filesystem::path& video){
    return video.parent_path() / (video.stem().string() + "_loom");
}

//Obrnuto: snimka uz mapu rezultata, ista imena bez _loom. Prazno kad je nema - rezultat koji je
//netko donio bez snimke se i dalje otvara, samo bez ploce iza kamere
inline std::string plateFor(const std::filesystem::path& resultFolder){
    std::string stem = resultFolder.filename().string();
    if(stem.size() > 5 && stem.substr(stem.size() - 5) == "_loom") stem.resize(stem.size() - 5);
    for(const std::filesystem::path& video : videosIn(resultFolder.parent_path())){
        if(video.stem().string() == stem) return video.string();
    }
    return {};
}

}
