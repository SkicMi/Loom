#include "SplatRenderer.h"
#include "VulkanRenderer.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <string>

namespace{

vk::DescriptorSetLayoutBinding storageBinding(uint32_t index){
    vk::DescriptorSetLayoutBinding binding;
    binding.binding = index;
    binding.descriptorType = vk::DescriptorType::eStorageBuffer;
    binding.descriptorCount = 1;
    binding.stageFlags = vk::ShaderStageFlagBits::eCompute;
    return binding;
}

ComputePipelineConfig configFor(const std::string& shader, uint32_t buffers, uint32_t pushSize,
                                bool lastIsImage = false){
    ComputePipelineConfig config;
    config.shaderPath = std::string(LOOM_SHADER_DIR) + "/" + shader;
    for(uint32_t i = 0; i < buffers; ++i){
        config.descriptorBindings.push_back(storageBinding(i));
    }
    if(lastIsImage){
        vk::DescriptorSetLayoutBinding image = storageBinding(buffers);
        image.descriptorType = vk::DescriptorType::eStorageImage;
        config.descriptorBindings.push_back(image);
    }
    config.pushConstantSize = pushSize;
    return config;
}

uint32_t groupsOf(uint32_t count, uint32_t size){
    return (count + size - 1) / size;
}

//Najmanje bita u koje stanu brojevi od 0 do count-1
uint32_t bitsFor(uint32_t count){
    uint32_t bits = 1;
    while(bits < 32 && (uint64_t(1) << bits) < count) ++bits;
    return bits;
}

}

SplatRenderer::SplatRenderer(const VulkanDevice& device,
                             const vk::raii::DescriptorPool& pool,
                             const VulkanImage& target,
                             vk::Extent2D extent,
                             const SplatRendererConfig& config)
