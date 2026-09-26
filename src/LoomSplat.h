#pragma once
//=============================================================================================
// SPLAT U POGLEDU EDITORA.
//
// Istrenirani gaussian splat se crta u pravokutnik pogleda, ispod crta scene (kamere, kocke,
// mreza), kroz istu kameru kao i sve ostalo - orbitu ili rijesenu kameru. Tako se vidi sjedi li
// splat na snimci i stoji li kocka na njemu.
//
// ISTI RASTERIZATOR KAO SPLATVIEWER (Loomov SplatRenderer), s tri razlike:
//
//   - SAMO NULTI CLAN BOJE. Visi sferni harmonici za scenu od 3.7 milijuna gaussiana su 666 MB
//     na kartici; za pregled u editoru boja koja ne ovisi o smjeru pogleda je dovoljna. Puna
//     scena je i dalje u SplatVieweru
//   - CITA SE U ZASEBNOJ NITI. Datoteka od 860 MB se cita sekundama; prozor za to vrijeme radi
//   - TRANSFORMACIJA GRUPE. Splat je u koordinatama solvea, dijete grupe koja nosi uspravnost i
//     mjerilo. Pogled koji rasterizator dobije je pogled kamere PUTA svjetska matrica splata, pa
//     splat ide s grupom kad se scena orijentira ili skalira. Kovarijanca se projicira istom
//     matricom, pa i jednoliko mjerilo ostaje tocno
//
// REZANJE KOCKOM (kao kocka za brisanje u SplatVieweru, ali kocka je obicna kocka iz scene: mice
// se, okrece i skalira istim alatima, i ne mora biti jednakih stranica). Brise se nad maskom
// zivih, pa je korak natrag besplatan; datoteka se ne dira dok se ne spremi. Spremanje cita .ply
// ponovno s diska i pise ga kroz masku - puni sferni harmonici tako ostaju, a u memoriji ih ne
// treba drzati (za 3.6 M gaussiana to bi bilo 650 MB samo za cuvanje)
//
// Slika se slaze preko ploce PREMULTIPLICIRANO (vidi BlendMode::Premultiplied): gdje splata nema,
// snimka se vidi. Pretvorba iz sRGB-a u linearno (splat_present) se radi na vec pomnozenoj boji,
// pa djelomicno prozirni rubovi nisu sasvim tocni - za pregled je to nevidljivo
//=============================================================================================
#include "Core/LoomInitializer.h"
#include "Core/Splat.h"
#include "Vulkan/Material.h"
#include "Vulkan/RenderTarget.h"
#include "Vulkan/SplatRenderer.h"
#include "Vulkan/Texture.h"

#include <Spool/GaussianPly.h>
#include <Treadle/Draw.h>

#include <algorithm>
#include <atomic>
#include <cstring>
#include <fstream>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace Loom{

class ViewportSplat{
public:
    explicit ViewportSplat(LoomInitializer& loom) : loom(loom){
        PipelineConfig config;
        config.vertexBindings.clear();
        config.vertexAttributes.clear();
        config.descriptorBindings = {Texture::getLayoutBinding(), Material::getDataLayoutBinding()};
        config.vertShaderPath = std::string(LOOM_SHADER_DIR) + "/splat_present.vert.spv";
        config.fragShaderPath = std::string(LOOM_SHADER_DIR) + "/splat_present.frag.spv";
        config.cullMode = vk::CullModeFlagBits::eNone;
        config.depthTestEnable = false;
        config.depthWriteEnable = false;
        config.blendMode = BlendMode::Premultiplied;
        pipeline.emplace(loom.createPipeline(config));
    }

    ~ViewportSplat(){
        if(loader.joinable()) loader.join();
    }

    //Koji splat treba. Novi put pokrene citanje; isti ne radi nista
    void want(const std::string& path){
        if(path == wantedPath) return;
        wantedPath = path;
        if(loader.joinable()) loader.join();
        {
            std::lock_guard<std::mutex> guard(lock);
            loaded = false;
            problem.clear();
            raw.clear();
            alive.clear();
            cuts.clear();
            redoCuts.clear();
        }
        uploadedPath.clear();
        splatCount = 0;
        loading = true;
        loader = std::thread([this, path]{ load(path); });
    }

    bool isLoading() const {return loading;}
    const std::string& path() const {return wantedPath;}

    //Ista datoteka iznova s diska - kad je upravo prepisana (spremljeno rezanje, ciscenje)
    void reload(){
        const std::string again = wantedPath;
        wantedPath.clear();
        want(again);
    }

