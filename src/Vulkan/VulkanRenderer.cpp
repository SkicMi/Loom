#include "VulkanRenderer.h"
#include "Barriers.h"
#include <glm/glm.hpp>

VulkanRenderer::VulkanRenderer(
    const VulkanDevice& device,
    VulkanSwapchain* swapchain,
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

    //And the same for binding 3, which is a cube and cannot be fed a 2D image
    ImageConfig cubePlaceholderConfig = placeholderConfig;
    cubePlaceholderConfig.cube = true;
    shadowCubePlaceholder.emplace(device, vk::Extent2D{1,1}, makeDepthConfig(device, cubePlaceholderConfig));

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

    //Both have to be in the layout a shader read expects before anything binds them
    command.transitionImageLayout(*shadowPlaceholder, vk::ImageLayout::eShaderReadOnlyOptimal);
    command.transitionImageLayout(*shadowCubePlaceholder, vk::ImageLayout::eShaderReadOnlyOptimal);
}

void VulkanRenderer::setShadowMap(const RenderTarget& target, const Light& light, const ShadowConfig& config){
    if(!target.hasDepth()){
        throw std::runtime_error("setShadowMap: the target has no depth attachment to shadow with");
    }
    if(!target.keepsDepth()){
        throw std::runtime_error("setShadowMap: the target does not keep its depth (RenderTargetConfig::keepDepth), so there would be nothing to sample");
    }
    if(light.getType() != LightType::Directional){
        throw std::runtime_error("setShadowMap: only a directional light has a single light space matrix - a point light needs a cube map (setShadowCube)");
    }

    //Same light again replaces its own slot; a light nobody has seen takes the next one
    const int existing = slotIndex(shadowMapSlots, &light);
    if(existing < 0 && shadowMapSlots.size() >= maxShadowMaps){
        throw std::runtime_error("setShadowMap: set 0 carries " + std::to_string(maxShadowMaps) +
            " shadow maps and they are all in use - raise maxShadowMaps and the array in the shaders together");
    }

    ShadowSlot slot;
    slot.target = &target;
    slot.light = &light;
    slot.config = config;

    if(existing < 0){
        shadowMapSlots.push_back(slot);
    }
    else{
        shadowMapSlots[static_cast<size_t>(existing)] = slot;
    }

    shadowDirty.assign(command.getCommandBuffers().size(), 1);

    updateShadowMatrices();
}

void VulkanRenderer::clearShadowMap(){
    shadowMapSlots.clear();
    shadowMatrices = {};
    shadowDirty.assign(command.getCommandBuffers().size(), 1);
}

const VulkanRenderer::ShadowSlot* VulkanRenderer::findSlot(const std::vector<ShadowSlot>& slots, const Light* light) const{
    for(const ShadowSlot& slot : slots){
        if(slot.light == light) return &slot;
    }
    return nullptr;
}

int VulkanRenderer::slotIndex(const std::vector<ShadowSlot>& slots, const Light* light) const{
    for(size_t i = 0; i < slots.size(); ++i){
        if(slots[i].light == light) return static_cast<int>(i);
    }
    return -1;
}

void VulkanRenderer::setShadowCube(const RenderTarget& target, const Light& light, const ShadowConfig& config){
    if(!target.hasDepth() || !target.keepsDepth()){
        throw std::runtime_error("setShadowCube: the target needs a depth attachment it keeps (RenderTargetConfig::keepDepth)");
    }
    if(!target.hasCubeDepth()){
        throw std::runtime_error("setShadowCube: the target's depth is a single image, not a cube (makeShadowCubeConfig)");
    }
    if(light.getType() != LightType::Point){
        throw std::runtime_error("setShadowCube: a cube map is for a point light - a directional light has one box (setShadowMap)");
    }

    const int existing = slotIndex(shadowCubeSlots, &light);
    if(existing < 0 && shadowCubeSlots.size() >= maxShadowCubes){
        throw std::runtime_error("setShadowCube: set 0 carries " + std::to_string(maxShadowCubes) +
            " shadow cubes and they are all in use - raise maxShadowCubes and the array in the shaders together");
    }

    ShadowSlot slot;
    slot.target = &target;
    slot.light = &light;
    slot.config = config;

    if(existing < 0){
        shadowCubeSlots.push_back(slot);
    }
    else{
        shadowCubeSlots[static_cast<size_t>(existing)] = slot;
    }

    shadowDirty.assign(command.getCommandBuffers().size(), 1);
}

