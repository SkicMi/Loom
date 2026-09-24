#include "DescriptorMatcher.h"

#include "Core/LoomInitializer.h"
#include "ComputeMaterial.h"
#include "VulkanBuffer.h"
#include "VulkanComputePipeline.h"

#include <cmath>
#include <cstring>
#include <optional>
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

ComputePipelineConfig configFor(const std::string& shader, uint32_t bindings){
    ComputePipelineConfig config;
    config.shaderPath = std::string(LOOM_SHADER_DIR) + "/" + shader;
    for(uint32_t i = 0; i < bindings; ++i) config.descriptorBindings.push_back(storageBinding(i));
    config.pushConstantSize = 32;
    return config;
}

struct Params{
    uint32_t pairCount = 0, totalThreads = 0, maxSquared = 0, padding0 = 0;
    float radiusSquared = 0, cell = 1, ratioSquared = 0, apartSquared = 0;
};

//Najvise niti po prolazu: 65535 grupa po 128, granica koju Vulkan jamci za x
constexpr uint32_t threadBudget = 65535u * 128u;
constexpr uint32_t pairBudget = 8192;
constexpr uint32_t none = 0xFFFFFFFFu;

}

struct DescriptorMatcher::State{
    VulkanComputePipeline best, reverse;
    State(const VulkanDevice& device)
    : best(device, configFor("sift_match_best.comp.spv", 8)),
      reverse(device, configFor("sift_match_reverse.comp.spv", 6)){}
};

DescriptorMatcher::DescriptorMatcher(LoomInitializer& loom) : loom(loom), state(std::make_unique<State>(loom.device)){}
DescriptorMatcher::~DescriptorMatcher() = default;