    //-- rezanje kockom ------------------------------------------------------------------------
    //cubeFromSplat vodi iz prostora splata u prostor JEDINICNE kocke (-0.5..0.5): inverz
    //svjetske matrice kocke puta svjetska matrica splata
    static bool inBox(const glm::mat4& cubeFromSplat, const glm::vec3& p){
        const glm::vec3 c = glm::vec3(cubeFromSplat * glm::vec4(p, 1.0f));
        return std::fabs(c.x) <= 0.5f && std::fabs(c.y) <= 0.5f && std::fabs(c.z) <= 0.5f;
    }

    //Koliko je zivih u kocki; prolaz kroz cijeli oblak (3.6 M: desetak ms), pa se zove samo
    //kad se kocka ili splat pomaknu
    size_t countInBox(const glm::mat4& cubeFromSplat) const{
        std::lock_guard<std::mutex> guard(lock);
        size_t n = 0;
        for(size_t i = 0; i < raw.size(); ++i) n += alive[i] && inBox(cubeFromSplat, glm::vec3(raw[i].positionOpacity));
        return n;
    }

    //Brise zive unutar (inside) ili izvan kocke; vraca koliko ih je obrisano
    size_t cutBox(const glm::mat4& cubeFromSplat, bool inside){
        std::lock_guard<std::mutex> guard(lock);
        std::vector<uint32_t> removed;
        for(size_t i = 0; i < raw.size(); ++i){
            if(alive[i] && inBox(cubeFromSplat, glm::vec3(raw[i].positionOpacity)) == inside){
                alive[i] = 0;
                removed.push_back(uint32_t(i));
            }
        }
        const size_t count = removed.size();
        if(count > 0){
            redoCuts.clear();
            cuts.push_back(std::move(removed));
            if(size.width > 0) build(size);
        }
        return count;
    }

    bool undoCut(){
        std::lock_guard<std::mutex> guard(lock);
        if(cuts.empty()) return false;
        std::vector<uint32_t> restored = std::move(cuts.back());
        cuts.pop_back();
        for(uint32_t i : restored) alive[i] = 1;
        redoCuts.push_back(std::move(restored));
        if(size.width > 0) build(size);
        return true;
    }

    bool redoCut(){
        std::lock_guard<std::mutex> guard(lock);
        if(redoCuts.empty()) return false;
        std::vector<uint32_t> removed = std::move(redoCuts.back());
        redoCuts.pop_back();
        for(uint32_t i : removed) alive[i] = 0;
        cuts.push_back(std::move(removed));
        if(size.width > 0) build(size);
        return true;
    }

    //Obrisano a nespremljeno
    size_t cutAway() const{
        std::lock_guard<std::mutex> guard(lock);
        size_t n = 0;
        for(const auto& cut : cuts) n += cut.size();
        return n;
    }
    bool hasCuts() const{ std::lock_guard<std::mutex> guard(lock); return !cuts.empty(); }

    //Srediste i polumjer VECINE zivih: medijan po osima i 90. percentil udaljenosti. Odbjegli
    //gaussiani (a splat ih ima) bi inace odredili okvir, i pogled bi stao daleko od scene
    bool bounds(glm::vec3& centre, float& radius) const{
        std::lock_guard<std::mutex> guard(lock);
        std::vector<glm::vec3> sample;
        const size_t stride = std::max<size_t>(1, raw.size() / 200000);
        for(size_t i = 0; i < raw.size(); i += stride) if(alive[i]) sample.push_back(glm::vec3(raw[i].positionOpacity));
        if(sample.size() < 2) return false;
        std::vector<float> axis(sample.size());
        for(int k = 0; k < 3; ++k){
            for(size_t i = 0; i < sample.size(); ++i) axis[i] = sample[i][k];
            std::nth_element(axis.begin(), axis.begin() + long(axis.size() / 2), axis.end());
            centre[k] = axis[axis.size() / 2];
        }
        for(size_t i = 0; i < sample.size(); ++i) axis[i] = glm::length(sample[i] - centre);
        const size_t at = axis.size() * 9 / 10;
        std::nth_element(axis.begin(), axis.begin() + long(at), axis.end());
        radius = std::max(axis[at], 1e-4f);
        return true;
    }
    size_t total() const{ std::lock_guard<std::mutex> guard(lock); return raw.size(); }

    //Zapisuje zive u out: izvorna datoteka se procita iznova (puni harmonici) i pise kroz masku.
    //Vraca prazno ili razlog. out smije biti i sama izvorna datoteka - procita se cijela prije
    //pisanja
    std::string saveCut(const std::string& out) const{
        std::vector<uint8_t> keep;
        {
            std::lock_guard<std::mutex> guard(lock);
            keep = alive;
        }
        size_t kept = 0;
        for(uint8_t k : keep) kept += k;
        if(kept == 0) return "nothing is left - an empty .ply is not written";
        try{
            const Spool::GaussianCloud cloud = Spool::loadGaussianPly(wantedPath);
            if(cloud.count() != keep.size()) return "the file changed on disk since it was loaded";
            Spool::saveGaussianPly(out, cloud, keep);
        }catch(const std::exception& e){
            return e.what();
        }
        return {};
    }

