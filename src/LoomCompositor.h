#pragma once
//=============================================================================================
// COMPOSITOR: radni prostor s cvorovima nad snimkom, kao Nuke u malom.
//
//   +-----+------------------------------------------+----------------+
//   | rail|                pregled (Viewer)          |   svojstva     |
//   |     +------------------------------------------+   odabranog    |
//   |     |                graf cvorova              |   cvora        |
//   +-----+------------------------------------------+----------------+
//   | timeline                                                        |
//
// CVOROVI. Read (snimka), Person Matte (osoba bez zelenog platna: RT-DETR nadje ljude, SAM2 ih
// prati - tools/comp/person_matte.py), Relight (snimka pod novim svjetlom iz procijenjenog -
// tools/comp/relight_plate.py), Background (boja ili sahovnica), Merge (A preko B), Viewer.
//
// TESKI RAD JE IZVAN EDITORA. Maska i preosvjetljavanje su Python alati koji pisu sekvencu slika u
// mapu cvora (proces u pozadini, napredak se cita iz izlaza). Editor po kadru samo slozi ono sto
// vec postoji, na procesoru, u velicini pregleda - pa se timeline vuce bez cekanja na model.
//=============================================================================================
#include "LoomPlate.h"

#include <Spool/ImageFile.h>
#include <Spool/VideoFile.h>
#include <Treadle/Ui.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace Loom{

//---------------------------------------------------------------------------------------------
// Pozadinski posao: naredba u ljusci, izlaz redak po redak, "napredak I/N" postaje postotak
//---------------------------------------------------------------------------------------------
struct CompJob{
    std::thread worker;
    std::mutex lock;
    std::atomic<bool> running{false};
    bool finished = false;
    int exitCode = 0;
    int done = 0, total = 0;
    std::string lastLine;

    void start(const std::string& command){
        if(worker.joinable()) worker.join();
        running = true;
        finished = false;
        done = total = 0;
        lastLine.clear();
        worker = std::thread([this, command]{
            FILE* pipe = popen((command + " 2>&1").c_str(), "r");
            if(!pipe){ std::lock_guard<std::mutex> g(lock); lastLine = "Could not start the tool."; exitCode = -1; finished = true; running = false; return; }
            char line[1024];
            while(std::fgets(line, sizeof(line), pipe)){
                std::string text(line);
                while(!text.empty() && (text.back() == '\n' || text.back() == '\r')) text.pop_back();
                if(text.empty()) continue;
                std::lock_guard<std::mutex> g(lock);
                int a = 0, b = 0;
                if(std::sscanf(text.c_str(), "napredak %d/%d", &a, &b) == 2){ done = a; total = b; }
                if(text.find("Warning") == std::string::npos && text.find("warn") == std::string::npos) lastLine = text;
            }
            const int status = pclose(pipe);
            std::lock_guard<std::mutex> g(lock);
            exitCode = status;
            finished = true;
            running = false;
        });
    }
    ~CompJob(){ if(worker.joinable()) worker.join(); }
};

//---------------------------------------------------------------------------------------------
// Graf
//---------------------------------------------------------------------------------------------
enum class CompKind : uint8_t{ Read, PersonMatte, Relight, Background, Merge, Viewer };

inline const char* compKindName(CompKind kind){
    switch(kind){
        case CompKind::Read: return "Read";
        case CompKind::PersonMatte: return "Person Matte";
        case CompKind::Relight: return "Relight";
        case CompKind::Background: return "Background";
        case CompKind::Merge: return "Merge";
        case CompKind::Viewer: return "Viewer";
    }
    return "?";
}
inline int compInputCount(CompKind kind){
    return kind == CompKind::Merge ? 2 : (kind == CompKind::Read || kind == CompKind::Background) ? 0 : 1;
}
inline Treadle::Color compKindColour(CompKind kind){
    switch(kind){
        case CompKind::Read: return {0.30f, 0.62f, 0.95f, 1.0f};
        case CompKind::PersonMatte: return {0.36f, 0.95f, 0.61f, 1.0f};
        case CompKind::Relight: return {1.00f, 0.72f, 0.30f, 1.0f};
        case CompKind::Background: return {0.62f, 0.56f, 0.86f, 1.0f};
        case CompKind::Merge: return {0.90f, 0.42f, 0.52f, 1.0f};
        case CompKind::Viewer: return {0.92f, 0.94f, 0.80f, 1.0f};
    }
    return {1, 1, 1, 1};
}

struct CompNode{
    int id = 0;
    CompKind kind = CompKind::Read;
    float x = 0.0f, y = 0.0f;                   //u prostoru grafa
    std::array<int, 2> input{ -1, -1 };          //id cvora na ulazu; Merge: 0 = A (gore), 1 = B (dolje)

