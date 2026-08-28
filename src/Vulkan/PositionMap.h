#pragma once
#include "VulkanImage.h"
#include "VulkanCommand.h"
#include "VulkanComputePipeline.h"
#include "ComputeMaterial.h"
#include "RenderTarget.h"
#include "SampledImage.h"
#include "Core/CameraIntrinsics.h"
#include "ImageData.h"
#include <memory>

//Odakle dubina dolazi i sto njen broj znaci
enum class DepthEncoding : uint32_t{
    //Loomov vlastiti depth buffer: nelinearan zapis kroz near i far
    Buffer = 0,

    //Ono sto daju modeli za relativnu dubinu (Depth Anything i slicni): 1 je najblize, 0
    //najdalje, i vrijednost je proporcionalna RECIPROCNOJ udaljenosti a ne udaljenosti
    Disparity = 1,

    //Metri izravno, kakve daju metricke varijante modela
    Metric = 2
};

//Kako se broj iz slike pretvara u udaljenost.
//
//Ovo je jedino mjesto na kojem se relativna dubina spaja sa stvarnim svijetom, i zato jedino
//na kojem se moze pogrijesiti tako da slika i dalje izgleda uvjerljivo. Svjetlo koje pada po
//inverznom kvadratu treba METRE; scena skalirana dvostruko izgleda kao scena s upola slabijim
//svjetlom, i nista u njoj ne izgleda kao greska
struct DepthMapping{
    DepthEncoding encoding = DepthEncoding::Disparity;

    //Disparity:  1/Z = disparityScale * vrijednost + disparityOffset
    //
    //Dva broja, jer je toliko i nepoznanica. Model daje nesto proporcionalno reciprocnoj
    //udaljenosti i ne zna ni razmjer ni pomak - cijela kalibracija je u ta dva broja, i to
    //je isti oblik u kojem se relativna dubina poravnava s istinom i u literaturi.
    //
    //Ne postavljaju se rukom nego kroz fromRange ili fromReferences
    float disparityScale = 1.0f;
    float disparityOffset = 0.0f;

    //Metric: metri = vrijednost * ovo
    float metricScale = 1.0f;

    //"Najblize je metar i pol, najdalje dvadeset" - pitanje na koje covjek zna odgovoriti
    //gledajuci sliku. Vrijedi kad je karta normalizirana na 0..1, sto vecina skripti radi
    static DepthMapping fromRange(float nearDistance, float farDistance);

    //Dvije poznate udaljenosti na dvije poznate vrijednosti. Ovo je prava kalibracija: ne
    //treba znati raspon karte ni je normalizirati, dovoljno je pokazati na dvije stvari u
    //slici i reci koliko su daleko
    static DepthMapping fromReferences(float valueA, float distanceA,
                                       float valueB, float distanceB);

    //Za modele koji vec daju metre
    static DepthMapping metric(float scale = 1.0f);

    //Isti racun koji radi i shader. Postoji da se kalibracija moze provjeriti bez karte
    float distanceAt(float value) const;
};

//Ono sto compute shader treba znati. Slaze se s Push strukturom u unproject.slang, i ako se
//to dvoje raziđe, prevodi se i tiho cita krive bajtove
struct UnprojectPush{
    uint32_t size[2];
    float fx;
    float fy;
    float cx;
    float cy;
    float nearPlane;
    float farPlane;

    uint32_t encoding;
    float disparityScale;
    float disparityOffset;
    float metricScale;
};

struct PositionMapConfig{
    //Gdje dispatch ostavlja sliku pozicija. eShaderReadOnlyOptimal da je sljedeci prolaz
    //semplira, eTransferSrcOptimal da je se procita natrag na CPU
    vk::ImageLayout finalLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
};

//Slika tocaka u prostoru, napravljena od slike dubine.
//
//Jedan teksel po pikselu, cetiri float-a: xyz je tocka u view prostoru, w je 1 gdje ploha
//postoji i 0 gdje je pozadina. Ta nula nije ukras - svjetlo koje obasja nebo je prvo sto se
//vidi da ne valja.
//
//Puni float po kanalu, ne pola. Pozicije se poslije usporeduju s poznatim brojevima, a
//polovicna preciznost na deset metara promasi vise nego cijela ova racunica smije.
class PositionMap{
    public:
    PositionMap(const VulkanDevice& device, vk::Extent2D extent, const PositionMapConfig& config = {});

    PositionMap(const PositionMap&) = delete;
    PositionMap& operator=(const PositionMap&) = delete;

    //Odakle se cita dubina. Semplirljiva slika i njena velicina - ne nuzno RenderTarget, jer
    //prozorska dubina nije meta, a tuda dubina iz filea nije ni jedno ni drugo.
    //
    //Sampler NE smije biti comparison: ovdje se cita sama vrijednost dubine, a ne odgovor na
    //usporedbu. Meta s depthCompare = true daje upravo takav sampler
    void setDepthSource(const vk::raii::DescriptorPool& pool,
                        const SampledImage& depth, vk::Extent2D depthExtent);

    //Isto, kad dubina jest meta
    void setDepthSource(const vk::raii::DescriptorPool& pool, const RenderTarget& depthTarget);

    //Dubina koja NIJE nastala crtanjem: karta iz filea, izlaz modela, tuda snimka.
    //
    //Shader je isti - i jedno i drugo je jedan kanal iz semplirljive slike - mijenja se samo
    //racun koji taj broj pretvori u udaljenost. To je i bila cijela poanta: relight prolaz
    //nikad ne sazna odakle mu je ploha dosla
    void setPlateDepth(const vk::raii::DescriptorPool& pool,
                       const SampledImage& depth, vk::Extent2D depthExtent,
                       const DepthMapping& mapping);

    void setMapping(const DepthMapping& value) {mapping = value;}
    const DepthMapping& getMapping() const {return mapping;}

    //Kroz sto se odprojicira. Bez ovoga dispatch nema kroz sto racunati i baca
    void setIntrinsics(const CameraIntrinsics& value) {intrinsics = value;}
    const CameraIntrinsics& getIntrinsics() const {return intrinsics;}

    //Sve sto treba za dispatch:
    //    renderer.dispatch(map.getComputeMaterial(), map.groupsX(), map.groupsY(),
    //                      &push, sizeof(push));
    //gdje je push = map.makePush()
    UnprojectPush makePush() const;
    const ComputeMaterial& getComputeMaterial() const;

    //8x8 niti po grupi, zaokruzeno gore
    uint32_t groupsX() const {return (extent.width + 7) / 8;}
    uint32_t groupsY() const {return (extent.height + 7) / 8;}

    //Tocke natrag na CPU. Trazi da je config.finalLayout eTransferSrcOptimal, jer inace
    //slika nije tamo gdje kopiranje ocekuje da jest
    ImageData readPixels(const VulkanCommand& command) const;

    //getters
    const VulkanImage& getImage() const {return image;}
    vk::Extent2D getExtent() const {return extent;}
    SampledImage getSampled() const;
    bool hasDepthSource() const {return depthExtent.width > 0;}

    private:
    const VulkanDevice& device;
    vk::Extent2D extent;
    PositionMapConfig config;
    VulkanImage image;

    CameraIntrinsics intrinsics;
    DepthMapping mapping{DepthEncoding::Buffer, 1.0f, 0.0f, 1.0f};
    vk::Extent2D depthExtent{0,0};

    vk::raii::Sampler sampler = nullptr;

    std::unique_ptr<VulkanComputePipeline> computePipeline;
    std::unique_ptr<ComputeMaterial> computeMaterial;
};
