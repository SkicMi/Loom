#pragma once
//=============================================================================================
//  Stepenica 1 - preset.
//
//  Ovaj header je granica. Sve iznad njega je najkraci put do slike; sve ispod njega su
//  Loomovi configs (stepenica 2) i Vulkan (stepenica 3).
//
//  PRAVILO: ovdje ne smije uci nijedan Vulkan simbol, ni izravno ni naslijeden. Ne kao
//  smjernica nego kao mjerenje - test "tier1_header_is_clean" preprocesira ovaj header i
//  trazi "vk::" i "vulkan" u rezultatu. Zato su svi Loomovi tipovi ispod skriveni iza
//  neprozirnih handleova i jednog pimpla.
//
//  glm je namjerno dopusten: matematika nije Vulkan, a vlastiti tipovi vektora bi znacili
//  konverziju na svakoj granici bez ikakve dobiti.
//
//  IZLAZ NA NIZU STEPENICU nije ovdje nego u <Loom/Preset_Advanced.h>. Tako oboje moze
//  vrijediti odjednom: ovaj header je cist, a onaj tko zeli config ili zive objekte
//  ukljuci drugi i time svjesno side stepenicu.
//=============================================================================================
#include <cstdint>
#include <string>
#include <vector>
#include <glm/glm.hpp>

//Vulkan-free vec danas, pa se izlazu kakvi jesu umjesto da im se izmisljaju dvojnici
#include "Core/Camera.h"
#include "Core/Light.h"
#include "Core/Environment.h"
#include "Core/MaterialData.h"

//Nepotpuni tipovi, i to je dovoljno. Za parametar po const referenci i za referentni
//povratni tip prevodiocu ne treba definicija - a bez definicije nema ni Vulkana.
//To je cijeli trik koji dopusta da ovaj header ostane cist, a da vrata na nizu stepenicu
//ipak postoje: definicije su u <Loom/Preset_Advanced.h> i tek ga taj header povlaci
struct LoomConfig;
class LoomInitializer;

namespace Loom{

//Definiran u <Loom/Preset_Advanced.h>. Ovdje je samo ime
class ConfigOverride;

//Sto preset ispunjava. Ne mijenja kako se crta - mijenja samo ono sto nisi rekao
enum class Preset{
    //Dubina, sjene, sunce, ambijent, perspektivna kamera. Najcesci slucaj
    Lit3D,

    //Bez dubine i bez svjetla. Redoslijed crtanja odlucuje sto je iznad cega
    Flat2D,

    //Bez prozora i bez sata. Frame N je na N/fps, pa isti program dvaput da iste slike
    Offscreen
};

//Sjencanje u blokovima, po udaljenosti.
//
//Loom crta scenu dvaput: prvo samo dubinu, pa iz nje compute odluci koliko grubo se koji
//blok piksela smije sjenciti, i tek onda boju. Prvi prolaz nije trosak - drugi ucita dubinu
//koju je on napisao i ne racuna je ponovno, pa se svaki piksel sjenca tocno jednom bez
//obzira koliko se trokuta preko njega preklapa.
//
//Sto je dalje, to grublje. Vrijednosti su udaljenosti u istim jedinicama u kojima je scena.
struct AdaptiveShading{
    bool enabled = false;
    float quarterDistance = 25.0f;     //dalje od ovoga se sjenca 2x2
    float sixteenthDistance = 70.0f;   //dalje od ovoga 4x4
};

//Neprozirni handleovi. Broj, ne pokazivac: kopiranje je besplatno, viseci handle ne
//postoji, a tip iza njega ostaje na stepenici 2 gdje mu je i mjesto
struct TextureHandle{
    uint32_t id = 0;
    bool isValid() const {return id != 0;}
};

struct TargetHandle{
    uint32_t id = 0;
    bool isValid() const {return id != 0;}
};

//Pozicija, rotacija i skala, sklopive u jednom izrazu. Pretvara se u glm::mat4 sama, pa
//tko vec ima svoju matricu nije nista izgubio - draw prima i jedno i drugo
class Transform{
    public:
    Transform() = default;

    Transform& at(float x, float y, float z);
    Transform& at(const glm::vec3& position);
    Transform& spun(float radians, const glm::vec3& axis);
    Transform& scaled(float uniform);
    Transform& scaled(const glm::vec3& factors);

    glm::mat4 matrix() const;
    operator glm::mat4() const {return matrix();}

    private:
    glm::vec3 position{0.0f};
    glm::vec3 scale{1.0f};
    glm::vec3 axis{0.0f, 1.0f, 0.0f};
    float angle = 0.0f;
};

class Scene;

//Niz slika na disku. Numeriranje, mapa i zapis su Spoolovi; ovdje se to ne vidi
class Sequence{
    public:
    Sequence();
    ~Sequence();

    Sequence(const Sequence&) = delete;
    Sequence& operator=(const Sequence&) = delete;

    void setDirectory(const std::string& directory);
    void setPrefix(const std::string& prefix);

    //Uzme ono sto je scena zadnje nacrtala i zapise ga kao sljedeci frame
    std::string write(const Scene& scene);

    uint32_t frameCount() const;