    std::string path;                            //Read: snimka
    std::string cache;                           //Person Matte / Relight: mapa sekvence
    int model = 1;                               //Person Matte: tiny, small, base, large
    bool matteOnly = false;                      //Person Matte: pokazi samo masku
    bool checker = true;                         //Background
    float colour[3] = {0.10f, 0.55f, 0.20f};
    float sunAzimuth = 90.0f, sunElevation = 35.0f, sunIntensity = 2.0f, sky = 1.0f;
    float sunColour[3] = {1.0f, 0.88f, 0.72f};
    std::string solveFolder, lightFile, splatFile; //Relight: izlaz VideoSolvea i relight.py
    std::shared_ptr<CompJob> job;
};

//Slika u velicini pregleda, RGBA u 0-1, ravna (ne premultiplicirana)
struct CompImage{
    uint32_t width = 0, height = 0;
    std::vector<float> rgba;
    bool valid() const { return width > 0 && height > 0 && rgba.size() == size_t(width) * height * 4; }
};

struct Compositor{
    bool open = false;
    std::vector<CompNode> nodes;
    int nextId = 1;
    int selected = -1;
    //Pogled grafa
    float panX = 40.0f, panY = 30.0f;
    int dragNode = -1;
    float dragOffsetX = 0.0f, dragOffsetY = 0.0f;
    int connectFrom = -1;                        //vuce se veza iz izlaza ovog cvora
    bool panning = false;
    float panStartX = 0.0f, panStartY = 0.0f, panMouseX = 0.0f, panMouseY = 0.0f;
    float addMenuX = 0.0f, addMenuY = 0.0f;
    float propertyScroll = 0.0f;
    std::string status;
    //Zahtjev editoru: timeline na duljinu i brzinu snimke (Read -> "Set timeline to video")
    int64_t timelineFrames = 0;
    double timelineFps = 0.0;

    //Kadrovi snimki (po Read cvoru) i sekvence s diska, posljednji ucitan po cvoru
    std::map<int, std::unique_ptr<PlateStream>> streams;
    struct RawFrame{ std::vector<uint8_t> pixels; uint32_t width = 0, height = 0; int64_t index = -1; };
    std::map<int, RawFrame> readRaw;             //zadnji kadar snimke po Read cvoru, puna velicina
    std::map<int, CompImage> readFrames;         //isti, u velicini pregleda
    std::map<int, int64_t> readIndex;
    std::map<std::string, CompImage> sequenceCache;   //kljuc: putanja datoteke
    //Izlaz za pregled
    std::vector<uint8_t> output;
    uint32_t outputWidth = 0, outputHeight = 0;
    bool outputChanged = false;
    uint32_t previewWidth = 1280;

    CompNode* find(int id){
        for(CompNode& n : nodes) if(n.id == id) return &n;
        return nullptr;
    }
    CompNode& add(CompKind kind, float x, float y){
        CompNode node;
        node.id = nextId++;
        node.kind = kind;
        node.x = x; node.y = y;
        nodes.push_back(node);
        return nodes.back();
    }
    void remove(int id){
        nodes.erase(std::remove_if(nodes.begin(), nodes.end(), [&](const CompNode& n){ return n.id == id; }), nodes.end());
        for(CompNode& n : nodes) for(int& in : n.input) if(in == id) in = -1;
        streams.erase(id); readRaw.erase(id); readFrames.erase(id); readIndex.erase(id);
        if(selected == id) selected = -1;
    }
};

//Zadani graf: osoba iz snimke preko iste snimke pod novim svjetlom
inline void compositorDefaultGraph(Compositor& comp, const std::string& video){
    if(!comp.nodes.empty()) return;
    CompNode& read = comp.add(CompKind::Read, 0.0f, 40.0f);
    read.path = video;
    const int readId = read.id;
    CompNode& matte = comp.add(CompKind::PersonMatte, 210.0f, 0.0f);
    matte.input[0] = readId;
    const int matteId = matte.id;
    CompNode& relight = comp.add(CompKind::Relight, 210.0f, 110.0f);
    relight.input[0] = readId;
    const int relightId = relight.id;
    CompNode& merge = comp.add(CompKind::Merge, 420.0f, 50.0f);
    merge.input[0] = matteId;
    merge.input[1] = relightId;
    const int mergeId = merge.id;
    CompNode& viewer = comp.add(CompKind::Viewer, 610.0f, 50.0f);
    viewer.input[0] = mergeId;
    comp.selected = matteId;
}

//Mapa sekvence cvora: uz snimku, <snimka>_comp/<vrsta>_<id>
inline std::string compCacheFolder(const std::string& video, const CompNode& node){
    const std::filesystem::path v(video);
    const std::string kind = node.kind == CompKind::PersonMatte ? "matte" : "relit";
    return (v.parent_path() / (v.stem().string() + "_comp") / (kind + "_" + std::to_string(node.id))).string();
}

//Read cvor uzvodno (za snimku, kadar i mapu)
inline const CompNode* compUpstreamRead(Compositor& comp, const CompNode& node, int depth = 0){
    if(node.kind == CompKind::Read) return &node;
    if(depth > 32) return nullptr;
    for(int in : node.input){
        if(CompNode* up = comp.find(in)){
            if(const CompNode* read = compUpstreamRead(comp, *up, depth + 1)) return read;
        }
    }
    return nullptr;
}

