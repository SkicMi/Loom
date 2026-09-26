#pragma once
//=============================================================================================
// ALATI EDITORA: materijali u svojstvima, ploha iz odabira, glTF model na mjestu pogleda.
//
// Stoji izvan loom_app.cpp namjerno: glavna datoteka editora ima vise autora u isto vrijeme, pa
// je u njoj samo nekoliko poziva, a sve ostalo ovdje - i testira se bez prozora.
//
//   MATERIJAL   tijelu ili svakom primitivu modela se bira materijal iz biblioteke scene (< >),
//               pravi novi, i uredjuje: boja, metalnost, hrapavost, emisija, alfa, dvostrano, i
//               pet mapa. Mapa se postavlja tako da se "naoruza" pa klikne slika u media prozoru
//   PLOHA (S)   pravokutnik u pogledu odabere tocke i gaussiane prve plohe (LoomSurface.h);
//               "Kocka na plohu" postavi kocku koja lezi na njoj, okrenutu po plohi
//   MODEL       .gltf/.glb iz media prozora stoji na podu ondje kamo pogled gleda, mjerilo iz
//               visine kamere (glTF pise metre, solve nema metre) - isto pravilo kao lik iz pokreta
//=============================================================================================
#include "LoomModel.h"
#include "LoomSurface.h"
#include "LoomViewport.h"

#include <Spool/Gltf.h>
#include <Treadle/Ui.h>
#include <Warp/Stage.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace Loom{

//-- slike za mape ----------------------------------------------------------------------------
inline std::vector<std::filesystem::path> imageFilesIn(const std::filesystem::path& directory){
    std::vector<std::filesystem::path> found;
    std::error_code error;
    for(const auto& entry : std::filesystem::directory_iterator(directory, error)){
        if(error) break;
        if(!entry.is_regular_file(error)) continue;
        std::string extension = entry.path().extension().string();
        std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char c){ return char(std::tolower(c)); });
        if(extension == ".png" || extension == ".jpg" || extension == ".jpeg" || extension == ".tga" || extension == ".bmp") found.push_back(entry.path());
    }
    std::sort(found.begin(), found.end());
    return found;
}

//=============================================================================================
// MATERIJAL U SVOJSTVIMA
//=============================================================================================
struct MaterialPanelState{
    int armedMaterial = -1;             //materijal i mapa koja ceka sliku iz media prozora
    int armedSlot = -1;                 //0 boja, 1 metalnost-hrapavost, 2 normal, 3 occlusion, 4 emisija
    int numericMaterial = -1;
    std::string colourEntry, metallicEntry, roughnessEntry, emissionEntry, emissionStrengthEntry;
    bool armed() const {return armedMaterial >= 0 && armedSlot >= 0;}
};

inline std::string materialFloatText(float value){
    char text[32];
    std::snprintf(text, sizeof(text), "%.6g", double(value));
    return text;
}

inline bool parseMaterialFloats(const std::string& text, float* values, size_t count){
    if(!values || count == 0) return false;
    const char* cursor = text.c_str();
    for(size_t i = 0; i < count; ++i){
        while(*cursor == ' ' || *cursor == '\t' || *cursor == ',' || *cursor == ';') ++cursor;
        char* end = nullptr;
        const float value = std::strtof(cursor, &end);
        if(end == cursor || !std::isfinite(value)) return false;
        values[i] = value;
        cursor = end;
    }
    while(*cursor == ' ' || *cursor == '\t' || *cursor == ',' || *cursor == ';') ++cursor;
    return *cursor == '\0';
}

inline Warp::TextureSlot& materialSlot(Warp::Material& m, int slot){
    switch(slot){
        case 0: return m.baseColorMap;
        case 1: return m.metallicRoughnessMap;
        case 2: return m.normalMap;
        case 3: return m.occlusionMap;
        default: return m.emissiveMap;
    }
}

inline const char* slotName(int slot){
    static const char* names[5] = {"Color", "Metal/Rough", "Normal", "Occlusion", "Emission"};
    return names[std::clamp(slot, 0, 4)];
}

