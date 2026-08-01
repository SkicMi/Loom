#include "VulkanRenderer.h"

VulkanRenderer::VulkanRenderer(
    const VulkanDevice& device,
    VulkanSwapchain& swapchain,
    const VulkanCommand& command,
    const RendererConfig& rendererConfig) : 
    command(command),
    device(device),
    swapchain(swapchain),
    rendererConfig(rendererConfig){

        createSyncObjects();
}

void VulkanRenderer::createSyncObjects(){
    size_t framesInFlight = command.getCommandBuffers().size();
    size_t imageCount = swapchain.getImageViews().size();

    imageAvailableSemaphores.reserve(framesInFlight);
    inFlightFences.reserve(framesInFlight);
    renderFinishedSemaphores.reserve(imageCount);

    vk::SemaphoreCreateInfo semaphoreInfo;
    vk::FenceCreateInfo fenceInfo;
    fenceInfo.flags = vk::FenceCreateFlagBits::eSignaled;

    for(size_t i = 0; i < framesInFlight; i++){
        imageAvailableSemaphores.emplace_back(device.getDevice(),semaphoreInfo);
        inFlightFences.emplace_back(device.getDevice(),fenceInfo);
    }

    for(size_t i = 0; i < imageCount; i++){
        renderFinishedSemaphores.emplace_back(device.getDevice(),semaphoreInfo);
    }
}

void VulkanRenderer::recordCommandBuffer(const vk::raii::CommandBuffer& commandBuffer, uint32_t imageIndex){
vk::CommandBufferBeginInfo beginInfo;
commandBuffer.begin(beginInfo);

const vk::Image& image = swapchain.getImages()[imageIndex];

//Transition: undefined - color attachment
vk::ImageMemoryBarrier2 toColor;
toColor.srcStageMask = vk::PipelineStageFlagBits2::eTopOfPipe;
toColor.srcAccessMask = {};
toColor.dstStageMask = vk::PipelineStageFlagBits2::eColorAttachmentOutput;
toColor.dstAccessMask = vk::AccessFlagBits2::eColorAttachmentWrite;
toColor.oldLayout = vk::ImageLayout::eUndefined;
toColor.newLayout = vk::ImageLayout::eColorAttachmentOptimal;
toColor.image = image;
toColor.subresourceRange = {vk::ImageAspectFlagBits::eColor,0,1,0,1};

vk::DependencyInfo toColorDep;
toColorDep.imageMemoryBarrierCount = 1;
toColorDep.pImageMemoryBarriers = &toColor;
commandBuffer.pipelineBarrier2(toColorDep);

//Dynamic rendering: clear the image
vk::RenderingAttachmentInfo colorAttachment;
colorAttachment.imageView = *swapchain.getImageViews()[imageIndex];
colorAttachment.imageLayout = vk::ImageLayout::eColorAttachmentOptimal;
colorAttachment.loadOp = vk::AttachmentLoadOp::eClear;
colorAttachment.storeOp = vk::AttachmentStoreOp::eStore;
colorAttachment.clearValue.color = vk::ClearColorValue(rendererConfig.clearColor);

vk::RenderingInfo renderingInfo;
renderingInfo.renderArea = vk::Rect2D({0,0},swapchain.getExtent());
renderingInfo.layerCount = 1;
renderingInfo.colorAttachmentCount = 1;
renderingInfo.pColorAttachments = &colorAttachment;

commandBuffer.beginRendering(renderingInfo);
//No actual drawing commands yet, just clearing the image
commandBuffer.endRendering();

//Transition: color attachment - present
vk::ImageMemoryBarrier2 toPresent;
toPresent.srcStageMask = vk::PipelineStageFlagBits2::eColorAttachmentOutput;
toPresent.srcAccessMask = vk::AccessFlagBits2::eColorAttachmentWrite;
toPresent.dstStageMask = vk::PipelineStageFlagBits2::eBottomOfPipe;
toPresent.dstAccessMask = {};
toPresent.oldLayout = vk::ImageLayout::eColorAttachmentOptimal;
toPresent.newLayout = vk::ImageLayout::ePresentSrcKHR;
toPresent.image = image;
toPresent.subresourceRange = {vk::ImageAspectFlagBits::eColor,0,1,0,1}; 

vk::DependencyInfo toPresentDep;
toPresentDep.imageMemoryBarrierCount = 1;
toPresentDep.pImageMemoryBarriers = &toPresent;
commandBuffer.pipelineBarrier2(toPresentDep);

commandBuffer.end();

}

