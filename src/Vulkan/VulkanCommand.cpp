#include "VulkanCommand.h"

VulkanCommand::VulkanCommand(const VulkanDevice& device, const CommandConfig& config) : device(device), config(config){
    createCommandPool();
    createTransferPool();
    allocateCommandBuffers();
}

void VulkanCommand::createCommandPool(){
    vk::CommandPoolCreateInfo poolInfo;
    poolInfo.flags = vk::CommandPoolCreateFlagBits::eResetCommandBuffer;
    poolInfo.queueFamilyIndex = device.getQueueIndices().graphicsFamilies.value();

    commandPool = vk::raii::CommandPool(device.getDevice(),poolInfo);
}

void VulkanCommand::createTransferPool(){
    vk::CommandPoolCreateInfo poolInfo;
    poolInfo.flags = vk::CommandPoolCreateFlagBits::eTransient;
    poolInfo.queueFamilyIndex = device.getQueueIndices().graphicsFamilies.value();

    transferPool = vk::raii::CommandPool(device.getDevice(), poolInfo);


}

void VulkanCommand::allocateCommandBuffers(){
    vk::CommandBufferAllocateInfo allocInfo;
    allocInfo.commandPool = *commandPool;
    allocInfo.level = vk::CommandBufferLevel::ePrimary;
    allocInfo.commandBufferCount = config.framesInFlight;

    commandBuffers = std::vector<vk::raii::CommandBuffer>(device.getDevice().allocateCommandBuffers(allocInfo));
}

void VulkanCommand::copyBuffer(const vk::raii::Buffer& src, const vk::raii::Buffer& dst, vk::DeviceSize size) const {
    vk::CommandBufferAllocateInfo allocInfo;
    allocInfo.commandPool = *transferPool;
    allocInfo.level = vk::CommandBufferLevel::ePrimary;
    allocInfo.commandBufferCount = 1;

    vk::raii::CommandBuffers buffers(device.getDevice(),allocInfo);
    const vk::raii::CommandBuffer& commandBuffer = buffers[0];

    vk::CommandBufferBeginInfo beginInfo;
    beginInfo.flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit;
    commandBuffer.begin(beginInfo);

    vk::BufferCopy region;
    region.srcOffset = 0;
    region.dstOffset = 0;
    region.size = size;
    commandBuffer.copyBuffer(*src, *dst , region);

    commandBuffer.end();

    vk::CommandBufferSubmitInfo commandBufferInfo(*commandBuffer);
    vk::SubmitInfo2 submitInfo;
    submitInfo.setCommandBufferInfos(commandBufferInfo);

    device.getGraphicsQueue().submit2(submitInfo);
    device.getGraphicsQueue().waitIdle();

}