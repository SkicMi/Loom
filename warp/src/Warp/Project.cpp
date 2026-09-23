#include "Warp/Project.h"
#include "Warp/Usda.h"

#include <cctype>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <sstream>

namespace Warp{

namespace{

constexpr const char* projectMarker = "loomProject";

//-- pisanje ------------------------------------------------------------------------------------

class Writer{
public:
    explicit Writer(std::ostream& out) : out(out){}

    void number(double value){
        char text[40];
        std::snprintf(text, sizeof(text), "%.9g", value);
        out << text;
    }
    void time(double value){
        char text[40];
        std::snprintf(text, sizeof(text), "%.12g", value);
        out << text;
    }
    void vector(const glm::vec3& v){ out << '('; number(v.x); out << ", "; number(v.y); out << ", "; number(v.z); out << ')'; }
    //USD pise kvaternion kao (w, x, y, z) - isti redoslijed kao glm::quat konstruktor
    void quaternion(const glm::quat& q){
        out << '('; number(q.w); out << ", "; number(q.x); out << ", "; number(q.y); out << ", "; number(q.z); out << ')';
    }
    void string(const std::string& value){
        out << '"';
        for(char c : value){
            if(c == '"' || c == '\\') out << '\\';
            if(c == '\n'){ out << "\\n"; continue; }
            out << c;
        }
        out << '"';
    }
    //Put do datoteke kao USD asset (@...@) - osim kad sadrzi @, tada kao string
    void asset(const std::string& value){
        if(value.find('@') == std::string::npos) out << '@' << value << '@';
        else string(value);
    }
    void indent(int depth){ for(int i = 0; i < depth; ++i) out << "    "; }

    //Ime prima materijala: USD ime (slova, brojke, _), jedinstveno po rednom broju
    static std::string materialPrim(const Stage& stage, int index){
        std::string name;
        for(char c : stage.materials[size_t(index)].name) name += (std::isalnum(static_cast<unsigned char>(c)) || c == '_') ? c : '_';
        if(name.empty() || std::isdigit(static_cast<unsigned char>(name[0]))) name = "_" + name;
        return name + "_" + std::to_string(index);
    }

    void slot(int depth, const char* name, const TextureSlot& t){
        if(t.empty()) return;
        indent(depth); out << "custom asset loom:" << name << " = "; asset(t.source); out << '\n';
        indent(depth); out << "custom int4 loom:" << name << "Info = (" << t.image << ", " << t.texCoord << ", 0, 0)\n";
        indent(depth); out << "custom float loom:" << name << "Amount = "; number(t.amount); out << '\n';
    }

    //Materijal kao USD Material s UsdPreviewSurfaceom - drugi alati vide boju, metalnost,
    //hrapavost i emisiju; tocne vrijednosti i mape za povratak u Loom su u loom: atributima
    void material(const Stage& stage, int index){
        const Material& m = stage.materials[size_t(index)];
        const std::string prim = materialPrim(stage, index);
        out << "\n    def Material \"" << prim << "\"\n    {\n";
        out << "        token outputs:surface.connect = </Materials/" << prim << "/Surface.outputs:surface>\n";
        out << "        custom string loom:name = "; string(m.name); out << '\n';
        out << "        custom float4 loom:baseColor = ("; number(m.baseColor.r); out << ", "; number(m.baseColor.g); out << ", ";
        number(m.baseColor.b); out << ", "; number(m.baseColor.a); out << ")\n";
        out << "        custom float loom:metallic = "; number(m.metallic); out << '\n';
        out << "        custom float loom:roughness = "; number(m.roughness); out << '\n';
        out << "        custom color3f loom:emissive = "; vector(m.emissive); out << '\n';
        out << "        custom float loom:emissiveStrength = "; number(m.emissiveStrength); out << '\n';
        out << "        custom token loom:alphaMode = \"" << (m.alphaMode == Material::Alpha::Mask ? "mask"
                                                              : m.alphaMode == Material::Alpha::Blend ? "blend" : "opaque") << "\"\n";
        out << "        custom float loom:alphaCutoff = "; number(m.alphaCutoff); out << '\n';
        out << "        custom bool loom:doubleSided = " << (m.doubleSided ? 1 : 0) << '\n';
        slot(2, "baseColorMap", m.baseColorMap);
        slot(2, "metallicRoughnessMap", m.metallicRoughnessMap);
        slot(2, "normalMap", m.normalMap);
        slot(2, "occlusionMap", m.occlusionMap);
        slot(2, "emissiveMap", m.emissiveMap);
        out << "\n        def Shader \"Surface\"\n        {\n";
        out << "            uniform token info:id = \"UsdPreviewSurface\"\n";
        out << "            color3f inputs:diffuseColor = ("; number(m.baseColor.r); out << ", "; number(m.baseColor.g); out << ", ";
        number(m.baseColor.b); out << ")\n";
        out << "            float inputs:metallic = "; number(m.metallic); out << '\n';
        out << "            float inputs:roughness = "; number(m.roughness); out << '\n';
        out << "            float inputs:opacity = "; number(m.alphaMode == Material::Alpha::Opaque ? 1.0 : m.baseColor.a); out << '\n';
        if(m.alphaMode == Material::Alpha::Mask){ out << "            float inputs:opacityThreshold = "; number(m.alphaCutoff); out << '\n'; }
        out << "            color3f inputs:emissiveColor = "; vector(m.emissive * m.emissiveStrength); out << '\n';
        out << "            token outputs:surface\n        }\n    }\n";
    }

