#pragma once
#include <vulkan/vulkan_raii.hpp>
#include <array>
#include <cstddef>
#include <vector>
#include <glm/glm.hpp>

struct Vertex{
    glm::vec3 position;
    glm::vec3 color;
    glm::vec2 texCoord;
    glm::vec3 normal;

    static vk::VertexInputBindingDescription getBindingDescription(){
        vk::VertexInputBindingDescription binding;
        binding.binding = 0;
        binding.stride = sizeof(Vertex); //size of bytes of whole struct as a step between each Vertex
        binding.inputRate = vk::VertexInputRate::eVertex;
        return binding;


    }

    static std::vector<vk::VertexInputAttributeDescription> getAttributeDescriptions(){
        std::vector<vk::VertexInputAttributeDescription> attributes(4);

        attributes[0].location = 0;
        attributes[0].binding = 0;
        attributes[0].format = vk::Format::eR32G32B32Sfloat;
        attributes[0].offset = offsetof(Vertex,position);

        attributes[1].location = 1;
        attributes[1].binding = 0;
        attributes[1].format = vk::Format::eR32G32B32Sfloat;
        attributes[1].offset = offsetof(Vertex,color);

        attributes[2].location = 2;
        attributes[2].binding = 0;
        attributes[2].format = vk::Format::eR32G32Sfloat;
        attributes[2].offset = offsetof(Vertex,texCoord);

        attributes[3].location = 3;
        attributes[3].binding = 0;
        attributes[3].format = vk::Format::eR32G32B32Sfloat;
        attributes[3].offset = offsetof(Vertex,normal);




        return attributes;
    }

    //Position alone, for a pass that computes nothing else - a shadow map reads where a
    //vertex is and has no use for its colour, its texture coordinate or its normal. The
    //binding stride stays sizeof(Vertex), so the very same Mesh feeds this and the full
    //layout above without a second copy of the geometry.
    //
    //This is not only about fetching less: a pipeline that declares an attribute its shader
    //never reads is a validation warning, and Slang strips unread inputs out of the SPIR-V
    static std::vector<vk::VertexInputAttributeDescription> getPositionAttribute(){
        std::vector<vk::VertexInputAttributeDescription> attributes(1);

        attributes[0].location = 0;
        attributes[0].binding = 0;
        attributes[0].format = vk::Format::eR32G32B32Sfloat;
        attributes[0].offset = offsetof(Vertex,position);

        return attributes;
    }

};