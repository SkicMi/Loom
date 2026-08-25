#pragma once
#include <vulkan/vulkan_raii.hpp>

//Which pipeline stages and accesses belong to a layout. One table instead of a switch in
//every place that moves an image, so a new layout is taught to the library once
struct LayoutAccess{
    vk::PipelineStageFlags2 stage = vk::PipelineStageFlagBits2::eAllCommands;
    vk::AccessFlags2 access = {};
};

inline LayoutAccess accessForLayout(vk::ImageLayout layout){
    switch(layout){
        case vk::ImageLayout::eUndefined:
            return {vk::PipelineStageFlagBits2::eTopOfPipe, {}};

        case vk::ImageLayout::eColorAttachmentOptimal:
            return {vk::PipelineStageFlagBits2::eColorAttachmentOutput,
                    vk::AccessFlagBits2::eColorAttachmentWrite | vk::AccessFlagBits2::eColorAttachmentRead};

        case vk::ImageLayout::eDepthAttachmentOptimal:
        case vk::ImageLayout::eDepthStencilAttachmentOptimal:
            return {vk::PipelineStageFlagBits2::eEarlyFragmentTests | vk::PipelineStageFlagBits2::eLateFragmentTests,
                    vk::AccessFlagBits2::eDepthStencilAttachmentWrite | vk::AccessFlagBits2::eDepthStencilAttachmentRead};

        case vk::ImageLayout::eShaderReadOnlyOptimal:
            return {vk::PipelineStageFlagBits2::eFragmentShader | vk::PipelineStageFlagBits2::eComputeShader,
                    vk::AccessFlagBits2::eShaderRead};

        case vk::ImageLayout::eGeneral:
            return {vk::PipelineStageFlagBits2::eComputeShader | vk::PipelineStageFlagBits2::eFragmentShader,
                    vk::AccessFlagBits2::eShaderRead | vk::AccessFlagBits2::eShaderWrite};

        case vk::ImageLayout::eTransferSrcOptimal:
            return {vk::PipelineStageFlagBits2::eCopy, vk::AccessFlagBits2::eTransferRead};

        case vk::ImageLayout::eTransferDstOptimal:
            return {vk::PipelineStageFlagBits2::eCopy, vk::AccessFlagBits2::eTransferWrite};

        case vk::ImageLayout::ePresentSrcKHR:
            //presentation is not a pipeline stage, the semaphore carries the dependency
            return {vk::PipelineStageFlagBits2::eBottomOfPipe, {}};

        default:
            return {vk::PipelineStageFlagBits2::eAllCommands,
                    vk::AccessFlagBits2::eMemoryRead | vk::AccessFlagBits2::eMemoryWrite};
    }
}

inline vk::ImageMemoryBarrier2 imageBarrier(vk::Image image,
                                            vk::ImageLayout oldLayout,
                                            vk::ImageLayout newLayout,
                                            vk::ImageAspectFlags aspect = vk::ImageAspectFlagBits::eColor,
                                            uint32_t layerCount = 1,
                                            uint32_t baseMipLevel = 0,
                                            uint32_t levelCount = 1){
    const LayoutAccess source = accessForLayout(oldLayout);
    const LayoutAccess destination = accessForLayout(newLayout);

    vk::ImageMemoryBarrier2 barrier;
    barrier.srcStageMask = source.stage;
    //nothing has to be flushed out of a layout that only ever read
    barrier.srcAccessMask = source.access & (vk::AccessFlagBits2::eColorAttachmentWrite |
                                             vk::AccessFlagBits2::eDepthStencilAttachmentWrite |
                                             vk::AccessFlagBits2::eShaderWrite |
                                             vk::AccessFlagBits2::eTransferWrite |
                                             vk::AccessFlagBits2::eMemoryWrite);
    barrier.dstStageMask = destination.stage;
    barrier.dstAccessMask = destination.access;
    barrier.oldLayout = oldLayout;
    barrier.newLayout = newLayout;
    barrier.image = image;
    barrier.subresourceRange = {aspect, baseMipLevel, levelCount, 0, layerCount};
    return barrier;
}

inline void recordBarrier(const vk::raii::CommandBuffer& commandBuffer, const vk::ImageMemoryBarrier2& barrier){
    vk::DependencyInfo dependency;
    dependency.imageMemoryBarrierCount = 1;
    dependency.pImageMemoryBarriers = &barrier;
    commandBuffer.pipelineBarrier2(dependency);
}
