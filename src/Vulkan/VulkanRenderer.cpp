#include "VulkanRenderer.h"
#include <glm/glm.hpp>

static void barrierDstFor(vk::ImageLayout layout, vk::PipelineStageFlags2& stage, vk::AccessFlags2& access){
    switch(layout){
        case vk::ImageLayout::eShaderReadOnlyOptimal:
            stage = vk::PipelineStageFlagBits2::eFragmentShader;
            access = vk::AccessFlagBits2::eShaderRead;
            break;
        case vk::ImageLayout::eTransferSrcOptimal:
            stage = vk::PipelineStageFlagBits2::eCopy;
            access = vk::AccessFlagBits2::eTransferRead;
            break;
        default:
            stage = vk::PipelineStageFlagBits2::eBottomOfPipe;
            access = {};
            break;

    }
}

VulkanRenderer::VulkanRenderer(
    const VulkanDevice& device,
    VulkanSwapchain& swapchain,
    const VulkanCommand& command,
    const VulkanGraphicsPipeline& graphicsPipeline,
    VulkanImage* depthImage,
    const vk::raii::DescriptorPool& descriptorPool,
    const RendererConfig& rendererConfig) :
    depthImage(depthImage),
    device(device),
    swapchain(swapchain),
    command(command),
    graphicsPipeline(graphicsPipeline),
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

void VulkanRenderer::startPass(vk::Image colorImage, vk::ImageView colorView, const VulkanImage* depth, vk::Extent2D extent, bool isOffscreen){


const auto& commandBuffer = command.getCommandBuffers()[currentFrame];

//Transition: undefined - color attachment
vk::ImageMemoryBarrier2 toColor;
toColor.srcStageMask = isOffscreen ? vk::PipelineStageFlagBits2::eFragmentShader : vk::PipelineStageFlagBits2::eTopOfPipe;
toColor.srcAccessMask = isOffscreen ? vk::AccessFlagBits2::eShaderRead : vk::AccessFlags2{};
toColor.dstStageMask = vk::PipelineStageFlagBits2::eColorAttachmentOutput;
toColor.dstAccessMask = vk::AccessFlagBits2::eColorAttachmentWrite;
toColor.oldLayout = vk::ImageLayout::eUndefined;
toColor.newLayout = vk::ImageLayout::eColorAttachmentOptimal;
toColor.image = colorImage;
toColor.subresourceRange = {vk::ImageAspectFlagBits::eColor,0,1,0,1};

vk::DependencyInfo toColorDep;
toColorDep.imageMemoryBarrierCount = 1;
toColorDep.pImageMemoryBarriers = &toColor;
commandBuffer.pipelineBarrier2(toColorDep);

if(depth){
    vk::ImageMemoryBarrier2 toDepth;
    toDepth.srcStageMask = vk::PipelineStageFlagBits2::eTopOfPipe;
    toDepth.srcAccessMask = {};
    toDepth.dstStageMask = vk::PipelineStageFlagBits2::eEarlyFragmentTests;
    toDepth.dstAccessMask = vk::AccessFlagBits2::eDepthStencilAttachmentWrite;
    toDepth.oldLayout = vk::ImageLayout::eUndefined;
    toDepth.newLayout = vk::ImageLayout::eDepthAttachmentOptimal;
    toDepth.image = *depth->getImage();
    toDepth.subresourceRange = {vk::ImageAspectFlagBits::eDepth,0,1,0,1};

    vk::DependencyInfo toDepthDep;
    toDepthDep.imageMemoryBarrierCount = 1;
    toDepthDep.pImageMemoryBarriers = &toDepth;
    commandBuffer.pipelineBarrier2(toDepthDep);
}

//color attachment info
vk::RenderingAttachmentInfo colorAttachment;
colorAttachment.imageView = colorView;
colorAttachment.imageLayout = vk::ImageLayout::eColorAttachmentOptimal;
colorAttachment.loadOp = vk::AttachmentLoadOp::eClear;
colorAttachment.storeOp = vk::AttachmentStoreOp::eStore;
colorAttachment.clearValue.color = vk::ClearColorValue(rendererConfig.clearColor);

//depth attachment info
vk::RenderingAttachmentInfo depthAttachment;
if(depth){
    depthAttachment.imageView = *depth->getImageView();
    depthAttachment.imageLayout = vk::ImageLayout::eDepthAttachmentOptimal;
    depthAttachment.loadOp = vk::AttachmentLoadOp::eClear;
    depthAttachment.storeOp = vk::AttachmentStoreOp::eDontCare;
    depthAttachment.clearValue.depthStencil = vk::ClearDepthStencilValue(rendererConfig.clearDepth,0);
}

vk::RenderingInfo renderingInfo;
renderingInfo.renderArea = vk::Rect2D({0,0},extent);
renderingInfo.layerCount = 1;
renderingInfo.colorAttachmentCount = 1;
renderingInfo.pColorAttachments = &colorAttachment;
if(depth){
    renderingInfo.pDepthAttachment = &depthAttachment;
}

commandBuffer.beginRendering(renderingInfo);

vk::Viewport viewport;
viewport.x = 0.0f;
viewport.y = 0.0f;
viewport.width = static_cast<float>(extent.width);
viewport.height = static_cast<float>(extent.height);
viewport.minDepth = 0.0f;
viewport.maxDepth = 1.0f;
commandBuffer.setViewport(0, viewport);

vk::Rect2D scissor({0,0}, extent);
commandBuffer.setScissor(0, scissor);

if(passIndex >= rendererConfig.maxPassesPerFrame){
    throw std::runtime_error("beginPass : to many passes in one frame");
}

frameData.projection = camera ? camera->getProjection(extent.width, extent.height) : glm::mat4(1.0f);

vk::DeviceSize offset = passIndex * frameDataStride;
frameBuffers[currentFrame].upload(&frameData, sizeof(FrameData), offset);

uint32_t dynamicOffset = static_cast<uint32_t>(offset);
commandBuffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics,
            *graphicsPipeline.getPipelineLayout(),
            VulkanGraphicsPipeline::frameSet, {*frameSets[currentFrame]}, {dynamicOffset});
    ++passIndex;

    passActive = true;
    boundPipeline = nullptr;

}

