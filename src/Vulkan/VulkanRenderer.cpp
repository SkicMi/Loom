#include "VulkanRenderer.h"
#include <glm/glm.hpp>

VulkanRenderer::VulkanRenderer(
    const VulkanDevice& device,
    VulkanSwapchain& swapchain,
    const VulkanCommand& command,
    const VulkanGraphicsPipeline& graphicsPipeline,
    VulkanImage* depthImage,
    const vk::raii::DescriptorPool& descriptorPool,
    const RendererConfig& rendererConfig) :
    device(device),
    swapchain(swapchain),
    command(command),
    graphicsPipeline(graphicsPipeline),
    depthImage(depthImage),
    rendererConfig(rendererConfig),
    descriptorPool(descriptorPool){

        createSyncObjects();
        createFrameResources();
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

void VulkanRenderer::beginRecording(const vk::raii::CommandBuffer& commandBuffer, uint32_t imageIndex){
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

if(depthImage){
    vk::ImageMemoryBarrier2 toDepth;
    toDepth.srcStageMask = vk::PipelineStageFlagBits2::eTopOfPipe;
    toDepth.srcAccessMask = {};
    toDepth.dstStageMask = vk::PipelineStageFlagBits2::eEarlyFragmentTests;
    toDepth.dstAccessMask = vk::AccessFlagBits2::eDepthStencilAttachmentWrite;
    toDepth.oldLayout = vk::ImageLayout::eUndefined;
    toDepth.newLayout = vk::ImageLayout::eDepthAttachmentOptimal;
    toDepth.image = *depthImage->getImage();
    toDepth.subresourceRange = {vk::ImageAspectFlagBits::eDepth,0,1,0,1};

    vk::DependencyInfo toDepthDep;
    toDepthDep.imageMemoryBarrierCount = 1;
    toDepthDep.pImageMemoryBarriers = &toDepth;
    commandBuffer.pipelineBarrier2(toDepthDep);
}

//color attachment info
vk::RenderingAttachmentInfo colorAttachment;
colorAttachment.imageView = *swapchain.getImageViews()[imageIndex];
colorAttachment.imageLayout = vk::ImageLayout::eColorAttachmentOptimal;
colorAttachment.loadOp = vk::AttachmentLoadOp::eClear;
colorAttachment.storeOp = vk::AttachmentStoreOp::eStore;
colorAttachment.clearValue.color = vk::ClearColorValue(rendererConfig.clearColor);

//depth attachment info
vk::RenderingAttachmentInfo depthAttachment;
if(depthImage){
    depthAttachment.imageView = *depthImage->getImageView();
    depthAttachment.imageLayout = vk::ImageLayout::eDepthAttachmentOptimal;
    depthAttachment.loadOp = vk::AttachmentLoadOp::eClear;
    depthAttachment.storeOp = vk::AttachmentStoreOp::eDontCare;
    depthAttachment.clearValue.depthStencil = vk::ClearDepthStencilValue(rendererConfig.clearDepth,0);
}

vk::RenderingInfo renderingInfo;
renderingInfo.renderArea = vk::Rect2D({0,0},swapchain.getExtent());
renderingInfo.layerCount = 1;
renderingInfo.colorAttachmentCount = 1;
renderingInfo.pColorAttachments = &colorAttachment;
if(depthImage){
    renderingInfo.pDepthAttachment = &depthAttachment;
}

commandBuffer.beginRendering(renderingInfo);

vk::Viewport viewport;
viewport.x = 0.0f;
viewport.y = 0.0f;
viewport.width = static_cast<float>(swapchain.getExtent().width);
viewport.height = static_cast<float>(swapchain.getExtent().height);
viewport.minDepth = 0.0f;
viewport.maxDepth = 1.0f;
commandBuffer.setViewport(0, viewport);

vk::Rect2D scissor({0,0}, swapchain.getExtent());
commandBuffer.setScissor(0, scissor);

commandBuffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics,
    *graphicsPipeline.getPipelineLayout(),
    VulkanGraphicsPipeline::frameSet, {*frameSets[currentFrame]}, {});

}

void VulkanRenderer::draw(const Mesh& mesh, const glm::mat4& model){
    draw(mesh,model,Material(graphicsPipeline));
}