void VulkanRenderer::clearShadowCube(){
    shadowCubeSlots.clear();
    shadowDirty.assign(command.getCommandBuffers().size(), 1);
}

void VulkanRenderer::updateShadowMatrices(){
    //Every map refits itself: they follow the same camera but each light sees it from a
    //different side, so one set of matrices could never serve two of them
    for(ShadowSlot& slot : shadowMapSlots){
        if(slot.config.fitToCamera && camera && slot.target){
            //The viewport the camera's own pass uses, which is only the window by default
            uint32_t viewportWidth = slot.config.viewportWidth;
            uint32_t viewportHeight = slot.config.viewportHeight;

            if(viewportWidth == 0 || viewportHeight == 0){
                if(!swapchain){
                    throw std::runtime_error("setShadowMap: fitToCamera needs a viewport - there is no window to take one from, so ShadowConfig::viewportWidth and viewportHeight have to be set");
                }
                viewportWidth = swapchain->getExtent().width;
                viewportHeight = swapchain->getExtent().height;
            }

            slot.matrices = slot.light->fitToCamera(*camera,
                viewportWidth, viewportHeight,
                slot.target->getExtent().width,
                slot.config.distance);
            continue;
        }

        slot.matrices.view = slot.light->getView();
        slot.matrices.projection = slot.light->getProjection();
        slot.matrices.viewProjection = slot.matrices.projection * slot.matrices.view;
    }

    //What getShadowMatrices() answers: the first map, which is the only one a scene with a
    //single shadow ever has
    shadowMatrices = shadowMapSlots.empty() ? LightMatrices{} : shadowMapSlots.front().matrices;
}

void VulkanRenderer::writeShadowMap(size_t frame) const{
    //Every slot of both arrays is written, every time. A descriptor array with a hole in it
    //is not an empty slot - it is undefined behaviour the moment anything indexes near it,
    //and the index comes from data, so "near it" is not something this side controls
    std::array<vk::DescriptorImageInfo, maxShadowMaps> mapInfos{};
    for(uint32_t i = 0; i < maxShadowMaps; ++i){
        const SampledImage image = (i < shadowMapSlots.size())
            ? shadowMapSlots[i].target->getDepthSampled()
            : SampledImage{*shadowPlaceholder->getImageView(), *shadowPlaceholderSampler, &*shadowPlaceholder, 0};

        mapInfos[i].imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
        mapInfos[i].imageView = image.view;
        mapInfos[i].sampler = image.sampler;
    }

    vk::WriteDescriptorSet write;
    write.dstSet = *frameSets[frame];
    write.dstBinding = 2;
    write.dstArrayElement = 0;
    write.descriptorType = vk::DescriptorType::eCombinedImageSampler;
    write.setImageInfo(mapInfos);

    //Binding 3 is the cubes. Their own placeholder, because a cube descriptor will not
    //accept a 2D view
    std::array<vk::DescriptorImageInfo, maxShadowCubes> cubeInfos{};
    for(uint32_t i = 0; i < maxShadowCubes; ++i){
        const SampledImage cube = (i < shadowCubeSlots.size())
            ? shadowCubeSlots[i].target->getDepthSampled()
            : SampledImage{*shadowCubePlaceholder->getImageView(), *shadowPlaceholderSampler, &*shadowCubePlaceholder, 0};

        cubeInfos[i].imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
        cubeInfos[i].imageView = cube.view;
        cubeInfos[i].sampler = cube.sampler;
    }

    vk::WriteDescriptorSet cubeWrite;
    cubeWrite.dstSet = *frameSets[frame];
    cubeWrite.dstBinding = 3;
    cubeWrite.dstArrayElement = 0;
    cubeWrite.descriptorType = vk::DescriptorType::eCombinedImageSampler;
    cubeWrite.setImageInfo(cubeInfos);

    const std::array<vk::WriteDescriptorSet,2> writes = {write, cubeWrite};
    device.getDevice().updateDescriptorSets(writes, nullptr);
}

