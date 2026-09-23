// glTF: model s materijalima, iz .gltf i iz .glb - poznate vrijednosti natrag.
//
// ZASTO SE OVO TESTIRA. glTF ima vise mjesta na kojima se tiho promasi nego sto izgleda:
//
//   stride i pomaci    akcesor s krivim pomakom da uvjerljive, ali krive vrhove
//   trake              traka trokuta mijenja redoslijed svakog drugog - bez toga je pola lica
//                      okrenuto naopako i nestane uz odbacivanje poledjine
//   matrica cvora      glTF je po stupcima; rastav u TRS mora vratiti isti pomak i rotaciju
//   GLB                slika u binarnom komadu, preko bufferViewa, ne kao datoteka
//   materijal          zadana metalnost je 1 (!), faktori se mnoze s teksturom
//
// NEGATIVNE KONTROLE: odrezan GLB, indeks izvan vrhova i nepostojeci .bin se odbijaju s razlogom
#include "TestHarness.h"

#include <Spool/Gltf.h>
#include <Spool/ImageFile.h>

#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>

namespace{

namespace fs = std::filesystem;

std::vector<uint8_t> readBytes(const fs::path& path){
    std::ifstream file(path, std::ios::binary);
    return std::vector<uint8_t>(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
}

template<class T> void append(std::vector<uint8_t>& out, const std::vector<T>& values){
    const size_t at = out.size();
    out.resize(at + values.size() * sizeof(T));
    std::memcpy(out.data() + at, values.data(), values.size() * sizeof(T));
}

//Binarni dio: cetverokut (pomak 0), traka (poslije), indeksi; sve poravnato na 4
struct Binary{
    std::vector<uint8_t> bytes;
    size_t quadPositions = 0, quadNormals = 0, quadUv = 0, quadIndices = 0, strip = 0, image = 0, imageLength = 0;
};

Binary makeBinary(const std::vector<uint8_t>& png){
    Binary b;
    b.quadPositions = b.bytes.size();
    append(b.bytes, std::vector<float>{0, 0, 0,  1, 0, 0,  1, 1, 0,  0, 1, 0});
    b.quadNormals = b.bytes.size();
    append(b.bytes, std::vector<float>{0, 0, 1,  0, 0, 1,  0, 0, 1,  0, 0, 1});
    b.quadUv = b.bytes.size();
    append(b.bytes, std::vector<float>{0, 1,  1, 1,  1, 0,  0, 0});
    b.quadIndices = b.bytes.size();
    append(b.bytes, std::vector<uint16_t>{0, 1, 2, 0, 2, 3});
    b.strip = b.bytes.size();
    append(b.bytes, std::vector<float>{0, 0, 0,  1, 0, 0,  0, 1, 0,  1, 1, 0});
    b.image = b.bytes.size();
    b.bytes.insert(b.bytes.end(), png.begin(), png.end());
    b.imageLength = png.size();
    while(b.bytes.size() % 4) b.bytes.push_back(0);
    return b;
}

//glTF JSON. bufferUri prazan: GLB; imageInBuffer: slika preko bufferViewa (GLB), inace datoteka
std::string makeJson(const Binary& b, const std::string& bufferUri, bool imageInBuffer){
    const std::string buffer = bufferUri.empty() ? "{\"byteLength\":" + std::to_string(b.bytes.size()) + "}"
                                                 : "{\"uri\":\"" + bufferUri + "\",\"byteLength\":" + std::to_string(b.bytes.size()) + "}";
    const std::string image = imageInBuffer ? "{\"bufferView\":5,\"mimeType\":\"image/png\"}" : "{\"uri\":\"boja%20tekstura.png\"}";
    //Matrica cvora: pomak (1,2,3), 90 st oko Y, mjerilo 2 - po stupcima
    const std::string matrix = "[0,0,-2,0, 0,2,0,0, 2,0,0,0, 1,2,3,1]";
    return std::string("{\"asset\":{\"version\":\"2.0\"},\"scene\":0,\"scenes\":[{\"nodes\":[0]}],") +
        "\"nodes\":[{\"name\":\"Korijen\",\"children\":[1]},{\"name\":\"Ploca\",\"mesh\":0,\"matrix\":" + matrix + "}]," +
        "\"meshes\":[{\"name\":\"Ploca\",\"primitives\":[" +
            "{\"attributes\":{\"POSITION\":0,\"NORMAL\":1,\"TEXCOORD_0\":2},\"indices\":3,\"material\":0}," +
            "{\"attributes\":{\"POSITION\":4},\"mode\":5}]}]," +
        "\"materials\":[{\"name\":\"Mjed\",\"pbrMetallicRoughness\":{\"baseColorFactor\":[0.8,0.6,0.2,1],\"baseColorTexture\":{\"index\":0}," +
            "\"metallicFactor\":0.3,\"roughnessFactor\":0.7},\"normalTexture\":{\"index\":0,\"scale\":0.5}," +
            "\"emissiveFactor\":[1,0.5,0],\"extensions\":{\"KHR_materials_emissive_strength\":{\"emissiveStrength\":4}}," +
            "\"alphaMode\":\"MASK\",\"alphaCutoff\":0.3,\"doubleSided\":true},{\"name\":\"Zadani\"}]," +
        "\"textures\":[{\"source\":0,\"sampler\":0}],\"samplers\":[{\"magFilter\":9728,\"wrapS\":33071}]," +
        "\"images\":[" + image + "]," +
        "\"accessors\":[" +
            "{\"bufferView\":0,\"componentType\":5126,\"count\":4,\"type\":\"VEC3\"}," +
            "{\"bufferView\":1,\"componentType\":5126,\"count\":4,\"type\":\"VEC3\"}," +
            "{\"bufferView\":2,\"componentType\":5126,\"count\":4,\"type\":\"VEC2\"}," +
            "{\"bufferView\":3,\"componentType\":5123,\"count\":6,\"type\":\"SCALAR\"}," +
            "{\"bufferView\":4,\"componentType\":5126,\"count\":4,\"type\":\"VEC3\"}]," +
        "\"bufferViews\":[" +
            "{\"buffer\":0,\"byteOffset\":" + std::to_string(b.quadPositions) + ",\"byteLength\":48}," +
            "{\"buffer\":0,\"byteOffset\":" + std::to_string(b.quadNormals) + ",\"byteLength\":48}," +
            "{\"buffer\":0,\"byteOffset\":" + std::to_string(b.quadUv) + ",\"byteLength\":32}," +
            "{\"buffer\":0,\"byteOffset\":" + std::to_string(b.quadIndices) + ",\"byteLength\":12}," +
            "{\"buffer\":0,\"byteOffset\":" + std::to_string(b.strip) + ",\"byteLength\":48}," +
            "{\"buffer\":0,\"byteOffset\":" + std::to_string(b.image) + ",\"byteLength\":" + std::to_string(b.imageLength) + "}]," +
        "\"buffers\":[" + buffer + "]}";
}

std::vector<uint8_t> makeGlb(const std::string& json, const std::vector<uint8_t>& binary){
    std::string padded = json;
    while(padded.size() % 4) padded += ' ';
    std::vector<uint8_t> out;
    auto word = [&](uint32_t v){ append(out, std::vector<uint32_t>{v}); };
    word(0x46546C67); word(2); word(uint32_t(12 + 8 + padded.size() + 8 + binary.size()));
    word(uint32_t(padded.size())); word(0x4E4F534A);
    out.insert(out.end(), padded.begin(), padded.end());
    word(uint32_t(binary.size())); word(0x004E4942);
    out.insert(out.end(), binary.begin(), binary.end());
    return out;
}

}

int main(){
    TestReport report("S gltf");
    const fs::path directory = fs::temp_directory_path() / "loom_gltf_test";
    fs::remove_all(directory);
    fs::create_directories(directory);

    //Tekstura 2x2: gornji lijevi piksel je poznat
    const uint8_t pixels[16] = {200, 100, 50, 255,  0, 0, 0, 255,  0, 0, 0, 255,  10, 20, 30, 255};
    Spool::savePng((directory / "boja tekstura.png").string(), Spool::imageFromPixels(pixels, 2, 2));
    const std::vector<uint8_t> png = readBytes(directory / "boja tekstura.png");
    const Binary binary = makeBinary(png);
    {
        std::ofstream(directory / "model.bin", std::ios::binary).write(reinterpret_cast<const char*>(binary.bytes.data()),
                                                                        std::streamsize(binary.bytes.size()));
        std::ofstream(directory / "model.gltf") << makeJson(binary, "model.bin", false);
        const std::vector<uint8_t> glb = makeGlb(makeJson(binary, "", true), binary.bytes);
        std::ofstream(directory / "model.glb", std::ios::binary).write(reinterpret_cast<const char*>(glb.data()), std::streamsize(glb.size()));
    }

    for(const char* name : {"model.gltf", "model.glb"}){
        Spool::GltfScene scene;
        std::string error;
        const bool ok = Spool::loadGltf((directory / name).string(), scene, error);
        report.check(fmt("%s se ucita", name).c_str(), ok && scene.meshes.size() == 1 && scene.meshes[0].primitives.size() == 2,
                     ok ? fmt("%zu cvora, %zu mreza", scene.nodes.size(), scene.meshes.size()) : error);
        if(!ok) continue;

        const Spool::GltfPrimitive& quad = scene.meshes[0].primitives[0];
        const bool geometry = quad.vertexCount() == 4 && quad.positions[6] == 1.0f && quad.positions[7] == 1.0f &&
                              quad.normals.size() == 12 && quad.normals[2] == 1.0f && quad.uv0.size() == 8 && quad.uv0[1] == 1.0f &&
                              quad.indices == std::vector<uint32_t>({0, 1, 2, 0, 2, 3}) && quad.material == 0;
        report.check(fmt("%s: vrhovi, normale, UV i indeksi", name).c_str(), geometry,
                     fmt("%zu vrhova, %zu indeksa", quad.vertexCount(), quad.indices.size()));

        //Traka 0,1,2,3 -> (0,1,2) i (2,1,3): drugi trokut zamijenjenog poretka, ista orijentacija
        const Spool::GltfPrimitive& strip = scene.meshes[0].primitives[1];
        report.check(fmt("%s: traka u trokute iste orijentacije", name).c_str(),
                     strip.indices == std::vector<uint32_t>({0, 1, 2, 2, 1, 3}) && strip.material == -1,
                     fmt("%zu indeksa", strip.indices.size()));

        const Spool::GltfMaterial& m = scene.materials[0];
        const bool material = m.name == "Mjed" && std::fabs(m.baseColor[1] - 0.6f) < 1e-6f && m.metallic == 0.3f &&
                              m.roughness == 0.7f && m.baseColorTexture.texture == 0 && m.normalTexture.scale == 0.5f &&
                              m.emissive[1] == 0.5f && m.emissiveStrength == 4.0f &&
                              m.alphaMode == Spool::GltfMaterial::Alpha::Mask && m.alphaCutoff == 0.3f && m.doubleSided &&
                              scene.materials[1].metallic == 1.0f && scene.materials[1].roughness == 1.0f;
        report.check(fmt("%s: PBR materijal i zadane vrijednosti (metal 1)", name).c_str(), material, m.name);

        const Spool::GltfImage& image = scene.images[0];
        report.check(fmt("%s: tekstura dekodirana, uzorkivac procitan", name).c_str(),
                     image.pixels.width == 2 && image.pixels.pixels[0] == 200 && image.pixels.pixels[1] == 100 &&
                     scene.samplers[0].nearest && !scene.samplers[0].repeatU && scene.samplers[0].repeatV,
                     fmt("%ux%u, prvi piksel %d", image.pixels.width, image.pixels.height, image.pixels.pixels.empty() ? 0 : int(image.pixels.pixels[0])));

        //Matrica po stupcima -> pomak (1,2,3), mjerilo 2, 90 st oko Y: kvaternion (0, 0.7071, 0, 0.7071)
        const Spool::GltfNode& node = scene.nodes[1];
        const bool trs = node.translation[0] == 1.0f && node.translation[2] == 3.0f && std::fabs(node.scale[0] - 2.0f) < 1e-6f &&
                         std::fabs(node.rotation[1] - 0.70710678f) < 1e-5f && std::fabs(node.rotation[3] - 0.70710678f) < 1e-5f &&
                         scene.roots == std::vector<int>({0}) && scene.nodes[0].children == std::vector<int>({1});
        report.check(fmt("%s: matrica cvora u pomak, rotaciju i mjerilo", name).c_str(), trs,
                     fmt("q (%.4f, %.4f, %.4f, %.4f), s %.3f", node.rotation[0], node.rotation[1], node.rotation[2], node.rotation[3], node.scale[0]));
    }

    //-- NEGATIVNE KONTROLE --------------------------------------------------------------------------
    {
        const std::vector<uint8_t> glb = readBytes(directory / "model.glb");
        std::ofstream(directory / "odrezan.glb", std::ios::binary).write(reinterpret_cast<const char*>(glb.data()), std::streamsize(glb.size() / 2));
        std::string json = makeJson(binary, "model.bin", false);
        std::ofstream(directory / "indeks.gltf") << json.replace(json.find("\"count\":6"), 9, "\"count\":7");
        std::ofstream(directory / "bez_bin.gltf") << makeJson(binary, "nema.bin", false);

        Spool::GltfScene scene;
        std::string cut, index, missing;
        const bool a = Spool::loadGltf((directory / "odrezan.glb").string(), scene, cut);
        const bool b = Spool::loadGltf((directory / "indeks.gltf").string(), scene, index);
        const bool c = Spool::loadGltf((directory / "bez_bin.gltf").string(), scene, missing);
        report.check("odrezan GLB, akcesor izvan buffera i nepostojeci .bin se odbijaju", !a && !b && !c &&
                     missing.find("nema.bin") != std::string::npos,
                     cut + " | " + index + " | " + missing);
    }

    fs::remove_all(directory);
    return report.result();
}
