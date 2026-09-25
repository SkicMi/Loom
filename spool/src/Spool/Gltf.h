#pragma once
#include "Spool/ImageFile.h"

#include <cstdint>
#include <string>
#include <vector>

namespace Spool{

//=============================================================================================
// glTF 2.0 - model s vlastitim materijalima, iz .gltf (JSON + .bin + slike) ili .glb.
//
// ZASTO glTF. To je jedini siroko podrzan format koji nosi PBR materijal KAKO GA RENDERER
// RACUNA: metalnost i hrapavost (metallic-roughness), normal, occlusion i emisijska mapa, uz
// alfa nacin. OBJ nosi Phongov .mtl, FBX nosi sto god je koji izvoznik smislio. Blender, Houdini,
// Substance i Sketchfab ga izvoze isto.
//
// STO SE CITA: cvorovi (TRS ili matrica), mreze s primitivima (polozaj, normala, dva UV skupa,
// boja vrha, indeksi i do cetiri skin utjecaja po vrhu; trake i lepeze se pretvore u trokute),
// skinovi s inverznim bind matricama, materijali s teksturama i
// uzorkivacima, slike (PNG/JPEG, iz datoteke, data: URI-ja ili iz GLB-a) i ekstenzija
// KHR_materials_emissive_strength.
//
// STO SE NE CITA: morph mete, animacije, kamere, svjetla, rijetki (sparse)
// akcesori i ostale ekstenzije materijala (clearcoat, transmission...). Datoteka koja ih ima se
// procita bez njih - model stoji i ima materijal, samo se ne mice - a sto je preskoceno pise u
// `skipped`, da se ne mora pogadjati.
//
// Spool ne ovisi o glm-u, pa su vektori obicni nizovi floatova. Kvaternion je (x, y, z, w) -
// redoslijed iz glTF-a
//=============================================================================================

struct GltfTextureRef{
    int texture = -1;           //-1: nema teksture
    int texCoord = 0;           //koji UV skup
    float scale = 1.0f;         //normalTexture.scale / occlusionTexture.strength
};

struct GltfMaterial{
    std::string name;
    float baseColor[4] = {1, 1, 1, 1};
    GltfTextureRef baseColorTexture;
    float metallic = 1.0f;          //glTF zadano: 1 - metal, dok ga tekstura ne kaze drukcije
    float roughness = 1.0f;
    GltfTextureRef metallicRoughnessTexture;    //G = hrapavost, B = metalnost
    GltfTextureRef normalTexture;
    GltfTextureRef occlusionTexture;            //R
    float emissive[3] = {0, 0, 0};
    float emissiveStrength = 1.0f;
    GltfTextureRef emissiveTexture;
    enum class Alpha{ Opaque, Mask, Blend } alphaMode = Alpha::Opaque;
    float alphaCutoff = 0.5f;
    bool doubleSided = false;
};

struct GltfSampler{
    bool repeatU = true, repeatV = true, mirrorU = false, mirrorV = false;
    bool nearest = false;
};

struct GltfTexture{
    int image = -1;
    int sampler = -1;
};

struct GltfImage{
    std::string name;
    std::string uri;            //prazno kad je slika u bufferu (GLB)
    Image pixels;               //RGBA8; prazno kad se nije moglo dekodirati ili nije trazeno
};

struct GltfPrimitive{
    std::vector<float> positions;       //xyz po vrhu
    std::vector<float> normals;         //xyz; prazno kad ih datoteka nema
    std::vector<float> uv0, uv1;        //uv; prazno kad ih nema
    std::vector<float> colors;          //rgba; prazno kad ih nema
    std::vector<uint16_t> jointIndices; //JOINTS_0, cetiri indeksa u GltfSkin::joints po vrhu
    std::vector<float> jointWeights;    //WEIGHTS_0; renderer ih normalizira pri deformaciji
    std::vector<uint32_t> indices;      //uvijek trokuti; bez indeksa u datoteci: 0, 1, 2...
    int material = -1;                  //-1: zadani materijal (bijeli, hrapav, nemetal)
    size_t vertexCount() const {return positions.size() / 3;}
};

struct GltfMesh{
    std::string name;
    std::vector<GltfPrimitive> primitives;
};

struct GltfNode{
    bool joint = false;                //skin joint marker; runtime skin deformation is separate
    std::string name;
    int mesh = -1;
    int skin = -1;
    std::vector<int> children;
    float translation[3] = {0, 0, 0};
    float rotation[4] = {0, 0, 0, 1};   //x, y, z, w
    float scale[3] = {1, 1, 1};
    //Kad cvor ima matricu, rastavi se u TRS (glTF zabranjuje smicanje u matrici cvora)
};

struct GltfSkin{
    std::string name;
    int skeleton = -1;
    std::vector<int> joints;                 //indices into GltfScene::nodes
    std::vector<float> inverseBindMatrices;  //MAT4 column-major, one per joint
};

struct GltfScene{
    std::string path;
    std::vector<GltfNode> nodes;
    std::vector<GltfSkin> skins;
    std::vector<int> roots;             //cvorovi scene koja se prikazuje
    std::vector<GltfMesh> meshes;
    std::vector<GltfMaterial> materials;
    std::vector<GltfTexture> textures;
    std::vector<GltfSampler> samplers;
    std::vector<GltfImage> images;
    std::vector<std::string> skipped;   //sto je u datoteci bilo, a ovdje se ne cita
};

struct GltfLoadConfig{
    bool decodeImages = true;   //false: samo geometrija i materijali, npr. za popis sadrzaja
};

//false uz razlog. Slika koja se ne da dekodirati NE rusi ucitavanje - model bez nje je i dalje
//model; razlog pise u skipped
bool loadGltf(const std::string& path, GltfScene& out, std::string& error, const GltfLoadConfig& config = {});

}