//Relight: popuni putanje iz mape rezultata snimke kad postoje (VideoSolve <snimka>_loom, relight.py uz nju)
inline void compGuessRelightFiles(CompNode& node, const std::string& video){
    namespace fs = std::filesystem;
    const fs::path v(video);
    const fs::path solve = v.parent_path() / (v.stem().string() + "_loom");
    std::error_code error;
    if(node.solveFolder.empty() && fs::exists(solve / "kamera.usda", error)) node.solveFolder = solve.string();
    if(!node.solveFolder.empty() && (node.lightFile.empty() || node.splatFile.empty())){
        for(const auto& entry : fs::directory_iterator(node.solveFolder, error)){
            const std::string name = entry.path().filename().string();
            if(node.lightFile.empty() && name.size() > 12 && name.substr(name.size() - 12) == "_svjetlo.json") node.lightFile = entry.path().string();
            if(node.splatFile.empty() && name.size() > 11 && name.substr(name.size() - 11) == "_albedo.ply") node.splatFile = entry.path().string();
        }
    }
}

inline std::string compToolCommand(const std::string& root, const std::string& tool, const std::vector<std::string>& args){
    std::string command = "cd \"" + root + "\" && PATH=\"" + root + "/.venv/bin:$PATH\" ./.venv/bin/python " + tool;
    for(const std::string& a : args) command += " \"" + a + "\"";
    return command;
}

//---------------------------------------------------------------------------------------------
// Slaganje kadra
//---------------------------------------------------------------------------------------------
inline void compResize(const uint8_t* rgba, uint32_t w, uint32_t h, CompImage& out, uint32_t ow, uint32_t oh){
    out.width = ow; out.height = oh;
    out.rgba.assign(size_t(ow) * oh * 4, 0.0f);
    for(uint32_t y = 0; y < oh; ++y){
        const uint32_t sy = std::min(h - 1, uint32_t((uint64_t(y) * h + h / 2) / oh));
        for(uint32_t x = 0; x < ow; ++x){
            const uint32_t sx = std::min(w - 1, uint32_t((uint64_t(x) * w + w / 2) / ow));
            const uint8_t* p = rgba + (size_t(sy) * w + sx) * 4;
            float* q = out.rgba.data() + (size_t(y) * ow + x) * 4;
            q[0] = p[0] / 255.0f; q[1] = p[1] / 255.0f; q[2] = p[2] / 255.0f; q[3] = p[3] / 255.0f;
        }
    }
}

//Slika sekvence s diska u velicini pregleda; zadnjih nekoliko se pamti
inline const CompImage* compSequenceFrame(Compositor& comp, const std::string& file, uint32_t w, uint32_t h){
    auto found = comp.sequenceCache.find(file);
    if(found != comp.sequenceCache.end() && found->second.width == w && found->second.height == h) return &found->second;
    std::error_code error;
    if(!std::filesystem::exists(file, error)) return nullptr;
    const Spool::Image image = Spool::loadImage(file);
    if(!image.isValid()) return nullptr;
    if(comp.sequenceCache.size() > 24) comp.sequenceCache.clear();
    CompImage& slot = comp.sequenceCache[file];
    compResize(image.pixels.data(), image.width, image.height, slot, w, h);
    return &slot;
}

inline std::string compFrameFile(const std::string& folder, const char* prefix, int64_t index){
    char name[64];
    std::snprintf(name, sizeof(name), "/%s_%05lld.png", prefix, (long long)index);
    return folder + name;
}

//Read: zatrazi kadar i pokupi ga ako je stigao iz niti
inline void compPollRead(Compositor& comp, const CompNode& node, int64_t index){
    if(node.path.empty()) return;
    auto& stream = comp.streams[node.id];
    if(!stream) stream = std::make_unique<PlateStream>();
    if(stream->path() != node.path){ comp.readRaw.erase(node.id); comp.readFrames.erase(node.id); comp.readIndex.erase(node.id); }
    stream->open(node.path);
    stream->request(index);
    Compositor::RawFrame fresh;
    if(stream->take(fresh.pixels, fresh.width, fresh.height, fresh.index) && fresh.width > 0){
        comp.readRaw[node.id] = std::move(fresh);
        comp.readIndex.erase(node.id);
    }
}