void VulkanRenderer::draw(const Mesh& mesh, const glm::mat4& model, const Material& material){
    if(!frameActive){
        throw std::runtime_error("draw: frame not started( missing beginFrame)");
    }

    const auto& commandBuffer = command.getCommandBuffers()[currentFrame];
    const VulkanGraphicsPipeline& pipeline = material.getPipeline();

    if(boundPipeline != &pipeline){
        commandBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, *pipeline.getPipeline());
        boundPipeline = &pipeline;
    }

    if(material.hasDescriptorSet()){
        material.uploadIfDirty(currentFrame);
        commandBuffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics,
            *pipeline.getPipelineLayout(),
            VulkanGraphicsPipeline::materialSet,
            {*material.getDescriptorSet(currentFrame)},{});
    }

    ObjectData objectData;
    objectData.model = model;
    objectData.normalMatrix = glm::transpose(glm::inverse(model));
    

    commandBuffer.pushConstants<ObjectData>(*pipeline.getPipelineLayout(),
    vk::ShaderStageFlagBits::eVertex,0,objectData);

    commandBuffer.bindVertexBuffers(0, {*mesh.getVertexBuffer().getBuffer()}, {0});

    if(mesh.hasIndices()) {
        commandBuffer.bindIndexBuffer(*mesh.getIndexBuffer().getBuffer(), 0, vk::IndexType::eUint16);
        commandBuffer.drawIndexed(mesh.getIndexCount(), 1,0,0,0);
    }
    else{
        commandBuffer.draw(mesh.getVertexCount(),1,0,0);
    }
}