//Slika iz media prozora u naoruzanu mapu. true kad je postavljena
inline bool assignArmedImage(Warp::Stage& stage, MaterialPanelState& state, const std::string& path){
    if(!state.armed() || size_t(state.armedMaterial) >= stage.materials.size()) return false;
    Warp::TextureSlot& slot = materialSlot(stage.materials[size_t(state.armedMaterial)], state.armedSlot);
    slot.source = path;
    slot.image = -1;
    state.armedMaterial = state.armedSlot = -1;
    return true;
}

namespace tools{

inline std::string slotLabel(const Warp::TextureSlot& t){
    if(t.empty()) return "(None)";
    const std::string file = std::filesystem::path(t.source).filename().string();
    return t.image >= 0 ? file + " #" + std::to_string(t.image) : file;
}

//Izbor materijala za jednu vezu: [<] ime [>] [novi]. -1 je zadani materijal
inline void bindingRow(Treadle::Ui& ui, Warp::Stage& stage, const std::string& label, int& binding){
    const std::string name = binding >= 0 && size_t(binding) < stage.materials.size() ? stage.materials[size_t(binding)].name : "(Default)";
    ui.value(label, Treadle::fitText(name, 170.0f, ui.style().textScale));
    const int clicked = ui.buttonRow({"<", ">", "New"});
    const int count = int(stage.materials.size());
    if(clicked == 0) binding = binding <= -1 ? count - 1 : binding - 1;
    if(clicked == 1) binding = binding >= count - 1 ? -1 : binding + 1;
    if(clicked == 2){
        Warp::Material fresh;
        if(binding >= 0 && size_t(binding) < stage.materials.size()) fresh = stage.materials[size_t(binding)];
        fresh.name = binding >= 0 ? fresh.name + " Copy" : "Material";
        binding = stage.addMaterial(fresh);
    }
}

inline void editMaterial(Treadle::Ui& ui, Warp::Stage& stage, int index, MaterialPanelState& state){
    if(index < 0 || size_t(index) >= stage.materials.size()) return;
    Warp::Material& m = stage.materials[size_t(index)];
    if(state.numericMaterial != index || !ui.wantsKeyboard()){
        state.numericMaterial = index;
        state.colourEntry = materialFloatText(m.baseColor.r) + " " + materialFloatText(m.baseColor.g) + " " + materialFloatText(m.baseColor.b);
        state.metallicEntry = materialFloatText(m.metallic);
        state.roughnessEntry = materialFloatText(m.roughness);
        state.emissionEntry = materialFloatText(m.emissive.r) + " " + materialFloatText(m.emissive.g) + " " + materialFloatText(m.emissive.b);
        state.emissionStrengthEntry = materialFloatText(m.emissiveStrength);
    }

    float colour[3] = {m.baseColor.r, m.baseColor.g, m.baseColor.b};
    if(ui.dragVector("Color", colour, 0.004f)){
        for(float& c : colour) c = std::clamp(c, 0.0f, 1.0f);
        m.baseColor = glm::vec4(colour[0], colour[1], colour[2], m.baseColor.a);
        state.colourEntry = materialFloatText(colour[0]) + " " + materialFloatText(colour[1]) + " " + materialFloatText(colour[2]);
    }
    ui.slider("Metallic", &m.metallic, 0.0f, 1.0f);
    ui.slider("Roughness", &m.roughness, 0.0f, 1.0f);
    float emissive[3] = {m.emissive.r, m.emissive.g, m.emissive.b};
    if(ui.dragVector("Emission", emissive, 0.004f)){
        for(float& c : emissive) c = std::clamp(c, 0.0f, 1.0f);
        m.emissive = glm::vec3(emissive[0], emissive[1], emissive[2]);
        state.emissionEntry = materialFloatText(emissive[0]) + " " + materialFloatText(emissive[1]) + " " + materialFloatText(emissive[2]);
    }
    ui.slider("Emission Strength", &m.emissiveStrength, 0.0f, 20.0f);

    ui.caption("EXACT VALUES · ENTER TO APPLY");
    Treadle::Ui::TextFieldConfig exactField;
    exactField.lines = 1;
    exactField.labelFraction = 0.43f;
    exactField.label = "Base color RGB";
    if(ui.textField("material-exact-color", &state.colourEntry, exactField).submitted){
        float values[3];
        if(parseMaterialFloats(state.colourEntry, values, 3)){
            for(float& value : values) value = std::clamp(value, 0.0f, 1.0f);
            m.baseColor = glm::vec4(values[0], values[1], values[2], m.baseColor.a);
            state.colourEntry = materialFloatText(values[0]) + " " + materialFloatText(values[1]) + " " + materialFloatText(values[2]);
        }
    }
    exactField.label = "Metallic";
    if(ui.textField("material-exact-metallic", &state.metallicEntry, exactField).submitted){
        float value[1];
        if(parseMaterialFloats(state.metallicEntry, value, 1)){
            m.metallic = std::clamp(value[0], 0.0f, 1.0f);
            state.metallicEntry = materialFloatText(m.metallic);
        }
    }
    exactField.label = "Roughness";
    if(ui.textField("material-exact-roughness", &state.roughnessEntry, exactField).submitted){
        float value[1];
        if(parseMaterialFloats(state.roughnessEntry, value, 1)){
            m.roughness = std::clamp(value[0], 0.0f, 1.0f);
            state.roughnessEntry = materialFloatText(m.roughness);
        }
    }
    exactField.label = "Emission RGB";
    if(ui.textField("material-exact-emission", &state.emissionEntry, exactField).submitted){
        float values[3];
        if(parseMaterialFloats(state.emissionEntry, values, 3)){
            for(float& value : values) value = std::clamp(value, 0.0f, 1.0f);
            m.emissive = glm::vec3(values[0], values[1], values[2]);
            state.emissionEntry = materialFloatText(values[0]) + " " + materialFloatText(values[1]) + " " + materialFloatText(values[2]);
        }
    }
    exactField.label = "Emission strength";
    if(ui.textField("material-exact-emission-strength", &state.emissionStrengthEntry, exactField).submitted){
        float value[1];
        if(parseMaterialFloats(state.emissionStrengthEntry, value, 1)){
            m.emissiveStrength = std::clamp(value[0], 0.0f, 20.0f);
            state.emissionStrengthEntry = materialFloatText(m.emissiveStrength);
        }
    }

    int alpha = int(m.alphaMode);
    if(ui.choice("Alpha Mode", {"Opaque", "Mask", "Blend"}, &alpha)) m.alphaMode = Warp::Material::Alpha(alpha);
    if(m.alphaMode != Warp::Material::Alpha::Opaque) ui.slider("Opacity", &m.baseColor.a, 0.0f, 1.0f);
    if(m.alphaMode == Warp::Material::Alpha::Mask) ui.slider("Mask Cutoff", &m.alphaCutoff, 0.0f, 1.0f);
    ui.checkbox("Double-Sided", &m.doubleSided);

    //Mape: ime, pa [iz datoteke] [ukloni]. Naoruzana ceka klik na sliku u media prozoru
    for(int slot = 0; slot < 5; ++slot){
        Warp::TextureSlot& t = materialSlot(m, slot);
        const bool waiting = state.armedMaterial == index && state.armedSlot == slot;
        ui.value(std::string("Texture: ") + slotName(slot),
                 waiting ? "Select an image on the left" : Treadle::fitText(slotLabel(t), 150.0f, ui.style().textScale));
        if(slot == 2 && !t.empty()) ui.slider("Normal Strength", &t.amount, 0.0f, 2.0f);
        if(slot == 3 && !t.empty()) ui.slider("Occlusion Strength", &t.amount, 0.0f, 1.0f);
        const int clicked = ui.buttonRow({waiting ? "Cancel" : "Choose Image", "Remove"});
        if(clicked == 0){
            if(waiting) state.armedMaterial = state.armedSlot = -1;
            else{ state.armedMaterial = index; state.armedSlot = slot; }
        }
        if(clicked == 1){ t = Warp::TextureSlot{}; if(waiting) state.armedMaterial = state.armedSlot = -1; }
    }
}

}

