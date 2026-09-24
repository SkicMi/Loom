#include "SiftDescriber.h"

#include "Core/LoomInitializer.h"
#include "ComputeMaterial.h"
#include "VulkanBuffer.h"
#include "VulkanComputePipeline.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <map>
#include <optional>
#include <string>
#include <chrono>
#include <cstdio>
#include <cstdlib>

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

struct BlurParams{ uint32_t width, height; int32_t reach; uint32_t kernelOffset; uint32_t padding[4]; };
struct GradientParams{ uint32_t width, height, packedSource, padding0; uint32_t padding[4]; };
struct DescribeParams{ uint32_t width, height, firstKey, keyCount, orient; float clampAt; uint32_t padding0, padding1; };
struct Key{ int32_t centreX, centreY; uint32_t patch, slot; };

//Jedan pojas mjerila: jezgra zamucenja (ili nista) i znacajke koje mu pripadaju
struct Band{
    int32_t reach = 0;          //0: pojas ne treba zamucenje, gradijenti idu sa same slike
    uint32_t kernelOffset = 0;
    uint32_t firstKey = 0, keyCount = 0;
};

}

struct SiftDescriber::State{
    VulkanComputePipeline rows, columns, gradients, describe;
    std::optional<ComputeMaterial> rowsMaterial, columnsMaterial, gradientsMaterial, describeMaterial;

    //Buffere se drzi izmedju kadrova; rastu kad zatreba
    std::optional<VulkanBuffer> image, kernels, across, soft, magnitude, direction, keys, descriptors, angles, valid;

    State(const VulkanDevice& device)
    : rows(device, configFor("sift_blur_rows.comp.spv", 3)),
      columns(device, configFor("sift_blur_columns.comp.spv", 3)),
      gradients(device, configFor("sift_gradients.comp.spv", 4)),
      describe(device, configFor("sift_describe.comp.spv", 6)){}
};

SiftDescriber::SiftDescriber(LoomInitializer& loom) : loom(loom), state(std::make_unique<State>(loom.device)){}
SiftDescriber::~SiftDescriber() = default;