: device(device),
  config(config),
  extent(extent),
  grid{groupsOf(extent.width, config.tileSize), groupsOf(extent.height, config.tileSize)},
  splats(device, vk::DeviceSize(config.maxSplats) * sizeof(SplatMath::PreparedSplat),
         vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferSrc,
         MemoryUsage::CPU_TO_GPU),
  rawSplats(device, vk::DeviceSize(config.maxSplats) * sizeof(SplatMath::RawSplat),
            vk::BufferUsageFlagBits::eStorageBuffer, MemoryUsage::CPU_TO_GPU),
  //Koeficijenti visih stupnjeva. Najmanje jedan float, jer buffer velicine nula ne postoji
  shRest(device, vk::DeviceSize(std::max<uint64_t>(1, uint64_t(config.maxSplats) * config.maxShCoefficients)) * sizeof(float),
         vk::BufferUsageFlagBits::eStorageBuffer, MemoryUsage::CPU_TO_GPU),
  prepareParams(device, sizeof(SplatMath::PrepareParams),
                vk::BufferUsageFlagBits::eStorageBuffer, MemoryUsage::CPU_TO_GPU),
  //Pise ga kartica, cita dispatchIndirect - i procesor poslije kadra, kad pita je li sve stalo
  pairSizes(device, vk::DeviceSize(pairSizeSlots) * sizeof(uint32_t),
            vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eIndirectBuffer,
            MemoryUsage::GPU_TO_CPU),
  counts(device, vk::DeviceSize(config.maxSplats) * sizeof(uint32_t),
         vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferSrc,
         MemoryUsage::GPU_ONLY),
  depthKeys(device, vk::DeviceSize(config.maxSplats) * sizeof(uint32_t),
            vk::BufferUsageFlagBits::eStorageBuffer, MemoryUsage::GPU_ONLY),
  splatOrder(device, vk::DeviceSize(config.maxSplats) * sizeof(uint32_t),
             vk::BufferUsageFlagBits::eStorageBuffer, MemoryUsage::GPU_ONLY),
  tileKeys(device, vk::DeviceSize(config.maxPairs) * sizeof(uint32_t),
           vk::BufferUsageFlagBits::eStorageBuffer, MemoryUsage::GPU_ONLY),
  pairSplats(device, vk::DeviceSize(config.maxPairs) * sizeof(uint32_t),
             vk::BufferUsageFlagBits::eStorageBuffer, MemoryUsage::GPU_ONLY),
  ranges(device, vk::DeviceSize(grid.width) * grid.height * 2 * sizeof(uint32_t),
         vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferSrc,
         MemoryUsage::GPU_ONLY),
  prefixSum(device, pool, counts, config.maxSplats),
  //Po dubini: splatova ima koliko procesor kaze, pa ovaj sort ide izravno i ne treba izvor broja
  sortByDepth(device, pool, depthKeys, splatOrder, config.maxSplats),
  //Po pločici: broj zna samo kartica, a kljuc treba samo onoliko bita koliko ima pločica
  sortByTile(device, pool, tileKeys, pairSplats, config.maxPairs, &pairSizes, bitsFor(grid.width * grid.height)),
  preparePipeline(device, configFor("splat_prepare.comp.spv", 4, 0)),
  pairSizesPipeline(device, configFor("splat_pair_sizes.comp.spv", 2, sizeof(PairSizeParams))),
  countPipeline(device, configFor("splat_count.comp.spv", 3, sizeof(TileParams))),
  expandPipeline(device, configFor("splat_expand.comp.spv", 5, sizeof(TileParams))),
  depthKeysPipeline(device, configFor("splat_depth_keys.comp.spv", 3, sizeof(PairParams))),
  clearPipeline(device, configFor("splat_clear_ranges.comp.spv", 1, sizeof(PairParams))),
  rangesPipeline(device, configFor("splat_ranges.comp.spv", 3, 0)),
  rasterPipeline(device, [&]{
      ComputePipelineConfig raster = configFor("splat_raster.comp.spv", 3, sizeof(RasterParams), true);
      raster.specializationConstants = {config.tileSize};   //velicina pločice JE velicina grupe
      return raster;
  }()),
  prepareMaterial(device, pool, preparePipeline),
  pairSizesMaterial(device, pool, pairSizesPipeline),
  countMaterial(device, pool, countPipeline),
  expandMaterial(device, pool, expandPipeline),
  depthKeysMaterial(device, pool, depthKeysPipeline),
  clearMaterial(device, pool, clearPipeline),
  rangesMaterial(device, pool, rangesPipeline),
  rasterMaterial(device, pool, rasterPipeline){

    if(config.tileSize == 0 || (config.tileSize & (config.tileSize - 1)) != 0){
        throw std::runtime_error("SplatRenderer: velicina pločice mora biti potencija dvojke");
    }
    if(config.tileSize * config.tileSize > device.getPhysicalDevice().getProperties().limits.maxComputeWorkGroupInvocations){
        throw std::runtime_error("SplatRenderer: pločica " + std::to_string(config.tileSize) +
                                 "x" + std::to_string(config.tileSize) + " trazi vise dretvi po "
                                 "grupi nego sto uredjaj dopusta");
    }

    //Velicine dispatcha racuna kartica, pa ih procesor nema kad provjeriti - zato se granica
    //uredjaja provjerava ovdje, za najveci broj koji ikad moze doci. Nije teoretski: 16<<20
    //parova je 65536 grupa po 256, jedna vise od najmanje granice koju Vulkan jamci
    const uint32_t maxGroups = device.getPhysicalDevice().getProperties().limits.maxComputeWorkGroupCount[0];
    if(groupsOf(config.maxPairs, pairGroupSize) > maxGroups || groupsOf(config.maxSplats, 256) > maxGroups){
        throw std::runtime_error("SplatRenderer: maxPairs " + std::to_string(config.maxPairs) +
                                 " ili maxSplats " + std::to_string(config.maxSplats) +
                                 " trazi vise od " + std::to_string(maxGroups) +
                                 " grupa po 256, koliko uredjaj dopusta");
    }

    //Prije prvog kadra nije se trazio nijedan par - i to mora pisati, a ne ono sto je zateceno
    //u memoriji
    const uint32_t zeros[pairSizeSlots] = {};
    pairSizes.upload(zeros, sizeof(zeros));

    pairSizesMaterial.setStorageBuffer(0, prefixSum.getTotal());
    pairSizesMaterial.setStorageBuffer(1, pairSizes);

    prepareMaterial.setStorageBuffer(0, rawSplats);
    prepareMaterial.setStorageBuffer(1, shRest);
    prepareMaterial.setStorageBuffer(2, prepareParams);
    prepareMaterial.setStorageBuffer(3, splats);

    depthKeysMaterial.setStorageBuffer(0, splats);
    depthKeysMaterial.setStorageBuffer(1, depthKeys);
    depthKeysMaterial.setStorageBuffer(2, splatOrder);

    countMaterial.setStorageBuffer(0, splats);
    countMaterial.setStorageBuffer(1, counts);
    countMaterial.setStorageBuffer(2, splatOrder);

    expandMaterial.setStorageBuffer(0, splats);
    expandMaterial.setStorageBuffer(1, counts);
    expandMaterial.setStorageBuffer(2, tileKeys);
    expandMaterial.setStorageBuffer(3, pairSplats);
    expandMaterial.setStorageBuffer(4, splatOrder);

    clearMaterial.setStorageBuffer(0, ranges);

    //Sort po pločici je kljuceve preslozio na mjestu, pa rasponi citaju bas njih
    rangesMaterial.setStorageBuffer(0, tileKeys);
    rangesMaterial.setStorageBuffer(1, ranges);
    rangesMaterial.setStorageBuffer(2, pairSizes);

    rasterMaterial.setStorageBuffer(0, splats);
    rasterMaterial.setStorageBuffer(1, ranges);
    rasterMaterial.setStorageBuffer(2, pairSplats);
    rasterMaterial.setStorageImage(3, target, vk::ImageLayout::eTransferSrcOptimal);
}

