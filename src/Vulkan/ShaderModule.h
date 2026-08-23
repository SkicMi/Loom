#pragma once
#include "VulkanDevice.h"
#include <fstream>
#include <string>
#include <vector>

inline std::vector<char> readShaderFile(const std::string& path){
    //ate - at the end - opens file with position on end, binary stops that bytes from being interpreted as text
    std::ifstream file(path, std::ios::ate | std::ios::binary);

    if(!file.is_open()){
        throw std::runtime_error("Failed to open file: " + path);
    }

    size_t fileSize = (size_t)file.tellg();
    std::vector<char> buffer(fileSize);

    file.seekg(0);
    file.read(buffer.data(),fileSize);

    return buffer;
}

inline vk::raii::ShaderModule loadShaderModule(const VulkanDevice& device, const std::string& path){
    std::vector<char> code = readShaderFile(path);

    vk::ShaderModuleCreateInfo createInfo;
    createInfo.codeSize = code.size(); //code size in bytes
    createInfo.pCode = reinterpret_cast<const uint32_t*>(code.data()); //Vulkan expects array of uint32_t, so reinterpret cast is needed to convert from char to uint32_t

    return vk::raii::ShaderModule(device.getDevice(),createInfo);
}
