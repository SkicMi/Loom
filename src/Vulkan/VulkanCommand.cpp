#include "VulkanCommand.h"

VulkanCommand::VulkanCommand(const VulkanDevice& device, const CommandConfig& config) : device(device), config(config){
    createCommandPool();
    allocateCommandBuffers();
}

void VulkanCommand::createCommandPool(){
    vk::CommandPoolCreateInfo poolInfo;
    poolInfo.flags = vk::CommandPoolCreateFlagBits::eResetCommandBuffer;
    poolInfo.queueFamilyIndex = device.getQueueIndices().graphicsFamilies.value();

    commandPool = vk::raii::CommandPool(device.getDevice(),poolInfo);
}

void VulkanCommand::allocateCommandBuffers(){
    vk::CommandBufferAllocateInfo allocInfo;
    allocInfo.commandPool = *commandPool;
    allocInfo.level = vk::CommandBufferLevel::ePrimary;
    allocInfo.commandBufferCount = config.framesInFlight;

    commandBuffers = std::vector<vk::raii::CommandBuffer>(device.getDevice().allocateCommandBuffers(allocInfo));
}