    template<class T, class Write>
    void attribute(int depth, const char* type, const char* name, const T& fixed, const Track<T>& keys, Write write){
        indent(depth); out << type << ' ' << name << " = "; write(fixed); out << '\n';
        if(keys.empty()) return;
        indent(depth); out << type << ' ' << name << ".timeSamples = {\n";
        for(size_t i = 0; i < keys.size(); ++i){
            indent(depth + 1); time(keys.times[i]); out << ": "; write(keys.values[i]);
            out << (i + 1 < keys.size() ? ",\n" : "\n");
        }
        indent(depth); out << "}\n";
    }

    void prim(const Stage& stage, const Entity& entity, int depth){
        const char* type = entity.camera ? "Camera" : entity.points ? "Points"
                         : entity.mesh ? (entity.mesh->shape == Shape::Cube ? "Cube" : "Mesh") : "Xform";
        indent(depth); out << "def " << type << ' '; string(entity.name); out << "\n";
        indent(depth); out << "{\n";
        const int in = depth + 1;

        attribute(in, "double3", "xformOp:translate", entity.local.translation, entity.translationKeys,
                  [&](const glm::vec3& v){ vector(v); });
        attribute(in, "quatf", "xformOp:orient", entity.local.rotation, entity.rotationKeys,
                  [&](const glm::quat& q){ quaternion(q); });
        attribute(in, "float3", "xformOp:scale", entity.local.scale, entity.scaleKeys,
                  [&](const glm::vec3& v){ vector(v); });
        indent(in); out << "uniform token[] xformOpOrder = [\"xformOp:translate\", \"xformOp:orient\", \"xformOp:scale\"]\n";
        if(!entity.visible){ indent(in); out << "token visibility = \"invisible\"\n"; }

        if(entity.camera){
            const Camera& lens = *entity.camera;
            const double aperture = 36.0;
            const double millimetresPerPixel = aperture / double(std::max(1u, lens.width));
            indent(in); out << "float focalLength = "; number(lens.focalPixels * millimetresPerPixel); out << '\n';
            indent(in); out << "float horizontalAperture = "; number(aperture); out << '\n';
            indent(in); out << "float verticalAperture = "; number(double(lens.height) * millimetresPerPixel); out << '\n';
            //Pomak glavne tocke od sredine; USD-ov y ide gore, piksel dolje
            indent(in); out << "float horizontalApertureOffset = "; number((lens.centreX - 0.5 * lens.width) * millimetresPerPixel); out << '\n';
            indent(in); out << "float verticalApertureOffset = "; number((0.5 * lens.height - lens.centreY) * millimetresPerPixel); out << '\n';
            indent(in); out << "float2 clippingRange = (0.001, 100000)\n";
            indent(in); out << "custom float loom:focalPixels = "; number(lens.focalPixels); out << '\n';
            indent(in); out << "custom float2 loom:centre = ("; number(lens.centreX); out << ", "; number(lens.centreY); out << ")\n";
            indent(in); out << "custom int2 loom:resolution = (" << lens.width << ", " << lens.height << ")\n";
            if(!lens.plate.empty()){ indent(in); out << "custom asset loom:plate = "; asset(lens.plate); out << '\n'; }
            indent(in); out << "custom int loom:plateFirstFrame = " << lens.plateFirstFrame << '\n';
        }
        if(entity.points){
            const Points& points = *entity.points;
            indent(in); out << "point3f[] points = [";
            for(size_t i = 0; i < points.positions.size(); ++i){ if(i) out << ", "; vector(points.positions[i]); }
            out << "]\n";
            if(points.colours.size() == points.positions.size() && !points.colours.empty()){
                //Boja kao cijeli bajt kroz 255: tako se procita natrag u isti bajt
                indent(in); out << "color3f[] primvars:displayColor = [";
                for(size_t i = 0; i < points.colours.size(); ++i){
                    if(i) out << ", ";
                    const glm::u8vec3& c = points.colours[i];
                    out << '(' << double(c.r) / 255.0 << ", " << double(c.g) / 255.0 << ", " << double(c.b) / 255.0 << ')';
                }
                out << "]\n";
                indent(in); out << "uniform token primvars:displayColor:interpolation = \"vertex\"\n";
            }
        }
        if(entity.mesh){
            const Mesh& mesh = *entity.mesh;
            if(mesh.shape == Shape::Cube){
                indent(in); out << "double size = 1\n";
            }else{
                indent(in); out << "int[] faceVertexCounts = [4]\n";
                indent(in); out << "int[] faceVertexIndices = [0, 3, 2, 1]\n";
                indent(in); out << "point3f[] points = [(-0.5, 0, -0.5), (0.5, 0, -0.5), (0.5, 0, 0.5), (-0.5, 0, 0.5)]\n";
                indent(in); out << "custom token loom:shape = \"plane\"\n";
            }
            indent(in); out << "color3f[] primvars:displayColor = ["; vector(mesh.colour); out << "]\n";
            if(mesh.material >= 0 && size_t(mesh.material) < stage.materials.size()){
                indent(in); out << "rel material:binding = </Materials/" << materialPrim(stage, mesh.material) << ">\n";
                indent(in); out << "custom string loom:material = "; string(stage.materials[size_t(mesh.material)].name); out << '\n';
            }
        }
        if(entity.model){
            const Model& model = *entity.model;
            indent(in); out << "custom asset loom:model = "; asset(model.path); out << '\n';
            indent(in); out << "custom int loom:mesh = " << model.mesh << '\n';
            indent(in); out << "custom string[] loom:materials = [";
            for(size_t i = 0; i < model.materials.size(); ++i){
                if(i) out << ", ";
                const int m = model.materials[i];
                string(m >= 0 && size_t(m) < stage.materials.size() ? stage.materials[size_t(m)].name : std::string());
            }
            out << "]\n";
        }
        if(entity.splat){ indent(in); out << "custom asset loom:splat = "; asset(entity.splat->path); out << '\n'; }
        if(entity.joint){ indent(in); out << "custom color3f loom:joint = "; vector(entity.joint->colour); out << '\n'; }

        for(Id child : entity.children){
            out << '\n';
            prim(stage, *stage.get(child), in);
        }
        indent(depth); out << "}\n";
    }

