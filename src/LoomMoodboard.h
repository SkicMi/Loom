#pragma once

#include <Treadle/Ui.h>
#include <Spool/ImageFile.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace Loom{

enum class MoodboardKind : uint8_t{ Note, Todo, Image };

struct MoodboardPreview{
    std::string source;
    uint32_t width = 0, height = 0;
    uint32_t originalWidth = 0, originalHeight = 0;
    std::vector<Treadle::Color> pixels;
    bool attempted = false;
    std::string error;
};

struct MoodboardCard{
    uint64_t id = 0;
    MoodboardKind kind = MoodboardKind::Note;
    std::string title;
    std::string body;
    std::string source;
    bool done = false;
    float x = 0.04f, y = 0.0f;
    uint8_t variant = 0;
    MoodboardPreview preview;
};

struct MoodboardLink{
    uint64_t id = 0;
    uint64_t from = 0, to = 0;
    bool directed = true;
    std::string label;
};

struct MoodboardState{
    bool open = false;
    float zoom = 1.25f;
    float panX = 0.0f, panY = 0.0f;
    uint64_t nextId = 1, nextLinkId = 1;
    uint64_t imagePickerFor = 0;
    uint64_t contextCardId = 0, contextLinkId = 0;
    uint64_t selectedCardId = 0, connectingFromId = 0;
    uint64_t draggingCardId = 0;
    float contextX = 0.0f, contextY = 0.0f;
    float dragOffsetX = 0.0f, dragOffsetY = 0.0f;
    float panLastX = 0.0f, panLastY = 0.0f;
    bool dragChanged = false, panDragging = false, panChanged = false;
    std::filesystem::path storagePath;
    std::vector<MoodboardCard> cards;
    std::vector<MoodboardLink> links;
    std::string status;
};

inline std::filesystem::path moodboardStoragePath(const std::filesystem::path& project,
                                                  const std::filesystem::path& home){
    if(!project.empty()) return std::filesystem::path(project.string() + ".moodboard");
    return (home.empty() ? std::filesystem::current_path() : home) / ".loom-moodboard";
}

inline std::string moodboardHex(const std::string& value){
    static const char digits[] = "0123456789abcdef";
    std::string result;
    result.reserve(value.size() * 2);
    for(unsigned char byte : value){
        result.push_back(digits[byte >> 4]);
        result.push_back(digits[byte & 0x0f]);
    }
    return result;
}

