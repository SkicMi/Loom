#pragma once
//=============================================================================================
// EDITOR: raspored prozora i ono sto stupci pokazuju.
//
//   +--------------------------------------------------------------+
//   | alatna traka                                                 |
//   +-----------+--------------------------------+-----------------+
//   |           |                                | hijerarhija     |
//   |  media    |           pogled               |                 |
//   |           |                                +-----------------+
//   |           |                                | svojstva        |
//   +-----------+--------------------------------+-----------------+
//   | timeline                                                     |
//   +--------------------------------------------------------------+
//
// RASPORED JE CISTI RACUN, bez prozora: dobije velicinu, vrati pravokutnike. Zato se da
// provjeriti testom - da se stupci ne preklapaju, da pokriju prozor i da pogled ostane
// upotrebljiv i u malom prozoru. Greska u rasporedu ne rusi nista, samo sakrije pola pogleda
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
    Treadle::Rect media;
    Treadle::Rect viewport;
    Treadle::Rect hierarchy;
    Treadle::Rect properties;
    Treadle::Rect timeline;
};

inline EditorLayout layoutEditor(float width, float height){
    EditorLayout layout;
    const float toolbarHeight = 40.0f;
    const float timelineHeight = std::clamp(height * 0.2f, 110.0f, 190.0f);

    //Stupci su sirine razmjerne prozoru, u granicama; u uskom prozoru se stisnu toliko da pogled
    //zadrzi barem 40 % sirine - pogled je razlog zasto editor postoji
    float side = std::clamp(width * 0.2f, 220.0f, 340.0f);
    if(width - 2.0f * side < width * 0.4f) side = width * 0.3f;

    const float middleTop = toolbarHeight;
    const float middleHeight = std::max(0.0f, height - toolbarHeight - timelineHeight);

    layout.toolbar = Treadle::Rect{0.0f, 0.0f, width, toolbarHeight};
    layout.media = Treadle::Rect{0.0f, middleTop, side, middleHeight};
    layout.viewport = Treadle::Rect{side, middleTop, std::max(0.0f, width - 2.0f * side), middleHeight};

    const float hierarchyHeight = std::floor(middleHeight * 0.5f);
    layout.hierarchy = Treadle::Rect{width - side, middleTop, side, hierarchyHeight};
    layout.properties = Treadle::Rect{width - side, middleTop + hierarchyHeight, side, middleHeight - hierarchyHeight};
    layout.timeline = Treadle::Rect{0.0f, middleTop + middleHeight, width, height - middleTop - middleHeight};
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

}
