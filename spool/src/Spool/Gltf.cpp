#include "Spool/Gltf.h"

#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <sstream>

namespace Spool{

namespace{

//=============================================================================================
// JSON - samo ono sto glTF treba: objekti, nizovi, brojevi, stringovi, true/false/null
//=============================================================================================
struct Json{
    enum class Kind{ Null, Bool, Number, String, Array, Object } kind = Kind::Null;
    bool boolean = false;
    double number = 0.0;
    std::string text;
    std::vector<Json> items;
    std::vector<std::pair<std::string, Json>> members;

    const Json* get(const char* key) const{
        if(kind != Kind::Object) return nullptr;
        for(const auto& [name, value] : members) if(name == key) return &value;
        return nullptr;
    }
    double num(const char* key, double fallback) const{
        const Json* v = get(key);
        return v && v->kind == Kind::Number ? v->number : fallback;
    }
    int integer(const char* key, int fallback) const{ return int(num(key, fallback)); }
    std::string str(const char* key) const{
        const Json* v = get(key);
        return v && v->kind == Kind::String ? v->text : std::string();
    }
    bool flag(const char* key, bool fallback) const{
        const Json* v = get(key);
        return v && v->kind == Kind::Bool ? v->boolean : fallback;
    }
    size_t size() const {return items.size();}
};

class JsonReader{
public:
    explicit JsonReader(const std::string& text) : text(text){}

    Json parse(){
        Json value = parseValue();
        skip();
        if(at != text.size()) fail("extra data after the JSON");
        return value;
    }

private:
    [[noreturn]] void fail(const std::string& what){ throw std::runtime_error("JSON, character " + std::to_string(at) + ": " + what); }
    void skip(){ while(at < text.size() && (text[at] == ' ' || text[at] == '\n' || text[at] == '\r' || text[at] == '\t')) ++at; }
    char peek(){ skip(); return at < text.size() ? text[at] : '\0'; }
    void expect(char c){ if(peek() != c) fail(std::string("expected '") + c + "'"); ++at; }

    Json parseValue(){
        const char c = peek();
        Json out;
        if(c == '{'){
            out.kind = Json::Kind::Object;
            ++at;
            if(peek() == '}'){ ++at; return out; }
            while(true){
                std::string key = parseString();
                expect(':');
                out.members.push_back({std::move(key), parseValue()});
                if(peek() == ','){ ++at; continue; }
                expect('}');
                return out;
            }
        }
        if(c == '['){
            out.kind = Json::Kind::Array;
            ++at;
            if(peek() == ']'){ ++at; return out; }
            while(true){
                out.items.push_back(parseValue());
                if(peek() == ','){ ++at; continue; }
                expect(']');
                return out;
            }
        }
        if(c == '"'){ out.kind = Json::Kind::String; out.text = parseString(); return out; }
        if(text.compare(at, 4, "true") == 0){ at += 4; out.kind = Json::Kind::Bool; out.boolean = true; return out; }
        if(text.compare(at, 5, "false") == 0){ at += 5; out.kind = Json::Kind::Bool; return out; }
        if(text.compare(at, 4, "null") == 0){ at += 4; return out; }
        char* end = nullptr;
        out.number = std::strtod(text.c_str() + at, &end);
        if(end == text.c_str() + at) fail("unexpected character");
        at = size_t(end - text.c_str());
        out.kind = Json::Kind::Number;
        return out;
    }

    std::string parseString(){
        expect('"');
        std::string out;
        while(at < text.size() && text[at] != '"'){
            char c = text[at++];
            if(c != '\\'){ out += c; continue; }
            if(at >= text.size()) break;
            const char e = text[at++];
            switch(e){
                case 'n': out += '\n'; break;
                case 't': out += '\t'; break;
                case 'r': out += '\r'; break;
                case 'b': out += '\b'; break;
                case 'f': out += '\f'; break;
                case 'u':{
                    //Imena u glTF-u su UTF-8; \uXXXX se pretvori u UTF-8 (bez zamjenskih parova)
                    if(at + 4 > text.size()) fail("short \\u");
                    const unsigned code = unsigned(std::stoul(text.substr(at, 4), nullptr, 16));
                    at += 4;
                    if(code < 0x80) out += char(code);
                    else if(code < 0x800){ out += char(0xC0 | (code >> 6)); out += char(0x80 | (code & 0x3F)); }
                    else{ out += char(0xE0 | (code >> 12)); out += char(0x80 | ((code >> 6) & 0x3F)); out += char(0x80 | (code & 0x3F)); }
                    break;
                }
                default: out += e;
            }
        }
        if(at >= text.size()) fail("unterminated string");
        ++at;
        return out;
    }