void VulkanRenderer::endRecording(const vk::raii::CommandBuffer& commandBuffer, uint32_t imageIndex){
const vk::Image& image = swapchain.getImages()[imageIndex];

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

bool VulkanRenderer::beginFrame(){
    if(frameActive){
        throw std::runtime_error("beginFrame: frame je vec zapocet (fali endFrame)");
    }

    const auto& dev = device.getDevice();
    const auto& fence = inFlightFences[currentFrame];

    //Wait until GPU finishes the frame that used these resources
    while(vk::Result::eTimeout == dev.waitForFences(*fence, VK_TRUE, UINT64_MAX));

    //Acquire an image from the swapchain
    needsRecreate = false;
    try{
        auto [acquireResult, index] = swapchain.getSwapchain().acquireNextImage(
            UINT64_MAX, *imageAvailableSemaphores[currentFrame], nullptr);
        currentImageIndex = index;
        if(acquireResult == vk::Result::eSuboptimalKHR){
            needsRecreate = true;
        }
    }
    catch(const vk::OutOfDateKHRError& e){
        recreateSwapchain();
        return false;
    }
        

    //Only reset fence after successful acquire, otherwise we can get into a deadlock
    dev.resetFences(*fence);

    //Record commands for this frame
    const auto& commandBuffer = command.getCommandBuffers()[currentFrame];
    commandBuffer.reset();
    frameData.view = glm::mat4(1.0f);
    frameData.projection = glm::mat4(1.0f);
    frameData.cameraPosition = glm::vec4(0.0f);
    if(camera){
        vk::Extent2D extent = swapchain.getExtent();
        frameData.view = camera->getView();
        frameData.projection = camera->getProjection(extent.width,extent.height);
        frameData.cameraPosition = glm::vec4(camera->getPosition(), 1.0f);
    }
    
    frameData.ambientColor = glm::vec4(1.0f);
    if(environment){
        frameData.ambientColor = glm::vec4(environment->getAmbient(), 0.0f);
    }
    lightStaging.clear();
    for(const Light* light : lights){

        if(lightStaging.size() >= rendererConfig.maxLights) break;

        GpuLight gpuLight;
        if(light->getType() == LightType::Directional){
            gpuLight.positionOrDirection = glm::vec4(light->getDirection(), 0.0f); //direction for directional light
        }
        else{
            gpuLight.positionOrDirection = glm::vec4(light->getPosition(),1.0f); //position for other types of lights
        }
        gpuLight.color = glm::vec4(light->getColor(),0.0f);
        gpuLight.params = glm::vec4(light->getRange(), 0.0f, 0.0f, 0.0f);

        lightStaging.push_back(gpuLight);

    }
    frameData.lightCount = static_cast<uint32_t>(lightStaging.size());
    if(!lightStaging.empty()){
        lightBuffers[currentFrame].upload(lightStaging.data(), lightStaging.size() * sizeof(GpuLight));
    }
    frameBuffers[currentFrame].upload(&frameData,sizeof(FrameData));
    



    beginRecording(commandBuffer, currentImageIndex);

    frameActive = true;
    boundPipeline = nullptr;
    return true;
}

void VulkanRenderer::endFrame(){
    if(!frameActive){
        throw std::runtime_error("endFrame: frame nije zapocet (fali beginFrame)");
    }

    const auto& fence = inFlightFences[currentFrame];
    const auto& commandBuffer = command.getCommandBuffers()[currentFrame];

    endRecording(commandBuffer, currentImageIndex);

    //Submit via sync2
    vk::SemaphoreSubmitInfo waitInfo;
    waitInfo.semaphore = *imageAvailableSemaphores[currentFrame];
    waitInfo.stageMask = vk::PipelineStageFlagBits2KHR::eColorAttachmentOutput;

    vk::CommandBufferSubmitInfo commandBufferInfo;
    commandBufferInfo.commandBuffer = *commandBuffer;

    vk::SemaphoreSubmitInfo signalInfo;
    signalInfo.semaphore = *renderFinishedSemaphores[currentImageIndex];
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
    vk::Semaphore waitSemaphore = *renderFinishedSemaphores[currentImageIndex];
    vk::SwapchainKHR swapchainHandle = *swapchain.getSwapchain();

    vk::PresentInfoKHR presentInfo;
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores = &waitSemaphore;
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = &swapchainHandle;
    presentInfo.pImageIndices = &currentImageIndex;

    
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
    frameActive = false;

    if(needsRecreate){
        recreateSwapchain();
    }
}

void VulkanRenderer::recreateSwapchain(){
    swapchain.recreateSwapchain();
    if(depthImage){
        depthImage->recreate(swapchain.getExtent());
    }
    imageAvailableSemaphores.clear();
    renderFinishedSemaphores.clear();
    inFlightFences.clear();
    createSyncObjects();

    currentFrame = 0; //Reset current frame to 0 after recreating swapchain
}

void VulkanRenderer::createFrameResources(){
    size_t framesInFlight = command.getCommandBuffers().size();

    frameBuffers.reserve(framesInFlight);
    frameSets.reserve(framesInFlight);
    lightBuffers.reserve(framesInFlight);

    vk::DescriptorSetLayout layout = *graphicsPipeline.getFrameSetLayout();

    for(size_t i = 0; i < framesInFlight; ++i){
        frameBuffers.emplace_back(device, 
            sizeof(FrameData),
            vk::BufferUsageFlagBits::eUniformBuffer,MemoryUsage::CPU_TO_GPU);
        lightBuffers.emplace_back(device, 
            sizeof(GpuLight) * rendererConfig.maxLights,
            vk::BufferUsageFlagBits::eStorageBuffer, MemoryUsage::CPU_TO_GPU);
    

        vk::DescriptorSetAllocateInfo allocInfo;
        allocInfo.descriptorPool = *descriptorPool;
        allocInfo.setSetLayouts(layout);
        vk::raii::DescriptorSets sets(device.getDevice(), allocInfo);
        frameSets.push_back(std::move(sets[0]));

        vk::DescriptorBufferInfo bufferInfo;
        bufferInfo.buffer = *frameBuffers[i].getBuffer();
        bufferInfo.offset = 0;
        bufferInfo.range = sizeof(FrameData);

        vk::WriteDescriptorSet write;
        write.dstSet = *frameSets[i];
        write.dstBinding = 0;
        write.dstArrayElement = 0;
        write.descriptorType = vk::DescriptorType::eUniformBuffer;
        write.setBufferInfo(bufferInfo);

        vk::DescriptorBufferInfo lightBufferInfo;
        lightBufferInfo.buffer = *lightBuffers[i].getBuffer();
        lightBufferInfo.offset = 0;
        lightBufferInfo.range = sizeof(GpuLight) * rendererConfig.maxLights;

        vk::WriteDescriptorSet lightWrite;
        lightWrite.dstSet = *frameSets[i];
        lightWrite.dstBinding = 1;
        lightWrite.dstArrayElement = 0;
        lightWrite.descriptorType = vk::DescriptorType::eStorageBuffer;
        lightWrite.setBufferInfo(lightBufferInfo);

        std::array<vk::WriteDescriptorSet,2> writes = {write,lightWrite};
        device.getDevice().updateDescriptorSets(writes,nullptr);
    }
}