//Vrijednost cvora u kadru. settled = false dok snimka jos nije dostavila trazeni kadar
inline CompImage compEvaluate(Compositor& comp, int id, int64_t index, uint32_t w, uint32_t h, bool& settled, int depth = 0){
    CompImage out;
    CompNode* node = comp.find(id);
    if(!node || depth > 32 || w == 0 || h == 0) return out;
    auto upstream = [&](int port){ return compEvaluate(comp, node->input[size_t(port)], index, w, h, settled, depth + 1); };
    switch(node->kind){
        case CompKind::Read:{
            compPollRead(comp, *node, index);
            auto raw = comp.readRaw.find(node->id);
            if(raw == comp.readRaw.end() || raw->second.width == 0) { settled = false; return out; }
            if(raw->second.index != index) settled = false;
            CompImage& shown = comp.readFrames[node->id];
            if(comp.readIndex.count(node->id) == 0 || comp.readIndex[node->id] != raw->second.index ||
               shown.width != w || shown.height != h){
                compResize(raw->second.pixels.data(), raw->second.width, raw->second.height, shown, w, h);
                comp.readIndex[node->id] = raw->second.index;
            }
            return shown;
        }
        case CompKind::Background:{
            out.width = w; out.height = h;
            out.rgba.resize(size_t(w) * h * 4);
            for(uint32_t y = 0; y < h; ++y) for(uint32_t x = 0; x < w; ++x){
                float* q = out.rgba.data() + (size_t(y) * w + x) * 4;
                if(node->checker){
                    const bool light = ((x / 16) + (y / 16)) % 2 == 0;
                    q[0] = q[1] = q[2] = light ? 0.62f : 0.42f;
                }else{
                    q[0] = node->colour[0]; q[1] = node->colour[1]; q[2] = node->colour[2];
                }
                q[3] = 1.0f;
            }
            return out;
        }
        case CompKind::PersonMatte:{
            out = upstream(0);
            if(!out.valid() || node->cache.empty()) return out;
            const CompImage* matte = compSequenceFrame(comp, compFrameFile(node->cache, "matte", index), w, h);
            if(!matte) return out;
            for(size_t i = 0; i < size_t(w) * h; ++i){
                const float a = matte->rgba[i * 4];
                if(node->matteOnly){ out.rgba[i * 4] = out.rgba[i * 4 + 1] = out.rgba[i * 4 + 2] = a; out.rgba[i * 4 + 3] = 1.0f; }
                else out.rgba[i * 4 + 3] *= a;
            }
            return out;
        }
        case CompKind::Relight:{
            out = upstream(0);
            if(!out.valid() || node->cache.empty()) return out;
            const CompImage* relit = compSequenceFrame(comp, compFrameFile(node->cache, "relit", index), w, h);
            if(!relit) return out;
            for(size_t i = 0; i < size_t(w) * h; ++i) for(int c = 0; c < 3; ++c) out.rgba[i * 4 + c] = relit->rgba[i * 4 + c];
            return out;
        }
        case CompKind::Merge:{
            CompImage a = upstream(0), b = upstream(1);
            if(!b.valid()) return a;
            if(!a.valid()) return b;
            out = b;
            for(size_t i = 0; i < size_t(w) * h; ++i){
                const float alpha = a.rgba[i * 4 + 3];
                for(int c = 0; c < 3; ++c) out.rgba[i * 4 + c] = a.rgba[i * 4 + c] * alpha + b.rgba[i * 4 + c] * (1.0f - alpha);
                out.rgba[i * 4 + 3] = alpha + b.rgba[i * 4 + 3] * (1.0f - alpha);
            }
            return out;
        }
        case CompKind::Viewer:
            return upstream(0);
    }
    return out;
}

//Velicina pregleda iz snimke prvog Read cvora (omjer), sirina previewWidth
inline bool compPreviewSize(Compositor& comp, uint32_t& w, uint32_t& h){
    for(CompNode& n : comp.nodes){
        if(n.kind != CompKind::Read || n.path.empty()) continue;
        auto raw = comp.readRaw.find(n.id);
        const uint32_t vw = raw != comp.readRaw.end() ? raw->second.width : 0;
        const uint32_t vh = raw != comp.readRaw.end() ? raw->second.height : 0;
        if(vw > 0){
            w = std::min(comp.previewWidth, vw);
            h = std::max(2u, uint32_t(std::lround(double(vh) * w / vw)) & ~1u);
            return true;
        }
    }
    return false;
}

