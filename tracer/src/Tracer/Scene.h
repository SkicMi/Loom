#pragma once
//=============================================================================================
// SCENA ZA TRACER: ono sto se racuna, u SVJETSKOM prostoru, bez ijedne veze s editorom.
//
// Editor (Warp) zna za entitete, kljuceve kroz vrijeme, glTF datoteke i putove do slika. Tracer
// ne zna nista od toga: dobije trokute vec pomaknute na mjesto u tom kadru, materijale s vec
// dekodiranim teksturama, svjetla i kameru. Prevodjenje je posao mosta (src/LoomRender.h) - pa
// se tracer da testirati scenom od tri trokuta napisanom u testu, bez ijedne datoteke.
//
// JEDINICE. Radijancija je linearna, u "jedinicama scene" - ista brojka koju Blender ili Nuke
// zovu 1.0 za bijelo. Svjetla:
//
//   Distant (sunce)  intensity * color je OZRACENOST okomite plohe. Bijela Lambertova ploha
//                    okrenuta suncu jakosti 3 ima radijanciju 3/pi - test to provjerava
//   Sphere / Spot    intensity * color je INTENZITET (po steradijanu): ozracenost na udaljenosti
//                    d je I / d^2. Radijus > 0 daje meke sjene, 0 je tockasto svjetlo
//   emisija          materijal koji svijetli je radijancija plohe (emission * strength); takvi
//                    trokuti sami postanu svjetla koja se uzorkuju izravno
//   okolina          radijancija neba, lat-long slika ili jedna boja, puta intensity
//
// KAMERA JE ISTA KAO U POGLEDU I SOLVERU: pinhole u pikselima, gleda niz -Z s +Y gore, a piksel
// je x = cx + f*X/-Z, y = cy - f*Y/-Z (y raste prema dolje). Render rijesene kamere tako pada na
// isti piksel snimke na koji pada kocka u pogledu editora - test to mjeri.
//=============================================================================================
#include <glm/glm.hpp>

#include <cstdint>
#include <string>
#include <vector>

namespace Tracer{

//---------------------------------------------------------------------------------------------
// TEKSTURA. Ili osam bita po kanalu (boja iz PNG/JPEG-a; RGB u sRGB-u kad je srgb, alfa uvijek
// linearna), ili float (HDR okolina, izracunato nebo). Uzorkuje se bilinearno i vraca LINEARNO.
//
// Osam bita se NE pretvara u float unaprijed: 4K tekstura je 64 MB u floatu i 16 MB ovako, a
// model iz Sketchfaba ih zna nositi desetak. Pretvorba sRGB -> linearno je tablica od 256 brojeva
//---------------------------------------------------------------------------------------------
struct Texture{
    uint32_t width = 0, height = 0;
    std::vector<uint8_t> bytes;         //RGBA8, gusto, prvi redak prvi
    std::vector<float> floats;          //RGBA32F, linearno; kad nije prazno, ima prednost
    bool srgb = true;                   //bytes: RGB su sRGB kodirani
    bool repeat = true;                 //izvan [0,1]: ponavljanje; inace rub

    bool valid() const {return width > 0 && height > 0 &&
                               (floats.size() == size_t(width) * height * 4 || bytes.size() == size_t(width) * height * 4);}
    glm::vec4 fetch(int x, int y) const;        //jedan teksel, linearno
    glm::vec4 sample(glm::vec2 uv) const;       //bilinearno; v = 0 je PRVI redak (glTF)