void VulkanRenderer::drawFrame(){
    const auto& dev = device.getDevice();
    const auto& fence = inFlightFences[currentFrame];

    //Wait until GPU finishes the frame that used these resources
    while(vk::Result::eTimeout == dev.waitForFences(*fence, VK_TRUE, UINT64_MAX));

    //Acquire an image from the swapchain
    bool needsRecreate = false;
    uint32_t imageIndex;
    try{
        auto [acquireResult, index] = swapchain.getSwapchain().acquireNextImage(
            UINT64_MAX, *imageAvailableSemaphores[currentFrame], nullptr);
        imageIndex = index;
        if(acquireResult == vk::Result::eSuboptimalKHR){
            needsRecreate = true;
        }
    }
    catch(const vk::OutOfDateKHRError& e){
        recreateSwapchain();
        return;
    }
        

    //Only reset fence after successful acquire, otherwise we can get into a deadlock
    dev.resetFences(*fence);

    //Record commands for this frame
    const auto& commandBuffer = command.getCommandBuffers()[currentFrame];
    commandBuffer.reset();
    recordCommandBuffer(commandBuffer, imageIndex);

    //Submit via sync2
    vk::SemaphoreSubmitInfo waitInfo;
    waitInfo.semaphore = *imageAvailableSemaphores[currentFrame];
    waitInfo.stageMask = vk::PipelineStageFlagBits2KHR::eColorAttachmentOutput;

    vk::CommandBufferSubmitInfo commandBufferInfo;
    commandBufferInfo.commandBuffer = *commandBuffer;

    vk::SemaphoreSubmitInfo signalInfo;
    signalInfo.semaphore = *renderFinishedSemaphores[imageIndex];
    signalInfo.stageMask = vk::PipelineStageFlagBits2KHR::eColorAttachmentOutput;

    vk::SubmitInfo2 submitInfo;
    submitInfo.waitSemaphoreInfoCount = 1;
    submitInfo.pWaitSemaphoreInfos = &waitInfo;
    submitInfo.commandBufferInfoCount = 1;
    submitInfo.pCommandBufferInfos = &commandBufferInfo;
    submitInfo.signalSemaphoreInfoCount = 1;
    submitInfo.pSignalSemaphoreInfos = &signalInfo;

    device.getGraphicsQueue().submit2(submitInfo, *fence);

    //Present the image
    vk::Semaphore waitSemaphore = *renderFinishedSemaphores[imageIndex];
    vk::SwapchainKHR swapchainHandle = *swapchain.getSwapchain();

    vk::PresentInfoKHR presentInfo;
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores = &waitSemaphore;
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = &swapchainHandle;
    presentInfo.pImageIndices = &imageIndex;

    
   try{
        auto presentResult = device.getPresentQueue().presentKHR(presentInfo);
        if(presentResult == vk::Result::eErrorOutOfDateKHR || presentResult == vk::Result::eSuboptimalKHR){
            needsRecreate = true;
        }
    }
    catch(const vk::OutOfDateKHRError& e){
        needsRecreate = true;
    }

    

    //Advance to the next frame
    currentFrame = (currentFrame + 1) % command.getCommandBuffers().size();

    if(needsRecreate){
        recreateSwapchain();
    }
}

void VulkanRenderer::recreateSwapchain(){
    swapchain.recreateSwapchain();
    imageAvailableSemaphores.clear();
    renderFinishedSemaphores.clear();
    inFlightFences.clear();
    createSyncObjects();

    currentFrame = 0; //Reset current frame to 0 after recreating swapchain
}