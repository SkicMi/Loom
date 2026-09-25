#pragma once
#include "LoomEditor.h"
#include "LoomEditorTools.h"
#include "LoomModel.h"
#include "LoomResult.h"
#include "LoomSplat.h"
#include "LoomWeaverMotion.h"

#include <Spool/ImageFile.h>
#include <Warp/Project.h>
#include <Warp/Stage.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <filesystem>
#include <set>
#include <string>
#include <vector>

namespace Loom{

//=============================================================================================
// IMPORTER - prozor za uvoz svega sto editor zna procitati, na jednom mjestu.
//
// Dosad je svaka vrsta imala svoj put: snimka i rezultat iz media stupca, model preko desnog
// klika na datoteku, slika samo dok je mapa materijala "naoruzana". Importer pregleda mapu,
// svakoj datoteci kaze sto je, i nudi ono sto se s njom moze: snimku u projekt (i solve), model
// u scenu, splat, rezultat solvea, pokret, projekt - i MATERIJAL iz tekstura.
//
// MATERIJAL NEMA SVOJU DATOTEKU. PBR materijal je skup tekstura (boja, normala, hrapavost,
// metalnost, AO, emisija), a te dolaze kao slike s imenom koje kaze sto su - "brick_basecolor",
// "brick_Normal_GL", "brick_rough". Importer ih po imenu razvrsta u mjesta materijala. Hrapavost,
// metalnost i AO u glTF-u zive u JEDNOJ slici (R = AO, G = hrapavost, B = metalnost), pa se
// odvojene slike zapisu u jednu, pored izvornika
//=============================================================================================

enum class ImportKind{ Folder, Video, Result, Project, Motion, Model, Splat, Image };

struct ImportEntry{
    std::filesystem::path path;
    ImportKind kind = ImportKind::Folder;
};

inline const char* importKindName(ImportKind kind){
    switch(kind){
        case ImportKind::Folder: return "Folder";
        case ImportKind::Video: return "Video";
        case ImportKind::Result: return "Solve result";
        case ImportKind::Project: return "Loom project";
        case ImportKind::Motion: return "Motion";
        case ImportKind::Model: return "3D model";
        case ImportKind::Splat: return "Gaussian splat";
        default: return "Image / texture";
    }
}

//Filtri u redu gumba; 0 je sve
inline const std::vector<std::string>& importFilterNames(){
    static const std::vector<std::string> names = {"All", "Video", "Model", "Splat", "Solve", "Motion", "Project", "Material"};
    return names;
}

inline bool importFilterAccepts(int filter, ImportKind kind){
    switch(filter){
        case 1: return kind == ImportKind::Video;
        case 2: return kind == ImportKind::Model;
        case 3: return kind == ImportKind::Splat;
        case 4: return kind == ImportKind::Result;
        case 5: return kind == ImportKind::Motion;
        case 6: return kind == ImportKind::Project;
        case 7: return kind == ImportKind::Image;
        default: return true;
    }
}

//Sve sto je u mapi i sto editor zna uvesti, poredano: mape, pa po vrsti, pa po imenu
inline std::vector<ImportEntry> importEntriesIn(const std::filesystem::path& directory){
    namespace fs = std::filesystem;
    std::vector<ImportEntry> out;
    std::error_code error;
    if(!fs::is_directory(directory, error)) return out;

    std::set<fs::path> results;
    for(const fs::path& result : resultsIn(directory)){
        results.insert(result);
        out.push_back({result, ImportKind::Result});
    }
    for(const auto& entry : fs::directory_iterator(directory, error)){
        if(error) break;
        const std::string name = entry.path().filename().string();
        if(name.empty() || name[0] == '.') continue;
        if(entry.is_directory(error) && !results.count(entry.path())) out.push_back({entry.path(), ImportKind::Folder});
    }
    for(const fs::path& video : videosIn(directory)) out.push_back({video, ImportKind::Video});
    for(const fs::path& model : modelFilesIn(directory)) out.push_back({model, ImportKind::Model});
    for(const fs::path& motion : weaverMotionFilesIn(directory)) out.push_back({motion, ImportKind::Motion});
    for(const fs::path& image : imageFilesIn(directory)) out.push_back({image, ImportKind::Image});
    for(const auto& entry : fs::directory_iterator(directory, error)){
        if(error) break;
        if(!entry.is_regular_file(error)) continue;
        const fs::path path = entry.path();
        if(path.extension() == ".usda" && Warp::isProjectFile(path.string())) out.push_back({path, ImportKind::Project});
        else if(path.extension() == ".ply" && isGaussianPly(path.string())) out.push_back({path, ImportKind::Splat});
    }
    std::stable_sort(out.begin(), out.end(), [](const ImportEntry& a, const ImportEntry& b){
        if(a.kind != b.kind) return int(a.kind) < int(b.kind);
        return a.path.filename().string() < b.path.filename().string();
    });
    return out;
}

//-------------------------------------------------------------------------------------------
// Materijal iz tekstura
//-------------------------------------------------------------------------------------------

enum class TextureRole{ Unknown, BaseColor, Normal, Roughness, Metallic, Occlusion, Emissive, Packed };

inline std::string lowerName(const std::filesystem::path& path){
    std::string name = path.stem().string();
    std::transform(name.begin(), name.end(), name.begin(), [](unsigned char c){ return char(std::tolower(c)); });
    return name;
}

//Uloga slike po imenu, kako ih imenuju Substance, Poly Haven, ambientCG i Quixel. Rijec se trazi
//kao DIO imena odvojen znakom koji nije slovo, da "normal" u "abnormal_wall" ne bude normala
inline TextureRole textureRole(const std::filesystem::path& path){
    const std::string name = lowerName(path);
    std::vector<std::string> words;
    std::string current;
    for(char c : name){
        if(std::isalnum(static_cast<unsigned char>(c))) current += c;
        else if(!current.empty()){ words.push_back(current); current.clear(); }
    }
    if(!current.empty()) words.push_back(current);
    auto has = [&](std::initializer_list<const char*> keys){
        for(const std::string& word : words) for(const char* key : keys) if(word == key) return true;
        return false;
    };
    if(has({"orm", "arm", "occlusionroughnessmetallic", "metallicroughness"})) return TextureRole::Packed;
    if(has({"normal", "nor", "nrm", "normalgl", "normaldx", "nmap"})) return TextureRole::Normal;
    if(has({"roughness", "rough", "rgh"})) return TextureRole::Roughness;
    if(has({"metallic", "metalness", "metal", "mtl"})) return TextureRole::Metallic;
    if(has({"ao", "occlusion", "ambientocclusion", "occ"})) return TextureRole::Occlusion;
    if(has({"emissive", "emission", "emit", "glow"})) return TextureRole::Emissive;
    if(has({"basecolor", "base", "albedo", "diffuse", "diff", "color", "colour", "col"})) return TextureRole::BaseColor;
    return TextureRole::Unknown;
}

//Zajednicki dio imena tekstura jednog materijala - "brick_wall_basecolor" i "brick_wall_normal"
//daju "brick_wall". Kad ga nema, ime mape
inline std::string materialNameFor(const std::vector<std::filesystem::path>& images){
    if(images.empty()) return "Material";
    std::string prefix = images.front().stem().string();
    for(const auto& image : images){
        const std::string stem = image.stem().string();
        size_t n = 0;
        while(n < prefix.size() && n < stem.size() && prefix[n] == stem[n]) ++n;
        prefix.resize(n);
    }
    while(!prefix.empty() && !std::isalnum(static_cast<unsigned char>(prefix.back()))) prefix.pop_back();
    if(prefix.size() >= 3 && images.size() > 1) return prefix;
    if(images.size() == 1) return images.front().stem().string();
    return images.front().parent_path().filename().string();
}

struct MaterialImportReport{
    int material = -1;              //indeks u Stage::materials; -1 kad nije napravljen
    std::string name;
    std::vector<std::string> assigned;   //"Color: brick_basecolor.png", ...
    std::string packed;             //zapisana slika hrapavost/metalnost/AO, kad je trebala
    std::string problem;
};

//Iz odvojenih slika hrapavosti, metalnosti i AO jedna glTF slika (R = AO, G = hrapavost,
//B = metalnost). Sto nedostaje dobije neutralnu vrijednost: AO 1, hrapavost 1, metalnost 0
inline bool packRoughMetal(const std::filesystem::path& roughness, const std::filesystem::path& metallic,
                           const std::filesystem::path& occlusion, const std::filesystem::path& target,
                           std::string* problem){
    try{
        Spool::Image rough, metal, ao;
        if(!roughness.empty()) rough = Spool::loadImage(roughness.string());
        if(!metallic.empty()) metal = Spool::loadImage(metallic.string());
        if(!occlusion.empty()) ao = Spool::loadImage(occlusion.string());
        const Spool::Image* reference = !rough.pixels.empty() ? &rough : !metal.pixels.empty() ? &metal : &ao;
        if(reference->pixels.empty()){ if(problem) *problem = "no roughness/metallic image to pack"; return false; }
        const uint32_t width = reference->width, height = reference->height;
        auto sample = [&](const Spool::Image& image, uint32_t x, uint32_t y, uint8_t fallback){
            if(image.pixels.empty()) return fallback;
            //Slike razlicite velicine: najblizi piksel
            const uint32_t sx = uint32_t(uint64_t(x) * image.width / width), sy = uint32_t(uint64_t(y) * image.height / height);
            return image.pixels[(size_t(sy) * image.width + sx) * 4];
        };
        std::vector<uint8_t> pixels(size_t(width) * height * 4);
        for(uint32_t y = 0; y < height; ++y){
            for(uint32_t x = 0; x < width; ++x){
                uint8_t* p = pixels.data() + (size_t(y) * width + x) * 4;
                p[0] = sample(ao, x, y, 255);
                p[1] = sample(rough, x, y, 255);
                p[2] = sample(metal, x, y, 0);
                p[3] = 255;
            }
        }
        Spool::savePng(target.string(), Spool::imageFromPixels(pixels.data(), width, height));
        return true;
    }catch(const std::exception& error){
        if(problem) *problem = error.what();
        return false;
    }
}

//Novi materijal iz slika. Slika bez prepoznate uloge ide u boju kad boje jos nema - jedna
//fotografija je tako materijal s tom slikom
inline MaterialImportReport materialFromTextures(Warp::Stage& stage, const std::vector<std::filesystem::path>& images){
    namespace fs = std::filesystem;
    MaterialImportReport report;
    if(images.empty()){ report.problem = "No images selected"; return report; }

    fs::path base, normal, roughness, metallic, occlusion, emissive, packed, unknown;
    for(const fs::path& image : images){
        switch(textureRole(image)){
            case TextureRole::BaseColor: if(base.empty()) base = image; break;
            case TextureRole::Normal: if(normal.empty()) normal = image; break;
            case TextureRole::Roughness: if(roughness.empty()) roughness = image; break;
            case TextureRole::Metallic: if(metallic.empty()) metallic = image; break;
            case TextureRole::Occlusion: if(occlusion.empty()) occlusion = image; break;
            case TextureRole::Emissive: if(emissive.empty()) emissive = image; break;
            case TextureRole::Packed: if(packed.empty()) packed = image; break;
            default: if(unknown.empty()) unknown = image; break;
        }
    }
    if(base.empty()) base = unknown;

    Warp::Material material;
    material.name = materialNameFor(images);
    report.name = material.name;
    auto assign = [&](Warp::TextureSlot& slot, const fs::path& image, const char* role){
        if(image.empty()) return;
        slot.source = image.string();
        report.assigned.push_back(std::string(role) + ": " + image.filename().string());
    };
    assign(material.baseColorMap, base, "Color");
    assign(material.normalMap, normal, "Normal");
    assign(material.emissiveMap, emissive, "Emission");
    if(!emissive.empty()) material.emissive = glm::vec3(1.0f);

    if(!packed.empty()){
        assign(material.metallicRoughnessMap, packed, "Metal/Rough");
        assign(material.occlusionMap, packed, "Occlusion");
        material.metallic = 1.0f;
        material.roughness = 1.0f;
    }else if(!roughness.empty() || !metallic.empty() || !occlusion.empty()){
        const fs::path source = !roughness.empty() ? roughness : !metallic.empty() ? metallic : occlusion;
        const fs::path target = source.parent_path() / (material.name + "_orm.png");
        std::string problem;
        if(packRoughMetal(roughness, metallic, occlusion, target, &problem)){
            report.packed = target.string();
            assign(material.metallicRoughnessMap, target, "Metal/Rough");
            if(!occlusion.empty()) assign(material.occlusionMap, target, "Occlusion");
            //Faktori mnoze sliku: pun raspon kad slika postoji, inace zadano
            material.roughness = roughness.empty() ? 0.5f : 1.0f;
            material.metallic = metallic.empty() ? 0.0f : 1.0f;
        }else{
            report.problem = "Roughness/metallic not packed: " + problem;
        }
    }
    if(report.assigned.empty()){ report.problem = "No usable texture among the selected images"; return report; }

    stage.materials.push_back(material);
    report.material = int(stage.materials.size()) - 1;
    return report;
}

//Materijal na entitet: kocka i ploha imaju jedan, model svaki primitiv. false kad entitet nema
//cemu dati materijal
inline bool assignMaterial(Warp::Stage& stage, Warp::Id id, int material){
    Warp::Entity* entity = stage.get(id);
    if(!entity || material < 0) return false;
    if(entity->mesh){ entity->mesh->material = material; return true; }
    if(entity->model){
        if(entity->model->materials.empty()) entity->model->materials.push_back(material);
        for(int& slot : entity->model->materials) slot = material;
        return true;
    }
    return false;
}

//-------------------------------------------------------------------------------------------
// Kamera iz pogleda
//-------------------------------------------------------------------------------------------

//Nova kamera tocno gdje je pogled: polozaj, smjer i vidno polje (kadar 1920x1080). world je
//matrica svijeta kamere (inverz pogleda), focalAtHeight zarisna u pikselima za visinu viewHeight
inline Warp::Id addCameraFromView(Warp::Stage& stage, const glm::mat4& world, float focalAtHeight, float viewHeight,
                                  double frame, Warp::Id parent = Warp::None){
    int count = 0;
    stage.walk([&](const Warp::Entity& e, int){ if(e.camera) ++count; });
    const Warp::Id id = stage.create("Camera " + std::to_string(count + 1), parent);
    Warp::Entity& entity = *stage.get(id);
    Warp::Camera lens;
    lens.width = 1920;
    lens.height = 1080;
    lens.centreX = 960.0f;
    lens.centreY = 540.0f;
    lens.focalPixels = viewHeight > 0.0f ? focalAtHeight * float(lens.height) / viewHeight : 1158.0f;
    entity.camera = lens;
    glm::mat4 local = world;
    if(parent != Warp::None) local = glm::inverse(stage.worldMatrix(parent, frame)) * world;
    entity.local.translation = glm::vec3(local[3]);
    entity.local.rotation = glm::normalize(glm::quat_cast(glm::mat3(glm::normalize(glm::vec3(local[0])),
                                                                     glm::normalize(glm::vec3(local[1])),
                                                                     glm::normalize(glm::vec3(local[2])))));
    return id;
}

//Stanje prozora: gdje je, sto je odabrano, koji filtar
struct ImporterState{
    bool open = false;
    std::filesystem::path at;
    std::vector<ImportEntry> entries;
    std::set<std::filesystem::path> chosen;     //vise slika za materijal; inace jedna datoteka
    int filter = 0;
    float scroll = 0.0f;
    std::string pathText;
    std::string status;

    void show(const std::filesystem::path& folder){
        open = true;
        if(!folder.empty()) at = folder;
        refresh();
    }
    void refresh(){
        entries = importEntriesIn(at);
        chosen.clear();
        pathText = at.string();
        scroll = 0.0f;
    }
    void enter(const std::filesystem::path& folder){
        std::error_code error;
        if(!std::filesystem::is_directory(folder, error)) return;
        at = folder;
        refresh();
    }
    const ImportEntry* single() const {
        if(chosen.size() != 1) return nullptr;
        for(const ImportEntry& entry : entries) if(entry.path == *chosen.begin()) return &entry;
        return nullptr;
    }
    std::vector<std::filesystem::path> chosenImages() const {
        std::vector<std::filesystem::path> out;
        for(const ImportEntry& entry : entries) if(entry.kind == ImportKind::Image && chosen.count(entry.path)) out.push_back(entry.path);
        return out;
    }
    std::vector<std::filesystem::path> allImages() const {
        std::vector<std::filesystem::path> out;
        for(const ImportEntry& entry : entries) if(entry.kind == ImportKind::Image) out.push_back(entry.path);
        return out;
    }
};

}
