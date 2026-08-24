#include "VulkanRenderer.h"
#include "Barriers.h"
#include <glm/glm.hpp>

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
        createShadowPlaceholder();
        createFrameResources();
}

void VulkanRenderer::createShadowPlaceholder(){
    //A single depth texel that is never rendered into and never read. It exists so that
    //binding 2 of set 0 always has a valid image and sampler behind it, whether or not the
    //application ever asks for shadows
    ImageConfig placeholderConfig;
    placeholderConfig.usage = vk::ImageUsageFlagBits::eSampled;
    placeholderConfig.dedicated = false;
    placeholderConfig.priority = 0.0f;

    shadowPlaceholder.emplace(device, vk::Extent2D{1,1}, makeDepthConfig(device, placeholderConfig));

    //The shader declares a comparison sampler, so this one has to be a comparison sampler
    //too - a plain sampler bound where the shader expects to compare is a validation error,
    //not a slightly wrong picture
    vk::SamplerCreateInfo samplerInfo;
    samplerInfo.magFilter = vk::Filter::eNearest;
    samplerInfo.minFilter = vk::Filter::eNearest;
    samplerInfo.addressModeU = vk::SamplerAddressMode::eClampToBorder;
    samplerInfo.addressModeV = vk::SamplerAddressMode::eClampToBorder;
    samplerInfo.addressModeW = vk::SamplerAddressMode::eClampToBorder;
    samplerInfo.borderColor = vk::BorderColor::eFloatOpaqueWhite;
    samplerInfo.compareEnable = VK_TRUE;
    samplerInfo.compareOp = vk::CompareOp::eLessOrEqual;
    samplerInfo.maxLod = 0.0f;

    shadowPlaceholderSampler = vk::raii::Sampler(device.getDevice(), samplerInfo);

    //It has to be in the layout a shader read expects before anything binds it
    command.transitionImageLayout(*shadowPlaceholder, vk::ImageLayout::eShaderReadOnlyOptimal);
}

void VulkanRenderer::setShadowMap(const RenderTarget& target, const Light& light, float depthBias){
    if(!target.hasDepth()){
        throw std::runtime_error("setShadowMap: the target has no depth attachment to shadow with");
    }
    if(!target.keepsDepth()){
        throw std::runtime_error("setShadowMap: the target does not keep its depth (RenderTargetConfig::keepDepth), so there would be nothing to sample");
    }
    if(light.getType() != LightType::Directional){
        throw std::runtime_error("setShadowMap: only a directional light has a single light space matrix - a point light needs a cube map");
    }

    shadowTarget = &target;
    shadowLight = &light;
    shadowDepthBias = depthBias;
    shadowDirty.assign(command.getCommandBuffers().size(), 1);
}

void VulkanRenderer::clearShadowMap(){
    shadowTarget = nullptr;
    shadowLight = nullptr;
    shadowDepthBias = 0.0f;
    shadowDirty.assign(command.getCommandBuffers().size(), 1);
}

