#pragma once
#include <vulkan/vulkan_raii.hpp>

struct SampledImage{
    vk::ImageView view = nullptr;
    vk::Sampler sampler = nullptr;

    bool isValid() const {return view && sampler;}
};