void VulkanRenderer::createSyncObjects(){
    size_t framesInFlight = command.getCommandBuffers().size();
    //One signal semaphore per swapchain image, because presentation waits on the semaphore
    //belonging to the image being presented. With no swapchain there is nothing to signal
    size_t imageCount = swapchain ? swapchain->getImageViews().size() : 0;

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

void VulkanRenderer::startPass(vk::Image colorImage, vk::ImageView colorView, const VulkanImage* depth, vk::Extent2D extent, uint32_t depthFace){

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
    //All six faces of a cube are moved together: the layout is tracked per image, not per
    //layer, and leaving five of them behind would be a lie the next pass believes
    recordBarrier(commandBuffer, imageBarrier(*depth->getImage(),
                                              depth->getCurrentLayout(),
                                              vk::ImageLayout::eDepthAttachmentOptimal,
                                              vk::ImageAspectFlagBits::eDepth,
                                              depth->getLayerCount()));
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
    //One face at a time. A layered image cannot be attached whole, and its cube view is for
    //sampling, not for rendering into
    depthAttachment.imageView = depth->getLayerCount() > 1
                              ? *depth->getLayerView(depthFace)
                              : *depth->getImageView();
    depthAttachment.imageLayout = vk::ImageLayout::eDepthAttachmentOptimal;
    //Ucitavanje cuva ono sto je prijasnji prolaz napisao; ciscenje ga baca.
    //
    //Depth prepass UVIJEK cisti, bez obzira na loadDepth: on je taj koji dubinu uspostavlja,
    //pa bi ucitavanje znacilo da nastavlja na ono sto je ostalo od proslog framea. loadDepth
    //se odnosi na prolaz KOJI DOLAZI POSLIJE prepassa, a to je onaj s bojom
    const bool loadDepth = passUsesColor && currentTarget && currentTarget->loadsDepth();

    depthAttachment.loadOp = loadDepth ? vk::AttachmentLoadOp::eLoad
                                       : vk::AttachmentLoadOp::eClear;
    //Depth is scratch space for the depth test and is normally thrown away here. A shadow
    //map is the exception: there the depth is the whole point of the pass
    depthAttachment.storeOp = (currentTarget && currentTarget->keepsDepth())
                            ? vk::AttachmentStoreOp::eStore
                            : vk::AttachmentStoreOp::eDontCare;
    depthAttachment.clearValue.depthStencil = vk::ClearDepthStencilValue(rendererConfig.clearDepth,0);
}

//Slika stope se prilaze prolazu, ne pipelineu: rasteriser je cita dok odlucuje koliko
//puta ce sjenciti svaki blok.
//
//Prolaz bez boje nema fragment shader, pa nema ni sto ocjenjivati - a prilaganje karte
//depth prepassu ju je trazilo u layoutu u koji je ulazi tek dispatch koji dolazi POSLIJE
//tog prepassa
const bool usesRateMap = shadingRateMap != nullptr && hasColor;

vk::RenderingFragmentShadingRateAttachmentInfoKHR rateAttachment;
if(usesRateMap){
    rateAttachment.imageView = *shadingRateMap->getImage().getImageView();
    rateAttachment.imageLayout = vk::ImageLayout::eFragmentShadingRateAttachmentOptimalKHR;
    rateAttachment.shadingRateAttachmentTexelSize = shadingRateMap->getTexelSize();
}

vk::RenderingInfo renderingInfo;
if(usesRateMap){
    renderingInfo.pNext = &rateAttachment;
}
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

//Deklarirano dinamicko stanje mora biti postavljeno prije prvog crtanja, pa svaki prolaz
//krece od jednog sjencanja po pikselu. Materijal koji zeli grublje to kaze u draw pozivu,
//i sljedeci prolaz opet krece od nule - stopa se ne prelijeva iz prolaza u prolaz
if(device.hasFragmentShadingRate()){
    setShadingRate(commandBuffer, ShadingRate::Full, ShadingImportance::Normal);
}

vk::Rect2D scissor({0,0}, extent);
commandBuffer.setScissor(0, scissor);

if(passIndex >= rendererConfig.maxPassesPerFrame){
    throw std::runtime_error("beginPass : to many passes in one frame");
}

//Where the pass is looking from. A shadow pass is the same scene seen by a light, so it
//swaps out both matrices - and because every pass already writes its own FrameData block
//at its own dynamic offset, nothing has to be restored afterwards
if(passLight){
    //A cube face has its own pair, one of six
    if(currentTarget && currentTarget->hasCubeDepth()){
        frameData.view = passLight->getCubeView(passFace);
        frameData.projection = passLight->getCubeProjection();
    }
    //A light with a map of its own uses the matrices computed in beginFrame - fitted and
    //texel snapped if that is on. Any other light falls back to its own box
    else if(const ShadowSlot* slot = findSlot(shadowMapSlots, passLight)){
        frameData.view = slot->matrices.view;
        frameData.projection = slot->matrices.projection;
    }
    else{
        frameData.view = passLight->getView();
        frameData.projection = passLight->getProjection();
    }
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

    const bool hasColor = passUsesColor && (currentTarget ? currentTarget->hasColor() : true);

    if(hasColor){
        vk::Image colorImage = currentTarget ? *currentTarget->getColorImage().getImage() : swapchain->getImages()[currentImageIndex];
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
                                                  vk::ImageAspectFlagBits::eDepth,
                                                  depth->getLayerCount()));
        depth->setCurrentLayout(depthFinal);
    }

    passActive = false;
    currentTarget = nullptr;
    passLight = nullptr;
    passFace = 0;
    passUsesColor = true;
}

