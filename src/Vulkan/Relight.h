#pragma once
#include "Material.h"
#include "NormalMap.h"
#include "PositionMap.h"
#include "VulkanGraphicsPipeline.h"
#include "Core/Camera.h"
#include "Core/CameraIntrinsics.h"
#include "Core/MaterialData.h"
#include <glm/glm.hpp>
#include <optional>

//Sjena trazena kroz samu sliku.
//
//Karte sjena ovdje nema i ne moze je biti: dubina daje LJUSKU, ne geometriju - postoji samo
//prednja ploha onoga sto se vidi. Zato se zaklon trazi hodanjem od tocke prema svjetlu.
//
//Sjenu baca samo ono sto je U KADRU. Predmet iza kamere ili izvan ruba ne postoji ni u jednoj
//slici, pa ni ne moze zakloniti - to je granica postupka, a ne njegova greska
struct ScreenShadowConfig{
    bool enabled = false;

    //Koliko se puta pogleda usput. Vise koraka je tocnija sjena i skuplja slika
    uint32_t steps = 24;

    //Koliko daleko se ide, u metrima. Zaklon dalji od ovoga se ne trazi - a upravo je to
    //razlika izmedu kontaktne sjene i sjene preko cijele scene
    float maxDistance = 8.0f;

    //Koliko se duboka pretpostavlja ploha koju vidimo. Bez ovoga bi svaka bila beskonacno
    //duboka i zaklanjala sve iza sebe do kraja scene - a mi vidimo samo njeno lice
    float thickness = 0.5f;

    //Odmak od vlastite plohe, da ne baca sjenu sama na sebe
    float bias = 0.02f;
};

//Payload relight prolaza. Slaze se s RelightData u include/Relight.slang
struct RelightData{
    glm::mat4 inverseView{1.0f};
    glm::mat4 view{1.0f};
    glm::vec4 baseColor{1.0f, 1.0f, 1.0f, 1.0f};
    glm::vec4 surface{32.0f, 1.0f, 0.0f, 0.0f};      //shininess, specularStrength
    glm::vec4 intrinsics{0.0f, 0.0f, 0.0f, 0.0f};    //fx, fy, cx, cy
    glm::vec4 shadow{0.0f, 8.0f, 0.5f, 0.02f};       //koraka, duljina, debljina, odmak
    glm::vec4 imageSize{0.0f, 0.0f, 0.0f, 0.0f};
};

struct RelightConfig{
    //Oboje se mora slagati s metom u koju se crta. Relight ne testira dubinu i ne pise je,
    //ali prolaz koji ima dubinski privitak trazi da to i pipeline zna - inace pet VUID-ova
    vk::Format colorFormat = vk::Format::eB8G8R8A8Srgb;
    vk::Format depthFormat = vk::Format::eUndefined;

    //Kakva je ploha koju obasjavamo. Dok albedo ne dolazi iz slike, baseColor je jedina
    //boja koju G-buffer ima
    MaterialData surface = {};

    //Sjena iz kadra. Iskljucena po defaultu: trazi intrinsike, pa se ne smije samo pojaviti
    ScreenShadowConfig shadow = {};
};

//Svjetlo nad G-bufferom.
//
//Jedan fullscreen prolaz koji cita pozicije i normale iz slika i osjenca ih postojecim
//svjetlima - istim kodom kojim se sjenca mesh. Za renderer je to obican drawFullscreen s
//materijalom; sve ostalo je unutra.
//
//Tocke i normale su u view prostoru jer je dubina iz koje su nastale bila, a svjetla su u
//svjetskom - zato ovaj prolaz jedini od svih treba kameru, i zato setCamera mora biti
//pozvan prije prvog crteza
class Relight{
    public:
    Relight(const VulkanDevice& device,
            const VulkanCommand& command,
            const vk::raii::DescriptorPool& pool,
            const PositionMap& positions,
            const NormalMap& normals,
            const RelightConfig& config = {});

    //Svjetlo ubaceno U SNIMKU: plate je ono sto se vec vidi, i ujedno albedo kojim ploha
    //odbija novo svjetlo. Gdje plohe nema, snimka prolazi nedirnuta - pa je izlaz ovog
    //prolaza cijela slika i ne trazi da je itko prije njega ispunio
    Relight(const VulkanDevice& device,
            const VulkanCommand& command,
            const vk::raii::DescriptorPool& pool,
            const PositionMap& positions,
            const NormalMap& normals,
            SampledImage plate,
            const RelightConfig& config = {});

    Relight(const Relight&) = delete;
    Relight& operator=(const Relight&) = delete;

    //Kroz koju kameru su pozicije nastale. Bez toga bi se tocke iz view prostora
    //osvjetljavale svjetlima iz svjetskog, sto je tiho krivo - slika izade, samo ne ta
    void setCamera(const Camera& camera);

    //Kroz koje su intrinsike tocke nastale. Trag sjene vraca tocku u piksel, pa treba isti
    //racun kojim je piksel postao tocka - bez toga sjena pada pokraj onoga sto je baca
    void setIntrinsics(const CameraIntrinsics& intrinsics, vk::Extent2D imageSize);

    void setShadow(const ScreenShadowConfig& shadow);
    bool castsShadows() const {return data.shadow.x > 0.0f;}

    //Boja i sjaj plohe. U kompoziciji baseColor MNOZI snimku, pa se albedo da prigusiti bez
    //diranja same snimke
    void setSurface(const MaterialData& surface);

    //Sljedeci frame snimke. Postoji zato da se za video ne gradi novi materijal po frameu
    void setPlate(const SampledImage& plate);

    bool compositesOverPlate() const {return hasPlate;}

    const Material& getMaterial() const {return *material;}
    const VulkanGraphicsPipeline& getPipeline() const {return *pipeline;}
    const RelightData& getData() const {return data;}

    private:
    RelightData data;
    bool hasPlate = false;

    //Redoslijed je bitan: materijal drzi pokazivac na pipeline, pa pipeline mora zivjeti
    //duze - a clanovi se rusе obrnutim redom od deklaracije
    std::optional<VulkanGraphicsPipeline> pipeline;
    std::optional<Material> material;
};