//Materijali odabranog entiteta: veza (ili po jedna za svaki primitiv modela) i uredjivanje
//materijala na koji pokazuje prva veza koja nije zadani materijal
inline void materialPanel(Treadle::Ui& ui, Warp::Stage& stage, Warp::Entity& entity, MaterialPanelState& state){
    if(!entity.mesh && !entity.model) return;
    int editing = -1;
    if(entity.mesh){
        tools::bindingRow(ui, stage, "Material", entity.mesh->material);
        editing = entity.mesh->material;
    }
    if(entity.model){
        for(size_t p = 0; p < entity.model->materials.size(); ++p){
            tools::bindingRow(ui, stage, entity.model->materials.size() > 1 ? "Primitive " + std::to_string(p) : "Material",
                              entity.model->materials[p]);
            if(editing < 0) editing = entity.model->materials[p];
        }
    }
    if(editing >= 0) tools::editMaterial(ui, stage, editing, state);
    else ui.label("(Default material - choose 'New' to edit)");
}

//=============================================================================================
// PLOHA IZ ODABIRA (S)
//=============================================================================================
struct SurfaceTool{
    bool active = false;
    bool dragging = false;
    glm::vec2 from{0.0f}, to{0.0f};
    std::vector<glm::vec3> selected;
    SurfaceFit fit;