bool VulkanRenderer::beginFrame(){
    if(frameActive){
        throw std::runtime_error("beginFrame: frame je vec zapocet (fali endFrame)");
    }

    const auto& dev = device.getDevice();
    const auto& fence = inFlightFences[currentFrame];

    //Wait until GPU finishes the frame that used these resources
    while(vk::Result::eTimeout == dev.waitForFences(*fence, VK_TRUE, UINT64_MAX));

    //Acquire an image from the swapchain. Headless has none to acquire: the frame below is
    //identical in every other way, it simply never waits for an image and never presents one
    needsRecreate = false;
    if(swapchain){
        try{
            auto [acquireResult, index] = swapchain->getSwapchain().acquireNextImage(
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
    }
        

    //Only reset fence after successful acquire, otherwise we can get into a deadlock
    dev.resetFences(*fence);

    //Record commands for this frame
    const auto& commandBuffer = command.getCommandBuffers()[currentFrame];
    commandBuffer.reset();

    passIndex = 0;

    //The camera has probably moved since last frame, so a fitted box has to be refitted
    updateShadowMatrices();

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

        //A light carries the kind of shadow it has and which map of that kind it is. A light
        //with neither keeps params.y at zero and the shader leaves it lit without ever
        //touching an array it has no index into
        const int mapIndex = slotIndex(shadowMapSlots, light);
        const int cubeIndex = slotIndex(shadowCubeSlots, light);

        if(mapIndex >= 0){
            const ShadowSlot& slot = shadowMapSlots[static_cast<size_t>(mapIndex)];
            gpuLight.params.y = 1.0f;
            gpuLight.params.z = slot.config.depthBias;
            gpuLight.shadow.x = float(mapIndex);
            //The matrices computed once above, not a second call to the light. The pass that
            //fills the map and the lookup that reads it have to agree exactly
            gpuLight.lightViewProjection = slot.matrices.viewProjection;
        }
        //A cube needs no matrix in the shader - the lookup is a direction. What it does need
        //is the near plane, so the fragment can rebuild the same depth the face wrote
        else if(cubeIndex >= 0){
            const ShadowSlot& slot = shadowCubeSlots[static_cast<size_t>(cubeIndex)];
            gpuLight.params.y = 2.0f;
            gpuLight.params.z = slot.config.depthBias;
            gpuLight.params.w = light->getShadowNear();
            gpuLight.shadow.x = float(cubeIndex);
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

    //Submit via sync2. The two semaphores exist only to hand an image between the acquire
    //and the present; with no swapchain there is no handover, and the fence alone is what
    //tells the next frame this one has finished
    vk::SemaphoreSubmitInfo waitInfo;
    vk::SemaphoreSubmitInfo signalInfo;

    if(swapchain){
        waitInfo.semaphore = *imageAvailableSemaphores[currentFrame];
        waitInfo.stageMask = vk::PipelineStageFlagBits2KHR::eColorAttachmentOutput;

        signalInfo.semaphore = *renderFinishedSemaphores[currentImageIndex];
        signalInfo.stageMask = vk::PipelineStageFlagBits2KHR::eColorAttachmentOutput;
    }

    vk::CommandBufferSubmitInfo commandBufferInfo;
    commandBufferInfo.commandBuffer = *commandBuffer;

    vk::SubmitInfo2 submitInfo;
    submitInfo.waitSemaphoreInfoCount = swapchain ? 1 : 0;
    submitInfo.pWaitSemaphoreInfos = swapchain ? &waitInfo : nullptr;
    submitInfo.commandBufferInfoCount = 1;
    submitInfo.pCommandBufferInfos = &commandBufferInfo;
    submitInfo.signalSemaphoreInfoCount = swapchain ? 1 : 0;
    submitInfo.pSignalSemaphoreInfos = swapchain ? &signalInfo : nullptr;

    device.getGraphicsQueue().submit2(submitInfo, *fence);

    //Present the image
    if(swapchain){
        vk::Semaphore waitSemaphore = *renderFinishedSemaphores[currentImageIndex];
        vk::SwapchainKHR swapchainHandle = *swapchain->getSwapchain();

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
    }

    

    //Advance to the next frame
    currentFrame = (currentFrame + 1) % command.getCommandBuffers().size();
    frameActive = false;

    if(needsRecreate){
        recreateSwapchain();
    }
}

void VulkanRenderer::recreateSwapchain(){
    if(!swapchain){
        return; //nothing to resize: a headless frame is always the size it was asked for
    }

    swapchain->recreateSwapchain();
    if(depthImage){
        depthImage->recreate(swapchain->getExtent());
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

    if(!swapchain){
        throw std::runtime_error("beginPass: there is no window to draw into (this Loom was built headless) - render into a RenderTarget instead");
    }

    currentTarget = nullptr;
    passLight = nullptr;
    passUsesColor = true;
    startPass(swapchain->getImages()[currentImageIndex],
                *swapchain->getImageViews()[currentImageIndex],
                depthImage,
                swapchain->getExtent());

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
void VulkanRenderer::beginDepthPass(const RenderTarget& target){
    if(!frameActive){
        throw std::runtime_error("beginDepthPass: frame not started");
    }
    if(passActive){
        throw std::runtime_error("beginDepthPass: a pass is already active (missing endPass)");
    }
    if(!target.hasDepth()){
        throw std::runtime_error("beginDepthPass: this target has no depth to write into");
    }

    //Boja se ne prilaze cak i kad je meta ima. To je ono sto dopusta da isti cilj primi
    //prepass bez boje i glavni prolaz s njom, dijeleci istu dubinu
    currentTarget = &target;
    passLight = nullptr;
    passFace = 0;
    passUsesColor = false;

    startPass(vk::Image(nullptr), vk::ImageView(nullptr), target.getDepthImage(), target.getExtent());
}

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
    passFace = 0;
    passUsesColor = true;
    startPass(target.hasColor() ? *target.getColorImage().getImage() : vk::Image(nullptr),
                target.hasColor() ? *target.getColorImage().getImageView() : vk::ImageView(nullptr),
                target.getDepthImage(),
                target.getExtent());
}

//One of the six. A point light has no single view, so the caller walks the faces and the
//pass takes the matrices of whichever one it was handed
void VulkanRenderer::beginPass(const RenderTarget& target, const Light& light, uint32_t face){
    if(!frameActive){
        throw std::runtime_error("beginPass: frame not started");
    }
    if(passActive){
    throw std::runtime_error("beginPass: a pass is already active (missing endPass)");
    }
    if(!target.hasCubeDepth()){
        throw std::runtime_error("beginPass: a face can only be rendered into a cube target (makeShadowCubeConfig)");
    }
    if(face > 5){
        throw std::runtime_error("beginPass: a cube has six faces, numbered 0 to 5");
    }

    currentTarget = &target;
    passLight = &light;
    passFace = face;
    passUsesColor = true;
    startPass(target.hasColor() ? *target.getColorImage().getImage() : vk::Image(nullptr),
                target.hasColor() ? *target.getColorImage().getImageView() : vk::ImageView(nullptr),
                target.getDepthImage(),
                target.getExtent(),
                face);
}

void VulkanRenderer::setShadingRateMap(const ShadingRateMap& map){
    if(!device.hasShadingRateImage()){
        throw std::runtime_error("setShadingRateMap: this device cannot take the shading rate from an image (attachmentFragmentShadingRate)");
    }
    shadingRateMap = &map;
}

void VulkanRenderer::clearShadingRateMap(){
    shadingRateMap = nullptr;
}

void VulkanRenderer::updateShadingRateMap(const ShadingRateMap& map){
    if(!camera){
        throw std::runtime_error("updateShadingRateMap: no camera - a depth buffer is not a distance until the near and far planes turn it into one");
    }
    if(!map.hasDepthSource()){
        throw std::runtime_error("updateShadingRateMap: the map has no depth source (setDepthSource)");
    }

    const vk::Extent2D depthExtent = map.getDepthSource().getExtent();
    const ShadingRateDistances& distances = map.getDistances();

    ShadingRatePush push;
    push.rateExtent[0] = map.getExtent().width;
    push.rateExtent[1] = map.getExtent().height;
    push.texelSize[0] = map.getTexelSize().width;
    push.texelSize[1] = map.getTexelSize().height;
    push.depthSize[0] = static_cast<float>(depthExtent.width);
    push.depthSize[1] = static_cast<float>(depthExtent.height);
    push.nearPlane = camera->getConfig().nearPlane;
    push.farPlane = camera->getConfig().farPlane;
    push.quarterDistance = distances.quarter;
    push.sixteenthDistance = distances.sixteenth;

    dispatch(map.getComputeMaterial(), map.groupsX(), map.groupsY(), 1, &push, sizeof(push));
}

void VulkanRenderer::setShadingRate(const vk::raii::CommandBuffer& commandBuffer,
                                    ShadingRate rate, ShadingImportance importance) const{
    const ShadingRateExtent extent = shadingRateExtent(rate);

    //Tri izvora stope: pipeline (ovo), primitiv (ne koristi se) i slika. Prvi kombinator
    //spaja prva dva i ostaje eKeep; drugi spaja rezultat sa slikom.
    //
    //eMax znaci "grublje od dvoje pobjeduje", a to je smisleni default upravo zato sto je
    //materijalova zadana stopa Full: bez toga slika stope ne bi radila nista. Critical
    //materijal stavlja eKeep i time se ispisuje iz slike
    const bool imageDecides = shadingRateMap != nullptr && importance == ShadingImportance::Normal;

    const std::array<vk::FragmentShadingRateCombinerOpKHR,2> combiners = {
        vk::FragmentShadingRateCombinerOpKHR::eKeep,
        imageDecides ? vk::FragmentShadingRateCombinerOpKHR::eMax
                     : vk::FragmentShadingRateCombinerOpKHR::eKeep
    };

    commandBuffer.setFragmentShadingRateKHR(vk::Extent2D{extent.width, extent.height}, combiners.data());
}

void VulkanRenderer::bindMaterial(const Material& material) {
    const auto& commandBuffer = command.getCommandBuffers()[currentFrame];
    const VulkanGraphicsPipeline& pipeline = material.getPipeline();

    //Dynamic rendering requires the pipeline's attachment formats to match the pass's. A
    //target with depth drawn by a pipeline that never declared any is a VUID at draw time
    //and a picture that mostly looks right - which is the worst kind of wrong. A sentence
    //here names the two formats instead
    const vk::Format passDepth = currentTarget ? currentTarget->getDepthFormat()
                                               : (depthImage ? depthImage->getFormat() : vk::Format::eUndefined);

    if(pipeline.depthFormat != passDepth){
        throw std::runtime_error("draw: this pass has depth format " + vk::to_string(passDepth) +
            " but the material's pipeline was built for " + vk::to_string(pipeline.depthFormat) +
            " - a pass and the pipelines drawing into it have to agree on their attachments");
    }

    if(boundPipeline != &pipeline){
        commandBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, *pipeline.getPipeline());
        boundPipeline = &pipeline;
    }

    //Po draw pozivu, jer je to razina na kojoj je odluka o vaznosti smislena
    if(device.hasFragmentShadingRate()){
        setShadingRate(commandBuffer, material.getShadingRate(), material.getImportance());
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
    if(!swapchain){
        throw std::runtime_error("readLastFrame: there is no window to read (this Loom was built headless) - read a RenderTarget instead");
    }
    if(!swapchain->canReadback()){
        throw std::runtime_error("readLastFrame: the swapchain was not created for readback (SwapchainConfig::allowReadback, or the surface does not support it)");
    }

    device.getDevice().waitIdle();

    const vk::Image image = swapchain->getImages()[currentImageIndex];
    const vk::Extent2D extent = swapchain->getExtent();
    const vk::Format format = swapchain->getSurfaceFormat().format;
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
