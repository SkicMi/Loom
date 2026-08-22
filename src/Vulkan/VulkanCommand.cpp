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
    oneTimeSubmit([&](const vk::raii::CommandBuffer& commandBuffer){
    vk::BufferCopy region;
    region.srcOffset = 0;
    region.dstOffset = 0;
    region.size = size;
    commandBuffer.copyBuffer(*src, *dst , region);
    });
}

    void VulkanCommand::oneTimeSubmit(const std::function<void(const vk::raii::CommandBuffer&)>& record) const{
        vk::CommandBufferAllocateInfo allocInfo;
        allocInfo.commandPool = *transferPool;
        allocInfo.level = vk::CommandBufferLevel::ePrimary;
        allocInfo.commandBufferCount = 1;


        vk::raii::CommandBuffers buffers(device.getDevice(),allocInfo);
        const vk::raii::CommandBuffer& commandBuffer = buffers[0];

        vk::CommandBufferBeginInfo beginInfo;
        beginInfo.flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit;
        commandBuffer.begin(beginInfo);

        record(commandBuffer);

        commandBuffer.end();

        vk::CommandBufferSubmitInfo commandBufferInfo(*commandBuffer);
        vk::SubmitInfo2 submitInfo;
        submitInfo.setCommandBufferInfos(commandBufferInfo);

        device.getGraphicsQueue().submit2(submitInfo);
        device.getGraphicsQueue().waitIdle();
    }

    void VulkanCommand::transitionImageLayout(const vk::raii::Image& image, vk::ImageLayout oldLayout, vk::ImageLayout newLayout, vk::ImageAspectFlags aspect) const{
        vk::ImageMemoryBarrier2 barrier;
        barrier.oldLayout = oldLayout;
        barrier.newLayout = newLayout;
        barrier.image = *image;
        barrier.subresourceRange = {aspect,0 , 1, 0 , 1};

        if(oldLayout == vk::ImageLayout::eUndefined && newLayout == vk::ImageLayout::eTransferDstOptimal){
            barrier.srcStageMask = vk::PipelineStageFlagBits2::eTopOfPipe;
            barrier.srcAccessMask = {};
            barrier.dstStageMask = vk::PipelineStageFlagBits2::eCopy;
            barrier.dstAccessMask = vk::AccessFlagBits2::eTransferWrite;
        }
        else if(oldLayout == vk::ImageLayout::eTransferDstOptimal && newLayout == vk::ImageLayout::eShaderReadOnlyOptimal){
            barrier.srcStageMask = vk::PipelineStageFlagBits2::eCopy;
            barrier.srcAccessMask = vk::AccessFlagBits2::eTransferWrite;
            barrier.dstStageMask = vk::PipelineStageFlagBits2::eFragmentShader;
            barrier.dstAccessMask = vk::AccessFlagBits2::eShaderRead;
        }
        else{
            throw std::runtime_error("transitionImageLayout: unssuported layout combionation");
        }
        oneTimeSubmit([&](const vk::raii::CommandBuffer& commandBuffer){
            vk::DependencyInfo dep;
            dep.imageMemoryBarrierCount = 1;
            dep.pImageMemoryBarriers = &barrier;
            commandBuffer.pipelineBarrier2(dep);

        });
    }

    void VulkanCommand::copyBufferToImage(const vk::raii::Buffer& src, const vk::raii::Image& dst, vk::Extent2D extent, vk::ImageAspectFlags aspect) const{
        oneTimeSubmit([&](const vk::raii::CommandBuffer& commandBuffer){
            vk::BufferImageCopy region;
            region.bufferOffset = 0;
            region.bufferRowLength = 0;
            region.bufferImageHeight = 0;
            region.imageSubresource.aspectMask = aspect;
            region.imageSubresource.mipLevel = 0;
            region.imageSubresource.baseArrayLayer = 0;
            region.imageSubresource.layerCount = 1;
            region.imageOffset = vk::Offset3D{0,0,0};
            region.imageExtent = vk::Extent3D{extent.width, extent.height, 1};

            commandBuffer.copyBufferToImage(*src,*dst,vk::ImageLayout::eTransferDstOptimal,region);
        });
    }

    void VulkanCommand::copyImageToBuffer(const vk::raii::Image& src, const vk::raii::Buffer& dst, vk::Extent2D extent, vk::ImageAspectFlags aspect) const{
        oneTimeSubmit([&](const vk::raii::CommandBuffer& commandBuffer){
            vk::BufferImageCopy region;
            region.bufferOffset = 0;
            region.bufferRowLength = 0;
            region.bufferImageHeight = 0;
            region.imageSubresource.aspectMask = aspect;
            region.imageSubresource.mipLevel = 0;
            region.imageSubresource.baseArrayLayer = 0;
            region.imageSubresource.layerCount = 1;
            region.imageOffset = vk::Offset3D{0,0,0};
            region.imageExtent = vk::Extent3D{extent.width,extent.height,1};

            commandBuffer.copyImageToBuffer(*src, vk::ImageLayout::eTransferSrcOptimal, *dst, region);
        });
    }


