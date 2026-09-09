#include "RadixSort.h"
#include "VulkanRenderer.h"

#include <cstring>
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

uint32_t RadixSort::keyFromFloat(float value){
    uint32_t bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));

    //Negativan broj: okreni sve bitove, pa veca mantisa postane manji kljuc i cijeli negativni
    //raspon padne ispod pozitivnog. Pozitivan: okreni samo predznak, da se digne iznad njih
    return (bits & 0x80000000u) ? ~bits : (bits | 0x80000000u);
}

uint32_t RadixSort::blocksFor(uint32_t count){
    return (count + elementsPerBlock - 1) / elementsPerBlock;
}

RadixSort::RadixSort(const VulkanDevice& device,
                     const vk::raii::DescriptorPool& pool,
                     VulkanBuffer& keys,
                     VulkanBuffer& values,
                     uint32_t capacity)
: device(device),
  keys(&keys),
  values(&values),
  scratchKeys(device, vk::DeviceSize(capacity) * sizeof(uint32_t),
              vk::BufferUsageFlagBits::eStorageBuffer, MemoryUsage::GPU_ONLY),
  scratchValues(device, vk::DeviceSize(capacity) * sizeof(uint32_t),
                vk::BufferUsageFlagBits::eStorageBuffer, MemoryUsage::GPU_ONLY),
  //Tablica je [znamenka][blok] i mora stati za najveci broj elemenata koji ce doci
  blockHistogram(device, vk::DeviceSize(digits) * blocksFor(capacity) * sizeof(uint32_t),
                 vk::BufferUsageFlagBits::eStorageBuffer, MemoryUsage::GPU_ONLY),
  histogramPipeline(device, configFor("radix_histogram.comp.spv", 2, sizeof(Params))),
  scanPipeline(device, configFor("radix_scan.comp.spv", 1, sizeof(Params))),
  scatterPipeline(device, configFor("radix_scatter.comp.spv", 5, sizeof(Params))),
  histogramForward(device, pool, histogramPipeline),
  histogramBackward(device, pool, histogramPipeline),
  scan(device, pool, scanPipeline),
  scatterForward(device, pool, scatterPipeline),
  scatterBackward(device, pool, scatterPipeline){

    if(capacity == 0){
        throw std::runtime_error("RadixSort: kapacitet nula - nema se sto sortirati");
    }

    histogramForward.setStorageBuffer(0, keys);
    histogramForward.setStorageBuffer(1, blockHistogram);

    histogramBackward.setStorageBuffer(0, scratchKeys);
    histogramBackward.setStorageBuffer(1, blockHistogram);

    scan.setStorageBuffer(0, blockHistogram);

    scatterForward.setStorageBuffer(0, keys);
    scatterForward.setStorageBuffer(1, values);
    scatterForward.setStorageBuffer(2, scratchKeys);
    scatterForward.setStorageBuffer(3, scratchValues);
    scatterForward.setStorageBuffer(4, blockHistogram);

    scatterBackward.setStorageBuffer(0, scratchKeys);
    scatterBackward.setStorageBuffer(1, scratchValues);
    scatterBackward.setStorageBuffer(2, keys);
    scatterBackward.setStorageBuffer(3, values);
    scatterBackward.setStorageBuffer(4, blockHistogram);
}

void RadixSort::sort(VulkanRenderer& renderer, uint32_t count){
    if(count == 0){
        return;   //nista za sortirati nije greska, samo nista
    }

    const uint32_t blocks = blocksFor(count);

    for(uint32_t pass = 0; pass < passes; ++pass){
        Params params;
        params.count = count;
        params.shift = pass * 4;
        params.blockCount = blocks;

        //Paran prolaz cita korisnikove buffere, neparan radne. Osam prolaza znaci da zadnji
        //pise natrag u korisnikove, i zato je broj prolaza paran a ne slucajan
        const bool forward = (pass % 2) == 0;

        renderer.dispatch(forward ? histogramForward : histogramBackward,
                          blocks, 1, 1, &params, sizeof(params));

        //Jedan blok, jedna dretva - vidi radix_scan.slang
        renderer.dispatch(scan, 1, 1, 1, &params, sizeof(params));

        renderer.dispatch(forward ? scatterForward : scatterBackward,
                          blocks, 1, 1, &params, sizeof(params));
    }
}