//Izlaz za pregled: Viewer (ili odabrani cvor kad Viewera nema) u kadru index
inline bool compositorUpdate(Compositor& comp, int64_t index){
    for(CompNode& n : comp.nodes) if(n.kind == CompKind::Read) compPollRead(comp, n, index);
    //Sekvenca koja vec postoji u mapi cvora (prosli rad) se preuzme bez ponovnog racunanja
    for(CompNode& n : comp.nodes){
        if((n.kind != CompKind::PersonMatte && n.kind != CompKind::Relight) || !n.cache.empty()) continue;
        const CompNode* read = compUpstreamRead(comp, n);
        if(!read || read->path.empty()) continue;
        const std::string folder = compCacheFolder(read->path, n);
        std::error_code error;
        if(std::filesystem::exists(std::filesystem::path(folder) / (n.kind == CompKind::PersonMatte ? "matte.json" : "relit.json"), error))
            n.cache = folder;
    }
    int shown = -1;
    for(const CompNode& n : comp.nodes) if(n.kind == CompKind::Viewer) shown = n.id;
    if(shown < 0) shown = comp.selected;
    uint32_t w = 0, h = 0;
    if(shown < 0 || !compPreviewSize(comp, w, h)) return false;
    bool settled = true;
    const CompImage image = compEvaluate(comp, shown, index, w, h, settled);
    if(!image.valid()) return false;
    comp.outputWidth = w; comp.outputHeight = h;
    comp.output.resize(size_t(w) * h * 4);
    for(size_t i = 0; i < size_t(w) * h; ++i){
        //Prozirno se u pregledu pokaze kao sahovnica, da se vidi sto maska uzima
        const float a = std::clamp(image.rgba[i * 4 + 3], 0.0f, 1.0f);
        const uint32_t x = uint32_t(i % w), y = uint32_t(i / w);
        const float board = (((x / 12) + (y / 12)) % 2 == 0) ? 0.30f : 0.22f;
        for(int c = 0; c < 3; ++c){
            const float v = image.rgba[i * 4 + c] * a + board * (1.0f - a);
            comp.output[i * 4 + c] = uint8_t(std::clamp(v, 0.0f, 1.0f) * 255.0f + 0.5f);
        }
        comp.output[i * 4 + 3] = 255;
    }
    comp.outputChanged = true;
    return settled;
}

//---------------------------------------------------------------------------------------------
// Raspored radnog prostora
//---------------------------------------------------------------------------------------------
struct CompositorLayout{
    Treadle::Rect viewer, graph, properties;
};
inline CompositorLayout layoutCompositor(const Treadle::Rect& middle){
    CompositorLayout layout;
    const float propertyWidth = std::clamp(middle.width * 0.24f, 280.0f, 360.0f);
    const float left = middle.width - propertyWidth - 6.0f;
    const float viewerHeight = std::floor(middle.height * 0.60f);
    layout.viewer = Treadle::Rect{middle.x, middle.y, left, viewerHeight};
    layout.graph = Treadle::Rect{middle.x, middle.y + viewerHeight + 4.0f, left, middle.height - viewerHeight - 4.0f};
    layout.properties = Treadle::Rect{middle.x + left + 6.0f, middle.y, propertyWidth, middle.height};
    return layout;
}

//Pravokutnik slike unutar pregleda, s ocuvanim omjerom
inline Treadle::Rect compositorImageRect(const Compositor& comp, const Treadle::Rect& viewer){
    if(comp.outputWidth == 0 || comp.outputHeight == 0) return Treadle::Rect{};
    const float inner = 8.0f;
    const float aw = viewer.width - 2 * inner, ah = viewer.height - 2 * inner - 20.0f;
    const float scale = std::min(aw / float(comp.outputWidth), ah / float(comp.outputHeight));
    const float w = float(comp.outputWidth) * scale, h = float(comp.outputHeight) * scale;
    return Treadle::Rect{viewer.x + (viewer.width - w) * 0.5f, viewer.y + 20.0f + inner + (ah - h) * 0.5f, w, h};
}

//---------------------------------------------------------------------------------------------
// Graf cvorova
//---------------------------------------------------------------------------------------------
constexpr float compNodeWidth = 150.0f, compNodeHeight = 46.0f;

inline Treadle::Rect compNodeBox(const Compositor& comp, const CompNode& node, const Treadle::Rect& area){
    return Treadle::Rect{area.x + comp.panX + node.x, area.y + comp.panY + node.y, compNodeWidth, compNodeHeight};
}
inline void compInputPort(const Treadle::Rect& box, int port, int count, float& x, float& y){
    x = box.x;
    y = count == 1 ? box.y + box.height * 0.5f : box.y + box.height * (port == 0 ? 0.30f : 0.72f);
}
inline void compOutputPort(const Treadle::Rect& box, float& x, float& y){
    x = box.x + box.width; y = box.y + box.height * 0.5f;
}
inline void compCurve(Treadle::DrawList& canvas, float x0, float y0, float x1, float y1, const Treadle::Color& colour){
    const float bend = std::max(40.0f, std::abs(x1 - x0) * 0.5f);
    float px = x0, py = y0;
    for(int i = 1; i <= 20; ++i){
        const float t = float(i) / 20.0f, u = 1.0f - t;
        const float x = u * u * u * x0 + 3 * u * u * t * (x0 + bend) + 3 * u * t * t * (x1 - bend) + t * t * t * x1;
        const float y = u * u * u * y0 + 3 * u * u * t * y0 + 3 * u * t * t * y1 + t * t * t * y1;
        canvas.line(px, py, x, y, 2.0f, colour);
        px = x; py = y;
    }
}