    Treadle::Rect rect() const{
        const glm::vec2 low = glm::min(from, to), high = glm::max(from, to);
        return Treadle::Rect{low.x, low.y, high.x - low.x, high.y - low.y};
    }
};

using PointEnumerator = std::function<void(const std::function<void(const glm::vec3&)>&)>;

//Mis u pogledu dok je alat upaljen. true: mis je alatov (pogled se ne okrece)
inline bool surfaceToolMouse(SurfaceTool& tool, bool leftDown, bool leftWasDown, glm::vec2 mouse,
                             bool overViewport, const Warp::Stage& stage, double frame,
                             const ViewCamera& camera, const PointEnumerator& extra,
                             bool pressEvent = false, bool releaseEvent = false){
    if(!tool.active) return false;
    const bool leftPressed = pressEvent || (leftDown && !leftWasDown);
    const bool leftReleased = releaseEvent || (!leftDown && leftWasDown);
    if(leftPressed && overViewport){
        tool.dragging = true;
        tool.from = tool.to = mouse;
    }
    if(tool.dragging){
        tool.to = mouse;
        if(leftReleased){
            tool.dragging = false;
            const Treadle::Rect r = tool.rect();
            if(r.width > 3.0f && r.height > 3.0f){
                tool.selected = selectFrontPoints(stage, frame, camera, r, 0.05f, extra);
                tool.fit = fitSurface(tool.selected, camera.eye);
            }
        }
        return true;
    }
    return false;
}


