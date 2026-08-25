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

namespace Loom{

//Sto preset ispunjava. Ne mijenja kako se crta - mijenja samo ono sto nisi rekao
enum class Preset{
    //Dubina, sjene, sunce, ambijent, perspektivna kamera. Najcesci slucaj
    Lit3D,

    //Bez dubine i bez svjetla. Redoslijed crtanja odlucuje sto je iznad cega
    Flat2D,

    //Bez prozora i bez sata. Frame N je na N/fps, pa isti program dvaput da iste slike
    Offscreen
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

    private:
    friend class Sequence;

    //Sve ispod je stepenica 2 i nize. Jedan pokazivac je cijena za to da Vulkan ne procuri
    struct State;
    State* state = nullptr;
};

}