    const std::string& text;
    size_t at = 0;
};

//=============================================================================================
// buffers
//=============================================================================================
std::vector<uint8_t> base64(const std::string& text){
    static int table[256];
    static bool ready = false;
    if(!ready){
        for(int& v : table) v = -1;
        const char* alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        for(int i = 0; i < 64; ++i) table[uint8_t(alphabet[i])] = i;
        ready = true;
    }
    std::vector<uint8_t> out;
    out.reserve(text.size() * 3 / 4);
    uint32_t bits = 0;
    int count = 0;
    for(char c : text){
        const int v = table[uint8_t(c)];
        if(v < 0) continue;
        bits = (bits << 6) | uint32_t(v);
        count += 6;
        if(count >= 8){ count -= 8; out.push_back(uint8_t((bits >> count) & 0xFF)); }
    }
    return out;
}

//URI u glTF-u je URL-kodiran: "moja%20tekstura.png"
std::string uriDecode(const std::string& uri){
    std::string out;
    for(size_t i = 0; i < uri.size(); ++i){
        if(uri[i] == '%' && i + 2 < uri.size()){
            out += char(std::stoi(uri.substr(i + 1, 2), nullptr, 16));
            i += 2;
        }else out += uri[i];
    }
    return out;
}

bool readFile(const std::filesystem::path& path, std::vector<uint8_t>& out){
    std::ifstream file(path, std::ios::binary);
    if(!file) return false;
    out.assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
    return true;
}

bool resolveUri(const std::string& uri, const std::filesystem::path& base, std::vector<uint8_t>& out, std::string& error){
    if(uri.rfind("data:", 0) == 0){
        const size_t comma = uri.find(',');
        if(comma == std::string::npos || uri.find(";base64", 0) > comma){ error = "data URI without base64"; return false; }
        out = base64(uri.substr(comma + 1));
        return true;
    }
    const std::filesystem::path path = base / uriDecode(uri);
    if(!readFile(path, out)){ error = "cannot read " + path.string(); return false; }
    return true;
}

//=============================================================================================
// akcesori
//=============================================================================================
struct Model{
    const Json* root = nullptr;
    std::vector<std::vector<uint8_t>> buffers;
};

int componentsOf(const std::string& type){
    if(type == "SCALAR") return 1;
    if(type == "VEC2") return 2;
    if(type == "VEC3") return 3;
    if(type == "VEC4") return 4;
    if(type == "MAT2") return 4;
    if(type == "MAT3") return 9;
    if(type == "MAT4") return 16;
    return 0;
}

size_t bytesOf(int componentType){
    switch(componentType){
        case 5120: case 5121: return 1;
        case 5122: case 5123: return 2;
        case 5125: case 5126: return 4;
        default: return 0;
    }
}

//Jedna komponenta u float. normalized: cijeli broj u [0, 1] ili [-1, 1], kako glTF propisuje
float componentAt(const uint8_t* p, int type, bool normalized){
    switch(type){
        case 5126:{ float v; std::memcpy(&v, p, 4); return v; }
        case 5121: return normalized ? float(*p) / 255.0f : float(*p);
        case 5120:{ const int8_t v = int8_t(*p); return normalized ? std::max(float(v) / 127.0f, -1.0f) : float(v); }
        case 5123:{ uint16_t v; std::memcpy(&v, p, 2); return normalized ? float(v) / 65535.0f : float(v); }
        case 5122:{ int16_t v; std::memcpy(&v, p, 2); return normalized ? std::max(float(v) / 32767.0f, -1.0f) : float(v); }
        case 5125:{ uint32_t v; std::memcpy(&v, p, 4); return float(v); }
        default: return 0.0f;
    }
}

//Akcesor u niz floatova, `components` po elementu. Vraca broj elemenata; -1 kad ne valja
long readAccessor(const Model& model, int index, std::vector<float>& out, int& components, std::string& error){
    const Json* accessors = model.root->get("accessors");
    if(!accessors || index < 0 || size_t(index) >= accessors->size()){ error = "accessor " + std::to_string(index) + " does not exist"; return -1; }
    const Json& accessor = accessors->items[size_t(index)];
    const int type = accessor.integer("componentType", 0);
    components = componentsOf(accessor.str("type"));
    const long count = long(accessor.num("count", 0));
    const bool normalized = accessor.flag("normalized", false);
    const size_t size = bytesOf(type);
    if(components == 0 || size == 0){ error = "accessor " + std::to_string(index) + ": unknown type"; return -1; }

    out.assign(size_t(count) * size_t(components), 0.0f);
    const int viewIndex = accessor.integer("bufferView", -1);
    if(viewIndex < 0) return count;                     //bez pogleda: nule, po specifikaciji

    const Json* views = model.root->get("bufferViews");
    if(!views || size_t(viewIndex) >= views->size()){ error = "bufferView does not exist"; return -1; }
    const Json& view = views->items[size_t(viewIndex)];
    const int buffer = view.integer("buffer", 0);
    if(buffer < 0 || size_t(buffer) >= model.buffers.size()){ error = "buffer does not exist"; return -1; }
    const std::vector<uint8_t>& data = model.buffers[size_t(buffer)];
    const size_t viewOffset = size_t(view.num("byteOffset", 0));
    const size_t viewLength = size_t(view.num("byteLength", double(data.size())));
    const size_t within = size_t(accessor.num("byteOffset", 0));
    const size_t offset = viewOffset + within;
    const size_t element = size * size_t(components);
    const size_t stride = view.get("byteStride") ? size_t(view.num("byteStride", 0)) : element;
    //Granica je POGLED, ne cijeli buffer: akcesor koji izadje iz svog pogleda cita susjedne
    //podatke - druge vrhove, sliku - i daje uvjerljive brojeve
    if(count > 0 && (within + stride * size_t(count - 1) + element > viewLength || viewOffset + viewLength > data.size())){
        error = "accessor " + std::to_string(index) + " runs past its buffer";
        return -1;
    }
    for(long i = 0; i < count; ++i){
        const uint8_t* p = data.data() + offset + stride * size_t(i);
        for(int c = 0; c < components; ++c) out[size_t(i) * size_t(components) + size_t(c)] = componentAt(p + size_t(c) * size, type, normalized);
    }
    return count;
}

GltfTextureRef textureRef(const Json* info, const char* scaleKey){
    GltfTextureRef ref;
    if(!info) return ref;
    ref.texture = info->integer("index", -1);
    ref.texCoord = info->integer("texCoord", 0);
    if(scaleKey) ref.scale = float(info->num(scaleKey, 1.0));
    return ref;
}

}

bool loadGltf(const std::string& path, GltfScene& out, std::string& error, const GltfLoadConfig& config){
    out = GltfScene{};
    out.path = path;
    std::vector<uint8_t> file;
    if(!readFile(path, file)){ error = "cannot read " + path; return false; }
    const std::filesystem::path base = std::filesystem::path(path).parent_path();

    //GLB: zaglavlje, pa JSON komad, pa (po zelji) binarni komad
    std::string jsonText;
    std::vector<uint8_t> glbBinary;
    bool glb = false;
    if(file.size() >= 12 && std::memcmp(file.data(), "glTF", 4) == 0){
        glb = true;
        size_t at = 12;
        while(at + 8 <= file.size()){
            uint32_t length = 0, type = 0;
            std::memcpy(&length, file.data() + at, 4);
            std::memcpy(&type, file.data() + at + 4, 4);
            at += 8;
            if(at + length > file.size()){ error = "GLB is truncated"; return false; }
            if(type == 0x4E4F534A) jsonText.assign(reinterpret_cast<const char*>(file.data() + at), length);
            else if(type == 0x004E4942) glbBinary.assign(file.begin() + long(at), file.begin() + long(at + length));
            at += length;
        }
        if(jsonText.empty()){ error = "GLB has no JSON chunk"; return false; }
    }else{
        jsonText.assign(file.begin(), file.end());
    }

    Json root;
    try{
        root = JsonReader(jsonText).parse();
    }catch(const std::exception& failure){
        error = path + ": " + failure.what();
        return false;
    }
    if(root.kind != Json::Kind::Object){ error = "glTF is not a JSON object"; return false; }
    if(const Json* asset = root.get("asset")){
        const std::string version = asset->str("version");
        if(!version.empty() && version[0] != '2'){ error = "glTF version " + version + " - only 2.x is read"; return false; }
    }

    Model model;
    model.root = &root;
    if(const Json* buffers = root.get("buffers")){
        for(size_t i = 0; i < buffers->size(); ++i){
            const Json& buffer = buffers->items[i];
            std::vector<uint8_t> data;
            const std::string uri = buffer.str("uri");
            if(uri.empty()){
                if(!glb || i != 0){ error = "buffer " + std::to_string(i) + " has no uri"; return false; }
                data = glbBinary;
            }else if(!resolveUri(uri, base, data, error)){
                return false;
            }
            model.buffers.push_back(std::move(data));
        }
    }

    for(const char* unsupported : {"skins", "animations", "cameras"}){
        if(const Json* list = root.get(unsupported)){
            if(list->size() > 0) out.skipped.push_back(std::string(unsupported) + ": " + std::to_string(list->size()));
        }
    }

    //-- uzorkivaci, teksture, slike ------------------------------------------------------------
    if(const Json* samplers = root.get("samplers")){
        for(const Json& s : samplers->items){
            GltfSampler sampler;
            const int wrapS = s.integer("wrapS", 10497), wrapT = s.integer("wrapT", 10497);
            sampler.repeatU = wrapS != 33071; sampler.mirrorU = wrapS == 33648;
            sampler.repeatV = wrapT != 33071; sampler.mirrorV = wrapT == 33648;
            sampler.nearest = s.integer("magFilter", 9729) == 9728;
            out.samplers.push_back(sampler);
        }
    }
    if(const Json* textures = root.get("textures")){
        for(const Json& t : textures->items){
            GltfTexture texture;
            texture.image = t.integer("source", -1);
            texture.sampler = t.integer("sampler", -1);
            out.textures.push_back(texture);
        }
    }
    if(const Json* images = root.get("images")){
        for(size_t i = 0; i < images->size(); ++i){
            const Json& im = images->items[i];
            GltfImage image;
            image.name = im.str("name");
            image.uri = im.str("uri");
            if(config.decodeImages){
                std::vector<uint8_t> bytes;
                std::string problem;
                bool have = false;
                if(!image.uri.empty()){
                    have = resolveUri(image.uri, base, bytes, problem);
                }else{
                    const int viewIndex = im.integer("bufferView", -1);
                    const Json* views = root.get("bufferViews");
                    if(views && viewIndex >= 0 && size_t(viewIndex) < views->size()){
                        const Json& view = views->items[size_t(viewIndex)];
                        const int buffer = view.integer("buffer", 0);
                        const size_t offset = size_t(view.num("byteOffset", 0)), length = size_t(view.num("byteLength", 0));
                        if(buffer >= 0 && size_t(buffer) < model.buffers.size() && offset + length <= model.buffers[size_t(buffer)].size()){
                            bytes.assign(model.buffers[size_t(buffer)].begin() + long(offset),
                                         model.buffers[size_t(buffer)].begin() + long(offset + length));
                            have = true;
                        }
                    }
                    if(!have) problem = "image " + std::to_string(i) + " has no data";
                }
                if(have){
                    try{ image.pixels = decodeImage(bytes.data(), bytes.size()); }
                    catch(const std::exception& failure){ problem = failure.what(); }
                    if(!image.pixels.isValid() && problem.empty()) problem = "cannot be decoded";
                }
                if(!problem.empty()) out.skipped.push_back("slika " + std::to_string(i) + " (" + image.name + image.uri + "): " + problem);
            }
            out.images.push_back(std::move(image));
        }
    }

    //-- materijali -------------------------------------------------------------------------------
    if(const Json* materials = root.get("materials")){
        for(const Json& m : materials->items){
            GltfMaterial material;
            material.name = m.str("name");
            if(const Json* pbr = m.get("pbrMetallicRoughness")){
                if(const Json* factor = pbr->get("baseColorFactor")){
                    for(size_t c = 0; c < 4 && c < factor->size(); ++c) material.baseColor[c] = float(factor->items[c].number);
                }
                material.baseColorTexture = textureRef(pbr->get("baseColorTexture"), nullptr);
                material.metallic = float(pbr->num("metallicFactor", 1.0));
                material.roughness = float(pbr->num("roughnessFactor", 1.0));
                material.metallicRoughnessTexture = textureRef(pbr->get("metallicRoughnessTexture"), nullptr);
            }else{
                //Bez PBR bloka glTF propisuje zadane vrijednosti - ali tada je materijal metal,
                //sto je rijetko ono sto je izvoznik htio. Ostaje kako specifikacija kaze
            }
            material.normalTexture = textureRef(m.get("normalTexture"), "scale");
            material.occlusionTexture = textureRef(m.get("occlusionTexture"), "strength");
            material.emissiveTexture = textureRef(m.get("emissiveTexture"), nullptr);
            if(const Json* emissive = m.get("emissiveFactor")){
                for(size_t c = 0; c < 3 && c < emissive->size(); ++c) material.emissive[c] = float(emissive->items[c].number);
            }
            if(const Json* extensions = m.get("extensions")){
                if(const Json* strength = extensions->get("KHR_materials_emissive_strength")){
                    material.emissiveStrength = float(strength->num("emissiveStrength", 1.0));
                }
                for(const auto& [name, value] : extensions->members){
                    if(name != "KHR_materials_emissive_strength") out.skipped.push_back("materijal " + material.name + ": " + name);
                }
            }
            const std::string alpha = m.str("alphaMode");
            material.alphaMode = alpha == "MASK" ? GltfMaterial::Alpha::Mask
                               : alpha == "BLEND" ? GltfMaterial::Alpha::Blend : GltfMaterial::Alpha::Opaque;
            material.alphaCutoff = float(m.num("alphaCutoff", 0.5));
            material.doubleSided = m.flag("doubleSided", false);
            out.materials.push_back(material);
        }
    }

    //-- mreze ------------------------------------------------------------------------------------
    if(const Json* meshes = root.get("meshes")){
        for(size_t meshIndex = 0; meshIndex < meshes->size(); ++meshIndex){
            const Json& m = meshes->items[meshIndex];
            GltfMesh mesh;
            mesh.name = m.str("name");
            const Json* primitives = m.get("primitives");
            for(size_t p = 0; primitives && p < primitives->size(); ++p){
                const Json& prim = primitives->items[p];
                const int mode = prim.integer("mode", 4);
                if(mode != 4 && mode != 5 && mode != 6){
                    out.skipped.push_back("mreza " + mesh.name + ": primitiv nacina " + std::to_string(mode) + " (tocke/crte)");
                    continue;
                }
                const Json* attributes = prim.get("attributes");
                if(!attributes || !attributes->get("POSITION")){ out.skipped.push_back("mreza " + mesh.name + ": primitiv bez polozaja"); continue; }
                if(prim.get("targets")) out.skipped.push_back("mreza " + mesh.name + ": morph mete");

                GltfPrimitive primitive;
                primitive.material = prim.integer("material", -1);
                int components = 0;
                const long vertices = readAccessor(model, attributes->integer("POSITION", -1), primitive.positions, components, error);
                if(vertices < 0 || components != 3){ if(error.empty()) error = "POSITION is not VEC3"; return false; }

                auto optional = [&](const char* name, std::vector<float>& target, int wanted){
                    const Json* a = attributes->get(name);
                    if(!a) return true;
                    int got = 0;
                    std::vector<float> values;
                    const long count = readAccessor(model, int(a->number), values, got, error);
                    if(count < 0) return false;
                    if(count != vertices){ error = std::string(name) + ": drukciji broj vrhova"; return false; }
                    if(got == wanted){ target = std::move(values); return true; }
                    //Boja vrha zna biti VEC3; dopuni alfu jedinicom
                    if(wanted == 4 && got == 3){
                        target.resize(size_t(vertices) * 4);
                        for(long v = 0; v < vertices; ++v){
                            for(int c = 0; c < 3; ++c) target[size_t(v) * 4 + size_t(c)] = values[size_t(v) * 3 + size_t(c)];
                            target[size_t(v) * 4 + 3] = 1.0f;
                        }
                        return true;
                    }
                    error = std::string(name) + ": unexpected number of components";
                    return false;
                };
                if(!optional("NORMAL", primitive.normals, 3) || !optional("TEXCOORD_0", primitive.uv0, 2) ||
                   !optional("TEXCOORD_1", primitive.uv1, 2) || !optional("COLOR_0", primitive.colors, 4)) return false;

                std::vector<uint32_t> order;
                if(const Json* indices = prim.get("indices")){
                    std::vector<float> values;
                    int got = 0;
                    if(readAccessor(model, int(indices->number), values, got, error) < 0) return false;
                    order.reserve(values.size());
                    for(float v : values){
                        const uint32_t index = uint32_t(v);
                        if(index >= uint32_t(vertices)){ error = "index outside the vertices in mesh " + mesh.name; return false; }
                        order.push_back(index);
                    }
                }else{
                    for(long v = 0; v < vertices; ++v) order.push_back(uint32_t(v));
                }
                //Trake i lepeze u obicne trokute; trake izmjenicno okrecu redoslijed
                if(mode == 4){
                    order.resize(order.size() - order.size() % 3);
                    primitive.indices = std::move(order);
                }else{
                    for(size_t i = 2; i < order.size(); ++i){
                        if(mode == 5){
                            if(i % 2 == 0) primitive.indices.insert(primitive.indices.end(), {order[i - 2], order[i - 1], order[i]});
                            else primitive.indices.insert(primitive.indices.end(), {order[i - 1], order[i - 2], order[i]});
                        }else{
                            primitive.indices.insert(primitive.indices.end(), {order[0], order[i - 1], order[i]});
                        }
                    }
                }
                mesh.primitives.push_back(std::move(primitive));
            }
            out.meshes.push_back(std::move(mesh));
        }
    }

    //-- cvorovi i scena --------------------------------------------------------------------------
    if(const Json* nodes = root.get("nodes")){
        for(const Json& n : nodes->items){
            GltfNode node;
            node.name = n.str("name");
            node.mesh = n.integer("mesh", -1);
            if(const Json* children = n.get("children")) for(const Json& c : children->items) node.children.push_back(int(c.number));
            if(const Json* matrix = n.get("matrix"); matrix && matrix->size() == 16){
                //Po stupcima (glTF je column-major, kao glm); rastav u TRS
                double m[16];
                for(int i = 0; i < 16; ++i) m[i] = matrix->items[size_t(i)].number;
                for(int c = 0; c < 3; ++c){
                    node.translation[c] = float(m[12 + c]);
                    node.scale[c] = float(std::sqrt(m[c * 4] * m[c * 4] + m[c * 4 + 1] * m[c * 4 + 1] + m[c * 4 + 2] * m[c * 4 + 2]));
                }
                double r[3][3];
                for(int c = 0; c < 3; ++c) for(int row = 0; row < 3; ++row) r[c][row] = m[c * 4 + row] / std::max(1e-12, double(node.scale[c]));
                //Negativna determinanta: jedna os je zrcaljena
                const double det = r[0][0] * (r[1][1] * r[2][2] - r[2][1] * r[1][2]) - r[1][0] * (r[0][1] * r[2][2] - r[2][1] * r[0][2]) +
                                   r[2][0] * (r[0][1] * r[1][2] - r[1][1] * r[0][2]);
                if(det < 0.0){ node.scale[0] = -node.scale[0]; for(int row = 0; row < 3; ++row) r[0][row] = -r[0][row]; }
                //Matrica rotacije u kvaternion (r[stupac][redak])
                const double trace = r[0][0] + r[1][1] + r[2][2];
                double q[4];
                if(trace > 0.0){
                    const double s = std::sqrt(trace + 1.0) * 2.0;
                    q[3] = 0.25 * s; q[0] = (r[1][2] - r[2][1]) / s; q[1] = (r[2][0] - r[0][2]) / s; q[2] = (r[0][1] - r[1][0]) / s;
                }else if(r[0][0] > r[1][1] && r[0][0] > r[2][2]){
                    const double s = std::sqrt(1.0 + r[0][0] - r[1][1] - r[2][2]) * 2.0;
                    q[3] = (r[1][2] - r[2][1]) / s; q[0] = 0.25 * s; q[1] = (r[1][0] + r[0][1]) / s; q[2] = (r[2][0] + r[0][2]) / s;
                }else if(r[1][1] > r[2][2]){
                    const double s = std::sqrt(1.0 + r[1][1] - r[0][0] - r[2][2]) * 2.0;
                    q[3] = (r[2][0] - r[0][2]) / s; q[0] = (r[1][0] + r[0][1]) / s; q[1] = 0.25 * s; q[2] = (r[2][1] + r[1][2]) / s;
                }else{
                    const double s = std::sqrt(1.0 + r[2][2] - r[0][0] - r[1][1]) * 2.0;
                    q[3] = (r[0][1] - r[1][0]) / s; q[0] = (r[2][0] + r[0][2]) / s; q[1] = (r[2][1] + r[1][2]) / s; q[2] = 0.25 * s;
                }
                for(int c = 0; c < 4; ++c) node.rotation[c] = float(q[c]);
            }else{
                if(const Json* t = n.get("translation")) for(size_t c = 0; c < 3 && c < t->size(); ++c) node.translation[c] = float(t->items[c].number);
                if(const Json* r = n.get("rotation")) for(size_t c = 0; c < 4 && c < r->size(); ++c) node.rotation[c] = float(r->items[c].number);
                if(const Json* s = n.get("scale")) for(size_t c = 0; c < 3 && c < s->size(); ++c) node.scale[c] = float(s->items[c].number);
            }
            if(n.get("skin")) out.skipped.push_back("cvor " + node.name + ": skin");
            out.nodes.push_back(node);
        }
    }
    //Read joint identity for skeleton inspection; keep the skinning warning until runtime support exists.
    if(const Json* skins = root.get("skins")){
        for(const Json& skin : skins->items){
            if(const Json* joints = skin.get("joints")){
                for(const Json& joint : joints->items){
                    if(joint.kind != Json::Kind::Number || !std::isfinite(joint.number) ||
                       joint.number < 0 || joint.number >= double(out.nodes.size()) ||
                       std::floor(joint.number) != joint.number){
                        error = "skin joint index outside nodes"; return false;
                    }
                    out.nodes[size_t(joint.number)].joint = true;
                }
            }
        }
    }
    const Json* scenes = root.get("scenes");
    const int sceneIndex = root.integer("scene", 0);
    if(scenes && sceneIndex >= 0 && size_t(sceneIndex) < scenes->size()){
        if(const Json* nodes = scenes->items[size_t(sceneIndex)].get("nodes")){
            for(const Json& n : nodes->items) out.roots.push_back(int(n.number));
        }
    }else{
        //Bez scene: korijeni su cvorovi koji nisu nicije dijete
        std::vector<uint8_t> child(out.nodes.size(), 0);
        for(const GltfNode& node : out.nodes) for(int c : node.children) if(c >= 0 && size_t(c) < child.size()) child[size_t(c)] = 1;
        for(size_t i = 0; i < out.nodes.size(); ++i) if(!child[i]) out.roots.push_back(int(i));
    }
    //Sanitacija: pokazivaci izvan niza bi kasnije srusili sve sto ih slijedi
    for(GltfNode& node : out.nodes){
        if(node.mesh >= int(out.meshes.size())) node.mesh = -1;
        std::vector<int> valid;
        for(int c : node.children) if(c >= 0 && size_t(c) < out.nodes.size()) valid.push_back(c);
        node.children = valid;
    }
    for(GltfMesh& mesh : out.meshes) for(GltfPrimitive& p : mesh.primitives) if(p.material >= int(out.materials.size())) p.material = -1;
    return true;
}

}
