// v7e: the shadow box fits itself to the camera instead of being placed by hand, and a
//      point light casts in every direction through a cube map
#include "TestHarness.h"
#include "TestScene.h"
#include "Core/LoomConfig.h"
#include "Core/Camera.h"
#include "Core/Light.h"
#include "Core/LoomShapes.h"
#include "Vulkan/Material.h"
#include "Vulkan/RenderTarget.h"
#include <glm/gtc/matrix_transform.hpp>

static glm::vec2 projectToPixels(const glm::mat4& viewProjection, const glm::vec3& world, vk::Extent2D extent){
    const glm::vec4 clip = viewProjection * glm::vec4(world, 1.0f);
    const glm::vec3 ndc = glm::vec3(clip) / clip.w;
    return {(ndc.x * 0.5f + 0.5f) * float(extent.width),
            (ndc.y * 0.5f + 0.5f) * float(extent.height)};
}

static size_t centresInside(glm::vec2 a, glm::vec2 b, vk::Extent2D extent){
    const float x0 = std::min(a.x, b.x), x1 = std::max(a.x, b.x);
    const float y0 = std::min(a.y, b.y), y1 = std::max(a.y, b.y);

    size_t count = 0;
    for(uint32_t y = 0; y < extent.height; ++y){
        const float cy = float(y) + 0.5f;
        if(cy <= y0 || cy >= y1) continue;
        for(uint32_t x = 0; x < extent.width; ++x){
            const float cx = float(x) + 0.5f;
            if(cx > x0 && cx < x1) ++count;
        }
    }
    return count;
}

static uint8_t channelAt(const std::vector<uint8_t>& pixels, vk::Extent2D extent, uint32_t x, uint32_t y){
    return pixels[(size_t(y) * extent.width + x) * 4];
}

static size_t countEqual(const std::vector<uint8_t>& pixels, vk::Extent2D extent, uint8_t value){
    size_t count = 0;
    for(uint32_t y = 0; y < extent.height; ++y){
        for(uint32_t x = 0; x < extent.width; ++x){
            if(channelAt(pixels, extent, x, y) == value) ++count;
        }
    }
    return count;
}