    //MIPMAPE: razine 1, 2, ... (pola, cetvrtina, ... do 1x1), 2x2 usrednjeno u LINEARNOM prostoru
    //(sRGB se dekodira, usrednji i kodira - inace crno-bijeli sah postane pretaman). Prazno: samo
    //osnovna razina. compile() ih gradi za teksture scene koje ih nemaju
    std::vector<Texture> mips;
    void buildMips();
    //Trilinearno: lod 0 = osnovna razina, 1 = pola, ... (izmedju razina linearno)
    glm::vec4 sample(glm::vec2 uv, float lod) const;
};

//---------------------------------------------------------------------------------------------
// MATERIJAL - principijelni model: metalnost i hrapavost kao glTF i UsdPreviewSurface, plus ono
// sto path tracer zna, a raster ne: prozirnost s lomom (transmission, ior) i lak (clearcoat).
//
// Mape se MNOZE s faktorima, kao u glTF-u. glTF-ova occlusion mapa se namjerno ne koristi:
// ona je zapecena procjena zaklanjanja, a path tracer zaklanjanje RACUNA - obje zajedno bi
// kutove zatamnile dvaput
//---------------------------------------------------------------------------------------------
struct Material{
    std::string name;
    glm::vec3 baseColor{0.8f};
    float opacity = 1.0f;                       //alfa faktor
    int baseColorTexture = -1;                  //indeks u Scene::textures; alfa u a
    float metallic = 0.0f;
    float roughness = 0.5f;
    int metallicRoughnessTexture = -1;          //G = hrapavost, B = metalnost (glTF)
    int normalTexture = -1;                     //tangentni prostor, +Y gore (glTF/OpenGL)
    float normalScale = 1.0f;
    float ior = 1.5f;                           //dielektrik: F0 = ((ior-1)/(ior+1))^2
    float specular = 1.0f;                      //mnozi dielektricni odsjaj, 0..1 (0 = cisti Lambert)
    float transmission = 0.0f;                  //0 neprozirno, 1 staklo
    float clearcoat = 0.0f;
    float clearcoatRoughness = 0.03f;
    glm::vec3 emission{0.0f};
    float emissionStrength = 1.0f;
    int emissionTexture = -1;
    bool emissionTwoSided = true;               //false: svijetli samo prednja strana (normale vrhova)
    enum class Alpha{ Opaque, Mask, Blend };
    Alpha alphaMode = Alpha::Opaque;
    float alphaCutoff = 0.5f;
};

//---------------------------------------------------------------------------------------------
// GEOMETRIJA. Ulaz je mreza u vlastitom prostoru i matrica u svijet; scena drzi trokute vec u
// svijetu. Instanciranja nema namjerno: sceni iz editora su to desetci objekata, a jedna ravna
// BVH je jednostavnija i brza od dvije razine
//---------------------------------------------------------------------------------------------
struct MeshData{
    std::vector<glm::vec3> positions;
    std::vector<glm::vec3> normals;             //prazno: izracunaju se glatke
    std::vector<glm::vec2> uvs;                 //prazno: nema mapa
    std::vector<uint32_t> indices;              //trokuti
};

//Sto objekt radi u slici. Shadow catcher je VFX alat: ploha koja se u slici NE vidi, ali skuplja
//sjene i zaklanjanje objekata na sebi - pa se CG objekt spusti na pod SNIMKE i baca sjenu na njega
struct ObjectFlags{
    bool cameraVisible = true;                  //false: kamera ga ne vidi, svjetlo i odrazi da
    bool castsShadows = true;
    bool shadowCatcher = false;
};

struct Object{
    std::string name;
    ObjectFlags flags;
    uint32_t firstTriangle = 0;
    uint32_t triangleCount = 0;
};

struct Triangle{
    uint32_t v[3] = {0, 0, 0};                  //indeksi u Scene::positions (i normals, uvs, tangents)
    uint32_t material = 0;
    uint32_t object = 0;
};

//---------------------------------------------------------------------------------------------
// SVJETLA
//---------------------------------------------------------------------------------------------
struct Light{
    enum class Type{ Distant, Sphere, Spot };
    Type type = Type::Distant;
    glm::vec3 color{1.0f};
    float intensity = 1.0f;                     //vidi JEDINICE gore
    glm::vec3 position{0.0f};                   //Sphere, Spot
    glm::vec3 direction{0.0f, -1.0f, 0.0f};     //smjer u kojem svjetlo PUTUJE (Distant, Spot os)
    float radius = 0.0f;                        //Sphere, Spot: polumjer izvora; 0 = tocka
    float angle = 0.0093f;                      //Distant: kutni PROMJER u radijanima (sunce 0.53 st)
    float spotAngle = 0.7854f;                  //Spot: polukut stosca
    float spotBlend = 0.15f;                    //Spot: udio stosca u kojem svjetlo mekano pada
};

//---------------------------------------------------------------------------------------------
// OKOLINA - lat-long (equirect): stupac 0 je smjer -Z pa ide oko osi Y prema +X, redak 0 je
// zenit (+Y). Bez mape je jedna boja. Uzorkuje se po vaznosti (svijetlo sunce na HDRI-ju se
// nadje u svakom uzorku, ne jednom u tisucu)
//---------------------------------------------------------------------------------------------
struct Environment{
    Texture map;                                //prazno: jedna boja
    glm::vec3 color{0.0f};
    float intensity = 1.0f;
    float rotation = 0.0f;                      //radijani oko +Y
    bool cameraVisible = true;                  //vidi li kamera nebo ili samo osvjetljava
};

//Nebo kao lat-long: Preethamov analiticki model dnevnog neba (Preetham, Shirley, Smits 1999)
//za zadani smjer sunca i zamucenost zraka. Zenit neba ima luminanciju `zenith`. Tlo ispod
//horizonta je jednolike boje `ground`. Samo nebo - SUNCE je zasebno Distant svjetlo, jer ga kao
//piksel lat-long mape ni najbolje uzorkovanje ne bi dalo ostrog
Texture makeSky(const glm::vec3& towardSun, float turbidity, float zenith, const glm::vec3& ground,
                uint32_t width = 512, uint32_t height = 256);

//---------------------------------------------------------------------------------------------
// KAMERA - pinhole u pikselima (vidi zaglavlje), s opcionalnom dubinskom ostrinom
//---------------------------------------------------------------------------------------------
struct Camera{
    glm::mat4 cameraToWorld{1.0f};
    float focalPixels = 1000.0f;
    glm::vec2 centre{960.0f, 540.0f};
    uint32_t width = 1920, height = 1080;
    float apertureRadius = 0.0f;                //u jedinicama scene; 0 = sve ostro
    float focusDistance = 1.0f;                 //udaljenost ostre ravnine, duz -Z kamere