    //Sredista gaussiana u koordinatama splata, za odabir plohe. Gotovo prozirni (lebdeci sum
    //treninga) se preskacu - oni nisu ploha
    template<class Visit>
    void forEachCentre(float minOpacity, Visit&& visit) const{
        std::lock_guard<std::mutex> guard(lock);
        for(size_t i = 0; i < raw.size(); ++i){
            if(alive[i] && raw[i].positionOpacity.w >= minOpacity) visit(glm::vec3(raw[i].positionOpacity));
        }
    }
    size_t count() const {return splatCount;}
    std::string error() const{ std::lock_guard<std::mutex> guard(lock); return problem; }

    //PRIJE beginFrame: (ponovno) gradi metu kad se velicina promijeni i salje splatove kad su
    //stigli, pa postavlja kameru. Vraca true kad ima sto crtati u ovom kadru.
    //
    //view i focal/centre su kamere pogleda, s glavnom tockom u pikselima PROZORA; area je dio
    //prozora u koji se crta
    bool prepare(const glm::mat4& viewTimesWorld, const glm::vec3& eyeLocal, float focal,
                 glm::vec2 centre, const Treadle::Rect& area, float pixelScale){
        const vk::Extent2D extent{uint32_t(std::max(16.0f, area.width * pixelScale)),
                                  uint32_t(std::max(16.0f, area.height * pixelScale))};
        bool arrived = false;
        {
            std::lock_guard<std::mutex> guard(lock);
            arrived = loaded && uploadedPath != wantedPath;
        }
        if(arrived || (renderer && (extent.width != size.width || extent.height != size.height))){
            std::lock_guard<std::mutex> guard(lock);
            if(!raw.empty()) build(extent);
            if(arrived) uploadedPath = wantedPath;
        }
        if(!renderer || splatCount == 0) return false;

        //Rasterizator ima jedan primjerak radnih polja: prosli kadar mora zavrsiti prije nego se
        //ovaj posalje (isto kao u SplatVieweru)
        loom.waitIdle();
        //fy NEGATIVAN: rasterizator racuna py = cy + fy * y / dubina (Loomova Vulkan konvencija,
        //vidi CameraIntrinsics), a pogled editora y = cy - f * y / dubina. S pozitivnim fy splat
        //kroz rijesenu kameru ispadne naopako - polica s vrha snimke na dnu
        renderer->setCamera(viewTimesWorld, eyeLocal, focal * pixelScale, -focal * pixelScale,
                            (centre.x - area.x) * pixelScale, (centre.y - area.y) * pixelScale);
        return true;
    }

    //POSLIJE beginFrame, PRIJE beginPass: racun na kartici
    void compute(){
        renderer->prepare(loom.renderer, uint32_t(splatCount));
        renderer->draw(loom.renderer, uint32_t(splatCount));
    }

    //U prolazu: slika u pravokutnik prozora (u pikselima okvira)
    void present(const vk::Rect2D& pixels, vk::Extent2D framebuffer){
        const vk::raii::CommandBuffer& commands = loom.renderer.borrowCommands();
        commands.setViewport(0, vk::Viewport{float(pixels.offset.x), float(pixels.offset.y),
                                             float(pixels.extent.width), float(pixels.extent.height), 0.0f, 1.0f});
        commands.setScissor(0, pixels);
        loom.renderer.drawFullscreen(*material);
        commands.setViewport(0, vk::Viewport{0.0f, 0.0f, float(framebuffer.width), float(framebuffer.height), 0.0f, 1.0f});
        commands.setScissor(0, vk::Rect2D{{0, 0}, framebuffer});
    }

