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
    //eTransient jer oneTimeSubmit alocira i baca bafer po pozivu; eResetCommandBuffer jer
    //streaming ga drzi i prepisuje svaki frame. Bez drugog se bafer ne smije resetirati
    //pojedinacno, nego samo cijeli pool - a to bi obrisalo i one koji su jos u letu
    poolInfo.flags = vk::CommandPoolCreateFlagBits::eTransient
                   | vk::CommandPoolCreateFlagBits::eResetCommandBuffer;
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

    void VulkanCommand::transitionImageLayout(vk::Image image, vk::ImageLayout oldLayout, vk::ImageLayout newLayout, vk::ImageAspectFlags aspect, uint32_t layerCount, uint32_t levelCount) const{
        const vk::ImageMemoryBarrier2 barrier = imageBarrier(image, oldLayout, newLayout, aspect, layerCount, 0, levelCount);

        oneTimeSubmit([&](const vk::raii::CommandBuffer& commandBuffer){
            recordBarrier(commandBuffer, barrier);
        });
    }

    void VulkanCommand::transitionImageLayout(const vk::raii::Image& image, vk::ImageLayout oldLayout, vk::ImageLayout newLayout, vk::ImageAspectFlags aspect, uint32_t layerCount, uint32_t levelCount) const{
        transitionImageLayout(*image, oldLayout, newLayout, aspect, layerCount, levelCount);
    }

    void VulkanCommand::transitionImageLayout(const VulkanImage& image, vk::ImageLayout newLayout) const{
        vk::ImageAspectFlags aspect = vk::ImageAspectFlagBits::eColor;
        if(image.getUsage() & vk::ImageUsageFlagBits::eDepthStencilAttachment){
            aspect = vk::ImageAspectFlagBits::eDepth;
        }

        //All six faces of a cube move together, and so does every mip level. Transitioning
        //only layer 0 or only level 0 would leave the rest in a layout the next user does
        //not expect - and a mip chain is exactly where that goes unnoticed, because the top
        //level is the one anything looks at first
        transitionImageLayout(image.getImage(), image.getCurrentLayout(), newLayout, aspect,
            image.getLayerCount(), image.getMipLevels());
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

    void VulkanCommand::copyImageToBuffer(vk::Image src, const vk::raii::Buffer& dst, vk::Extent2D extent, vk::ImageAspectFlags aspect, uint32_t layer) const{
        oneTimeSubmit([&](const vk::raii::CommandBuffer& commandBuffer){
            vk::BufferImageCopy region;
            region.bufferOffset = 0;
            region.bufferRowLength = 0;
            region.bufferImageHeight = 0;
            region.imageSubresource.aspectMask = aspect;
            region.imageSubresource.mipLevel = 0;
            region.imageSubresource.baseArrayLayer = layer; //one face of a cube, or the only layer there is
            region.imageSubresource.layerCount = 1;
            region.imageOffset = vk::Offset3D{0,0,0};
            region.imageExtent = vk::Extent3D{extent.width,extent.height,1};

            commandBuffer.copyImageToBuffer(src, vk::ImageLayout::eTransferSrcOptimal, *dst, region);
        });
    }

    void VulkanCommand::copyImageToBuffer(const vk::raii::Image& src, const vk::raii::Buffer& dst, vk::Extent2D extent, vk::ImageAspectFlags aspect, uint32_t layer) const{
        copyImageToBuffer(*src, dst, extent, aspect, layer);
    }

    void VulkanCommand::generateMipmaps(const VulkanImage& image) const{
        const uint32_t levels = image.getMipLevels();
        if(levels <= 1){
            return; //nista za smanjivati
        }

        //The chain is built by blitting, and a blit filters. A format the driver cannot
        //filter linearly would silently fall back to nearest and the whole point would be
        //lost, so it is asked rather than assumed
        const vk::FormatProperties properties = device.getPhysicalDevice().getFormatProperties(image.getFormat());
        if(!(properties.optimalTilingFeatures & vk::FormatFeatureFlagBits::eSampledImageFilterLinear)){
            throw std::runtime_error("generateMipmaps: this format cannot be filtered linearly, so a mip chain built from it would be no better than the top level");
        }

        const vk::Extent2D extent = image.getExtent();
        const uint32_t layers = image.getLayerCount();

        oneTimeSubmit([&](const vk::raii::CommandBuffer& commandBuffer){
            int32_t width = static_cast<int32_t>(extent.width);
            int32_t height = static_cast<int32_t>(extent.height);

            for(uint32_t level = 1; level < levels; ++level){
                //The level about to be read has to stop being a destination first
                recordBarrier(commandBuffer, imageBarrier(*image.getImage(),
                    vk::ImageLayout::eTransferDstOptimal, vk::ImageLayout::eTransferSrcOptimal,
                    vk::ImageAspectFlagBits::eColor, layers, level - 1, 1));

                const int32_t nextWidth = width > 1 ? width / 2 : 1;
                const int32_t nextHeight = height > 1 ? height / 2 : 1;

                vk::ImageBlit blit;
                blit.srcOffsets[0] = vk::Offset3D{0,0,0};
                blit.srcOffsets[1] = vk::Offset3D{width, height, 1};
                blit.srcSubresource = vk::ImageSubresourceLayers{vk::ImageAspectFlagBits::eColor, level - 1, 0, layers};
                blit.dstOffsets[0] = vk::Offset3D{0,0,0};
                blit.dstOffsets[1] = vk::Offset3D{nextWidth, nextHeight, 1};
                blit.dstSubresource = vk::ImageSubresourceLayers{vk::ImageAspectFlagBits::eColor, level, 0, layers};

                commandBuffer.blitImage(*image.getImage(), vk::ImageLayout::eTransferSrcOptimal,
                    *image.getImage(), vk::ImageLayout::eTransferDstOptimal,
                    blit, vk::Filter::eLinear);

                //Read from, and finished with: this level goes where a shader can see it
                recordBarrier(commandBuffer, imageBarrier(*image.getImage(),
                    vk::ImageLayout::eTransferSrcOptimal, vk::ImageLayout::eShaderReadOnlyOptimal,
                    vk::ImageAspectFlagBits::eColor, layers, level - 1, 1));

                width = nextWidth;
                height = nextHeight;
            }

            //The last level was only ever written to, so it never became a source
            recordBarrier(commandBuffer, imageBarrier(*image.getImage(),
                vk::ImageLayout::eTransferDstOptimal, vk::ImageLayout::eShaderReadOnlyOptimal,
                vk::ImageAspectFlagBits::eColor, layers, levels - 1, 1));
        });

        image.setCurrentLayout(vk::ImageLayout::eShaderReadOnlyOptimal);
    }

    vk::raii::CommandBuffer VulkanCommand::createTransferBuffer() const{
        vk::CommandBufferAllocateInfo allocInfo;
        allocInfo.commandPool = *transferPool;
        allocInfo.level = vk::CommandBufferLevel::ePrimary;
        allocInfo.commandBufferCount = 1;

        vk::raii::CommandBuffers buffers(device.getDevice(), allocInfo);
        return std::move(buffers[0]);
    }

    void VulkanCommand::submitWithoutWaiting(const vk::raii::CommandBuffer& buffer,
                                             const vk::raii::Fence& fence,
                                             const std::function<void(const vk::raii::CommandBuffer&)>& record) const{
        buffer.reset();

        vk::CommandBufferBeginInfo beginInfo;
        beginInfo.flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit;
        buffer.begin(beginInfo);

        record(buffer);

        buffer.end();

        vk::CommandBufferSubmitInfo commandBufferInfo(*buffer);
        vk::SubmitInfo2 submitInfo;
        submitInfo.setCommandBufferInfos(commandBufferInfo);

        device.getGraphicsQueue().submit2(submitInfo, *fence);
    }
