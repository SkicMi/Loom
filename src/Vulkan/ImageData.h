#pragma once
#include <vector>
#include <cstdint>
#include <stdexcept>
#include <vulkan/vulkan_raii.hpp>

struct ImageData{
    std::vector<uint8_t> pixels;
    vk::Extent2D extent = {0,0};
    vk::Format format = vk::Format::eUndefined;
    size_t pixelCount() const{return size_t(extent.width) * extent.height;}
};

inline uint32_t bytesPerPixel(vk::Format format){
    switch(format){
        case vk::Format::eR8G8B8A8Srgb:
        case vk::Format::eR8G8B8A8Unorm:
        case vk::Format::eB8G8R8A8Srgb:
        case vk::Format::eB8G8R8A8Unorm:
            return 4;
        default:
        throw std::runtime_error("bytesPerPixel: unsuported format");
    }
}