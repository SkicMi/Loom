#pragma once
//=============================================================================================
// EDITOR: Scene workspace ili Animator workspace s viewportom, alatima i vremenskom trakom.
//
//   +--------------------------------------------------------------+
//   | alatna traka                                                 |
//   +-----+--------------------------------------------------------+
//   | rail | Scene Atlas |             viewport | Instrument Deck |
//   | rail | Animator controls | rig viewport | clip inspector      |
//   +-----+--------------------------------------------------------+
//   | timeline                                                     |
//   +--------------------------------------------------------------+
//
// RASPORED: dva radna prostora koriste isti viewport; Animator dodaje motion i clip kontrole.
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
    Treadle::Rect motion;       //Animator workspace controls
    Treadle::Rect timeline;
    Treadle::Rect terminal;
};

//Visina timelinea. Visok je kad je objekt na timelineu otvoren (red za svaku kost): tada treba
//mjesta za redove, a pogled se smanji dok se ne zatvori
inline float editorTimelineHeight(float height, bool timelineVisible, bool timelineTall = false){
    if(!timelineVisible) return 0.0f;
    return timelineTall ? std::clamp(height * 0.4f, 190.0f, 440.0f) : std::clamp(height * 0.2f, 110.0f, 190.0f);
}

inline EditorLayout layoutEditor(float width, float height, bool outlineVisible = true, bool componentsVisible = true,
                                 bool timelineVisible = true, bool terminalVisible = false, bool animatorWorkspace = false,
                                 float railReveal = 1.0f, bool timelineTall = false){
    EditorLayout layout;
    const float toolbarHeight = 40.0f;
    const float timelineHeight = editorTimelineHeight(height, timelineVisible, timelineTall);
    const float terminalHeight = terminalVisible ? std::min(std::clamp(height * 0.23f, 150.0f, 250.0f),
                                                        std::max(0.0f, height - toolbarHeight - timelineHeight - 170.0f)) : 0.0f;
    const float fullRailWidth = std::min(56.0f, std::max(46.0f, width * 0.045f));
    const float railMinWidth = std::min(12.0f, fullRailWidth);
    const float railWidth = railMinWidth + (fullRailWidth - railMinWidth) * std::clamp(railReveal, 0.0f, 1.0f);
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
    if(animatorWorkspace){
        const float gap = 8.0f;
        const float viewportMinimum = 380.0f;
        const float panelBudget = std::max(0.0f, width - railWidth - viewportMinimum - gap * 3.0f);
        const float propertyWidth = std::min(std::clamp(width * 0.19f, 230.0f, 340.0f), panelBudget * 0.42f);
        const float motionWidth = std::min(std::clamp(width * 0.26f, 300.0f, 410.0f),
                                           std::max(220.0f, panelBudget - propertyWidth));
        layout.hierarchy = Treadle::Rect{railWidth, toolbarHeight, 0.0f, middleHeight};
        layout.properties = Treadle::Rect{width - propertyWidth, toolbarHeight, propertyWidth, middleHeight};
        layout.motion = Treadle::Rect{railWidth + gap, toolbarHeight + 6.0f,
                                      motionWidth, std::max(0.0f, middleHeight - 12.0f)};
        const float viewportX = railWidth + motionWidth + gap * 2.0f;
        const float viewportRight = layout.properties.x - gap;
        layout.viewport = Treadle::Rect{viewportX, toolbarHeight + 6.0f,
                                        std::max(0.0f, viewportRight - viewportX),
                                        std::max(0.0f, middleHeight - 12.0f)};
        layout.terminal = Treadle::Rect{0.0f, toolbarHeight + middleHeight, width, terminalHeight};
        layout.timeline = Treadle::Rect{0.0f, toolbarHeight + middleHeight + terminalHeight, width, timelineHeight};
        return layout;
    }
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