inline void compositorGraph(Treadle::Ui& ui, Compositor& comp, const Treadle::Rect& area, const Treadle::Input& input){
    const Treadle::Theme& theme = ui.style();
    Treadle::DrawList& canvas = ui.canvas();
    canvas.rect(area, Treadle::Color{0.035f, 0.045f, 0.040f, 1.0f});
    canvas.outline(area, 1.0f, theme.panelEdge);
    for(float gx = std::fmod(comp.panX, 24.0f); gx < area.width; gx += 24.0f)
        canvas.rect(area.x + gx, area.y, 1.0f, area.height, Treadle::Color{1, 1, 1, 0.025f});
    for(float gy = std::fmod(comp.panY, 24.0f); gy < area.height; gy += 24.0f)
        canvas.rect(area.x, area.y + gy, area.width, 1.0f, Treadle::Color{1, 1, 1, 0.025f});
    canvas.text(area.x + 8.0f, area.y + 6.0f, "NODE GRAPH   drag nodes, drag from an output dot to an input, right-click to add, Del removes",
                theme.dim, theme.textScale * 0.62f);

    const Treadle::Ui::Region surface = ui.region("comp-graph", area);
    //Delete / Backspace brise odabrani cvor dok je mis nad grafom (a tipkovnica ne pripada polju)
    if(surface.hot && !ui.wantsKeyboard() && comp.selected >= 0){
        for(const Treadle::KeyEvent& key : input.keys){
            if(key.key == Treadle::Key::Delete || key.key == Treadle::Key::Backspace){ comp.remove(comp.selected); break; }
        }
    }

    //Veze
    for(const CompNode& node : comp.nodes){
        const Treadle::Rect box = compNodeBox(comp, node, area);
        const int count = compInputCount(node.kind);
        for(int port = 0; port < count; ++port){
            const CompNode* from = nullptr;
            for(const CompNode& n : comp.nodes) if(n.id == node.input[size_t(port)]) from = &n;
            if(!from) continue;
            float x0, y0, x1, y1;
            compOutputPort(compNodeBox(comp, *from, area), x0, y0);
            compInputPort(box, port, count, x1, y1);
            compCurve(canvas, x0, y0, x1, y1, Treadle::Color{0.80f, 0.84f, 0.74f, 0.75f});
        }
    }

    //Cvorovi i njihovi dijelovi: prvo se trazi sto je pod misem
    int hoverNode = -1, hoverOutput = -1, hoverInput = -1, hoverPort = -1;
    for(const CompNode& node : comp.nodes){
        const Treadle::Rect box = compNodeBox(comp, node, area);
        float ox, oy;
        compOutputPort(box, ox, oy);
        if(node.kind != CompKind::Viewer && std::hypot(surface.mouseX - ox, surface.mouseY - oy) < 9.0f) hoverOutput = node.id;
        const int count = compInputCount(node.kind);
        for(int port = 0; port < count; ++port){
            float ix, iy;
            compInputPort(box, port, count, ix, iy);
            if(std::hypot(surface.mouseX - ix, surface.mouseY - iy) < 9.0f){ hoverInput = node.id; hoverPort = port; }
        }
        if(box.contains(surface.mouseX, surface.mouseY)) hoverNode = node.id;
    }

    if(surface.pressed){
        if(hoverOutput >= 0){
            comp.connectFrom = hoverOutput;
        }else if(hoverInput >= 0){
            //Povuci postojecu vezu s ulaza
            CompNode* target = comp.find(hoverInput);
            if(target && target->input[size_t(hoverPort)] >= 0){
                comp.connectFrom = target->input[size_t(hoverPort)];
                target->input[size_t(hoverPort)] = -1;
            }
        }else if(hoverNode >= 0){
            CompNode* node = comp.find(hoverNode);
            comp.selected = hoverNode;
            comp.dragNode = hoverNode;
            comp.dragOffsetX = surface.mouseX - (area.x + comp.panX + node->x);
            comp.dragOffsetY = surface.mouseY - (area.y + comp.panY + node->y);
        }else{
            comp.panning = true;
            comp.panStartX = comp.panX; comp.panStartY = comp.panY;
            comp.panMouseX = surface.mouseX; comp.panMouseY = surface.mouseY;
        }
    }
    if(surface.held){
        if(comp.dragNode >= 0){
            if(CompNode* node = comp.find(comp.dragNode)){
                node->x = surface.mouseX - comp.dragOffsetX - area.x - comp.panX;
                node->y = surface.mouseY - comp.dragOffsetY - area.y - comp.panY;
            }
        }else if(comp.panning){
            comp.panX = comp.panStartX + surface.mouseX - comp.panMouseX;
            comp.panY = comp.panStartY + surface.mouseY - comp.panMouseY;
        }
    }else{
        if(comp.connectFrom >= 0 && hoverInput >= 0 && hoverInput != comp.connectFrom){
            if(CompNode* target = comp.find(hoverInput)) target->input[size_t(hoverPort)] = comp.connectFrom;
        }
        comp.connectFrom = -1;
        comp.dragNode = -1;
        comp.panning = false;
    }
    if(comp.connectFrom >= 0){
        if(const CompNode* from = comp.find(comp.connectFrom)){
            float x0, y0;
            compOutputPort(compNodeBox(comp, *from, area), x0, y0);
            compCurve(canvas, x0, y0, surface.mouseX, surface.mouseY, theme.accent);
        }
    }
    if(surface.rightPressed){
        comp.addMenuX = surface.mouseX - area.x - comp.panX;
        comp.addMenuY = surface.mouseY - area.y - comp.panY;
        ui.openMenuAt("comp-add", surface.mouseX, surface.mouseY);
    }

    for(const CompNode& node : comp.nodes){
        const Treadle::Rect box = compNodeBox(comp, node, area);
        if(box.x + box.width < area.x || box.x > area.x + area.width || box.y + box.height < area.y || box.y > area.y + area.height) continue;
        const Treadle::Color colour = compKindColour(node.kind);
        const bool chosen = comp.selected == node.id;
        canvas.rect(box, chosen ? Treadle::Color{0.13f, 0.17f, 0.13f, 1.0f} : Treadle::Color{0.085f, 0.11f, 0.09f, 1.0f});
        canvas.rect(box.x, box.y, 4.0f, box.height, colour);
        canvas.outline(box, chosen ? 2.0f : 1.0f, chosen ? theme.accent : theme.panelEdge);
        canvas.text(box.x + 12.0f, box.y + 8.0f, compKindName(node.kind), theme.title, theme.textScale * 0.78f);
        std::string detail;
        if(node.kind == CompKind::Read) detail = node.path.empty() ? "no video" : std::filesystem::path(node.path).filename().string();
        else if(node.job && node.job->running){
            std::lock_guard<std::mutex> g(node.job->lock);
            detail = node.job->total > 0 ? std::to_string(node.job->done) + "/" + std::to_string(node.job->total) : "starting...";
        }
        else if(node.kind == CompKind::PersonMatte || node.kind == CompKind::Relight) detail = node.cache.empty() ? "not computed" : "ready";
        else if(node.kind == CompKind::Merge) detail = "A over B";
        if(!detail.empty()) canvas.text(box.x + 12.0f, box.y + 26.0f, Treadle::fitText(detail, box.width - 18.0f, theme.textScale * 0.6f),
                                        theme.dim, theme.textScale * 0.6f);
        const int count = compInputCount(node.kind);
        for(int port = 0; port < count; ++port){
            float ix, iy;
            compInputPort(box, port, count, ix, iy);
            canvas.rect(ix - 5.0f, iy - 5.0f, 10.0f, 10.0f, hoverInput == node.id && hoverPort == port ? theme.accent : theme.text);
            if(node.kind == CompKind::Merge) canvas.text(ix - 16.0f, iy - 6.0f, port == 0 ? "A" : "B", theme.dim, theme.textScale * 0.55f);
        }
        if(node.kind != CompKind::Viewer){
            float ox, oy;
            compOutputPort(box, ox, oy);
            canvas.rect(ox - 5.0f, oy - 5.0f, 10.0f, 10.0f, hoverOutput == node.id ? theme.accent : colour);
        }
    }

    if(ui.beginMenu("comp-add")){
        const std::array<CompKind, 6> kinds{CompKind::Read, CompKind::PersonMatte, CompKind::Relight,
                                            CompKind::Background, CompKind::Merge, CompKind::Viewer};
        for(CompKind kind : kinds){
            if(ui.menuItem(std::string("Add ") + compKindName(kind))){
                CompNode& added = comp.add(kind, comp.addMenuX, comp.addMenuY);
                comp.selected = added.id;
            }
        }
        ui.endMenu();
    }
}

