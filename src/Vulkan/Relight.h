#pragma once
#include "Material.h"
#include "Texture.h"
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

    //Koliko odmak RASTE po metru puta.
    //
    //Ploha se moze zasjeniti samu sebe kad je svjetlo dalje od kamere nego ona: trag tada ide
    //dublje i inFront raste sam od sebe. Jedna brojka za cijelu sliku to ne rjesava - ono sto
    //zaustavi akne pojede kontaktnu sjenu - pa odmak raste s putem. Nula iskljucuje.
    //
    //Mjereno u test_shadow_slope: 0.0 pusta 16556 aknih piksela, 1.0 ih ostavlja 3396, 3.0
    //samo 316, a prava sjena prezivi
    float slopeBias = 0.0f;

    //Koliko debljina raste po duljini dosega. Sto se dalje odhodalo, to se manje zna o tome
    //sto je iza plohe koja se uzorkuje. Nula znaci konstantna debljina
    float thicknessGrowth = 0.0f;

    //Ploha blize kameri od samog svjetla ne moze biti zaklon - svjetlo je ispred nje. Ovo je
    //pojas u metrima preko kojeg se takav uzorak prestaje racunati. Nula iskljucuje pravilo
    float frontFade = 0.0f;

    //Zaklanja li SAMO ono sto je u masci. Bez toga zaklon je sve sto se vidi, pa i zid sam
    //sebi; s njom sjenu baca samo subjekt i njena silueta dolazi od tijela. Trazi da maska
    //bude postavljena (setOccluderMask) - bez nje je maska bijela i pravilo ne mijenja nista
    bool maskOnly = false;

    //Polumjer, u pikselima, iz kojeg trag uzima MEDIJAN dubine umjesto same tocke.
    //
    //Geometrija koja baca sjenu nije ista koja se sjenca: sjencanju trebaju fine normale,
    //tragu gruba silueta. Nabor na majici je za normale detalj, a za trag mrlja koja baca
    //sjenu koje nema. Medijan pet uzoraka izbaci usamljeni izbocaj a tijelo ostavi. Nula
    //iskljucuje, i tada trag cita tocno onu dubinu koju cita i sjencanje
    float occluderBlur = 0.0f;

    //Polumjer svjetla, u metrima. Nula znaci tockasto, dakle tvrda sjena.
    //
    //Polusjena postoji zato sto se s ruba sjene dio izvora vidi a dio ne - dakle zato sto
    //izvor IMA velicinu. Zato se ovdje ne mekSa odluka nego se hoda vise puta, prema
    //razlicitim tockama na disku ovog polumjera.
    //
    //ODBACENA ALTERNATIVA, jer je izmjerena: TypeGPU umjesto toga zbraja koliko je koji korak
    //zaklonjen i time dobiva mekocu. Preneseno i probano - jezgra sjene ostane tamna ali
    //sitna, a ostatak se razmaze u gradijent koji se na svijetlom zidu ne vidi. Polusjena je
    //tamo posljedica grubog uzorkovanja, a ne velicine izvora, pa joj sirina ovisi o broju
    //koraka umjesto o sceni
    float lightRadius = 0.0f;

    //Koliko tragova po pikselu. Jedan je tocka i tvrda sjena; vise ih uzorkuje disk.
    //Realtime bi ovo platio N puta po pikselu - offline je to jedina cijena
    uint32_t rays = 1;
};

//Ambijentna okluzija iz same karte dubine.
//
//Ono sto ubaceno svjetlo cini SJEDECIM: bez nje predmet lebdi nad pozadinom, jer nista ne
//potamni tamo gdje se dodiruju. Ne treba joj svjetlo - racuna se iz geometrije.
//
//Racuna se u fragment shaderu, ne u zasebnu teksturu. TypeGPU je preracunava jer mora stati
//u osam milisekundi; Loomu slika smije trajati, pa 32 uzorka po pikselu stede cijelu jednu
//sliku, njen binding i njeno odrzavanje
struct ScreenOcclusionConfig{
    //Nula znaci iskljuceno. U KOMPOZICIJI ovo mnozi same piksele snimke - jedino mjesto na
    //kojem Loom dira ono sto je vec bilo na slici, umjesto da samo dodaje svjetlo
    float strength = 0.0f;

    //Dva prstena, u pikselima: blizi hvata dodir, dalji sjenu udubine. Piksel je mjera koja
    //ovisi o rezoluciji, pa ih na drugoj velicini slike treba skalirati
    float nearRadius = 3.0f;
    float farRadius = 9.0f;

    //Koliko metara razlike u dubini vec znaci pun zaklon. Iz njega se izvode i prag suma
    //(desetina) i domet dodira (cetiri puta), pa se namjesta jedan broj a ne tri
    float scale = 0.05f;
};

//Payload relight prolaza. Slaze se s RelightData u include/Relight.slang
struct RelightData{
    glm::mat4 inverseView{1.0f};
    glm::mat4 view{1.0f};
    glm::vec4 baseColor{1.0f, 1.0f, 1.0f, 1.0f};
    glm::vec4 surface{32.0f, 1.0f, 0.0f, 0.0f};      //shininess, specularStrength, omotani difuz
    glm::vec4 intrinsics{0.0f, 0.0f, 0.0f, 0.0f};    //fx, fy, cx, cy
    glm::vec4 shadow{0.0f, 8.0f, 0.5f, 0.02f};       //koraka, duljina, debljina, odmak
    glm::vec4 shadowSlope{0.0f, 0.0f, 0.0f, 0.0f};   //rast odmaka, -, rast debljine, prednji pojas
    glm::vec4 occlusion{0.0f, 3.0f, 9.0f, 0.05f};    //jacina, blizi prsten, dalji prsten, mjerilo
    glm::vec4 occluder{0.0f, 0.0f, 0.0f, 0.0f};      //samo maska, glacanje zaklona
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

    //Ambijentna okluzija. Iskljucena po defaultu jer u kompoziciji mijenja same piksele snimke
    ScreenOcclusionConfig occlusion = {};
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

    //Mijenja se i nakon gradnje: koliko okluzije treba vidi se tek kad se pogleda
    void setOcclusion(const ScreenOcclusionConfig& occlusion);

    //Tko smije zaklanjati. Dok se ne postavi, maska je bijeli piksel - dakle svatko, i
    //ponasanje je isto kao da je nema
    void setOccluderMask(const SampledImage& mask);
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

    //Bijeli piksel: maska koja nikoga ne iskljucuje. Postoji da bi raspored deskriptora bio
    //isti bez obzira postavi li se prava maska ili ne - inace bi trebala dva pipelinea
    std::optional<Texture> whiteMask;
};