std::vector<std::vector<DescriptorMatcher::Match>> DescriptorMatcher::match(
        const std::vector<Frame>& frames, const std::vector<std::pair<uint32_t, uint32_t>>& pairs, const Rules& rules){
    std::vector<std::vector<Match>> out(pairs.size());
    if(frames.empty() || pairs.empty()) return out;

    //Sve znacajke svih kadrova u jedno polje
    std::vector<uint32_t> frameInfo(frames.size() * 2);
    uint64_t total = 0;
    for(size_t f = 0; f < frames.size(); ++f){
        frameInfo[f * 2] = uint32_t(total);
        frameInfo[f * 2 + 1] = frames[f].count;
        total += frames[f].count;
    }
    if(total == 0) return out;
    std::vector<uint8_t> descriptors(static_cast<size_t>(total) * 128);
    std::vector<float> positions(static_cast<size_t>(total) * 2);
    std::vector<uint32_t> valid(static_cast<size_t>(total), 0u);
    for(size_t f = 0; f < frames.size(); ++f){
        const size_t first = frameInfo[f * 2];
        if(frames[f].count == 0) continue;
        std::memcpy(descriptors.data() + first * 128, frames[f].descriptors, size_t(frames[f].count) * 128);
        std::memcpy(positions.data() + first * 2, frames[f].positions, size_t(frames[f].count) * 2 * sizeof(float));
        for(uint32_t i = 0; i < frames[f].count; ++i) valid[first + i] = frames[f].valid[i] ? 1u : 0u;
    }

    const VulkanDevice& device = loom.device;
    const auto storage = vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferDst | vk::BufferUsageFlagBits::eTransferSrc;
    auto onCard = [&](const void* data, vk::DeviceSize bytes){
        VulkanBuffer card(device, std::max<vk::DeviceSize>(bytes, 16), storage, MemoryUsage::GPU_ONLY);
        if(bytes > 0){
            VulkanBuffer staging(device, bytes, vk::BufferUsageFlagBits::eTransferSrc, MemoryUsage::CPU_TO_GPU);
            staging.upload(data, bytes);
            loom.command.copyBuffer(staging.getBuffer(), card.getBuffer(), bytes);
        }
        return card;
    };
    VulkanBuffer descriptorBuffer = onCard(descriptors.data(), descriptors.size());
    VulkanBuffer positionBuffer = onCard(positions.data(), positions.size() * sizeof(float));
    VulkanBuffer validBuffer = onCard(valid.data(), valid.size() * sizeof(uint32_t));
    VulkanBuffer frameBuffer = onCard(frameInfo.data(), frameInfo.size() * sizeof(uint32_t));

    VulkanBuffer pairBuffer(device, vk::DeviceSize(pairBudget) * 16, storage, MemoryUsage::GPU_ONLY);
    VulkanBuffer bestTo(device, vk::DeviceSize(threadBudget) * 4, storage, MemoryUsage::GPU_ONLY);
    VulkanBuffer bestSquared(device, vk::DeviceSize(threadBudget) * 4, storage, MemoryUsage::GPU_ONLY);
    VulkanBuffer accepted(device, vk::DeviceSize(threadBudget) * 4, storage, MemoryUsage::GPU_ONLY);
    VulkanBuffer bestFrom(device, vk::DeviceSize(threadBudget) * 4, storage, MemoryUsage::GPU_ONLY);

    ComputeMaterial bestMaterial(device, loom.getDescriptorPool(), state->best);
    ComputeMaterial reverseMaterial(device, loom.getDescriptorPool(), state->reverse);
    for(ComputeMaterial* m : {&bestMaterial, &reverseMaterial}){
        m->setStorageBuffer(0, descriptorBuffer);
        m->setStorageBuffer(1, positionBuffer);
        m->setStorageBuffer(2, validBuffer);
        m->setStorageBuffer(3, pairBuffer);
        m->setStorageBuffer(4, frameBuffer);
    }
    bestMaterial.setStorageBuffer(5, bestTo);
    bestMaterial.setStorageBuffer(6, bestSquared);
    bestMaterial.setStorageBuffer(7, accepted);
    reverseMaterial.setStorageBuffer(5, bestFrom);

    Params params;
    params.radiusSquared = rules.radius * rules.radius;
    params.cell = std::max(1.0f, rules.radius);
    params.maxSquared = uint32_t(int32_t(rules.maxDistance * rules.maxDistance));
    params.ratioSquared = float(double(rules.ratio) * double(rules.ratio));
    params.apartSquared = rules.secondBestApart * rules.secondBestApart;

    size_t next = 0;
    std::vector<uint32_t> readTo, readSquared, readAccepted, readFrom;
    while(next < pairs.size()){
        //Paket parova koji stane u granicu niti za oba prolaza
        std::vector<uint32_t> packed;
        uint32_t threadsA = 0, threadsB = 0;
        size_t last = next;
        while(last < pairs.size() && (last - next) < pairBudget){
            const uint32_t countA = frames[pairs[last].first].count, countB = frames[pairs[last].second].count;
            if(last > next && (uint64_t(threadsA) + countA > threadBudget || uint64_t(threadsB) + countB > threadBudget)) break;
            if(countA > threadBudget || countB > threadBudget) throw std::runtime_error("DescriptorMatcher: kadar ima vise znacajki nego sto stane u jedan prolaz");
            packed.insert(packed.end(), {pairs[last].first, pairs[last].second, threadsA, threadsB});
            threadsA += countA;
            threadsB += countB;
            ++last;
        }
        {
            VulkanBuffer staging(device, packed.size() * 4, vk::BufferUsageFlagBits::eTransferSrc, MemoryUsage::CPU_TO_GPU);
            staging.upload(packed.data(), packed.size() * 4);
            loom.command.copyBuffer(staging.getBuffer(), pairBuffer.getBuffer(), packed.size() * 4);
        }
        params.pairCount = uint32_t(last - next);

        loom.renderer.beginFrame();
        if(threadsA > 0){
            params.totalThreads = threadsA;
            loom.renderer.dispatch(bestMaterial, (threadsA + 127) / 128, 1, 1, &params, sizeof(params));
        }
        if(threadsB > 0){
            params.totalThreads = threadsB;
            loom.renderer.dispatch(reverseMaterial, (threadsB + 127) / 128, 1, 1, &params, sizeof(params));
        }
        loom.renderer.endFrame();
        loom.waitIdle();

        auto readBack = [&](const VulkanBuffer& source, uint32_t count, std::vector<uint32_t>& into){
            into.resize(count);
            if(count == 0) return;
            VulkanBuffer staging(device, vk::DeviceSize(count) * 4, vk::BufferUsageFlagBits::eTransferDst, MemoryUsage::GPU_TO_CPU);
            loom.command.copyBuffer(source.getBuffer(), staging.getBuffer(), vk::DeviceSize(count) * 4);
            staging.download(into.data(), vk::DeviceSize(count) * 4);
        };
        readBack(bestTo, threadsA, readTo);
        readBack(bestSquared, threadsA, readSquared);
        readBack(accepted, threadsA, readAccepted);
        readBack(bestFrom, threadsB, readFrom);

        //Uzajamno najbolji: A->B prihvacen, i B->A pokazuje natrag
        for(size_t p = next; p < last; ++p){
            const size_t local = p - next;
            const uint32_t startA = packed[local * 4 + 2], startB = packed[local * 4 + 3];
            const uint32_t countA = frames[pairs[p].first].count;
            std::vector<Match>& found = out[p];
            for(uint32_t i = 0; i < countA; ++i){
                if(!readAccepted[startA + i]) continue;
                const uint32_t j = readTo[startA + i];
                if(j == none || readFrom[startB + j] != i) continue;
                found.push_back(Match{i, j, std::sqrt(float(readSquared[startA + i]))});
            }
        }
        next = last;
    }
    return out;
}