    std::ostream& out;
};

//-- citanje ------------------------------------------------------------------------------------

glm::vec3 asVector(const usda::Value& v, glm::vec3 fallback){
    if(v.kind != usda::Value::Kind::List || v.items.size() < 3) return fallback;
    return glm::vec3(float(v.at(0)), float(v.at(1)), float(v.at(2)));
}

glm::quat asQuaternion(const usda::Value& v, glm::quat fallback){
    if(v.kind != usda::Value::Kind::List || v.items.size() < 4) return fallback;
    //Normalizira se samo kvaternion koji stvarno nije jedinicni (tudja datoteka). Loomov je
    //zapisan jedinicni, i ponovna normalizacija bi mu promijenila zadnji bit pri svakom otvaranju
    const glm::quat q(float(v.at(0)), float(v.at(1)), float(v.at(2)), float(v.at(3)));
    return std::fabs(glm::length(q) - 1.0f) > 1e-5f ? glm::normalize(q) : q;
}

double numberOf(const usda::Prim& prim, const std::string& name, double fallback){
    const usda::Attribute* a = prim.find(name);
    return a && a->value.kind == usda::Value::Kind::Number ? a->value.number : fallback;
}

std::string textOf(const usda::Prim& prim, const std::string& name){
    const usda::Attribute* a = prim.find(name);
    return a && a->value.kind == usda::Value::Kind::String ? a->value.text : std::string();
}

void readEntity(const usda::Prim& prim, Stage& stage, Id parent){
    const Id id = stage.create(prim.name, parent);
    Entity& entity = *stage.get(id);

    if(const usda::Attribute* a = prim.find("xformOp:translate")){
        entity.local.translation = asVector(a->value, entity.local.translation);
        for(const auto& [time, value] : a->samples.samples) entity.translationKeys.set(time, asVector(value, glm::vec3(0.0f)));
    }
    if(const usda::Attribute* a = prim.find("xformOp:orient")){
        entity.local.rotation = asQuaternion(a->value, entity.local.rotation);
        for(const auto& [time, value] : a->samples.samples) entity.rotationKeys.set(time, asQuaternion(value, glm::quat(1, 0, 0, 0)));
    }
    if(const usda::Attribute* a = prim.find("xformOp:scale")){
        entity.local.scale = asVector(a->value, entity.local.scale);
        for(const auto& [time, value] : a->samples.samples) entity.scaleKeys.set(time, asVector(value, glm::vec3(1.0f)));
    }
    entity.visible = textOf(prim, "visibility") != "invisible";

    if(prim.type == "Camera"){
        Camera lens;
        if(const usda::Attribute* r = prim.find("loom:resolution")){
            lens.width = uint32_t(r->value.at(0, lens.width));
            lens.height = uint32_t(r->value.at(1, lens.height));
        }
        const double aperture = numberOf(prim, "horizontalAperture", 36.0);
        lens.focalPixels = float(numberOf(prim, "loom:focalPixels",
                                          numberOf(prim, "focalLength", 50.0) / aperture * lens.width));
        lens.centreX = 0.5f * float(lens.width);
        lens.centreY = 0.5f * float(lens.height);
        if(const usda::Attribute* c = prim.find("loom:centre")){
            lens.centreX = float(c->value.at(0, lens.centreX));
            lens.centreY = float(c->value.at(1, lens.centreY));
        }
        lens.plate = textOf(prim, "loom:plate");
        lens.plateFirstFrame = int(numberOf(prim, "loom:plateFirstFrame", 0.0));
        entity.camera = lens;
    }
    if(prim.type == "Points"){
        Points points;
        if(const usda::Attribute* p = prim.find("points")){
            points.positions.reserve(p->value.items.size());
            for(const usda::Value& v : p->value.items) points.positions.push_back(asVector(v, glm::vec3(0.0f)));
        }
        if(const usda::Attribute* c = prim.find("primvars:displayColor")){
            if(c->value.items.size() == points.positions.size()){
                points.colours.reserve(c->value.items.size());
                for(const usda::Value& v : c->value.items){
                    const glm::vec3 colour = asVector(v, glm::vec3(0.0f));
                    points.colours.push_back(glm::u8vec3(glm::clamp(glm::round(colour * 255.0f), 0.0f, 255.0f)));
                }
            }
        }
        entity.points = std::move(points);
    }
    if(prim.type == "Cube" || (prim.type == "Mesh" && textOf(prim, "loom:shape") == "plane")){
        Mesh mesh;
        mesh.shape = prim.type == "Cube" ? Shape::Cube : Shape::Plane;
        if(const usda::Attribute* c = prim.find("primvars:displayColor")){
            if(!c->value.items.empty()) mesh.colour = asVector(c->value.items[0], mesh.colour);
        }
        const std::string bound = textOf(prim, "loom:material");
        for(size_t i = 0; i < stage.materials.size() && !bound.empty(); ++i) if(stage.materials[i].name == bound) mesh.material = int(i);
        entity.mesh = mesh;
    }
    const std::string modelPath = textOf(prim, "loom:model");
    if(!modelPath.empty()){
        Model model;
        model.path = modelPath;
        model.mesh = int(numberOf(prim, "loom:mesh", -1.0));
        if(const usda::Attribute* list = prim.find("loom:materials")){
            for(const usda::Value& v : list->value.items){
                int index = -1;
                for(size_t i = 0; i < stage.materials.size() && !v.text.empty(); ++i) if(stage.materials[i].name == v.text) index = int(i);
                model.materials.push_back(index);
            }
        }
        entity.model = model;
    }
    if(const usda::Attribute* joint = prim.find("loom:joint")) entity.joint = Joint{asVector(joint->value, Joint{}.colour)};
    const std::string splat = textOf(prim, "loom:splat");
    if(!splat.empty()) entity.splat = Splat{splat};

    for(const usda::Prim& child : prim.children) readEntity(child, stage, id);
}

}

bool saveProject(const Stage& stage, const std::string& path, std::string& error){
    //Atomski: prvo privremena datoteka, pa preimenovanje. Spremanje koje padne na pola ne smije
    //pojesti jedini primjerak projekta
    const std::string temporary = path + ".tmp";
    {
        std::ofstream file(temporary);
        if(!file){ error = "ne mogu pisati " + temporary; return false; }
        Writer writer(file);
        std::string defaultPrim;
        if(!stage.roots().empty()) defaultPrim = stage.get(stage.roots().front())->name;

        file << "#usda 1.0\n(\n";
        file << "    customLayerData = {\n        string " << projectMarker << " = \"1\"\n    }\n";
        if(!defaultPrim.empty()){ file << "    defaultPrim = "; writer.string(defaultPrim); file << '\n'; }
        file << "    upAxis = \"Y\"\n    metersPerUnit = 1\n";
        file << "    startTimeCode = "; writer.time(stage.startFrame); file << '\n';
        file << "    endTimeCode = "; writer.time(stage.endFrame); file << '\n';
        file << "    timeCodesPerSecond = "; writer.time(stage.framesPerSecond); file << '\n';
        file << "    framesPerSecond = "; writer.time(stage.framesPerSecond); file << '\n';
        file << ")\n";
        file << "\n# Loomov projekt. Scena je USD: kamera iz solvea, tocke i sve sto je postavljeno.\n";
        file << "# MJERILO JE SLOBODNO - solve iz same snimke ne zna metre.\n";

        for(Id root : stage.roots()){
            file << '\n';
            writer.prim(stage, *stage.get(root), 0);
        }

        if(!stage.materials.empty()){
            file << "\ndef Scope \"Materials\"\n{\n    custom bool loom:materialLibrary = 1\n";
            for(size_t i = 0; i < stage.materials.size(); ++i) writer.material(stage, int(i));
            file << "}\n";
        }

        if(!stage.media.empty()){
            file << "\ndef Scope \"LoomMedia\"\n{\n    custom bool loom:media = 1\n";
            for(size_t i = 0; i < stage.media.size(); ++i){
                const Media& media = stage.media[i];
                file << "\n    def Scope \"snimka" << i << "\"\n    {\n";
                file << "        custom asset loom:path = "; writer.asset(media.path); file << '\n';
                file << "        custom string loom:result = "; writer.string(media.result); file << '\n';
                file << "        custom int loom:frames = " << media.frames << '\n';
                file << "        custom double loom:framesPerSecond = "; writer.time(media.framesPerSecond); file << '\n';
                file << "        custom int2 loom:size = (" << media.width << ", " << media.height << ")\n";
                file << "    }\n";
            }
            file << "}\n";
        }
        if(!file){ error = "pisanje u " + temporary + " nije uspjelo"; return false; }
    }
    if(std::rename(temporary.c_str(), path.c_str()) != 0){
        error = "ne mogu preimenovati " + temporary + " u " + path;
        return false;
    }
    return true;
}

bool loadProject(const std::string& path, Stage& stage, std::string& error){
    std::ifstream file(path);
    if(!file){ error = "ne mogu otvoriti " + path; return false; }
    std::stringstream buffer;
    buffer << file.rdbuf();

    usda::Layer layer;
    if(!usda::parse(buffer.str(), layer, error)){
        error = path + ": " + error;
        return false;
    }

    Stage loaded;
    if(const usda::Value* v = layer.meta("startTimeCode")) loaded.startFrame = v->number;
    if(const usda::Value* v = layer.meta("endTimeCode")) loaded.endFrame = v->number;
    if(const usda::Value* v = layer.meta("framesPerSecond")) loaded.framesPerSecond = v->number;
    else if(const usda::Value* t = layer.meta("timeCodesPerSecond")) loaded.framesPerSecond = t->number;

    //Materijali PRVI: tijela na njih pokazuju imenom, a scope je zapisan iza scene
    for(const usda::Prim& prim : layer.prims){
        if(!prim.find("loom:materialLibrary")) continue;
        for(const usda::Prim& entry : prim.children){
            Material m;
            m.name = textOf(entry, "loom:name");
            if(m.name.empty()) m.name = entry.name;
            if(const usda::Attribute* c = entry.find("loom:baseColor")){
                m.baseColor = glm::vec4(float(c->value.at(0, 1)), float(c->value.at(1, 1)), float(c->value.at(2, 1)), float(c->value.at(3, 1)));
            }else if(!entry.children.empty()){
                //Tudji materijal: boja iz UsdPreviewSurfacea
                if(const usda::Attribute* d = entry.children.front().find("inputs:diffuseColor")) m.baseColor = glm::vec4(asVector(d->value, glm::vec3(1.0f)), 1.0f);
            }
            m.metallic = float(numberOf(entry, "loom:metallic", m.metallic));
            m.roughness = float(numberOf(entry, "loom:roughness", m.roughness));
            if(const usda::Attribute* e = entry.find("loom:emissive")) m.emissive = asVector(e->value, m.emissive);
            m.emissiveStrength = float(numberOf(entry, "loom:emissiveStrength", 1.0));
            const std::string alpha = textOf(entry, "loom:alphaMode");
            m.alphaMode = alpha == "mask" ? Material::Alpha::Mask : alpha == "blend" ? Material::Alpha::Blend : Material::Alpha::Opaque;
            m.alphaCutoff = float(numberOf(entry, "loom:alphaCutoff", 0.5));
            m.doubleSided = numberOf(entry, "loom:doubleSided", 0.0) != 0.0;
            auto slot = [&](const char* name, TextureSlot& t){
                t.source = textOf(entry, std::string("loom:") + name);
                if(const usda::Attribute* info = entry.find(std::string("loom:") + name + "Info")){
                    t.image = int(info->value.at(0, -1));
                    t.texCoord = int(info->value.at(1, 0));
                }
                t.amount = float(numberOf(entry, std::string("loom:") + name + "Amount", 1.0));
            };
            slot("baseColorMap", m.baseColorMap);
            slot("metallicRoughnessMap", m.metallicRoughnessMap);
            slot("normalMap", m.normalMap);
            slot("occlusionMap", m.occlusionMap);
            slot("emissiveMap", m.emissiveMap);
            loaded.materials.push_back(m);
        }
    }

    for(const usda::Prim& prim : layer.prims){
        if(prim.find("loom:materialLibrary")) continue;
        if(prim.find("loom:media")){
            for(const usda::Prim& entry : prim.children){
                Media media;
                media.path = textOf(entry, "loom:path");
                media.result = textOf(entry, "loom:result");
                media.frames = uint32_t(numberOf(entry, "loom:frames", 0.0));
                media.framesPerSecond = numberOf(entry, "loom:framesPerSecond", 25.0);
                if(const usda::Attribute* size = entry.find("loom:size")){
                    media.width = uint32_t(size->value.at(0));
                    media.height = uint32_t(size->value.at(1));
                }
                loaded.media.push_back(media);
            }
            continue;
        }
        readEntity(prim, loaded, None);
    }
    stage = std::move(loaded);
    return true;
}

bool isProjectFile(const std::string& path){
    std::ifstream file(path);
    if(!file) return false;
    std::string line;
    for(int i = 0; i < 6 && std::getline(file, line); ++i){
        if(line.find(projectMarker) != std::string::npos) return true;
    }
    return false;
}

}