void SplatRenderer::upload(const std::vector<SplatMath::PreparedSplat>& prepared){
    if(prepared.size() > config.maxSplats){
        throw std::runtime_error("SplatRenderer: vise splatova nego sto je receno u maxSplats");
    }
    splats.upload(prepared.data(), prepared.size() * sizeof(SplatMath::PreparedSplat));
}

std::vector<uint32_t> SplatRenderer::tileCounts(const std::vector<SplatMath::PreparedSplat>& prepared) const{
    //Ista matematika koju racuna splat_count.slang. Da se razidju, prefiksni zbroj bi rezervirao
    //jedan broj mjesta a sirenje zapisalo drugi - test ih zato usporedjuje izravno
    std::vector<uint32_t> counts(prepared.size(), 0);

    for(size_t i = 0; i < prepared.size(); ++i){
        const float radius = prepared[i].conicOpacityDepth.w;
        if(radius <= 0.0f) continue;

        const float tile = float(config.tileSize);
        const int firstX = std::clamp(int(std::floor((prepared[i].centerConic.x - radius) / tile)), 0, int(grid.width));
        const int firstY = std::clamp(int(std::floor((prepared[i].centerConic.y - radius) / tile)), 0, int(grid.height));
        const int lastX  = std::clamp(int(std::floor((prepared[i].centerConic.x + radius) / tile)) + 1, 0, int(grid.width));
        const int lastY  = std::clamp(int(std::floor((prepared[i].centerConic.y + radius) / tile)) + 1, 0, int(grid.height));

        counts[i] = uint32_t(std::max(0, lastX - firstX)) * uint32_t(std::max(0, lastY - firstY));
    }
    return counts;
}

void SplatRenderer::uploadRaw(const std::vector<SplatMath::RawSplat>& raw,
                              const std::vector<float>& rest,
                              uint32_t shDegree,
                              uint32_t coeffsPerChannel){
    if(raw.size() > config.maxSplats){
        throw std::runtime_error("SplatRenderer: vise splatova nego sto je receno u maxSplats");
    }
    if(coeffsPerChannel * 3 > config.maxShCoefficients){
        throw std::runtime_error("SplatRenderer: " + std::to_string(coeffsPerChannel * 3) +
                                 " koeficijenata po splatu, a maxShCoefficients je " +
                                 std::to_string(config.maxShCoefficients));
    }
    if(!rest.empty() && rest.size() != raw.size() * coeffsPerChannel * 3){
        throw std::runtime_error("SplatRenderer: broj koeficijenata ne odgovara broju splatova");
    }

    rawSplats.upload(raw.data(), raw.size() * sizeof(SplatMath::RawSplat));
    if(!rest.empty()){
        shRest.upload(rest.data(), rest.size() * sizeof(float));
    }

    storedDegree = shDegree;
    storedCoeffs = coeffsPerChannel;
}

void SplatRenderer::setCamera(const glm::mat4& view, const glm::vec3& cameraPosition,
                              float focalX, float focalY, float principalX, float principalY,
                              float blur){
    SplatMath::PrepareParams params;
    params.view = view;
    params.cameraPosition = glm::vec4(cameraPosition, 0.0f);
    params.focalPrincipal = glm::vec4(focalX, focalY, principalX, principalY);

    //Ogranicenje omjera se izvodi iz same slike, isto kao u SplatMath::prepare
    params.limitsBlur = glm::vec4(1.3f * principalX / focalX,
                                  1.3f * principalY / std::fabs(focalY),
                                  blur, 0.0f);
    params.counts = glm::uvec4(0, storedDegree, storedCoeffs, 0);

    pendingParams = params;
}

void SplatRenderer::prepare(VulkanRenderer& renderer, uint32_t splatCount){
    pendingParams.counts.x = splatCount;
    prepareParams.upload(&pendingParams, sizeof(pendingParams));

    //Oznake ne koste nista kad renderer ne mjeri - vidi RendererConfig::maxTimestamps
    renderer.timestamp("pocetak pripreme");
    renderer.dispatch(prepareMaterial, groupsOf(splatCount, 256), 1, 1);
    renderer.timestamp("priprema");
}