int main(){
    TestReport report("v7e fit and point shadow");

    const vk::Extent2D size{512,512};
    const vk::Extent2D mapSize{1024,1024};

    LoomConfig config;
    config.width = 512; config.height = 512;
    config.appName = "v7e"; config.engineName = "Loom tests";
    config.enableDepth = true;
    config.pipelineConfig.cullMode = vk::CullModeFlagBits::eNone;
    config.pipelineConfig.depthTestEnable = true;
    config.pipelineConfig.depthWriteEnable = true;
    config.rendererConfig.maxPassesPerFrame = 16; //six cube faces plus the camera and the window

    LoomInitializer loom(config);
    LoomShapes::Primitives shapes(loom);

    // ===============================================================================
    // Dio 1: kutija se sama pripasa kameri
    // ===============================================================================

    CameraConfig fitCamConfig;
    fitCamConfig.position = {0.0f, 2.0f, 6.0f};
    fitCamConfig.target = {0.0f, 0.0f, 0.0f};
    Camera fitCamera(fitCamConfig);
    loom.renderer.setCamera(fitCamera);

    LightConfig sunConfig;
    sunConfig.type = LightType::Directional;
    sunConfig.direction = {-0.4f, -1.0f, -0.3f};
    sunConfig.shadowExtent = 50.0f;   //deliberately far too big: fitting has to beat this
    Light sun(sunConfig);

    RenderTarget sunMap(loom.device, mapSize, makeShadowMapConfig());

    ShadowConfig fitted;
    fitted.fitToCamera = true;
    fitted.distance = 20.0f;
    loom.renderer.setShadowMap(sunMap, sun, fitted);

    const LightMatrices atOrigin = loom.renderer.getShadowMatrices();

    //An orthographic projection's first element is one over its half width, so the box the
    //fit chose can be read straight back out of the matrix it produced
    auto radiusOf = [](const LightMatrices& matrices){
        return 1.0f / matrices.projection[0][0];
    };

    report.check("kutija je tijesna", radiusOf(atOrigin) < sunConfig.shadowExtent,
        fmt("pripasana na %.3f, rucno postavljena je bila %.1f",
            double(radiusOf(atOrigin)), double(sunConfig.shadowExtent)));

    //Every corner of the slice the fit was asked to cover has to land inside the box. A box
    //that misses a corner is a corner with no shadows in it
    const glm::mat4 cameraProjection = fitCamera.getProjection(size.width, size.height);
    const glm::mat4 inverseCamera = glm::inverse(cameraProjection * fitCamera.getView());
    const float farNdcZ = (cameraProjection[2][2] * (-fitted.distance) + cameraProjection[3][2]) / fitted.distance;

    size_t outside = 0;
    for(float z : {0.0f, farNdcZ}){
        for(float y : {-1.0f, 1.0f}){
            for(float x : {-1.0f, 1.0f}){
                const glm::vec4 point = inverseCamera * glm::vec4(x, y, z, 1.0f);
                const glm::vec4 inLight = atOrigin.viewProjection * (glm::vec4(glm::vec3(point) / point.w, 1.0f));
                //A texel of slack: the box was rounded up to a whole texel and then snapped
                //to the texel grid, so a corner may sit a fraction of a texel past the edge
                const float slack = 2.0f / float(mapSize.width);
                if(std::abs(inLight.x) > 1.0f + slack || std::abs(inLight.y) > 1.0f + slack ||
                   inLight.z < -slack || inLight.z > 1.0f + slack){
                    ++outside;
                }
            }
        }
    }
    report.check("frustum stane", outside == 0, fmt("%zu od 8 uglova izvan kutije", outside));

    //Turn the camera and the box must not change size. An axis aligned box of the same
    //corners would breathe with every degree, and a shadow map that changes scale makes
    //every edge in the scene crawl
    float worstRadiusDrift = 0.0f;
    for(int degrees = 0; degrees < 360; degrees += 15){
        const float angle = glm::radians(float(degrees));
        fitCamera.setPosition({6.0f * std::sin(angle), 2.0f, 6.0f * std::cos(angle)});
        fitCamera.lookAt({0.0f, 0.0f, 0.0f});
        loom.renderer.setShadowMap(sunMap, sun, fitted);

        const float drift = std::abs(radiusOf(loom.renderer.getShadowMatrices()) - radiusOf(atOrigin));
        worstRadiusDrift = std::max(worstRadiusDrift, drift);
    }
    report.check("velicina ne titra", worstRadiusDrift < 1e-3f,
        fmt("kroz 24 kuta kamere, najveca promjena polumjera %.3e", double(worstRadiusDrift)));

    //And the box has to land on whole texels. Walk the camera by amounts that are nothing
    //like a texel and the snapped origin still has to come out whole
    float worstSnap = 0.0f;
    for(int step = 0; step < 20; ++step){
        fitCamera.setPosition({0.013f * float(step), 2.0f, 6.0f + 0.007f * float(step)});
        fitCamera.lookAt({0.0f, 0.0f, 0.0f});
        loom.renderer.setShadowMap(sunMap, sun, fitted);

        glm::vec4 origin = loom.renderer.getShadowMatrices().viewProjection * glm::vec4(0.0f,0.0f,0.0f,1.0f);
        origin *= float(mapSize.width) * 0.5f;

        worstSnap = std::max(worstSnap, std::abs(origin.x - std::round(origin.x)));
        worstSnap = std::max(worstSnap, std::abs(origin.y - std::round(origin.y)));
    }
    report.check("snapping na teksel", worstSnap < 1e-2f,
        fmt("kroz 20 pomaka kamere, najdalje od cijelog teksela %.3e", double(worstSnap)));

    //A shorter shadow distance has to buy a smaller box, or the setting means nothing
    ShadowConfig closer = fitted;
    closer.distance = 5.0f;
    fitCamera.setPosition({0.0f, 2.0f, 6.0f});
    fitCamera.lookAt({0.0f, 0.0f, 0.0f});
    loom.renderer.setShadowMap(sunMap, sun, closer);
    const float closeRadius = radiusOf(loom.renderer.getShadowMatrices());

    loom.renderer.setShadowMap(sunMap, sun, fitted);
    const float farRadius = radiusOf(loom.renderer.getShadowMatrices());

    report.check("udaljenost odlucuje", closeRadius < farRadius,
        fmt("na 5 jedinica polumjer %.3f, na 20 jedinica %.3f", double(closeRadius), double(farRadius)));

    // ===============================================================================
    // Dio 2: tockasto svjetlo i kocka od sest lica
    // ===============================================================================

    loom.renderer.clearShadowMap();
    loom.renderer.clearLights();

    const float ambient = 0.2f;
    EnvironmentConfig envConfig; envConfig.ambientColor = {ambient, ambient, ambient};
    Environment environment(envConfig);
    loom.renderer.setEnvironment(environment);

    //Straight above the origin, looking down at the floor
    CameraConfig camConfig;
    camConfig.position = {0.0f, 6.0f, 0.0f};
    camConfig.target = {0.0f, 0.0f, 0.0f};
    camConfig.up = {0.0f, 0.0f, 1.0f};
    Camera camera(camConfig);
    loom.renderer.setCamera(camera);

    const float lightHeight = 3.0f;
    LightConfig bulbConfig;
    bulbConfig.type = LightType::Point;
    bulbConfig.position = {0.0f, lightHeight, 0.0f};
    bulbConfig.range = 20.0f;
    bulbConfig.intensity = 3.0f;
    bulbConfig.shadowNear = 0.1f;
    Light bulb(bulbConfig);
    loom.renderer.addLight(bulb);

    RenderTarget cubeMap(loom.device, mapSize, makeShadowCubeConfig());

    PipelineConfig shadowPipelineConfig;
    shadowPipelineConfig.vertShaderPath = std::string(LOOM_SHADER_DIR) + "/shadow.vert.spv";
    shadowPipelineConfig.fragShaderPath = "";
    shadowPipelineConfig.enableColor = false;
    shadowPipelineConfig.vertexAttributes = Vertex::getPositionAttribute();
    shadowPipelineConfig.cullMode = vk::CullModeFlagBits::eNone;
    shadowPipelineConfig.depthTestEnable = true;
    shadowPipelineConfig.depthWriteEnable = true;

    VulkanGraphicsPipeline shadowPipeline(loom.device, loom.swapchain,
        shadowPipelineConfig, cubeMap.getDepthFormat());
    Material shadowMaterial(shadowPipeline);

    RenderTargetConfig readConfig;
    readConfig.finalLayout = vk::ImageLayout::eTransferSrcOptimal;
    readConfig.extraColorUsage = vk::ImageUsageFlagBits::eTransferSrc;

    RenderTarget outShadowed(loom.device, size, readConfig);
    RenderTarget outNoCube(loom.device, size, readConfig);

    //A blocker one unit up, and a floor for its shadow to fall on. The blocker is drawn only
    //into the cube map, so the camera sees a bare floor with a shadow on it
    const glm::mat4 blockerModel = glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 1.0f, 0.0f));
    const glm::mat4 floorModel = glm::scale(glm::mat4(1.0f), glm::vec3(6.0f, 1.0f, 6.0f));

    auto render = [&](bool bindCube, RenderTarget& out){
        if(bindCube){
            ShadowConfig cubeShadow;
            cubeShadow.depthBias = 0.002f;
            loom.renderer.setShadowCube(cubeMap, bulb, cubeShadow);
        }
        else{
            loom.renderer.clearShadowCube();
        }

        int drawn = 0;
        while(drawn < 3 && !loom.window.shouldClose()){
            loom.window.pollEvents();
            if(!loom.renderer.beginFrame()) continue;

            if(bindCube){
                for(uint32_t face = 0; face < 6; ++face){
                    loom.renderer.beginPass(cubeMap, bulb, face);
                    shapes.cube(shadowMaterial, blockerModel);
                    loom.renderer.endPass();
                }
            }

            loom.renderer.beginPass(out);
            shapes.plane(floorModel);
            loom.renderer.endPass();

            loom.renderer.beginPass();
            shapes.plane(floorModel);
            loom.renderer.endPass();

            loom.renderer.endFrame();
            ++drawn;
        }
        loom.waitIdle();
    };

    render(true, outShadowed);
    render(false, outNoCube);

    const uint8_t ambientByte = encodeByte(ambient);

    const std::vector<uint8_t> shadowed = outShadowed.readPixels(loom.command).pixels;
    const std::vector<uint8_t> noCube = outNoCube.readPixels(loom.command).pixels;

    const size_t shadowedCount = countEqual(shadowed, size, ambientByte);
    const size_t noCubeCount = countEqual(noCube, size, ambientByte);

    //The blocker's silhouette from a light directly above it is its top face, and the top
    //face is the corner that throws furthest: a half unit wide at height 1.5, under a light
    //at 3, projects to a full unit on the floor
    const float scale = lightHeight / (lightHeight - 1.5f);
    const float half = 0.5f * scale;

    const glm::mat4 cameraViewProjection = camera.getProjection(size.width, size.height) * camera.getView();
    const glm::vec2 cornerA = projectToPixels(cameraViewProjection, {-half, 0.0f, -half}, size);
    const glm::vec2 cornerB = projectToPixels(cameraViewProjection, { half, 0.0f,  half}, size);
    const size_t footprint = centresInside(cornerA, cornerB, size);

    report.check("kocka baca sjenu", shadowedCount > 0,
        fmt("%zu piksela tocno na ambijentu", shadowedCount));

    //Within the filtered edge of the analytic footprint. A point light's shadow edge is
    //softer than a directional one's, so this is a band rather than an exact count
    const double ratio = footprint > 0 ? double(shadowedCount) / double(footprint) : 0.0;
    report.check("otisak", ratio > 0.8 && ratio < 1.2,
        fmt("%zu piksela u sjeni, analiticki otisak %zu, omjer %.3f", shadowedCount, footprint, ratio));

    report.check("bez kocke nema sjene", noCubeCount == 0,
        fmt("%zu piksela u sjeni bez vezane kocke", noCubeCount));

    // -------------------------------------------------------------------------------
    // Dubina u licu kocke je ono sto formula kaze
    // -------------------------------------------------------------------------------

    //A floor point off to the side, outside the shadow. Its longest axis is Y, so it belongs
    //to face 3, the one looking down
    const glm::vec3 probe{2.0f, 0.0f, 0.0f};
    const glm::vec3 toProbe = probe - bulbConfig.position;
    const float axis = std::max(std::abs(toProbe.x), std::max(std::abs(toProbe.y), std::abs(toProbe.z)));
    const float near = bulbConfig.shadowNear;
    const float far = bulbConfig.range;
    const float expectedDepth = (far * (axis - near)) / ((far - near) * axis);

    const ImageData downFace = cubeMap.readDepthPixels(loom.command, 3);
    const glm::vec2 probePixel = projectToPixels(bulb.getCubeViewProjection(3), probe, mapSize);

    //The floor is not in the cube map - only the blocker is - so this texel holds the cleared
    //1.0. What is being checked is the arithmetic, so the probe is projected instead: the
    //formula the shader uses has to agree with the matrix the face was rendered with
    const glm::vec4 clip = bulb.getCubeViewProjection(3) * glm::vec4(probe, 1.0f);
    const float matrixDepth = clip.z / clip.w;

    report.check("formula dubine", std::abs(matrixDepth - expectedDepth) < 1e-5f,
        fmt("matrica daje %.7f, formula iz shadera %.7f", double(matrixDepth), double(expectedDepth)));

    report.check("lice je nacrtano",
        probePixel.x >= 0.0f && probePixel.x < float(mapSize.width) &&
        downFace.extent.width == mapSize.width,
        fmt("lice 3 procitano, %u x %u", downFace.extent.width, downFace.extent.height));

    //The blocker sits directly under the light, so the face looking down must hold something
    //nearer than the clear value
    size_t writtenInFace = 0;
    for(uint32_t y = 0; y < mapSize.height; y += 4){
        for(uint32_t x = 0; x < mapSize.width; x += 4){
            if(depthAt(downFace, x, y) < 1.0f) ++writtenInFace;
        }
    }
    report.check("lice ima sadrzaj", writtenInFace > 0,
        fmt("%zu uzoraka lica 3 blize od praznog", writtenInFace));

    // -------------------------------------------------------------------------------
    // Granice
    // -------------------------------------------------------------------------------

    bool flatTargetThrew = false;
    try{
        RenderTarget flat(loom.device, size, makeShadowMapConfig());
        loom.renderer.setShadowCube(flat, bulb);
    }
    catch(const std::exception&){
        flatTargetThrew = true;
    }
    report.check("kocka od ravne karte", flatTargetThrew, "baca iznimku");

    bool directionalCubeThrew = false;
    try{
        loom.renderer.setShadowCube(cubeMap, sun);
    }
    catch(const std::exception&){
        directionalCubeThrew = true;
    }
    report.check("kocka za usmjereno", directionalCubeThrew, "baca iznimku");

    bool seventhFaceThrew = false;
    try{
        if(loom.renderer.beginFrame()){
            loom.renderer.beginPass(cubeMap, bulb, 6);
            loom.renderer.endPass();
            loom.renderer.endFrame();
        }
    }
    catch(const std::exception&){
        seventhFaceThrew = true;
        loom.waitIdle();
    }
    report.check("sedmo lice", seventhFaceThrew, "baca iznimku");

    bool pointFitThrew = false;
    try{
        bulb.fitToCamera(camera, size.width, size.height, mapSize.width, 20.0f);
    }
    catch(const std::exception&){
        pointFitThrew = true;
    }
    report.check("fit za tockasto", pointFitThrew, "baca iznimku");

    report.checkNoValidationMessages();
    return report.result();
}
