#include "PrefixSum.h"
#include "VulkanRenderer.h"

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

ComputePipelineConfig configFor(const std::string& shader, uint32_t bindings, uint32_t pushSize){
    ComputePipelineConfig config;
    config.shaderPath = std::string(LOOM_SHADER_DIR) + "/" + shader;
    for(uint32_t i = 0; i < bindings; ++i){
        config.descriptorBindings.push_back(storageBinding(i));
    }
    config.pushConstantSize = pushSize;
    return config;
}

}

uint32_t PrefixSum::blocksFor(uint32_t count){
    return (count + elementsPerBlock - 1) / elementsPerBlock;
}

PrefixSum::PrefixSum(const VulkanDevice& device,
                     const vk::raii::DescriptorPool& pool,
                     VulkanBuffer& values,
                     uint32_t capacity)
: device(device),
  blockTotals(device, vk::DeviceSize(blocksFor(capacity)) * sizeof(uint32_t),
              vk::BufferUsageFlagBits::eStorageBuffer, MemoryUsage::GPU_ONLY),
  total(device, sizeof(uint32_t),
        vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferSrc,
        MemoryUsage::GPU_ONLY),
  blocksPipeline(device, configFor("scan_blocks.comp.spv", 3, sizeof(Params))),
  totalsPipeline(device, configFor("scan_totals.comp.spv", 1, sizeof(Params))),
  addPipeline(device, configFor("scan_add.comp.spv", 3, sizeof(Params))),
  blocksMaterial(device, pool, blocksPipeline),
  totalsMaterial(device, pool, totalsPipeline),
  addMaterial(device, pool, addPipeline){

    if(capacity == 0){
        throw std::runtime_error("PrefixSum: kapacitet nula - nema se sto zbrajati");
    }

    //Zbroj blokova mora stati u jednu grupu prolaza 2, koja ide u komadima po 256. Za milijun
    //elemenata to je 977 blokova i nekoliko komada - ali granica postoji i bolje je da se cuje
    if(blocksFor(capacity) > 65535){
        throw std::runtime_error("PrefixSum: previse blokova za jedan prolaz drugog stupnja");
    }

    blocksMaterial.setStorageBuffer(0, values);
    blocksMaterial.setStorageBuffer(1, blockTotals);
    blocksMaterial.setStorageBuffer(2, total);

    totalsMaterial.setStorageBuffer(0, blockTotals);

    addMaterial.setStorageBuffer(0, values);
    addMaterial.setStorageBuffer(1, blockTotals);
    addMaterial.setStorageBuffer(2, total);
}

void PrefixSum::scan(VulkanRenderer& renderer, uint32_t count){
    if(count == 0){
        return;
    }

    Params params;
    params.count = count;
    params.blockCount = blocksFor(count);

    renderer.dispatch(blocksMaterial, params.blockCount, 1, 1, &params, sizeof(params));
    renderer.dispatch(totalsMaterial, 1, 1, 1, &params, sizeof(params));
    renderer.dispatch(addMaterial, params.blockCount, 1, 1, &params, sizeof(params));
}
