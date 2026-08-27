#pragma once
#include "Material.h"
#include "NormalMap.h"
#include "PositionMap.h"
#include "VulkanGraphicsPipeline.h"
#include "Core/Camera.h"
#include "Core/MaterialData.h"
#include <glm/glm.hpp>
#include <optional>

//Payload relight prolaza. Slaze se s RelightData u relight.slang
struct RelightData{
    glm::mat4 inverseView{1.0f};
    glm::vec4 baseColor{1.0f, 1.0f, 1.0f, 1.0f};
    float shininess = 32.0f;
    float specularStrength = 1.0f;
    float padding0 = 0.0f;
    float padding1 = 0.0f;
};

struct RelightConfig{
    //Oboje se mora slagati s metom u koju se crta. Relight ne testira dubinu i ne pise je,
    //ali prolaz koji ima dubinski privitak trazi da to i pipeline zna - inace pet VUID-ova
    vk::Format colorFormat = vk::Format::eB8G8R8A8Srgb;
    vk::Format depthFormat = vk::Format::eUndefined;

    //Kakva je ploha koju obasjavamo. Dok albedo ne dolazi iz slike, baseColor je jedina
    //boja koju G-buffer ima
    MaterialData surface = {};
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
