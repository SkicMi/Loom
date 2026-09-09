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
         vk::BufferUsageFlagBits::eStorageBuffer, MemoryUsage::CPU_TO_GPU),
  counts(device, vk::DeviceSize(config.maxSplats) * sizeof(uint32_t),
         vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferSrc,
         MemoryUsage::GPU_ONLY),
  tileKeys(device, vk::DeviceSize(config.maxPairs) * sizeof(uint32_t),
           vk::BufferUsageFlagBits::eStorageBuffer, MemoryUsage::GPU_ONLY),
  depthKeys(device, vk::DeviceSize(config.maxPairs) * sizeof(uint32_t),
            vk::BufferUsageFlagBits::eStorageBuffer, MemoryUsage::GPU_ONLY),
  splatIndices(device, vk::DeviceSize(config.maxPairs) * sizeof(uint32_t),
               vk::BufferUsageFlagBits::eStorageBuffer, MemoryUsage::GPU_ONLY),
  sortValues(device, vk::DeviceSize(config.maxPairs) * sizeof(uint32_t),
             vk::BufferUsageFlagBits::eStorageBuffer, MemoryUsage::GPU_ONLY),
  gatheredTiles(device, vk::DeviceSize(config.maxPairs) * sizeof(uint32_t),
                vk::BufferUsageFlagBits::eStorageBuffer, MemoryUsage::GPU_ONLY),
  ranges(device, vk::DeviceSize(grid.width) * grid.height * 2 * sizeof(uint32_t),
         vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferSrc,
         MemoryUsage::GPU_ONLY),
  prefixSum(device, pool, counts, config.maxSplats),
  sortByDepth(device, pool, depthKeys, sortValues, config.maxPairs),
  sortByTile(device, pool, gatheredTiles, sortValues, config.maxPairs),
  countPipeline(device, configFor("splat_count.comp.spv", 2, sizeof(TileParams))),
  expandPipeline(device, configFor("splat_expand.comp.spv", 6, sizeof(TileParams))),
  gatherPipeline(device, configFor("splat_gather_tiles.comp.spv", 3, sizeof(PairParams))),
  clearPipeline(device, configFor("splat_clear_ranges.comp.spv", 1, sizeof(PairParams))),
  rangesPipeline(device, configFor("splat_ranges.comp.spv", 2, sizeof(PairParams))),
  rasterPipeline(device, [&]{
      ComputePipelineConfig raster = configFor("splat_raster.comp.spv", 4, sizeof(RasterParams), true);
      raster.specializationConstants = {config.tileSize};   //velicina pločice JE velicina grupe
      return raster;
  }()),
  countMaterial(device, pool, countPipeline),
  expandMaterial(device, pool, expandPipeline),
  gatherMaterial(device, pool, gatherPipeline),
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

    countMaterial.setStorageBuffer(0, splats);
    countMaterial.setStorageBuffer(1, counts);

    expandMaterial.setStorageBuffer(0, splats);
    expandMaterial.setStorageBuffer(1, counts);
    expandMaterial.setStorageBuffer(2, tileKeys);
    expandMaterial.setStorageBuffer(3, depthKeys);
    expandMaterial.setStorageBuffer(4, splatIndices);
    expandMaterial.setStorageBuffer(5, sortValues);

    gatherMaterial.setStorageBuffer(0, tileKeys);
    gatherMaterial.setStorageBuffer(1, sortValues);
    gatherMaterial.setStorageBuffer(2, gatheredTiles);

    clearMaterial.setStorageBuffer(0, ranges);

    rangesMaterial.setStorageBuffer(0, gatheredTiles);
    rangesMaterial.setStorageBuffer(1, ranges);

    rasterMaterial.setStorageBuffer(0, splats);
    rasterMaterial.setStorageBuffer(1, ranges);
    rasterMaterial.setStorageBuffer(2, sortValues);
    rasterMaterial.setStorageBuffer(3, splatIndices);
    rasterMaterial.setStorageImage(4, target, vk::ImageLayout::eTransferSrcOptimal);
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

uint32_t SplatRenderer::countPairs(const std::vector<SplatMath::PreparedSplat>& prepared) const{
    uint64_t total = 0;
    for(uint32_t count : tileCounts(prepared)) total += count;

    if(total > config.maxPairs){
        throw std::runtime_error("SplatRenderer: " + std::to_string(total) + " parova, a maxPairs "
                                 "je " + std::to_string(config.maxPairs));
    }
    return uint32_t(total);
}

void SplatRenderer::draw(VulkanRenderer& renderer, uint32_t splatCount, uint32_t pairCount){
    TileParams tileParams;
    tileParams.gridX = grid.width;
    tileParams.gridY = grid.height;
    tileParams.tileSize = config.tileSize;
    tileParams.splatCount = splatCount;

    PairParams pairParams;
    pairParams.pairCount = pairCount;

    RasterParams rasterParams;
    rasterParams.imageX = extent.width;
    rasterParams.imageY = extent.height;
    rasterParams.gridX = grid.width;
    rasterParams.gridY = grid.height;
    rasterParams.pairCount = pairCount;

    //Rasponi se ciste uvijek, i kad nema nijednog para - inace bi pločica zadrzala ono sto je
    //u njoj pisalo prosli kadar
    PairParams clearParams;
    clearParams.pairCount = getTileCount();
    renderer.dispatch(clearMaterial, groupsOf(getTileCount(), 256), 1, 1, &clearParams, sizeof(clearParams));

    if(splatCount > 0 && pairCount > 0){
        renderer.dispatch(countMaterial, groupsOf(splatCount, 256), 1, 1, &tileParams, sizeof(tileParams));
        prefixSum.scan(renderer, splatCount);
        renderer.dispatch(expandMaterial, groupsOf(splatCount, 256), 1, 1, &tileParams, sizeof(tileParams));

        sortByDepth.sort(renderer, pairCount);
        renderer.dispatch(gatherMaterial, groupsOf(pairCount, 256), 1, 1, &pairParams, sizeof(pairParams));
        sortByTile.sort(renderer, pairCount);

        renderer.dispatch(rangesMaterial, groupsOf(pairCount, 256), 1, 1, &pairParams, sizeof(pairParams));
    }

    renderer.dispatch(rasterMaterial, grid.width, grid.height, 1, &rasterParams, sizeof(rasterParams));
}
