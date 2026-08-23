#include "VulkanCommand.h"
#include "Barriers.h"

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

    void VulkanCommand::transitionImageLayout(vk::Image image, vk::ImageLayout oldLayout, vk::ImageLayout newLayout, vk::ImageAspectFlags aspect) const{
        const vk::ImageMemoryBarrier2 barrier = imageBarrier(image, oldLayout, newLayout, aspect);

        oneTimeSubmit([&](const vk::raii::CommandBuffer& commandBuffer){
            recordBarrier(commandBuffer, barrier);
        });
    }

    void VulkanCommand::transitionImageLayout(const vk::raii::Image& image, vk::ImageLayout oldLayout, vk::ImageLayout newLayout, vk::ImageAspectFlags aspect) const{
        transitionImageLayout(*image, oldLayout, newLayout, aspect);
    }

    void VulkanCommand::transitionImageLayout(const VulkanImage& image, vk::ImageLayout newLayout) const{
        vk::ImageAspectFlags aspect = vk::ImageAspectFlagBits::eColor;
        if(image.getUsage() & vk::ImageUsageFlagBits::eDepthStencilAttachment){
            aspect = vk::ImageAspectFlagBits::eDepth;
        }

        transitionImageLayout(image.getImage(), image.getCurrentLayout(), newLayout, aspect);
        image.setCurrentLayout(newLayout);
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

    void VulkanCommand::copyImageToBuffer(vk::Image src, const vk::raii::Buffer& dst, vk::Extent2D extent, vk::ImageAspectFlags aspect) const{
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

            commandBuffer.copyImageToBuffer(src, vk::ImageLayout::eTransferSrcOptimal, *dst, region);
        });
    }

    void VulkanCommand::copyImageToBuffer(const vk::raii::Image& src, const vk::raii::Buffer& dst, vk::Extent2D extent, vk::ImageAspectFlags aspect) const{
        copyImageToBuffer(*src, dst, extent, aspect);
    }