    //DISTORZIJA LECE (Brown, radijalno), u pikselima slike: stvarni piksel = distort(pinhole piksel)
    //s vlastitim f i c objektiva (Warp::Camera). Zraka ide kroz ISPRAVLJENU tocku, pa render ima
    //zakrivljenje snimke bez ikakvog prevzorkovanja. lensFx 0: ravna leca
    glm::vec4 lens{0.0f};                       //fx, fy, cx, cy
    float k1 = 0.0f, k2 = 0.0f;
    bool distorted() const {return lens.x > 0.0f && lens.y > 0.0f && (k1 != 0.0f || k2 != 0.0f);}
    glm::vec2 distortPixel(glm::vec2 pinhole) const;
    glm::vec2 undistortPixel(glm::vec2 pixel) const;
    //Tocka u prostoru kamere (z < 0) u piksele slike, s distorzijom
    glm::vec2 pixelOf(const glm::vec3& local) const;

    //Zraka kroz tocku slike (u pikselima, kontinuirano). lensSample u [0,1)^2 bira tocku na leci
    void ray(glm::vec2 pixel, glm::vec2 lensSample, glm::vec3& origin, glm::vec3& direction) const;
    //Obrnuto: svijet u piksele. false iza kamere
    bool project(const glm::vec3& world, glm::vec2& pixel) const;
};

//Jednolika magla u kutiji -0.5..0.5 lokalno (Volume.h): gustoca = gubitak po jedinici scene
//(sivo), albedo = boja rasprsenja, anisotropy = Henyey-Greenstein g
struct Volume{
    glm::mat4 toWorld{1.0f};
    glm::vec3 albedo{1.0f};
    float density = 0.0f;
    float anisotropy = 0.0f;
};

//---------------------------------------------------------------------------------------------
// SCENA
//---------------------------------------------------------------------------------------------
struct Scene{
    std::vector<glm::vec3> positions;
    std::vector<glm::vec3> normals;
    std::vector<glm::vec2> uvs;
    std::vector<glm::vec4> tangents;            //xyz tangenta, w predznak bitangente
    std::vector<Triangle> triangles;
    std::vector<Object> objects;
    std::vector<Material> materials;
    std::vector<Texture> textures;
    std::vector<Light> lights;
    Environment environment;
    Camera camera;

    //SNIMKA IZA SCENE za lom i odraz. Kad kamera kroz staklo vidi ono sto je iza, iza je SNIMKA,
    //ne nebo. Ploca se tretira kao beskonacno daleka i projicira kroz kameru: zraka koja nakon
    //samo zrcalnih odbijanja i lomova pobjegne iz scene uzme piksel snimke u svom smjeru.
    //Prazno: nema snimke, bjezi u okolinu
    Texture backplate;

    //HOLDOUT IZ DUBINE STVARNE SCENE (npr. splat projiciran kroz kameru): po pikselu kadra
    //udaljenost duz -Z do stvarne plohe (floats, R; 0 ili vise od NoDepth/2 = nista). Uzorak iz
    //kamere koji pogodi nesto IZA te plohe (vise od holdoutBias relativno) je pozadina: snimka
    //se vidi, CG iza stvarnog zida se ne crta. Rub je po uzorku, pa je antialiasiran
    Texture holdout;
    float holdoutBias = 0.02f;

    //VOLUMENI: magla u kutijama. Zraka koja kroz nju prolazi se rasprsi (vidljive zrake svjetla,
    //pruge sjena), zraka sjene oslabi za exp(-gustoca * put)
    std::vector<Volume> volumes;

    uint32_t addTexture(Texture texture);
    uint32_t addMaterial(Material material);
    //Mreza u svijet. Vraca indeks objekta. Neispravni indeksi i degenerirani trokuti se preskoce
    uint32_t addMesh(const MeshData& mesh, const glm::mat4& world, uint32_t material,
                     const std::string& name = {}, ObjectFlags flags = {});
};

//Jedinicna kocka (-0.5..0.5) i ravnina 1x1 u XZ - isti oblici kao Warp::Shape u editoru
MeshData unitCube();
MeshData unitPlane();
//UV kugla, za testove i probne scene
MeshData uvSphere(float radius, uint32_t segments = 64, uint32_t rings = 32);

//sRGB <-> linearno, tocna krivulja (ne gama 2.2)
float srgbToLinear(float value);
float linearToSrgb(float value);

}
