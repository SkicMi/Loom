#pragma once
#include <vulkan/vulkan_raii.hpp>
#include <array>
#include <cstddef>
#include <vector>
#include <glm/glm.hpp>

struct Vertex{
    glm::vec3 position;
    glm::vec3 color;

    static vk::VertexInputBindingDescription getBindingDescription(){
        vk::VertexInputBindingDescription binding;
        binding.binding = 0;
        binding.stride = sizeof(Vertex); //size of bytes of whole struct as a step between each Vertex
        binding.inputRate = vk::VertexInputRate::eVertex;
        return binding;


    }

    static std::vector<vk::VertexInputAttributeDescription> getAttributeDescriptions(){
        std::vector<vk::VertexInputAttributeDescription> attributes(2);

        attributes[0].location = 0;
        attributes[0].binding = 0;
        attributes[0].format = vk::Format::eR32G32B32Sfloat;
        attributes[0].offset = offsetof(Vertex,position);

        attributes[1].location = 1;
        attributes[1].binding = 0;
        attributes[1].format = vk::Format::eR32G32B32Sfloat;
        attributes[1].offset = offsetof(Vertex,color);

        return attributes;
    }

};