void VulkanRenderer::draw(const Mesh& mesh, const glm::mat4& model){
    draw(mesh,model,Material(graphicsPipeline));
}

void VulkanRenderer::draw(const Mesh& mesh, const glm::mat4& model, const Material& material){
    if(!passActive){
        throw std::runtime_error("draw: frame not started( missing beginFrame)");
    }

    bindMaterial(material);
    const auto& commandBuffer = command.getCommandBuffers()[currentFrame];
    const VulkanGraphicsPipeline& pipeline = material.getPipeline();



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

void VulkanRenderer::endPass(){
    if(!passActive){
        throw std::runtime_error("endPass : no pass active");
    }

    const auto& commandBuffer = command.getCommandBuffers()[currentFrame];
    commandBuffer.endRendering();

    vk::Image colorImage = currentTarget ? *currentTarget->getColorImage().getImage() : swapchain.getImages()[currentImageIndex];
    vk::ImageLayout finalLayout = currentTarget ? currentTarget->getFinalLayout() : vk::ImageLayout::ePresentSrcKHR;

    vk::PipelineStageFlags2 dstStage;
    vk::AccessFlags2 dstAccess;
    barrierDstFor(finalLayout, dstStage, dstAccess);
    
    vk::ImageMemoryBarrier2 toFinal;
    toFinal.srcStageMask = vk::PipelineStageFlagBits2::eColorAttachmentOutput;
    toFinal.srcAccessMask = vk::AccessFlagBits2::eColorAttachmentWrite;
    toFinal.dstStageMask = dstStage;
    toFinal.dstAccessMask = dstAccess;
    toFinal.oldLayout = vk::ImageLayout::eColorAttachmentOptimal;
    toFinal.newLayout = finalLayout;
    toFinal.image = colorImage;
    toFinal.subresourceRange = {vk::ImageAspectFlagBits::eColor,0,1,0,1};

    vk::DependencyInfo dep;
    dep.imageMemoryBarrierCount = 1;
    dep.pImageMemoryBarriers = &toFinal;
    commandBuffer.pipelineBarrier2(dep);

    passActive = false;
    currentTarget = nullptr;
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

    passIndex = 0;

    frameData.view = glm::mat4(1.0f);
    frameData.projection = glm::mat4(1.0f);
    frameData.cameraPosition = glm::vec4(0.0f);
    if(camera){
        frameData.view = camera->getView();
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


    



    vk::CommandBufferBeginInfo beginInfo;
    commandBuffer.begin(beginInfo);

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

    if(passActive){
        throw std::runtime_error("endFrame : a pass is still active ( missing endPass)");
    }
    commandBuffer.end();

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

    vk::DeviceSize alignment = device.getPhysicalDevice().getProperties().limits.minUniformBufferOffsetAlignment;
    frameDataStride = (sizeof(FrameData) + alignment - 1) & ~ (alignment - 1); //spec garanties that alignment is potent of number 2 and this delets lower bites, for 64 it is & ~ 63


    for(size_t i = 0; i < framesInFlight; ++i){
        frameBuffers.emplace_back(device, 
            frameDataStride * rendererConfig.maxPassesPerFrame,
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
        write.descriptorType = vk::DescriptorType::eUniformBufferDynamic;
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

void VulkanRenderer::beginPass(){
    if(!frameActive){
        throw std::runtime_error("beginPass: frame not started");
    }
    if(passActive){
    throw std::runtime_error("beginPass: a pass is already active (missing endPass)");
    }

    currentTarget = nullptr;
    startPass(swapchain.getImages()[currentImageIndex],
                *swapchain.getImageViews()[currentImageIndex],
                depthImage,
                swapchain.getExtent(), false);

}

void VulkanRenderer::beginPass(const RenderTarget& target){
    if(!frameActive){
        throw std::runtime_error("beginPass: frame not started");
    }
    if(passActive){
    throw std::runtime_error("beginPass: a pass is already active (missing endPass)");
    }

    currentTarget = &target;
    startPass(*target.getColorImage().getImage(),
                *target.getColorImage().getImageView(),
                target.getDepthImage(),
                target.getExtent(), true);
}

void VulkanRenderer::bindMaterial(const Material& material) {
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

}

void VulkanRenderer::drawFullscreen(const Material& material){
    if(!passActive){
        throw std::runtime_error("drawFullscreen: no pass active");
    }

    bindMaterial(material);
    command.getCommandBuffers()[currentFrame].draw(3,1,0,0);
}

void VulkanRenderer::dispatch(const ComputeMaterial& material,
                              uint32_t groupsX, uint32_t groupsY, uint32_t groupsZ,
                              const void* pushData, uint32_t pushSize){

    if(!frameActive){
        throw std::runtime_error("dispatch: frame not started (missing beginFrame)");
    }
    if(passActive){
        throw std::runtime_error("dispatch: a pass is active, compute cannot run inside a render pass");
    }

    const auto& commandBuffer = command.getCommandBuffers()[currentFrame];
    const VulkanComputePipeline& pipeline = material.getPipeline();

    //compute and graphics are separate bind points, so boundPipeline stays valid
    commandBuffer.bindPipeline(vk::PipelineBindPoint::eCompute, *pipeline.getPipeline());

    commandBuffer.bindDescriptorSets(vk::PipelineBindPoint::eCompute,
        *pipeline.getPipelineLayout(),
        VulkanComputePipeline::resourceSet,
        {*material.getDescriptorSet()}, {});

    if(pushData && pushSize > 0){
        commandBuffer.pushConstants<uint8_t>(*pipeline.getPipelineLayout(),
            vk::ShaderStageFlagBits::eCompute, 0,
            vk::ArrayProxy<const uint8_t>(pushSize, static_cast<const uint8_t*>(pushData)));
    }

    //storage images must be in eGeneral while the dispatch runs. currentLayout starts as
    //eUndefined, which discards contents - correct only for the first dispatch, and after
    //that the slot remembers where the previous dispatch left the image
    if(!material.getStorageImages().empty()){
        std::vector<vk::ImageMemoryBarrier2> toGeneral;
        toGeneral.reserve(material.getStorageImages().size());

        for(const StorageImageSlot& slot : material.getStorageImages()){
            vk::ImageMemoryBarrier2 barrier;
            barrier.srcStageMask = vk::PipelineStageFlagBits2::eAllCommands;
            barrier.srcAccessMask = vk::AccessFlagBits2::eMemoryWrite;
            barrier.dstStageMask = vk::PipelineStageFlagBits2::eComputeShader;
            barrier.dstAccessMask = vk::AccessFlagBits2::eShaderWrite | vk::AccessFlagBits2::eShaderRead;
            barrier.oldLayout = slot.currentLayout;
            barrier.newLayout = vk::ImageLayout::eGeneral;
            barrier.image = slot.image;
            barrier.subresourceRange = {vk::ImageAspectFlagBits::eColor,0,1,0,1};
            toGeneral.push_back(barrier);
        }

        vk::DependencyInfo dep;
        dep.setImageMemoryBarriers(toGeneral);
        commandBuffer.pipelineBarrier2(dep);
    }

    commandBuffer.dispatch(groupsX, groupsY, groupsZ);

    if(!material.getStorageImages().empty()){
        std::vector<vk::ImageMemoryBarrier2> toFinal;
        toFinal.reserve(material.getStorageImages().size());

        for(const StorageImageSlot& slot : material.getStorageImages()){
            vk::ImageMemoryBarrier2 barrier;
            barrier.srcStageMask = vk::PipelineStageFlagBits2::eComputeShader;
            barrier.srcAccessMask = vk::AccessFlagBits2::eShaderWrite;
            barrier.dstStageMask = vk::PipelineStageFlagBits2::eAllCommands;
            barrier.dstAccessMask = vk::AccessFlagBits2::eMemoryRead;
            barrier.oldLayout = vk::ImageLayout::eGeneral;
            barrier.newLayout = slot.finalLayout;
            barrier.image = slot.image;
            barrier.subresourceRange = {vk::ImageAspectFlagBits::eColor,0,1,0,1};
            toFinal.push_back(barrier);

            slot.currentLayout = slot.finalLayout;
        }

        vk::DependencyInfo dep;
        dep.setImageMemoryBarriers(toFinal);
        commandBuffer.pipelineBarrier2(dep);
    }

    //deliberately wide: dispatch does not know who reads the result next
    vk::MemoryBarrier2 barrier;
    barrier.srcStageMask = vk::PipelineStageFlagBits2::eComputeShader;
    barrier.srcAccessMask = vk::AccessFlagBits2::eShaderWrite;
    barrier.dstStageMask = vk::PipelineStageFlagBits2::eAllCommands | vk::PipelineStageFlagBits2::eHost;
    barrier.dstAccessMask = vk::AccessFlagBits2::eShaderRead | vk::AccessFlagBits2::eTransferRead | vk::AccessFlagBits2::eHostRead;

    vk::DependencyInfo dep;
    dep.memoryBarrierCount = 1;
    dep.pMemoryBarriers = &barrier;
    commandBuffer.pipelineBarrier2(dep);
}