SiftDescriber::Output SiftDescriber::describe(const uint8_t* pixels, uint32_t width, uint32_t height, uint32_t stride,
                                              const float* points, const float* scales, uint32_t count,
                                              const Settings& settings){
    Output out;
    out.values.assign(size_t(count) * 128, 0);
    out.angles.assign(count, 0.0f);
    out.valid.assign(count, 0);
    if(count == 0 || width < 3 || height < 3) return out;
    if(stride == 0) stride = width;

    //---------------------------------------------------------------------------------------
    // Pojasevi, zakrpe i jezgre - na procesoru, istim racunom kao describeSiftScaled i blurred()
    //---------------------------------------------------------------------------------------
    const uint32_t bandsPerOctave = std::max(1u, settings.scaleBands);
    std::map<int, std::vector<uint32_t>> byBand;     //poredano po pojasu, kao i na procesoru
    for(uint32_t i = 0; i < count; ++i){
        if(!(scales[i] > 0.0f)) continue;
        byBand[int(std::lround(std::log2(double(scales[i])) * double(bandsPerOctave)))].push_back(i);
    }

    std::vector<float> kernelValues;
    std::vector<Key> keyList;
    std::vector<Band> bands;
    for(const auto& [band, members] : byBand){
        Band one;
        const float scale = float(std::pow(2.0, double(band) / double(bandsPerOctave)));
        const float already = 0.5f;
        const float step = scale > already ? std::sqrt(scale * scale - already * already) : 0.0f;
        if(step > 0.0f){
            one.reach = std::max(1, int(std::ceil(3.0f * step)));
            one.kernelOffset = uint32_t(kernelValues.size());
            std::vector<float> kernel(size_t(2 * one.reach + 1));
            float total = 0.0f;
            for(int d = -one.reach; d <= one.reach; ++d){
                kernel[size_t(d + one.reach)] = std::exp(-float(d * d) / (2.0f * step * step));
                total += kernel[size_t(d + one.reach)];
            }
            for(float& k : kernel) k /= total;
            kernelValues.insert(kernelValues.end(), kernel.begin(), kernel.end());
        }
        one.firstKey = uint32_t(keyList.size());
        for(uint32_t which : members){
            const float x = points[size_t(which) * 2], y = points[size_t(which) * 2 + 1];
            const uint32_t patch = std::max(4u, uint32_t(std::lround(double(settings.patchPerScale) * double(scales[which]))));
            const int reach = int(patch) + 1;
            if(x < float(reach) || y < float(reach) ||
               x >= float(width) - float(reach) || y >= float(height) - float(reach)) continue;
            keyList.push_back(Key{int32_t(std::lround(x)), int32_t(std::lround(y)), patch, which});
        }
        one.keyCount = uint32_t(keyList.size()) - one.firstKey;
        if(one.keyCount > 0) bands.push_back(one);
    }
    if(keyList.empty()) return out;
    if(kernelValues.empty()) kernelValues.push_back(0.0f);

    //---------------------------------------------------------------------------------------
    // Buffere na kartici
    //---------------------------------------------------------------------------------------
    const VulkanDevice& device = loom.device;
    const auto storage = vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferDst | vk::BufferUsageFlagBits::eTransferSrc;
    bool rebind = !state->rowsMaterial;
    auto ensure = [&](std::optional<VulkanBuffer>& buffer, vk::DeviceSize bytes){
        bytes = std::max<vk::DeviceSize>(bytes, 16);
        if(buffer && buffer->getSize() >= bytes) return;
        buffer.reset();
        buffer.emplace(device, bytes + bytes / 4, storage, MemoryUsage::GPU_ONLY);
        rebind = true;
    };
    const size_t pixelCount = size_t(width) * height;
    ensure(state->image, (pixelCount + 3) / 4 * 4);
    ensure(state->kernels, kernelValues.size() * sizeof(float));
    ensure(state->across, pixelCount * sizeof(float));
    ensure(state->soft, pixelCount * sizeof(uint32_t));
    ensure(state->magnitude, pixelCount * sizeof(float));
    ensure(state->direction, pixelCount * sizeof(float));
    ensure(state->keys, keyList.size() * sizeof(Key));
    ensure(state->descriptors, size_t(count) * 128);
    ensure(state->angles, size_t(count) * sizeof(float));
    ensure(state->valid, size_t(count) * sizeof(uint32_t));

    if(rebind){
        //Materijali se grade jednom; nakon rasta buffera samo se ponovno vezu (prethodni rad je
        //zavrsen, waitIdle na kraju svakog poziva)
        if(!state->rowsMaterial){
            state->rowsMaterial.emplace(device, loom.getDescriptorPool(), state->rows);
            state->columnsMaterial.emplace(device, loom.getDescriptorPool(), state->columns);
            state->gradientsMaterial.emplace(device, loom.getDescriptorPool(), state->gradients);
            state->describeMaterial.emplace(device, loom.getDescriptorPool(), state->describe);
        }
        state->rowsMaterial->setStorageBuffer(0, *state->image);
        state->rowsMaterial->setStorageBuffer(1, *state->kernels);
        state->rowsMaterial->setStorageBuffer(2, *state->across);
        state->columnsMaterial->setStorageBuffer(0, *state->kernels);
        state->columnsMaterial->setStorageBuffer(1, *state->across);
        state->columnsMaterial->setStorageBuffer(2, *state->soft);
        state->gradientsMaterial->setStorageBuffer(0, *state->image);
        state->gradientsMaterial->setStorageBuffer(1, *state->soft);
        state->gradientsMaterial->setStorageBuffer(2, *state->magnitude);
        state->gradientsMaterial->setStorageBuffer(3, *state->direction);
        state->describeMaterial->setStorageBuffer(0, *state->magnitude);
        state->describeMaterial->setStorageBuffer(1, *state->direction);
        state->describeMaterial->setStorageBuffer(2, *state->keys);
        state->describeMaterial->setStorageBuffer(3, *state->descriptors);
        state->describeMaterial->setStorageBuffer(4, *state->angles);
        state->describeMaterial->setStorageBuffer(5, *state->valid);
    }

    auto upload = [&](const VulkanBuffer& target, const void* data, vk::DeviceSize bytes){
        VulkanBuffer staging(device, bytes, vk::BufferUsageFlagBits::eTransferSrc, MemoryUsage::CPU_TO_GPU);
        staging.upload(data, bytes);
        loom.command.copyBuffer(staging.getBuffer(), target.getBuffer(), bytes);
    };
    {
        //Slika bez razmaka izmedju redova, cetiri piksela po uint-u
        std::vector<uint8_t> packed((pixelCount + 3) / 4 * 4, 0);
        for(uint32_t y = 0; y < height; ++y) std::memcpy(packed.data() + size_t(y) * width, pixels + size_t(y) * stride, width);
        upload(*state->image, packed.data(), packed.size());
    }
    upload(*state->kernels, kernelValues.data(), kernelValues.size() * sizeof(float));
    upload(*state->keys, keyList.data(), keyList.size() * sizeof(Key));

    //---------------------------------------------------------------------------------------
    // Svi pojasevi u jednom predavanju: zamucenje, gradijenti, potpisi
    //---------------------------------------------------------------------------------------
    const uint32_t groupsX = (width + 15) / 16, groupsY = (height + 15) / 16;
    const auto submitted = std::chrono::steady_clock::now();
    loom.renderer.beginFrame();
    for(const Band& band : bands){
        if(band.reach > 0){
            BlurParams blur{width, height, band.reach, band.kernelOffset, {}};
            loom.renderer.dispatch(*state->rowsMaterial, groupsX, groupsY, 1, &blur, sizeof(blur));
            loom.renderer.dispatch(*state->columnsMaterial, groupsX, groupsY, 1, &blur, sizeof(blur));
        }
        GradientParams gradient{width, height, band.reach > 0 ? 0u : 1u, 0u, {}};
        loom.renderer.dispatch(*state->gradientsMaterial, groupsX, groupsY, 1, &gradient, sizeof(gradient));
        DescribeParams describe{width, height, band.firstKey, band.keyCount, settings.orient ? 1u : 0u, settings.clamp, 0u, 0u};
        loom.renderer.dispatch(*state->describeMaterial, band.keyCount, 1, 1, &describe, sizeof(describe));
    }
    loom.renderer.endFrame();
    loom.waitIdle();
    if(std::getenv("SIFT_DESCRIBER_TIMING")) std::printf("  sift na kartici: %zu pojaseva, %zu znacajki, racun %.3f s\n", bands.size(), keyList.size(),
        std::chrono::duration<double>(std::chrono::steady_clock::now() - submitted).count());

    auto download = [&](const VulkanBuffer& source, void* into, vk::DeviceSize bytes){
        VulkanBuffer staging(device, bytes, vk::BufferUsageFlagBits::eTransferDst, MemoryUsage::GPU_TO_CPU);
        loom.command.copyBuffer(source.getBuffer(), staging.getBuffer(), bytes);
        staging.download(into, bytes);
    };
    std::vector<uint8_t> values(size_t(count) * 128);
    std::vector<float> angles(count);
    std::vector<uint32_t> valid(count);
    download(*state->descriptors, values.data(), values.size());
    download(*state->angles, angles.data(), angles.size() * sizeof(float));
    download(*state->valid, valid.data(), valid.size() * sizeof(uint32_t));

    //Samo mjesta koja je neki pojas stvarno racunao; ostala su iz proslog kadra
    for(const Key& key : keyList){
        if(!valid[key.slot]) continue;
        std::memcpy(out.values.data() + size_t(key.slot) * 128, values.data() + size_t(key.slot) * 128, 128);
        out.angles[key.slot] = angles[key.slot];
        out.valid[key.slot] = 1;
    }
    return out;
}