//---------------------------------------------------------------------------------------------
// Svojstva odabranog cvora
//---------------------------------------------------------------------------------------------
inline void compositorProperties(Treadle::Ui& ui, Compositor& comp, const Treadle::Rect& area, const std::string& root,
                                 const std::vector<std::filesystem::path>& videos, const std::string& cameraPlate){
    ui.dock("NODE", area, &comp.propertyScroll);
    CompNode* node = comp.find(comp.selected);
    if(!node){
        ui.label("Select a node in the graph.");
        ui.label("Right-click the graph to add nodes.");
        return;
    }
    ui.value("Node", compKindName(node->kind));
    auto jobStatus = [&](){
        if(!node->job) return;
        std::lock_guard<std::mutex> g(node->job->lock);
        if(node->job->running){
            ui.value("Progress", node->job->total > 0 ? std::to_string(node->job->done) + " / " + std::to_string(node->job->total) : "loading models...");
        }else if(node->job->finished){
            ui.value("Last run", node->job->exitCode == 0 ? "finished" : "failed");
        }
        if(!node->job->lastLine.empty()) ui.label(Treadle::fitText(node->job->lastLine, area.width - 30.0f, ui.style().textScale * 0.8f));
    };
    const CompNode* read = compUpstreamRead(comp, *node);
    const std::string video = read ? read->path : std::string();
    const bool busy = node->job && node->job->running;

    switch(node->kind){
        case CompKind::Read:{
            ui.textField("comp-read-path", &node->path);
            if(!cameraPlate.empty() && ui.button("Use camera plate")) node->path = cameraPlate;
            if(!node->path.empty() && ui.button("Set timeline to video")){
                try{
                    Spool::VideoReader reader(node->path);
                    comp.timelineFrames = reader.info().frameCount;
                    comp.timelineFps = reader.info().frameRate();
                }catch(const std::exception&){
                    comp.timelineFrames = 0;
                }
            }
            if(!videos.empty()){
                ui.label("Videos in the media folder:");
                for(const auto& v : videos) if(ui.selectable(v.filename().string(), node->path == v.string())) node->path = v.string();
            }
            break;
        }
        case CompKind::PersonMatte:{
            ui.label("Finds people (RT-DETR) and tracks them");
            ui.label("through the video (SAM2): a matte with");
            ui.label("no green screen.");
            ui.choice("Model", {"tiny", "small", "base", "large"}, &node->model);
            ui.checkbox("Show matte only", &node->matteOnly);
            if(video.empty()) ui.label("Connect a Read node with a video.");
            else if(!busy && ui.button(node->cache.empty() ? "Detect people & cut out" : "Run again")){
                const std::string folder = compCacheFolder(video, *node);
                static const char* models[] = {"tiny", "small", "base", "large"};
                node->job = std::make_shared<CompJob>();
                node->job->start(compToolCommand(root, "tools/comp/person_matte.py",
                                                 {video, folder, "--model", models[std::clamp(node->model, 0, 3)]}));
                node->cache = folder;
            }
            jobStatus();
            if(!node->cache.empty()) ui.value("Frames", std::filesystem::path(node->cache).filename().string());
            break;
        }
        case CompKind::Relight:{
            ui.label("Relights the plate with a new sun and sky,");
            ui.label("from the light estimated for this shot.");
            if(!video.empty()) compGuessRelightFiles(*node, video);
            ui.label("Solve folder (kamera.usda):");
            ui.textField("comp-relight-solve", &node->solveFolder);
            ui.label("Estimated light (relight.py .json):");
            ui.textField("comp-relight-light", &node->lightFile);
            ui.label("Splat with normals (_albedo.ply):");
            ui.textField("comp-relight-splat", &node->splatFile);
            ui.slider("Sun azimuth", &node->sunAzimuth, -180.0f, 180.0f, "deg");
            ui.slider("Sun elevation", &node->sunElevation, -10.0f, 90.0f, "deg");
            ui.slider("Sun strength", &node->sunIntensity, 0.0f, 8.0f);
            ui.slider("Sun red", &node->sunColour[0], 0.0f, 1.0f);
            ui.slider("Sun green", &node->sunColour[1], 0.0f, 1.0f);
            ui.slider("Sun blue", &node->sunColour[2], 0.0f, 1.0f);
            ui.slider("Sky", &node->sky, 0.0f, 3.0f);
            const bool ready = !video.empty() && !node->solveFolder.empty() && !node->lightFile.empty() && !node->splatFile.empty();
            if(!ready) ui.label("Needs a solved shot and relight.py output.");
            else if(!busy && ui.button(node->cache.empty() ? "Render relit plate" : "Render again")){
                const std::string folder = compCacheFolder(video, *node);
                char colour[64];
                std::snprintf(colour, sizeof(colour), "%.3f,%.3f,%.3f", node->sunColour[0], node->sunColour[1], node->sunColour[2]);
                node->job = std::make_shared<CompJob>();
                node->job->start(compToolCommand(root, "tools/comp/relight_plate.py",
                    {node->solveFolder, node->lightFile, node->splatFile, video, folder,
                     "--sun-azimuth", std::to_string(node->sunAzimuth), "--sun-elevation", std::to_string(node->sunElevation),
                     "--sun-intensity", std::to_string(node->sunIntensity), "--sun-colour", colour,
                     "--sky-scale", std::to_string(node->sky)}));
                node->cache = folder;
                comp.sequenceCache.clear();
            }
            jobStatus();
            break;
        }
        case CompKind::Background:{
            ui.checkbox("Checkerboard", &node->checker);
            if(!node->checker){
                ui.slider("Red", &node->colour[0], 0.0f, 1.0f);
                ui.slider("Green", &node->colour[1], 0.0f, 1.0f);
                ui.slider("Blue", &node->colour[2], 0.0f, 1.0f);
            }
            break;
        }
        case CompKind::Merge:
            ui.label("A (top input) over B (bottom input),");
            ui.label("using A's alpha.");
            break;
        case CompKind::Viewer:
            ui.label("Shows its input in the viewer above.");
            break;
    }
    ui.separator();
    if(ui.button("Delete node")) comp.remove(node->id);
}

//Mapa kadrova se na disku mijenja dok posao radi: pamti se po imenu datoteke, pa se tijekom posla
//brise da se vide novi kadrovi
inline void compositorTick(Compositor& comp){
    for(CompNode& n : comp.nodes){
        if(n.job && n.job->running) comp.sequenceCache.clear();
    }
}

}