inline bool moodboardUnhex(const std::string& value, std::string& result){
    if(value.size() % 2 != 0) return false;
    auto digit = [](char c) -> int{
        if(c >= '0' && c <= '9') return c - '0';
        if(c >= 'a' && c <= 'f') return c - 'a' + 10;
        if(c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    };
    result.clear();
    result.reserve(value.size() / 2);
    for(size_t i = 0; i < value.size(); i += 2){
        const int high = digit(value[i]), low = digit(value[i + 1]);
        if(high < 0 || low < 0) return false;
        result.push_back(char((high << 4) | low));
    }
    return true;
}

inline bool saveMoodboard(MoodboardState& state, const std::filesystem::path& target = {}){
    const std::filesystem::path path = target.empty() ? state.storagePath : target;
    if(path.empty()) return false;
    std::error_code error;
    if(!path.parent_path().empty()) std::filesystem::create_directories(path.parent_path(), error);
    if(error){ state.status = "Could not create the moodboard folder: " + error.message(); return false; }

    const std::filesystem::path temporary = path.string() + ".tmp";
    {
        std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
        if(!file){ state.status = "Could not save the moodboard."; return false; }
        file << "LOOM_MOODBOARD 4\n" << state.nextId << ' ' << state.nextLinkId << ' '
             << state.zoom << ' ' << state.panX << ' ' << state.panY << '\n';
        for(const MoodboardCard& card : state.cards){
            file << "CARD\t" << card.id << '\t' << unsigned(card.kind) << '\t' << (card.done ? 1 : 0) << '\t'
                 << moodboardHex(card.title) << '\t' << moodboardHex(card.body) << '\t'
                 << moodboardHex(card.source) << '\t' << card.x << '\t' << card.y << '\t'
                 << unsigned(card.variant) << '\n';
        }
        for(const MoodboardLink& link : state.links){
            file << "LINK\t" << link.id << '\t' << link.from << '\t' << link.to << '\t'
                 << (link.directed ? 1 : 0) << '\t' << moodboardHex(link.label) << '\n';
        }
        file.flush();
        if(!file){ state.status = "Could not write the moodboard file."; return false; }
    }
    std::filesystem::rename(temporary, path, error);
    if(error){
        std::error_code ignored;
        std::filesystem::remove(temporary, ignored);
        state.status = "Could not finish saving the moodboard: " + error.message();
        return false;
    }
    state.storagePath = path;
    state.status.clear();
    return true;
}

inline void loadMoodboard(MoodboardState& state, const std::filesystem::path& path){
    if(path.empty() || state.storagePath == path) return;
    state.storagePath = path;
    state.cards.clear();
    state.nextId = 1;
    state.zoom = 1.25f;
    state.panX = state.panY = 0.0f;
    state.nextLinkId = 1;
    state.imagePickerFor = 0;
    state.contextCardId = state.contextLinkId = 0;
    state.selectedCardId = state.connectingFromId = 0;
    state.draggingCardId = 0;
    state.panDragging = state.panChanged = false;
    state.links.clear();
    state.status.clear();

    std::ifstream file(path, std::ios::binary);
    if(!file) return;
    std::string header;
    std::getline(file, header);
    const bool versionOne = header == "LOOM_MOODBOARD 1";
    const bool versionTwo = header == "LOOM_MOODBOARD 2";
    const bool versionThree = header == "LOOM_MOODBOARD 3";
    const bool versionFour = header == "LOOM_MOODBOARD 4";
    if(!versionOne && !versionTwo && !versionThree && !versionFour){
        state.status = "This moodboard file has an unsupported format.";
        return;
    }
    if(versionFour){
        if(!(file >> state.nextId >> state.nextLinkId >> state.zoom >> state.panX >> state.panY)){
            state.nextId = 1; state.nextLinkId = 1; state.zoom = 1.25f; state.panX = state.panY = 0.0f;
        }
        state.zoom = std::clamp(state.zoom, 0.70f, 1.80f);
    }else if(versionThree){
        if(!(file >> state.nextId >> state.zoom)) state.zoom = 1.25f;
        state.zoom = std::clamp(state.zoom, 0.70f, 1.80f);
    }else file >> state.nextId;
    std::string line;
    std::getline(file, line);
    size_t loadedIndex = 0;
    while(std::getline(file, line)){
        std::vector<std::string> fields;
        size_t first = 0;
        for(;;){
            const size_t tab = line.find('\t', first);
            fields.push_back(line.substr(first, tab == std::string::npos ? tab : tab - first));
            if(tab == std::string::npos) break;
            first = tab + 1;
        }
        if(versionFour && !fields.empty() && fields[0] == "LINK"){
            if(fields.size() != 6u) continue;
            MoodboardLink link;
            unsigned directed = 1;
            std::istringstream linkNumbers(fields[1] + " " + fields[2] + " " + fields[3] + " " + fields[4]);
            if(!(linkNumbers >> link.id >> link.from >> link.to >> directed) || link.from == link.to) continue;
            if(!moodboardUnhex(fields[5], link.label)) continue;
            link.directed = directed != 0;
            state.links.push_back(std::move(link));
            state.nextLinkId = std::max(state.nextLinkId, state.links.back().id + 1);
            continue;
        }
        const size_t cardOffset = versionFour ? 1u : 0u;
        if(versionFour && (fields.empty() || fields[0] != "CARD")) continue;
        if(fields.size() != (versionOne ? 6u : versionFour ? 10u : 9u)) continue;
        uint64_t id = 0;
        unsigned kind = 0, done = 0;
        std::istringstream numbers(fields[cardOffset] + " " + fields[cardOffset + 1] + " " + fields[cardOffset + 2]);
        if(!(numbers >> id >> kind >> done)) continue;
        std::string title, body, source;
        if(kind > unsigned(MoodboardKind::Image) ||
           !moodboardUnhex(fields[cardOffset + 3], title) || !moodboardUnhex(fields[cardOffset + 4], body) ||
           !moodboardUnhex(fields[cardOffset + 5], source)) continue;
        MoodboardCard card;
        card.id = id;
        card.kind = MoodboardKind(kind);
        card.done = done != 0;
        card.title = std::move(title);
        card.body = std::move(body);
        card.source = std::move(source);
        if(versionFour){
            unsigned variant = 0;
            std::istringstream position(fields[7] + " " + fields[8] + " " + fields[9]);
            if(!(position >> card.x >> card.y >> variant)) continue;
            card.variant = uint8_t(variant % 5u);
        }else if(!versionOne){
            unsigned variant = 0;
            std::istringstream position(fields[6] + " " + fields[7] + " " + fields[8]);
            if(!(position >> card.x >> card.y >> variant)) continue;
            card.x *= 1000.0f;
            card.y *= 700.0f;
            card.variant = uint8_t(variant % 5u);
        }else{
            card.x = (0.04f + float(loadedIndex % 3u) * 0.30f) * 1000.0f;
            card.y = float(loadedIndex / 3u) * 210.0f;
            card.variant = uint8_t(loadedIndex % 5u);
        }
        card.x = std::clamp(card.x, -1000000.0f, 1000000.0f);
        card.y = std::clamp(card.y, -1000000.0f, 1000000.0f);
        state.cards.push_back(std::move(card));
        ++loadedIndex;
        state.nextId = std::max(state.nextId, id + 1);
    }
    if(versionFour){
        state.panX = std::clamp(state.panX, -1000000.0f, 1000000.0f);
        state.panY = std::clamp(state.panY, -1000000.0f, 1000000.0f);
        state.links.erase(std::remove_if(state.links.begin(), state.links.end(), [&](const MoodboardLink& link){
            const auto hasCard = [&](uint64_t id){
                return std::any_of(state.cards.begin(), state.cards.end(), [id](const MoodboardCard& card){ return card.id == id; });
            };
            return !hasCard(link.from) || !hasCard(link.to);
        }), state.links.end());
    }
}

inline std::filesystem::path resolveMoodboardImage(const MoodboardState& state, const std::string& source){
    std::filesystem::path path(source);
    if(path.is_relative()) path = state.storagePath.parent_path() / path;
    std::error_code error;
    const std::filesystem::path absolute = std::filesystem::absolute(path, error);
    return (error ? path : absolute).lexically_normal();
}

inline void loadMoodboardPreview(MoodboardCard& card, const MoodboardState& state){
    if(card.preview.attempted && card.preview.source == card.source) return;
    card.preview = MoodboardPreview{};
    card.preview.source = card.source;
    card.preview.attempted = true;
    if(card.source.empty()) return;
    try{
        const Spool::Image image = Spool::loadImage(resolveMoodboardImage(state, card.source).string());
        card.preview.originalWidth = image.width;
        card.preview.originalHeight = image.height;
        if(!image.isValid()){
            card.preview.error = "The image file is empty.";
            return;
        }
        const float scale = std::min(96.0f / float(image.width), 64.0f / float(image.height));
        card.preview.width = std::max(1u, uint32_t(std::lround(float(image.width) * scale)));
        card.preview.height = std::max(1u, uint32_t(std::lround(float(image.height) * scale)));
        card.preview.pixels.resize(size_t(card.preview.width) * card.preview.height);
        for(uint32_t y = 0; y < card.preview.height; ++y){
            const uint32_t sourceY = std::min(image.height - 1,
                uint32_t((uint64_t(y) * image.height) / card.preview.height));
            for(uint32_t x = 0; x < card.preview.width; ++x){
                const uint32_t sourceX = std::min(image.width - 1,
                    uint32_t((uint64_t(x) * image.width) / card.preview.width));
                const size_t pixel = (size_t(sourceY) * image.width + sourceX) * 4;
                card.preview.pixels[size_t(y) * card.preview.width + x] = {
                    float(image.pixels[pixel]) / 255.0f,
                    float(image.pixels[pixel + 1]) / 255.0f,
                    float(image.pixels[pixel + 2]) / 255.0f,
                    1.0f
                };
            }
        }
    }catch(const std::exception& error){
        card.preview.error = error.what();
    }
}

inline void addMoodboardCard(MoodboardState& state, MoodboardKind kind, float x = -1.0f, float y = -1.0f){
    MoodboardCard card;
    card.id = state.nextId++;
    card.kind = kind;
    const size_t index = state.cards.size();
    card.x = x < 0.0f ? 40.0f + float(index % 3u) * 320.0f : x;
    card.y = y < 0.0f ? float(index / 3u) * 300.0f : y;
    card.variant = uint8_t((card.id - 1u) % 5u);
    card.title = kind == MoodboardKind::Todo ? "A small next step" :
                 kind == MoodboardKind::Image ? "Untitled image" : "Untitled note";
    state.cards.push_back(std::move(card));
    saveMoodboard(state);
}

inline float moodboardStickerHeight(const MoodboardCard& card, float zoom = 1.0f){
    if(card.kind == MoodboardKind::Todo) return 245.0f * zoom;
    if(card.kind == MoodboardKind::Image) return 430.0f * zoom;
    return 265.0f * zoom;
}

inline Treadle::Color moodboardStickerAccent(uint8_t variant){
    static const Treadle::Color colours[] = {
        {0.95f, 0.68f, 0.35f, 1.0f}, {0.54f, 0.78f, 0.72f, 1.0f},
        {0.84f, 0.61f, 0.82f, 1.0f}, {0.83f, 0.78f, 0.45f, 1.0f},
        {0.57f, 0.69f, 0.91f, 1.0f}
    };
    return colours[variant % 5u];
}

inline bool drawMoodboard(Treadle::Ui& ui, MoodboardState& state,
                          const std::vector<std::filesystem::path>& mediaImages,
                          const std::filesystem::path& mediaDirectory,
                          const Treadle::Rect& area){
    if(!state.open) return false;
    Treadle::Theme& theme = ui.style();
    constexpr float preferredCardWidth = 286.0f;
    state.zoom = std::clamp(state.zoom, 0.70f, 1.80f);
    const float boardTop = area.y + 210.0f;
    const float cardWidth = std::max(190.0f, std::min(preferredCardWidth * state.zoom, area.width - 20.0f));
    auto positionFor = [&](const MoodboardCard& card){
        return Treadle::Rect{area.x + 8.0f + (card.x - state.panX) * state.zoom,
                             boardTop + (card.y - state.panY) * state.zoom,
                             cardWidth, moodboardStickerHeight(card, state.zoom)};
    };

    const Treadle::Ui::Region board = ui.region("moodboard-canvas", area);
    if(board.rightPressed){
        state.contextCardId = state.contextLinkId = 0;
        state.contextX = board.mouseX;
        state.contextY = board.mouseY;
        ui.openMenuAt("moodboard-context", board.mouseX, board.mouseY);
    }
    if(board.wheel != 0.0f){
        state.zoom = std::clamp(state.zoom + board.wheel * 0.08f, 0.70f, 1.80f);
        saveMoodboard(state);
    }

    bool pointerOnSticker = false;
    for(const MoodboardCard& card : state.cards){
        const Treadle::Rect box = positionFor(card);
        const Treadle::Rect hit{box.x - 16.0f * state.zoom, box.y,
                                box.width + 32.0f * state.zoom, box.height};
        if(hit.contains(board.mouseX, board.mouseY)){ pointerOnSticker = true; break; }
    }
    if(board.pressed && board.mouseY >= boardTop && !pointerOnSticker){
        state.panDragging = true;
        state.panChanged = false;
        state.panLastX = board.mouseX;
        state.panLastY = board.mouseY;
        state.selectedCardId = 0;
        state.connectingFromId = 0;
    }
    if(state.panDragging && board.held){
        const float dx = board.mouseX - state.panLastX;
        const float dy = board.mouseY - state.panLastY;
        if(std::abs(dx) + std::abs(dy) > 0.01f){
            state.panX -= dx / state.zoom;
            state.panY -= dy / state.zoom;
            state.panChanged = true;
        }
        state.panLastX = board.mouseX;
        state.panLastY = board.mouseY;
    }else if(state.panDragging){
        if(state.panChanged) saveMoodboard(state);
        state.panDragging = false;
        state.panChanged = false;
    }

    ui.dock("MOODBOARD   /   sticker canvas", area);
    ui.label("Drag headers to move  /  drag empty canvas to pan  /  connect dots  /  scroll to zoom");
    size_t tasks = 0, completed = 0;
    for(const MoodboardCard& card : state.cards){
        if(card.kind == MoodboardKind::Todo){ ++tasks; completed += card.done ? 1u : 0u; }
    }
    ui.value("TO-DO", std::to_string(completed) + " / " + std::to_string(tasks) + " complete");
    const int toolbarAction = ui.buttonRow({"+ Text", "+ To-do", "+ Image", "Close  M"});
    if(toolbarAction == 0 || toolbarAction == 1 || toolbarAction == 2){
        const MoodboardKind kind = toolbarAction == 1 ? MoodboardKind::Todo :
                                   toolbarAction == 2 ? MoodboardKind::Image : MoodboardKind::Note;
        addMoodboardCard(state, kind);
        const MoodboardCard& added = state.cards.back();
        ui.focusTextField((kind == MoodboardKind::Image ? "moodboard-image-" : "moodboard-title-") + std::to_string(added.id));
    }else if(toolbarAction == 3){
        return true;
    }
    const int zoomAction = ui.buttonRow({"Zoom -", "Reset " + std::to_string(int(std::lround(state.zoom * 100.0f))) + "%", "Zoom +"});
    if(zoomAction == 0 || zoomAction == 2){
        state.zoom = std::clamp(state.zoom + (zoomAction == 2 ? 0.10f : -0.10f), 0.70f, 1.80f);
        saveMoodboard(state);
    }else if(zoomAction == 1){
        state.zoom = 1.25f;
        saveMoodboard(state);
    }
    if(!state.status.empty()) ui.label(state.status);

    auto addAtPointer = [&](MoodboardKind kind){
        const float x = std::max(0.0f, state.panX + (state.contextX - area.x - 8.0f) / state.zoom);
        const float y = std::max(0.0f, state.panY + (state.contextY - boardTop) / state.zoom);
        addMoodboardCard(state, kind, x, y);
        if(kind == MoodboardKind::Image) state.imagePickerFor = state.cards.back().id;
        else ui.focusTextField("moodboard-title-" + std::to_string(state.cards.back().id));
    };

    if(state.cards.empty()){
        Treadle::DrawList& canvas = ui.canvas();
        const Treadle::Rect hint{area.x + 28.0f, boardTop + 26.0f, std::min(area.width - 56.0f, 430.0f), 76.0f};
        canvas.rect(hint, Treadle::Color{theme.panel.r, theme.panel.g, theme.panel.b, 0.74f});
        canvas.outline(hint, 1.0f, theme.panelEdge);
        canvas.text(hint.x + 14.0f, hint.y + 14.0f, "RIGHT CLICK TO ADD YOUR FIRST STICKER", theme.title, theme.textScale);
        canvas.text(hint.x + 14.0f, hint.y + 43.0f, "Text, to-do notes and image references live here.", theme.dim, theme.textScale * 0.82f);
    }

    theme.padding *= state.zoom;
    theme.spacing *= state.zoom;
    theme.rowHeight *= state.zoom;
    theme.textScale *= state.zoom;
    theme.widgetRadius *= state.zoom;

    auto findCard = [&](uint64_t id) -> MoodboardCard*{
        const auto found = std::find_if(state.cards.begin(), state.cards.end(), [id](const MoodboardCard& card){ return card.id == id; });
        return found == state.cards.end() ? nullptr : &*found;
    };
    auto portX = [&](const MoodboardCard& card, bool output){
        const Treadle::Rect box = positionFor(card);
        return output ? box.x + box.width + 2.0f : box.x - 12.0f;
    };
    auto portY = [&](const MoodboardCard& card){
        return positionFor(card).y + 58.0f * state.zoom;
    };
    auto cubicPoint = [](float p0, float p1, float p2, float p3, float t){
        const float u = 1.0f - t;
        return u*u*u*p0 + 3.0f*u*u*t*p1 + 3.0f*u*t*t*p2 + t*t*t*p3;
    };

    Treadle::DrawList& boardCanvas = ui.canvas();
    bool contextLinkHit = false;
    for(const MoodboardLink& link : state.links){
        const MoodboardCard* from = findCard(link.from);
        const MoodboardCard* to = findCard(link.to);
        if(!from || !to) continue;
        const float x0 = portX(*from, true) + 5.0f;
        const float y0 = portY(*from) + 5.0f;
        const float x3 = portX(*to, false) + 5.0f;
        const float y3 = portY(*to) + 5.0f;
        const float bend = std::max(42.0f * state.zoom, std::abs(x3 - x0) * 0.42f);
        const float x1 = x0 + bend;
        const float x2 = x3 - bend;
        const Treadle::Color ink = link.id == state.contextLinkId
            ? Treadle::Color{0.96f, 0.77f, 0.37f, 0.96f}
            : Treadle::Color{0.68f, 0.79f, 0.66f, 0.82f};
        float previousX = x0, previousY = y0;
        constexpr int segments = 18;
        for(int segment = 1; segment <= segments; ++segment){
            const float t = float(segment) / float(segments);
            const float x = cubicPoint(x0, x1, x2, x3, t);
            const float y = cubicPoint(y0, y0, y3, y3, t);
            boardCanvas.line(previousX, previousY, x, y, 2.0f * state.zoom, ink);
            const float minX = std::min(previousX, x) - 8.0f * state.zoom;
            const float minY = std::min(previousY, y) - 8.0f * state.zoom;
            const Treadle::Rect hitBox{minX, minY, std::max(16.0f * state.zoom, std::abs(x - previousX) + 16.0f * state.zoom),
                                       std::max(16.0f * state.zoom, std::abs(y - previousY) + 16.0f * state.zoom)};
            const Treadle::Ui::Region linkHit = ui.region("moodboard-link-hit-" + std::to_string(link.id) + "-" + std::to_string(segment), hitBox);
            if(linkHit.rightPressed){
                state.contextLinkId = link.id;
                state.contextCardId = 0;
                ui.openMenuAt("moodboard-context", linkHit.mouseX, linkHit.mouseY);
                contextLinkHit = true;
            }
            previousX = x;
            previousY = y;
        }
        if(link.directed){
            const float dx = x3 - x2;
            const float length = std::max(1.0f, std::abs(dx));
            const float ux = dx / length, uy = 0.0f;
            const float size = 8.0f * state.zoom;
            boardCanvas.line(x3, y3, x3 - ux*size - uy*size*0.58f, y3 - uy*size + ux*size*0.58f, 2.0f * state.zoom, ink);
            boardCanvas.line(x3, y3, x3 - ux*size + uy*size*0.58f, y3 - uy*size - ux*size*0.58f, 2.0f * state.zoom, ink);
        }
        if(!link.label.empty()){
            const float mx = cubicPoint(x0, x1, x2, x3, 0.5f);
            const float my = cubicPoint(y0, y0, y3, y3, 0.5f);
            boardCanvas.text(mx + 5.0f, my - 9.0f, link.label, theme.text, theme.textScale * 0.68f);
        }
    }
    if(state.connectingFromId){
        const MoodboardCard* from = findCard(state.connectingFromId);
        if(from){
            const float x0 = portX(*from, true) + 5.0f;
            const float y0 = portY(*from) + 5.0f;
            boardCanvas.line(x0, y0, board.mouseX, board.mouseY, 2.0f * state.zoom,
                             Treadle::Color{0.96f, 0.77f, 0.37f, 0.78f});
        }
    }

    int removeCardAtEnd = -1;
    for(size_t index = 0; index < state.cards.size(); ++index){
        MoodboardCard& card = state.cards[index];
        const std::string suffix = std::to_string(card.id);
        const Treadle::Rect box = positionFor(card);
        const Treadle::Ui::Region hit = ui.region("moodboard-card-hit-" + suffix, box);
        if(hit.rightPressed && !contextLinkHit){
            state.contextCardId = card.id;
            state.contextLinkId = 0;
            state.contextX = hit.mouseX;
            state.contextY = hit.mouseY;
            ui.openMenuAt("moodboard-context", hit.mouseX, hit.mouseY);
        }
        if(hit.pressed) state.selectedCardId = card.id;

        ui.panel("STICKER   /   " + suffix, box.x, box.y, box.width);
        const float connectorY = box.y + 58.0f * state.zoom;
        const Treadle::Rect inputBox{box.x - 12.0f * state.zoom, connectorY,
                                     12.0f * state.zoom, 12.0f * state.zoom};
        const Treadle::Rect outputBox{box.x + box.width, connectorY,
                                      12.0f * state.zoom, 12.0f * state.zoom};
        const Treadle::Ui::Region input = ui.region("moodboard-port-in-" + suffix, inputBox);
        const Treadle::Ui::Region output = ui.region("moodboard-port-out-" + suffix, outputBox);
        if(input.pressed){
            if(state.connectingFromId && state.connectingFromId != card.id){
                const bool duplicate = std::any_of(state.links.begin(), state.links.end(), [&](const MoodboardLink& link){
                    return link.from == state.connectingFromId && link.to == card.id;
                });
                if(!duplicate){
                    state.links.push_back({state.nextLinkId++, state.connectingFromId, card.id, true, ""});
                    saveMoodboard(state);
                }
                state.connectingFromId = 0;
            }
            state.selectedCardId = card.id;
        }
        if(output.pressed){
            state.selectedCardId = card.id;
            state.connectingFromId = state.connectingFromId == card.id ? 0 : card.id;
        }
        const Treadle::Color portColour = state.connectingFromId == card.id
            ? Treadle::Color{0.98f, 0.80f, 0.36f, 1.0f}
            : moodboardStickerAccent(card.variant);
        ui.canvas().rect(inputBox, moodboardStickerAccent(card.variant));
        ui.canvas().rect(outputBox, portColour);

        const Treadle::Ui::Region drag = ui.region("moodboard-card-drag-" + suffix,
                                                    {box.x, box.y, box.width, 42.0f * state.zoom});
        if(drag.pressed){
            state.draggingCardId = card.id;
            state.dragOffsetX = drag.mouseX - box.x;
            state.dragOffsetY = drag.mouseY - box.y;
            state.dragChanged = false;
        }
        if(drag.held && state.draggingCardId == card.id){
            const float newX = state.panX + (drag.mouseX - state.dragOffsetX - area.x - 8.0f) / state.zoom;
            const float newY = state.panY + (drag.mouseY - state.dragOffsetY - boardTop) / state.zoom;
            if(std::abs(newX - card.x) > 0.01f || std::abs(newY - card.y) > 0.01f){
                card.x = newX;
                card.y = newY;
                state.dragChanged = true;
            }
        }else if(state.draggingCardId == card.id){
            if(state.dragChanged) saveMoodboard(state);
            state.draggingCardId = 0;
            state.dragChanged = false;
        }

        const char* kindName = card.kind == MoodboardKind::Todo ? "TO-DO" :
                               card.kind == MoodboardKind::Image ? "IMAGE" : "TEXT";
        ui.label(std::string(kindName) + "   /   " + suffix);
        const Treadle::Rect kindRow = ui.lastRowRect();
        ui.canvas().rect(kindRow.x + kindRow.width - 17.0f, kindRow.y + 4.0f, 10.0f, 10.0f,
                         moodboardStickerAccent(card.variant));

        bool remove = false;
        if(card.kind == MoodboardKind::Todo && ui.checkbox("Mark complete", &card.done)) saveMoodboard(state);

        Treadle::Ui::TextFieldConfig titleConfig;
        titleConfig.placeholder = card.kind == MoodboardKind::Todo ? "What needs doing?" :
                                  card.kind == MoodboardKind::Image ? "Image title" : "Give this note a title";
        titleConfig.maxLength = 1024;
        if(ui.textField("moodboard-title-" + suffix, &card.title, titleConfig).changed) saveMoodboard(state);

        if(card.kind == MoodboardKind::Note || card.kind == MoodboardKind::Todo){
            Treadle::Ui::TextFieldConfig bodyConfig;
            bodyConfig.lines = card.kind == MoodboardKind::Todo ? 2 : 4;
            bodyConfig.placeholder = card.kind == MoodboardKind::Todo ? "Add a little context..." : "Write a thought, a plan or a reminder...";
            bodyConfig.maxLength = 24000;
            bodyConfig.enterSubmits = false;
            if(ui.textField("moodboard-body-" + suffix, &card.body, bodyConfig).changed) saveMoodboard(state);
            if(ui.button("Remove sticker")) remove = true;
        }else{
            Treadle::Ui::TextFieldConfig pathConfig;
            pathConfig.placeholder = "Paste an image path, or choose from Media";
            pathConfig.maxLength = 4096;
            if(ui.textField("moodboard-image-" + suffix, &card.source, pathConfig).changed){
                card.preview = MoodboardPreview{};
                card.preview.source = card.source;
                card.preview.attempted = true;
                if(!card.source.empty()) card.preview.error = "Click Refresh preview when the path is ready.";
                saveMoodboard(state);
            }
            const int imageAction = ui.buttonRow({"Refresh preview", "Choose from Media", "Remove"});
            if(imageAction == 0){
                card.preview = MoodboardPreview{};
                loadMoodboardPreview(card, state);
            }else if(imageAction == 1){
                state.imagePickerFor = state.imagePickerFor == card.id ? 0 : card.id;
            }else if(imageAction == 2){
                remove = true;
            }

            if(state.imagePickerFor == card.id){
                ui.label("IMAGES IN  " + mediaDirectory.filename().string());
                if(mediaImages.empty()) ui.label("No images in this Media folder.");
                else for(const std::filesystem::path& path : mediaImages){
                    if(ui.assetRow(path.filename().string(), "IMG", false, Treadle::Color{0.91f, 0.47f, 0.70f, 1.0f})){
                        std::error_code error;
                        const std::filesystem::path absolute = std::filesystem::absolute(path, error);
                        card.source = (error ? path : absolute).lexically_normal().string();
                        if(card.title.empty() || card.title == "Untitled image") card.title = path.stem().string();
                        card.preview = MoodboardPreview{};
                        loadMoodboardPreview(card, state);
                        saveMoodboard(state);
                    }
                }
            }

            loadMoodboardPreview(card, state);
            if(card.preview.originalWidth && card.preview.originalHeight)
                ui.value("SOURCE SIZE", std::to_string(card.preview.originalWidth) + " x " + std::to_string(card.preview.originalHeight));
            if(!card.preview.error.empty()) ui.label("Image preview: " + card.preview.error);
            ui.label("PREVIEW");
            const Treadle::Rect firstRow = ui.lastRowRect();
            for(int row = 0; row < 5; ++row) ui.label("");
            const Treadle::Rect lastRow = ui.lastRowRect();
            const float previewBottom = lastRow.y + lastRow.height;
            const bool visible = firstRow.y >= area.y + 24.0f && previewBottom <= area.y + area.height - theme.padding * 0.5f;
            if(card.preview.width && card.preview.height && visible){
                const float maxWidth = firstRow.width - 4.0f;
                const float maxHeight = std::min(170.0f, previewBottom - firstRow.y - 8.0f);
                const float scale = std::min(maxWidth / float(card.preview.width), maxHeight / float(card.preview.height));
                const float width = float(card.preview.width) * scale;
                const float height = float(card.preview.height) * scale;
                const Treadle::Rect imageBox{firstRow.x + 2.0f, firstRow.y + 4.0f, width, height};
                Treadle::DrawList& canvas = ui.canvas();
                canvas.rect(imageBox, Treadle::Color{0.025f, 0.035f, 0.03f, 1.0f});
                const float previousRadius = canvas.cornerRadius;
                canvas.cornerRadius = 0.0f;
                const float cellWidth = width / float(card.preview.width);
                const float cellHeight = height / float(card.preview.height);
                for(uint32_t y = 0; y < card.preview.height; ++y){
                    for(uint32_t x = 0; x < card.preview.width; ++x){
                        canvas.rect(imageBox.x + float(x) * cellWidth, imageBox.y + float(y) * cellHeight,
                                    cellWidth + 0.25f, cellHeight + 0.25f,
                                    card.preview.pixels[size_t(y) * card.preview.width + x]);
                    }
                }
                canvas.cornerRadius = previousRadius;
                canvas.outline(imageBox, 1.0f, theme.panelEdge);
            }
            if(ui.button("Remove image sticker")) remove = true;
        }

        if(remove){
            if(state.imagePickerFor == card.id) state.imagePickerFor = 0;
            if(state.draggingCardId == card.id) state.draggingCardId = 0;
            state.links.erase(std::remove_if(state.links.begin(), state.links.end(), [&](const MoodboardLink& link){
                return link.from == card.id || link.to == card.id;
            }), state.links.end());
            if(state.selectedCardId == card.id) state.selectedCardId = 0;
            if(state.connectingFromId == card.id) state.connectingFromId = 0;
            removeCardAtEnd = int(index);
        }
    }
    if(removeCardAtEnd >= 0){
        state.cards.erase(state.cards.begin() + removeCardAtEnd);
        saveMoodboard(state);
    }

    int menuAction = 0;
    const bool cardMenu = state.contextCardId != 0;
    const bool linkMenu = state.contextLinkId != 0;
    if(ui.beginMenu("moodboard-context")){
        if(linkMenu){
            if(ui.menuItem("Delete connection")) menuAction = 20;
            if(ui.menuItem("Reverse arrow")) menuAction = 21;
            ui.menuSeparator();
            if(ui.menuItem("Label: inspires")) menuAction = 22;
            if(ui.menuItem("Label: follows")) menuAction = 23;
            if(ui.menuItem("Label: related")) menuAction = 24;
            if(ui.menuItem("Clear label")) menuAction = 25;
        }else if(cardMenu){
            if(ui.menuItem("Bring sticker to front")) menuAction = 10;
            if(ui.menuItem("Duplicate sticker")) menuAction = 11;
            if(ui.menuItem("Change sticker color")) menuAction = 12;
            if(ui.menuItem("Remove sticker")) menuAction = 13;
            ui.menuSeparator();
            if(ui.menuItem("Add text sticker here")) menuAction = 1;
            if(ui.menuItem("Add to-do sticker here")) menuAction = 2;
            if(ui.menuItem("Add image sticker here")) menuAction = 3;
        }else{
            if(ui.menuItem("Add text sticker")) menuAction = 1;
            if(ui.menuItem("Add to-do sticker")) menuAction = 2;
            if(ui.menuItem("Add image sticker")) menuAction = 3;
            ui.menuSeparator();
            if(ui.menuItem("Close Moodboard   M")) menuAction = 4;
        }
        ui.endMenu();
    }

    if(menuAction == 4) return true;
    if(menuAction >= 1 && menuAction <= 3){
        const MoodboardKind kind = menuAction == 1 ? MoodboardKind::Note :
                                   menuAction == 2 ? MoodboardKind::Todo : MoodboardKind::Image;
        addAtPointer(kind);
    }else if(menuAction >= 20){
        auto found = std::find_if(state.links.begin(), state.links.end(), [&](const MoodboardLink& link){
            return link.id == state.contextLinkId;
        });
        if(found != state.links.end()){
            if(menuAction == 20) state.links.erase(found);
            else if(menuAction == 21) found->directed = !found->directed;
            else if(menuAction == 22) found->label = "inspires";
            else if(menuAction == 23) found->label = "follows";
            else if(menuAction == 24) found->label = "related";
            else if(menuAction == 25) found->label.clear();
            saveMoodboard(state);
        }
        state.contextLinkId = 0;
    }else if(menuAction >= 10){
        const auto found = std::find_if(state.cards.begin(), state.cards.end(), [&](const MoodboardCard& card){
            return card.id == state.contextCardId;
        });
        if(found != state.cards.end()){
            if(menuAction == 10){
                std::rotate(found, found + 1, state.cards.end());
                saveMoodboard(state);
            }else if(menuAction == 11){
                MoodboardCard duplicate = *found;
                duplicate.id = state.nextId++;
                duplicate.x += 32.0f;
                duplicate.y += 32.0f;
                duplicate.variant = uint8_t((duplicate.variant + 1u) % 5u);
                duplicate.preview = MoodboardPreview{};
                state.cards.push_back(std::move(duplicate));
                saveMoodboard(state);
            }else if(menuAction == 12){
                found->variant = uint8_t((found->variant + 1u) % 5u);
                saveMoodboard(state);
            }else if(menuAction == 13){
                const uint64_t removedId = found->id;
                if(state.imagePickerFor == removedId) state.imagePickerFor = 0;
                state.links.erase(std::remove_if(state.links.begin(), state.links.end(), [removedId](const MoodboardLink& link){
                    return link.from == removedId || link.to == removedId;
                }), state.links.end());
                if(state.selectedCardId == removedId) state.selectedCardId = 0;
                if(state.connectingFromId == removedId) state.connectingFromId = 0;
                state.cards.erase(found);
                saveMoodboard(state);
            }
        }
    }
    return false;
}

}