    //Slika koju je rasterizator zadnje nacrtao - za test, float RGBA
    //Rasterizator ostavlja metu u eTransferSrcOptimal (vidi setStorageImage u SplatRendereru), pa
    //se kopira izravno; RenderTarget::readPixels trazi da je to i njezin zavrsni raspored
    ImageData readPixels() const{
        ImageData out;
        if(!target) return out;
        loom.waitIdle();
        const vk::DeviceSize bytes = vk::DeviceSize(size.width) * size.height * 16;
        VulkanBuffer staging(loom.device, bytes, vk::BufferUsageFlagBits::eTransferDst, MemoryUsage::GPU_TO_CPU);
        loom.command.copyImageToBuffer(target->getColorImage().getImage(), staging.getBuffer(), size);
        out.extent = size;
        out.format = vk::Format::eR32G32B32A32Sfloat;
        out.pixels.resize(size_t(bytes));
        staging.download(out.pixels.data(), bytes);
        return out;
    }

private:
    void load(const std::string& path){
        std::vector<SplatMath::RawSplat> result;
        std::string failure;
        try{
            const Spool::GaussianCloud cloud = Spool::loadGaussianPly(path);
            result.reserve(cloud.count());
            for(const Spool::Gaussian& g : cloud.gaussians){
                SplatMath::RawSplat one;
                one.positionOpacity = glm::vec4(g.position[0], g.position[1], g.position[2],
                                                SplatMath::activateOpacity(g.opacity));
                one.scale = glm::vec4(SplatMath::activateScale(glm::vec3(g.scale[0], g.scale[1], g.scale[2])), 0.0f);
                const glm::quat rotation = SplatMath::activateRotation(glm::vec4(g.rotation[0], g.rotation[1],
                                                                                g.rotation[2], g.rotation[3]));
                one.rotation = glm::vec4(rotation.w, rotation.x, rotation.y, rotation.z);
                one.dc = glm::vec4(g.dc[0], g.dc[1], g.dc[2], 0.0f);
                result.push_back(one);
            }
        }catch(const std::exception& e){
            failure = e.what();
        }
        std::lock_guard<std::mutex> guard(lock);
        raw = std::move(result);
        alive.assign(raw.size(), 1);
        cuts.clear();
        redoCuts.clear();
        problem = failure;
        loaded = failure.empty() && !raw.empty();
        loading = false;
    }

    //Meta, prikaz i rasterizator se grade iznova: nijedan se ne da prosiriti na mjestu (vidi
    //SplatViewer::buildForSize). Splatovi ostaju na procesoru da se mogu poslati ponovno
    void build(vk::Extent2D extent){
        loom.waitIdle();
        renderer.reset();
        material.reset();
        target.reset();

        RenderTargetConfig targetConfig;
        targetConfig.colorFormat = vk::Format::eR32G32B32A32Sfloat;
        targetConfig.extraColorUsage = vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eStorage;
        targetConfig.enableDepth = false;
        targetConfig.finalLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
        target.emplace(loom.device, extent, targetConfig);
        material.emplace(loom.device, loom.command, loom.getDescriptorPool(), *pipeline, target->getSampled());

        //Rezani splat salje samo zive; bez rezanja cijeli, bez kopije
        std::vector<SplatMath::RawSplat> living;
        if(!cuts.empty()){
            for(size_t i = 0; i < raw.size(); ++i) if(alive[i]) living.push_back(raw[i]);
        }
        const std::vector<SplatMath::RawSplat>& shown = cuts.empty() ? raw : living;
        splatCount = shown.size();
        size = extent;
        if(shown.empty()) return;          //sve odrezano: nema sto crtati, prepare vraca false

        SplatRendererConfig config;
        config.maxSplats = uint32_t(shown.size());
        config.maxPairs = 65535u * 256u;
        config.maxShCoefficients = 0;
        renderer.emplace(loom.device, loom.getDescriptorPool(), target->getColorImage(), extent, config);
        renderer->uploadRaw(shown, {}, 0, 0);
    }

    LoomInitializer& loom;
    std::optional<VulkanGraphicsPipeline> pipeline;
    std::optional<RenderTarget> target;
    std::optional<Material> material;
    std::optional<SplatRenderer> renderer;
    vk::Extent2D size{0, 0};

    std::string wantedPath, uploadedPath;
    std::thread loader;
    mutable std::mutex lock;
    std::vector<SplatMath::RawSplat> raw;
    std::vector<uint8_t> alive;                   //po gaussianu iz datoteke: 0 = odrezan
    std::vector<std::vector<uint32_t>> cuts;      //svako rezanje, za korak natrag
    std::vector<std::vector<uint32_t>> redoCuts;
    std::string problem;
    bool loaded = false;
    std::atomic<bool> loading{false};
    size_t splatCount = 0;
};

//Je li .ply gaussian splat (a ne mreza ili oblak tocaka): glava mora imati koeficijente boje
//f_dc i mjerilo. Cita se samo glava, prvih par kilobajta
inline bool isGaussianPly(const std::string& path){
    std::ifstream file(path, std::ios::binary);
    if(!file) return false;
    char head[4096] = {0};
    file.read(head, sizeof(head) - 1);
    const std::string text(head, size_t(file.gcount()));
    const size_t end = text.find("end_header");
    if(text.rfind("ply", 0) != 0 || end == std::string::npos) return false;
    const std::string header = text.substr(0, end);
    return header.find("f_dc_0") != std::string::npos && header.find("scale_0") != std::string::npos;
}

}