//Pravokutnik dok se vuce, odabrane tocke, i ploha: obrub diska u ravnini i normala
inline void paintSurfaceTool(const SurfaceTool& tool, const ViewCamera& camera, Treadle::DrawList& list){
    const Treadle::Color gold{1.0f, 0.82f, 0.30f, 0.95f};
    if(tool.dragging) list.outline(tool.rect(), 1.5f, gold);
    //Najvise 12000 istaknutih tocaka: vise se na zaslonu ionako ne razlikuje, a sloj ima granicu
    const size_t stride = std::max<size_t>(1, (tool.selected.size() + 11999) / 12000);
    for(size_t i = 0; i < tool.selected.size(); i += stride){
        glm::vec2 p;
        if(project(camera, tool.selected[i], p) && camera.rect.contains(p.x, p.y)) list.rect(p.x - 1.5f, p.y - 1.5f, 3.0f, 3.0f, {1.0f, 0.9f, 0.4f, 0.8f});
    }
    if(!tool.fit.valid) return;
    //Pravokutnik koji ce ravnina pokriti, u osima u kojima ce stajati: na zidu uspravan
    const glm::vec3 x = tool.fit.tangent * std::max(tool.fit.halfWidth, 1e-4f);
    const glm::vec3 z = tool.fit.depthAxis() * std::max(tool.fit.halfDepth, 1e-4f);
    const glm::vec3 corners[4] = {tool.fit.centre - x - z, tool.fit.centre + x - z, tool.fit.centre + x + z, tool.fit.centre - x + z};
    for(int i = 0; i < 4; ++i) segment(list, camera, corners[i], corners[(i + 1) % 4], 2.0f, gold);
    const float r = std::min(tool.fit.halfWidth, tool.fit.halfDepth);
    segment(list, camera, tool.fit.centre, tool.fit.centre + tool.fit.normal * r * 0.6f, 3.0f, {0.4f, 0.9f, 0.35f, 1.0f});
    //Na zidu: kratka crta prema gore uz plohu, da se vidi da je panel uspravan
    if(tool.fit.wall) segment(list, camera, tool.fit.centre, tool.fit.centre - tool.fit.depthAxis() * r * 0.5f, 2.0f, {0.4f, 0.7f, 1.0f, 1.0f});
}

//Tijelo na odabranoj plohi, na vrhu scene. Ravnina pokrije odabrani komad; kocka stoji na njemu,
//velika 70 % krace strane komada
inline Warp::Id placeOnSurface(Warp::Stage& stage, const SurfaceTool& tool, Warp::Shape shape){
    if(!tool.fit.valid) return Warp::None;
    const Warp::Id id = stage.create(Warp::shapeName(shape));
    Warp::Entity& entity = *stage.get(id);
    entity.mesh = Warp::Mesh{shape};
    if(shape == Warp::Shape::Cube){
        const float size = std::max(1e-4f, 1.4f * std::min(tool.fit.halfWidth, tool.fit.halfDepth));
        entity.local = onSurface(tool.fit, size, shape);
    }else{
        entity.local = panelOnSurface(tool.fit);
    }
    return id;
}

//=============================================================================================
// MODEL NA MJESTU POGLEDA
//=============================================================================================
//Geometrija i materijali se procitaju odmah (bez dekodiranja slika - to je brzo), a slike i
//geometriju na kartici ucita LoomPbr u svojoj niti
inline ModelImportReport importModelAtView(Warp::Stage& stage, const std::filesystem::path& path, double frame,
                                           const ViewCamera& camera, const SceneExtent& extent){
    Spool::GltfScene scene;
    std::string error;
    Spool::GltfLoadConfig config;
    config.decodeImages = false;
    if(!Spool::loadGltf(path.string(), scene, error, config)){
        ModelImportReport report;
        report.problem = error;
        return report;
    }
    if(stage.size() == 0) return importGltf(stage, scene);

    //Na podu ispod mjesta u koje pogled gleda; metar iz visine kamere nad podom (kamera iz ruke
    //je na visini oka, oko 1.5 m) - kao lik iz pokreta
    const glm::vec2 centre(camera.frame.x + camera.frame.width * 0.5f, camera.frame.y + camera.frame.height * 0.5f);
    const Ray ray = rayThrough(camera, centre);
    glm::vec3 place;
    if(ray.direction.y < -1e-3f && ray.origin.y > 0.0f && -ray.origin.y / ray.direction.y < extent.radius * 20.0f){
        place = ray.origin + ray.direction * (-ray.origin.y / ray.direction.y);
    }else{
        glm::vec3 looked;
        if(!surfaceAt(stage, frame, camera, centre, looked)) looked = ray.origin + ray.direction * glm::length(extent.centre - ray.origin);
        place = glm::vec3(looked.x, 0.0f, looked.z);
    }
    const float scale = ray.origin.y > 1e-4f ? ray.origin.y / 1.5f : 1.0f;
    return importGltf(stage, scene, Warp::None, place, scale);
}

}