    private:
    struct State;
    State* state = nullptr;
};

//Sve sto preset postavi, iza jednog objekta. Petlja ostaje korisnikova
class Scene{
    public:
    explicit Scene(Preset preset);

    //Isti preset, ali config prije upotrebe prolazi kroz tvoju ruku. Da bi se ovo dalo
    //pozvati treba ukljuciti <Loom/Preset_Advanced.h> - bez njega je ConfigOverride samo
    //ime i poziv se ne prevodi. Ne stoji ti nista na putu, ali si svjesno sisao stepenicu
    Scene(Preset preset, const ConfigOverride& override);

    ~Scene();

    Scene(const Scene&) = delete;
    Scene& operator=(const Scene&) = delete;

    //-- postavke koje svaka aplikacija dira, pa su promaknute ovamo -----------------------
    //Sve ostalo ide kroz override callback u <Loom/Preset_Advanced.h>, i zato ovaj popis
    //ne raste kad Loom dobije novu mogucnost
    void setTitle(const std::string& title);
    void setSize(uint32_t width, uint32_t height);
    void setClearColor(const glm::vec4& color);
    void setShadows(bool enabled);

    //Vidi AdaptiveShading. Uredaj koji to ne podrzava crta jednako, samo bez ustede - pa
    //ovo nikad ne treba ograditi provjerom
    void setAdaptiveShading(const AdaptiveShading& settings);
    bool adaptiveShadingActive() const;

    //-- tok --------------------------------------------------------------------------------
    //true dok prozor stoji. Bez prozora uvijek true, pa Offscreen broji frameove sam
    bool isRunning();

    void startRendering();
    void endRendering();

    //-- vrijeme ----------------------------------------------------------------------------
    //Sat prozora, u sekundama. Offscreen ga ne koristi
    float time() const;

    //Vrijeme kao parametar: frame N je na N/fps. Ovo je razlika izmedu sekvence koja se
    //moze nastaviti i one koja se ne moze
    void setFrame(uint32_t frame, float framesPerSecond);
    uint32_t frame() const;

    //-- sadrzaj ----------------------------------------------------------------------------
    TextureHandle loadTexture(const std::string& path);

    //Pikseli koje si sam napravio: RGBA, osam bita po kanalu, gusto pakirano, prvi red prvi.
    //Isti raspored koji Spool vraca s diska, pa proceduralna tekstura i ucitana idu istim
    //putem - stepenica 1 koja zna samo za datoteke ne bi mogla nacrtati ni sahovnicu
    TextureHandle createTexture(const void* pixels, uint32_t width, uint32_t height);

    //Cetiri oblika, jedan poziv po obliku
    void drawPlane(TextureHandle texture, const glm::mat4& transform = glm::mat4(1.0f));
    void drawCube(TextureHandle texture, const glm::mat4& transform = glm::mat4(1.0f));
    void drawSphere(TextureHandle texture, const glm::mat4& transform = glm::mat4(1.0f));
    void drawPyramid(TextureHandle texture, const glm::mat4& transform = glm::mat4(1.0f));

    //2D: ravnina okrenuta prema kameri, bez dubine
    void drawSprite(TextureHandle texture, const glm::mat4& transform = glm::mat4(1.0f));

    //-- citanje ----------------------------------------------------------------------------
    //Zadnji nacrtani frame, uvijek RGBA. BGRA koji Vulkanova povrsina vraca je detalj
    //formata koji je swapchain izpregovarao i ovdje mu nije mjesto
    std::vector<uint8_t> readPixels() const;

    uint32_t width() const;
    uint32_t height() const;

    //-- scena ------------------------------------------------------------------------------
    //Vec bez Vulkana, pa se daju kakvi jesu. Ovo je i dalje stepenica 1
    Camera& camera();
    Light& sun();
    Environment& environment();

    //Jos jedno svjetlo. Scena ga POSJEDUJE, a ne posuduje - renderer drzi pokazivace na
    //svjetla, pa bi Light deklariran pokraj Scene morao nadzivjeti nju, a to je pravilo koje
    //nitko ne bi trebao morati pamtiti. Vraceni referenca se smije pomicati svaki frame.
    //
    //castsShadows radi i za usmjereno (jedna karta) i za tockasto (kocka od sest lica);
    //scena sama vodi te prolaze, jer su draw pozivi njezini
    Light& addLight(const LightConfig& config, bool castsShadows = false);

    //Koliko ih je, ukljucujuci presetovo sunce
    uint32_t lightCount() const;

    //-- izlaz na nizu stepenicu --------------------------------------------------------------
    //Zivi objekti, onakvi kakve bi rucno slozio. Za razliku od overridea gore, ovo mijenja
    //stvari DOK rade. Oba potpisa spominju tipove koji su ovdje samo imena, pa se koriste
    //tek uz <Loom/Preset_Advanced.h>
    LoomInitializer& loom();
    const LoomInitializer& loom() const;

    //Sto je preset odlucio, za citanje. Najkraci odgovor na "a sto ovo zapravo postavlja"
    const LoomConfig& config() const;

    private:
    friend class Sequence;

    //Sve ispod je stepenica 2 i nize. Jedan pokazivac je cijena za to da Vulkan ne procuri
    struct State;
    State* state = nullptr;
};

}
