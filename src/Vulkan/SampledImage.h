#pragma once
#include <vulkan/vulkan_raii.hpp>
#include <cstdint>

class VulkanImage;

struct SampledImage{
    vk::ImageView view = nullptr;
    vk::Sampler sampler = nullptr;

    //Optional. When set, whoever holds this can notice the image was recreated and
    //fetch the new view instead of using a handle that died in resize
    const VulkanImage* source = nullptr;
    uint64_t generation = 0;

    bool isValid() const {return view && sampler;}
};