void VulkanRenderer::writeShadowMap(size_t frame) const{
    const SampledImage image = shadowTarget
        ? shadowTarget->getDepthSampled()
        : SampledImage{*shadowPlaceholder->getImageView(), *shadowPlaceholderSampler, &*shadowPlaceholder, 0};

    vk::DescriptorImageInfo imageInfo;
    imageInfo.imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
    imageInfo.imageView = image.view;
    imageInfo.sampler = image.sampler;

    vk::WriteDescriptorSet write;
    write.dstSet = *frameSets[frame];
    write.dstBinding = 2;
    write.dstArrayElement = 0;
    write.descriptorType = vk::DescriptorType::eCombinedImageSampler;
    write.setImageInfo(imageInfo);

    device.getDevice().updateDescriptorSets(write, nullptr);
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

void VulkanRenderer::startPass(vk::Image colorImage, vk::ImageView colorView, const VulkanImage* depth, vk::Extent2D extent){

const auto& commandBuffer = command.getCommandBuffers()[currentFrame];

//A tracked image is transitioned out of the layout it is really in, so whatever it holds
//survives. A swapchain image is not tracked, and eUndefined says its contents are free
//A depth only pass has no colour view to hand over. Nothing here is skipped for the
//window, which always has one
const bool hasColor = colorView != vk::ImageView(nullptr);

const VulkanImage* trackedColor = (currentTarget && currentTarget->hasColor()) ? &currentTarget->getColorImage() : nullptr;
const vk::ImageLayout colorFrom = trackedColor ? trackedColor->getCurrentLayout() : vk::ImageLayout::eUndefined;

if(hasColor){
    recordBarrier(commandBuffer, imageBarrier(colorImage, colorFrom, vk::ImageLayout::eColorAttachmentOptimal));
    if(trackedColor){
        trackedColor->setCurrentLayout(vk::ImageLayout::eColorAttachmentOptimal);
    }
}

if(depth){
    recordBarrier(commandBuffer, imageBarrier(*depth->getImage(),
                                              depth->getCurrentLayout(),
                                              vk::ImageLayout::eDepthAttachmentOptimal,
                                              vk::ImageAspectFlagBits::eDepth));
    depth->setCurrentLayout(vk::ImageLayout::eDepthAttachmentOptimal);
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
    //Depth is scratch space for the depth test and is normally thrown away here. A shadow
    //map is the exception: there the depth is the whole point of the pass
    depthAttachment.storeOp = (currentTarget && currentTarget->keepsDepth())
                            ? vk::AttachmentStoreOp::eStore
                            : vk::AttachmentStoreOp::eDontCare;
    depthAttachment.clearValue.depthStencil = vk::ClearDepthStencilValue(rendererConfig.clearDepth,0);
}

vk::RenderingInfo renderingInfo;
renderingInfo.renderArea = vk::Rect2D({0,0},extent);
renderingInfo.layerCount = 1;
renderingInfo.colorAttachmentCount = hasColor ? 1 : 0;
renderingInfo.pColorAttachments = hasColor ? &colorAttachment : nullptr;
if(depth){
    renderingInfo.pDepthAttachment = &depthAttachment;
}

commandBuffer.beginRendering(renderingInfo);

//the colour attachment was transitioned from eUndefined, so whatever the image held is gone

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

//Where the pass is looking from. A shadow pass is the same scene seen by a light, so it
//swaps out both matrices - and because every pass already writes its own FrameData block
//at its own dynamic offset, nothing has to be restored afterwards
if(passLight){
    frameData.view = passLight->getView();
    frameData.projection = passLight->getProjection();
}
else{
    frameData.view = camera ? camera->getView() : glm::mat4(1.0f);
    frameData.projection = camera ? camera->getProjection(extent.width, extent.height) : glm::mat4(1.0f);
}

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



    if(pipeline.getPushConstantSize() < sizeof(ObjectData)){
        throw std::runtime_error("draw: pipeline's push constant range is too small for ObjectData");
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

void VulkanRenderer::endPass(){
    if(!passActive){
        throw std::runtime_error("endPass : no pass active");
    }

    const auto& commandBuffer = command.getCommandBuffers()[currentFrame];
    commandBuffer.endRendering();

    const bool hasColor = currentTarget ? currentTarget->hasColor() : true;

    if(hasColor){
        vk::Image colorImage = currentTarget ? *currentTarget->getColorImage().getImage() : swapchain.getImages()[currentImageIndex];
        vk::ImageLayout finalLayout = currentTarget ? currentTarget->getFinalLayout() : vk::ImageLayout::ePresentSrcKHR;

        recordBarrier(commandBuffer, imageBarrier(colorImage, vk::ImageLayout::eColorAttachmentOptimal, finalLayout));

        if(currentTarget){
            currentTarget->getColorImage().setCurrentLayout(finalLayout);
        }
    }

    //Depth that was stored is depth somebody is going to read, so it leaves the pass in the
    //layout the reader expects instead of in eDepthAttachmentOptimal. Scratch depth is left
    //alone - the next pass clears it anyway
    if(currentTarget && currentTarget->keepsDepth()){
        const VulkanImage* depth = currentTarget->getDepthImage();
        const vk::ImageLayout depthFinal = currentTarget->getDepthFinalLayout();

        recordBarrier(commandBuffer, imageBarrier(*depth->getImage(),
                                                  depth->getCurrentLayout(),
                                                  depthFinal,
                                                  vk::ImageAspectFlagBits::eDepth));
        depth->setCurrentLayout(depthFinal);
    }

    passActive = false;
    currentTarget = nullptr;
    passLight = nullptr;
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

    //The fence above has already been waited on, so nothing is reading this frame's set
    if(currentFrame < shadowDirty.size() && shadowDirty[currentFrame]){
        writeShadowMap(currentFrame);
        shadowDirty[currentFrame] = 0;
    }

    //VMA times its budget heuristics in frames, and it only knows which frame it is if it is
    //told. Costs one store; without it every allocation looks equally old
    device.getAllocator().setCurrentFrameIndex(frameCounter++);

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

        //Only the one light a shadow map was set for carries a matrix. Everything else keeps
        //params.y at zero, and the shader leaves it lit without ever touching the map
        if(shadowTarget && shadowLight == light){
            gpuLight.params.y = 1.0f;
            gpuLight.params.z = shadowDepthBias;
            gpuLight.lightViewProjection = light->getViewProjection();
        }

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


    //Both of these are written by the CPU every single frame and never read back, which is
    //exactly what CPU_TO_GPU promises VMA. They stay mapped for their whole life
    //(BufferConfig::persistentlyMapped, on by default), so upload() is now a memcpy into a
    //pointer that already exists instead of a vkMapMemory/vkUnmapMemory pair per frame.
    //minAlignment makes the buffer itself start on the same boundary the dynamic offsets
    //below step by, so offset arithmetic and the buffer agree by construction
    BufferConfig frameBufferConfig;
    frameBufferConfig.minAlignment = alignment;

    for(size_t i = 0; i < framesInFlight; ++i){
        frameBuffers.emplace_back(device, 
            frameDataStride * rendererConfig.maxPassesPerFrame,
            vk::BufferUsageFlagBits::eUniformBuffer,MemoryUsage::CPU_TO_GPU,
            frameBufferConfig);
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

        //Binding 2 gets the placeholder now, so the set is complete from the moment it
        //exists. setShadowMap marks it dirty and beginFrame swaps in the real map
        writeShadowMap(i);
    }

    shadowDirty.assign(framesInFlight, 0);
}

void VulkanRenderer::beginPass(){
    if(!frameActive){
        throw std::runtime_error("beginPass: frame not started");
    }
    if(passActive){
    throw std::runtime_error("beginPass: a pass is already active (missing endPass)");
    }

    currentTarget = nullptr;
    passLight = nullptr;
    startPass(swapchain.getImages()[currentImageIndex],
                *swapchain.getImageViews()[currentImageIndex],
                depthImage,
                swapchain.getExtent());

}

void VulkanRenderer::beginPass(const RenderTarget& target){
    if(!frameActive){
        throw std::runtime_error("beginPass: frame not started");
    }
    if(passActive){
    throw std::runtime_error("beginPass: a pass is already active (missing endPass)");
    }

    currentTarget = &target;
    passLight = nullptr;
    startPass(target.hasColor() ? *target.getColorImage().getImage() : vk::Image(nullptr),
                target.hasColor() ? *target.getColorImage().getImageView() : vk::ImageView(nullptr),
                target.getDepthImage(),
                target.getExtent());
}

//The same pass, seen from a light. Everything drawn between here and endPass is projected
//by the light's matrices, which is what turns a depth buffer into a shadow map
void VulkanRenderer::beginPass(const RenderTarget& target, const Light& light){
    if(!frameActive){
        throw std::runtime_error("beginPass: frame not started");
    }
    if(passActive){
    throw std::runtime_error("beginPass: a pass is already active (missing endPass)");
    }
    if(!target.hasDepth()){
        throw std::runtime_error("beginPass: a light driven pass needs a depth attachment to write into");
    }

    currentTarget = &target;
    passLight = &light;
    startPass(target.hasColor() ? *target.getColorImage().getImage() : vk::Image(nullptr),
                target.hasColor() ? *target.getColorImage().getImageView() : vk::ImageView(nullptr),
                target.getDepthImage(),
                target.getExtent());
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

    //storage images are moved into eGeneral for the dispatch and back to where the caller
    //wants them. The image itself carries the layout, so two materials agree about it
    const std::vector<StorageImageSlot>& images = material.getStorageImages();

    if(!images.empty()){
        std::vector<vk::ImageMemoryBarrier2> toGeneral;
        toGeneral.reserve(images.size());
        for(const StorageImageSlot& slot : images){
            toGeneral.push_back(imageBarrier(*slot.image->getImage(),
                                             slot.image->getCurrentLayout(),
                                             vk::ImageLayout::eGeneral));
        }

        vk::DependencyInfo dep;
        dep.setImageMemoryBarriers(toGeneral);
        commandBuffer.pipelineBarrier2(dep);
    }

    commandBuffer.dispatch(groupsX, groupsY, groupsZ);

    if(!images.empty()){
        std::vector<vk::ImageMemoryBarrier2> toFinal;
        toFinal.reserve(images.size());
        for(const StorageImageSlot& slot : images){
            toFinal.push_back(imageBarrier(*slot.image->getImage(),
                                           vk::ImageLayout::eGeneral,
                                           slot.finalLayout));
            slot.image->setCurrentLayout(slot.finalLayout);
        }

        vk::DependencyInfo dep;
        dep.setImageMemoryBarriers(toFinal);
        commandBuffer.pipelineBarrier2(dep);
    }

    //buffers carry no layout, so they need a plain memory barrier. Images got their own
    //above, and a dispatch that binds no buffer needs none of this
    if(material.hasStorageBuffers()){
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
}

ImageData VulkanRenderer::readLastFrame() const{
    if(frameActive){
        throw std::runtime_error("readLastFrame: a frame is still being recorded (missing endFrame)");
    }
    if(!swapchain.canReadback()){
        throw std::runtime_error("readLastFrame: the swapchain was not created for readback (SwapchainConfig::allowReadback, or the surface does not support it)");
    }

    device.getDevice().waitIdle();

    const vk::Image image = swapchain.getImages()[currentImageIndex];
    const vk::Extent2D extent = swapchain.getExtent();
    const vk::Format format = swapchain.getSurfaceFormat().format;
    const vk::DeviceSize bytes = vk::DeviceSize(extent.width) * extent.height * bytesPerPixel(format);

    //the image was left ready for presentation, so it is borrowed and put back
    command.transitionImageLayout(image, vk::ImageLayout::ePresentSrcKHR, vk::ImageLayout::eTransferSrcOptimal);

    VulkanBuffer staging(device, bytes, vk::BufferUsageFlagBits::eTransferDst, MemoryUsage::GPU_TO_CPU);
    command.copyImageToBuffer(image, staging.getBuffer(), extent);

    command.transitionImageLayout(image, vk::ImageLayout::eTransferSrcOptimal, vk::ImageLayout::ePresentSrcKHR);

    ImageData out;
    out.extent = extent;
    out.format = format;
    out.pixels.resize(static_cast<size_t>(bytes));
    staging.download(out.pixels.data(), bytes);

    return out;
}
