#pragma once
#include <vector>
#include <cstdint>
#include <stdexcept>
#include <cstring>
#include <vulkan/vulkan_raii.hpp>

struct ImageData{
    std::vector<uint8_t> pixels;
    vk::Extent2D extent = {0,0};
    vk::Format format = vk::Format::eUndefined;
    size_t pixelCount() const{return size_t(extent.width) * extent.height;}
};

//Whether the bytes are in B,G,R,A order. The swapchain almost always negotiates eB8G8R8A8Srgb, so an
//image saved as if it were RGBA has red and blue swapped - a bug the window does NOT show, because it
//reads the same format it was written in. So the order is asked of the format, not remembered by heart
inline bool isBgraFormat(vk::Format format){
    switch(format){
        case vk::Format::eB8G8R8A8Unorm:
        case vk::Format::eB8G8R8A8Srgb:
        case vk::Format::eB8G8R8A8Snorm:
        case vk::Format::eB8G8R8A8Uint:
        case vk::Format::eB8G8R8A8Sint:
            return true;
        default:
            return false;
    }
}

inline bool isDepthFormat(vk::Format format){
    switch(format){
        case vk::Format::eD16Unorm:
        case vk::Format::eD32Sfloat:
        case vk::Format::eD24UnormS8Uint:
        case vk::Format::eD32SfloatS8Uint:
            return true;
        default:
            return false;
    }
}

inline uint32_t bytesPerPixel(vk::Format format){
    switch(format){
        case vk::Format::eR8G8B8A8Srgb:
        case vk::Format::eR8G8B8A8Unorm:
        case vk::Format::eB8G8R8A8Srgb:
        case vk::Format::eB8G8R8A8Unorm:
            return 4;

        //Four full floats. An image of positions, not colours
        case vk::Format::eR32G32B32A32Sfloat:
            return 16;

        //One channel. A depth map from a file is not a colour and has no business in four
        case vk::Format::eR32Sfloat:
            return 4;
        case vk::Format::eR16Unorm:
            return 2;
        case vk::Format::eR8Unorm:
            return 1;

        //Depth, as vkCmdCopyImageToBuffer hands it over. A copy names one aspect, and the
        //depth aspect of a combined format still arrives as its depth part alone - the
        //stencil byte is a separate copy nobody here asks for
        case vk::Format::eD16Unorm:
            return 2;
        case vk::Format::eD32Sfloat:
        case vk::Format::eD24UnormS8Uint:
        case vk::Format::eD32SfloatS8Uint:
            return 4;

        default:
        throw std::runtime_error("bytesPerPixel: unsuported format");
    }
}

//Depth as the number the shader wrote, not as bytes. Only for a 32 bit float depth buffer:
//a unorm depth is a different decode and pretending otherwise would read noise
inline float depthAt(const ImageData& image, uint32_t x, uint32_t y){
    if(image.format != vk::Format::eD32Sfloat && image.format != vk::Format::eD32SfloatS8Uint){
        throw std::runtime_error("depthAt: only a 32 bit float depth format can be read this way");
    }
    if(x >= image.extent.width || y >= image.extent.height){
        throw std::runtime_error("depthAt: outside the image");
    }

    const size_t offset = (size_t(y) * image.extent.width + x) * 4;
    float value = 0.0f;
    std::memcpy(&value, image.pixels.data() + offset, sizeof(float));
    return value;
}