uint32_t SplatRenderer::countPairs(const std::vector<SplatMath::PreparedSplat>& prepared) const{
    uint64_t total = 0;
    for(uint32_t count : tileCounts(prepared)) total += count;

    if(total > config.maxPairs){
        throw std::runtime_error("SplatRenderer: " + std::to_string(total) + " parova, a maxPairs "
                                 "je " + std::to_string(config.maxPairs));
    }
    return uint32_t(total);
}

uint32_t SplatRenderer::requestedPairs() const{
    uint32_t value = 0;
    pairSizes.download(&value, sizeof(value), vk::DeviceSize(requestedSlot) * sizeof(uint32_t));
    return value;
}

uint32_t SplatRenderer::lastPairCount() const{
    uint32_t value = 0;
    pairSizes.download(&value, sizeof(value), vk::DeviceSize(sortSlot) * sizeof(uint32_t));
    return value;
}

void SplatRenderer::draw(VulkanRenderer& renderer, uint32_t splatCount){
    if(splatCount > config.maxSplats){
        throw std::runtime_error("SplatRenderer: vise splatova nego sto je receno u maxSplats");
    }

    TileParams tileParams;
    tileParams.gridX = grid.width;
    tileParams.gridY = grid.height;
    tileParams.tileSize = config.tileSize;
    tileParams.splatCount = splatCount;
    tileParams.maxPairs = config.maxPairs;

    PairSizeParams sizeParams;
    sizeParams.maxPairs = config.maxPairs;
    sizeParams.groupSize = pairGroupSize;
    sizeParams.radixBlockSize = RadixSort::elementsPerBlock;

    //Raster broj parova ne cita (on gleda raspone), pa polje ostaje nula
    RasterParams rasterParams;
    rasterParams.imageX = extent.width;
    rasterParams.imageY = extent.height;
    rasterParams.gridX = grid.width;
    rasterParams.gridY = grid.height;

    //Rasponi se ciste uvijek, i kad nema nijednog para - inace bi pločica zadrzala ono sto je
    //u njoj pisalo prosli kadar
    //Svaka oznaka imenuje korak koji je upravo zavrsio. Ova je samo pocetak: s pripremom prije
    //nje razmak je prazan hod, a bez nje (upload) od nje se tek pocinje brojati
    renderer.timestamp("pocetak crtanja");

    PairParams clearParams;
    clearParams.pairCount = getTileCount();
    renderer.dispatch(clearMaterial, groupsOf(getTileCount(), 256), 1, 1, &clearParams, sizeof(clearParams));

    if(splatCount > 0){
        //Poredak po dubini, nad SPLATOVIMA. Njihov broj procesor zna, pa ovaj sort ide izravno
        PairParams keyParams;
        keyParams.pairCount = splatCount;   //isti oblik, a broji splatove
        renderer.dispatch(depthKeysMaterial, groupsOf(splatCount, 256), 1, 1, &keyParams, sizeof(keyParams));
        sortByDepth.sort(renderer, splatCount);
        renderer.timestamp("sort po dubini");

        renderer.dispatch(countMaterial, groupsOf(splatCount, 256), 1, 1, &tileParams, sizeof(tileParams));
        renderer.timestamp("brojanje");
        prefixSum.scan(renderer, splatCount);
        renderer.timestamp("zbroj");

        //Od ovdje broj parova zna samo kartica. Ovaj dispatch ga ogranici i upise velicine svega
        //sto slijedi, pa procesor nista ne ceka i nista ne cita
        renderer.dispatch(pairSizesMaterial, 1, 1, 1, &sizeParams, sizeof(sizeParams));
        renderer.dispatch(expandMaterial, groupsOf(splatCount, 256), 1, 1, &tileParams, sizeof(tileParams));
        renderer.timestamp("sirenje");

        //Parovi su vec poredani po dubini. Stabilan sort po pločici taj poredak cuva unutar svake
        //pločice, i to je cijeli razlog zasto ovdje nema drugog sorta
        sortByTile.sortIndirect(renderer, sortSlot);
        renderer.timestamp("sort po plocici");

        renderer.dispatchIndirect(rangesMaterial, pairSizes, vk::DeviceSize(pairGroupsSlot) * sizeof(uint32_t));
        renderer.timestamp("rasponi");
    }

    renderer.dispatch(rasterMaterial, grid.width, grid.height, 1, &rasterParams, sizeof(rasterParams));
    renderer.timestamp("crtanje");
}
