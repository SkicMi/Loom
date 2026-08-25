//=============================================================================================
//  Stepenica 1, implementacija.
//
//  Ovaj file smije vidjeti sve: Loomove configs, Vulkan, Spool. Header koji ga objavljuje ne
//  smije vidjeti nista od toga, i to je jedina razlika izmedu njih.
//
//  Preset je aplikacijska razina i jedino mjesto koje zna i za Loom i za Spool - zato je
//  LoomPreset zaseban target, a ne dio Looma. Da je unutra, Loom bi ovisio o Spoolu.
//=============================================================================================
#include "Loom.h"
#include "Preset_Advanced.h"

#include "Core/LoomConfig.h"
#include "Core/LoomInitializer.h"
#include "Core/LoomShapes.h"
#include "Vulkan/Material.h"
#include "Vulkan/RenderTarget.h"
#include "Vulkan/Texture.h"
#include "Spool/ImageFile.h"
#include "Spool/Sequence.h"

#include <glm/gtc/matrix_transform.hpp>
#include <memory>
#include <stdexcept>
#include <vector>

namespace Loom{

// ---------------------------------------------------------------------------------------
// Transform
// ---------------------------------------------------------------------------------------

Transform& Transform::at(float x, float y, float z){ position = {x,y,z}; return *this; }
Transform& Transform::at(const glm::vec3& newPosition){ position = newPosition; return *this; }
Transform& Transform::scaled(float uniform){ scale = glm::vec3(uniform); return *this; }
Transform& Transform::scaled(const glm::vec3& factors){ scale = factors; return *this; }

Transform& Transform::spun(float radians, const glm::vec3& newAxis){
    angle = radians;
    axis = newAxis;
    return *this;
}

glm::mat4 Transform::matrix() const{
    glm::mat4 out = glm::translate(glm::mat4(1.0f), position);
    if(angle != 0.0f){
        out = glm::rotate(out, angle, axis);
    }
    return glm::scale(out, scale);
}

// ---------------------------------------------------------------------------------------
// Scene
// ---------------------------------------------------------------------------------------

namespace{

enum class Shape : uint8_t { Plane, Cube, Sphere, Pyramid, Sprite };

//Everything the preset decides, in one place, so that the tier 2 program a test compares
//against can be written from reading this and nothing else
LoomConfig presetConfig(Preset preset){
    LoomConfig config;
    config.appName = "Loom";
    config.engineName = "Loom";
    config.width = 1280;
    config.height = 720;

    //The window is readable so that readPixels works the same way in every preset. It costs
    //one usage flag and only when the surface supports it
    config.swapchainConfig.allowReadback = true;

    switch(preset){
        case Preset::Lit3D:
            config.enableDepth = true;
            config.pipelineConfig.depthTestEnable = true;
            config.pipelineConfig.depthWriteEnable = true;
            config.pipelineConfig.cullMode = vk::CullModeFlagBits::eBack;
            config.rendererConfig.clearColor = {0.02f, 0.02f, 0.04f, 1.0f};
            //A directional map, six cube faces and the camera pass is eight already, so this
            //leaves room rather than sitting exactly on the ceiling
            config.rendererConfig.maxPassesPerFrame = 16;
            break;

        case Preset::Flat2D:
            //No depth at all: order of drawing is what decides what sits on top, which is
            //how every 2D system works and what a 2D user expects
            config.enableDepth = false;
            config.pipelineConfig.depthTestEnable = false;
            config.pipelineConfig.depthWriteEnable = false;
            config.pipelineConfig.cullMode = vk::CullModeFlagBits::eNone;
            config.rendererConfig.clearColor = {0.05f, 0.05f, 0.08f, 1.0f};
            break;

        case Preset::Offscreen:
            config.headless = true;
            config.enableDepth = true;
            config.pipelineConfig.depthTestEnable = true;
            config.pipelineConfig.depthWriteEnable = true;
            config.pipelineConfig.cullMode = vk::CullModeFlagBits::eBack;
            config.rendererConfig.clearColor = {0.02f, 0.02f, 0.04f, 1.0f};
            config.rendererConfig.maxPassesPerFrame = 16;
            //Pinned rather than inherited from a swapchain that does not exist
            config.headlessColorFormat = vk::Format::eB8G8R8A8Srgb;
            config.pipelineConfig.colorFormat = vk::Format::eB8G8R8A8Srgb;
            break;
    }

    return config;
}

//What the preset points the camera and the sun at when nobody said otherwise
CameraConfig presetCamera(Preset preset){
    CameraConfig camera;
    if(preset == Preset::Flat2D){
        //Looking straight down the axis at a unit square, so a sprite at scale 1 fills a
        //predictable part of the view
        camera.position = {0.0f, 0.0f, 2.0f};
        camera.target = {0.0f, 0.0f, 0.0f};
        return camera;
    }

    camera.position = {3.5f, 2.5f, 4.5f};
    camera.target = {0.0f, 0.5f, 0.0f};
    return camera;
}

LightConfig presetSun(){
    LightConfig sun;
    sun.type = LightType::Directional;
    sun.direction = {-0.45f, -1.0f, -0.35f};
    sun.color = {1.0f, 0.96f, 0.88f};
    sun.intensity = 1.0f;
    return sun;
}

EnvironmentConfig presetEnvironment(Preset preset){
    EnvironmentConfig environment;
    //2D has no lighting to speak of, so the ambient carries the whole picture
    environment.ambientColor = (preset == Preset::Flat2D) ? glm::vec3(1.0f)
                                                          : glm::vec3(0.10f, 0.11f, 0.14f);
    return environment;
}

constexpr uint32_t shadowMapSize = 2048;

//A cube is six faces, so the same number of texels costs six times as much. Half the width
//of the directional map is the usual trade and it is barely visible: a point light's shadow
//is nearer the thing casting it
constexpr uint32_t shadowCubeSize = 1024;

ShadowConfig presetShadow(){
    ShadowConfig shadow;
    shadow.fitToCamera = true;
    shadow.distance = 14.0f;
    shadow.depthBias = 0.0015f;
    return shadow;
}

}

struct Scene::State{
    Preset preset;
    LoomConfig config;
    bool built = false;
    bool shadowsWanted = true;

    std::unique_ptr<LoomInitializer> loom;
    std::unique_ptr<LoomShapes::Primitives> shapes;

    //Only the Offscreen preset draws into a target of its own; the others draw to the window
    std::unique_ptr<RenderTarget> offscreen;

    //One directional map and one cube. That ceiling is the renderer's, not the preset's:
    //set 0 has one binding for each kind, so a second of either would need an array of
    //descriptors. Asked for a second, this says so rather than silently ignoring it
    std::unique_ptr<RenderTarget> shadowMap;
    const Light* shadowLight = nullptr;

    std::unique_ptr<RenderTarget> shadowCube;
    const Light* shadowCubeLight = nullptr;

    std::unique_ptr<VulkanGraphicsPipeline> shadowPipeline;
    std::unique_ptr<Material> shadowMaterial;

    //Owned, so that nothing the renderer points at can go out of scope before it does
    std::vector<std::unique_ptr<Light>> extraLights;

    void ensureShadowPipeline(vk::Format depthFormat);

    //unique_ptr rather than Texture by value: a Material holds a view of its texture, and a
    //vector that reallocates would move the object it points at
    std::vector<std::unique_ptr<Texture>> textures;

    Camera camera;
    Light sun;
    Environment environment;

    struct Draw{
        Shape shape = Shape::Cube;
        uint32_t texture = 0;
        glm::mat4 transform{1.0f};
    };

    //Draws are queued rather than issued, because the same list has to be replayed into the
    //shadow pass and then into the camera pass, and the caller only writes it once
    std::vector<Draw> queue;

    void enqueue(Shape shape, TextureHandle texture, const glm::mat4& transform);

    bool frameOpen = false;
    bool frameAcquired = false;
    uint32_t frameIndex = 0;
    float explicitTime = 0.0f;
    bool timeIsExplicit = false;

    void build();
    void teardown();
    void replay(const Material* material);
};

void Scene::State::build(){
    if(built) return;

    loom = std::make_unique<LoomInitializer>(config);

    //Primitives builds its own textured pipeline, so it has to be handed the same settings
    //the rest of the scene was built with. Without this an override of pipelineConfig lands
    //in the config, reads back correctly, and then changes nothing at all on screen - which
    //is worse than not being overridable, because it looks like it worked
    LoomShapes::PrimitivesConfig primitivesConfig;
    primitivesConfig.cullMode = config.pipelineConfig.cullMode;
    primitivesConfig.depthTest = config.pipelineConfig.depthTestEnable;

    shapes = std::make_unique<LoomShapes::Primitives>(*loom, primitivesConfig);

    camera = Camera(presetCamera(preset));
    sun = Light(presetSun());
    environment = Environment(presetEnvironment(preset));

    loom->renderer.setCamera(camera);
    loom->renderer.setEnvironment(environment);
    if(preset != Preset::Flat2D){
        loom->renderer.addLight(sun);
    }

    if(preset == Preset::Offscreen){
        RenderTargetConfig targetConfig;
        targetConfig.finalLayout = vk::ImageLayout::eTransferSrcOptimal;
        targetConfig.extraColorUsage = vk::ImageUsageFlagBits::eTransferSrc;
        targetConfig.enableDepth = config.enableDepth;
        offscreen = std::make_unique<RenderTarget>(loom->device,
            vk::Extent2D{config.width, config.height}, targetConfig);
    }

    if(shadowsWanted && preset != Preset::Flat2D){
        shadowMap = std::make_unique<RenderTarget>(loom->device,
            vk::Extent2D{shadowMapSize, shadowMapSize}, makeShadowMapConfig());
        shadowLight = &sun;

        ensureShadowPipeline(shadowMap->getDepthFormat());

        ShadowConfig shadow = presetShadow();
        //Headless has no window to take an aspect ratio from, and a fit to the wrong one puts
        //the shadows in the wrong place
        shadow.viewportWidth = config.width;
        shadow.viewportHeight = config.height;
        loom->renderer.setShadowMap(*shadowMap, sun, shadow);
    }

    built = true;
}

void Scene::State::ensureShadowPipeline(vk::Format depthFormat){
    //One pipeline serves both the flat map and the cube: every depth target here comes out of
    //makeDepthConfig, so they all agree on the format
    if(shadowPipeline) return;

    PipelineConfig shadowPipelineConfig;
    shadowPipelineConfig.vertShaderPath = std::string(LOOM_SHADER_DIR) + "/shadow.vert.spv";
    shadowPipelineConfig.fragShaderPath = "";
    shadowPipelineConfig.enableColor = false;
    shadowPipelineConfig.vertexAttributes = Vertex::getPositionAttribute();
    shadowPipelineConfig.depthTestEnable = true;
    shadowPipelineConfig.depthWriteEnable = true;
    //Back faces into the map: a whole object's thickness of margin, which is more than any
    //bias could buy
    shadowPipelineConfig.cullMode = vk::CullModeFlagBits::eFront;

    shadowPipeline = std::make_unique<VulkanGraphicsPipeline>(loom->device,
        shadowPipelineConfig, loom->getColorFormat(), depthFormat);
    shadowMaterial = std::make_unique<Material>(*shadowPipeline);
}

void Scene::State::teardown(){
    //By hand and in this order: everything below holds buffers and images allocated out of
    //the Loom's device, so the Loom is the last thing to go
    shadowMaterial.reset();
    shadowPipeline.reset();
    shadowCube.reset();
    shadowMap.reset();
    extraLights.clear();
    offscreen.reset();
    textures.clear();
    shapes.reset();
    loom.reset();
}

void Scene::State::replay(const Material* material){
    for(const Draw& draw : queue){
        //A shadow pass draws with the depth only material; the camera pass draws with each
        //shape's own texture
        const Material* chosen = material;

        if(chosen != nullptr){
            switch(draw.shape){
                case Shape::Plane:   shapes->plane(*chosen, draw.transform); break;
                case Shape::Cube:    shapes->cube(*chosen, draw.transform); break;
                case Shape::Sphere:  shapes->sphere(*chosen, draw.transform); break;
                case Shape::Pyramid: shapes->pyramid(*chosen, draw.transform); break;
                case Shape::Sprite:  shapes->plane(*chosen, draw.transform); break;
            }
            continue;
        }

        const Texture& texture = *textures[draw.texture - 1];
        switch(draw.shape){
            case Shape::Plane:   shapes->plane(texture, draw.transform); break;
            case Shape::Cube:    shapes->cube(texture, draw.transform); break;
            case Shape::Sphere:  shapes->sphere(texture, draw.transform); break;
            case Shape::Pyramid: shapes->pyramid(texture, draw.transform); break;
            //A sprite is a plane turned to face the camera. Flat2D has no depth, so this is
            //the whole of what makes it two dimensional
            case Shape::Sprite:  shapes->plane(texture,
                draw.transform * glm::rotate(glm::mat4(1.0f), glm::radians(90.0f), glm::vec3(1,0,0))); break;
        }
    }
}

Scene::Scene(Preset preset) : state(new State()){
    state->preset = preset;
    state->config = presetConfig(preset);
}

Scene::Scene(Preset preset, const ConfigOverride& override) : Scene(preset){
    //Runs on the config the preset just filled, before anything has been built from it.
    //Everything the override does not touch keeps what the preset chose - that is what makes
    //the two styles combine rather than replace each other
    override(state->config);
}

Scene::~Scene(){
    if(state){
        state->teardown();
        delete state;
        state = nullptr;
    }
}

void Scene::setTitle(const std::string& title){
    if(state->built) throw std::runtime_error("Loom::Scene: setTitle has to be called before the first frame");
    state->config.appName = title;
}

void Scene::setSize(uint32_t width, uint32_t height){
    if(state->built) throw std::runtime_error("Loom::Scene: setSize has to be called before the first frame");
    if(width == 0 || height == 0) throw std::runtime_error("Loom::Scene: a scene cannot be zero pixels across");
    state->config.width = width;
    state->config.height = height;
}

void Scene::setClearColor(const glm::vec4& color){
    if(state->built) throw std::runtime_error("Loom::Scene: setClearColor has to be called before the first frame");
    state->config.rendererConfig.clearColor = {color.r, color.g, color.b, color.a};
}

void Scene::setShadows(bool enabled){
    if(state->built) throw std::runtime_error("Loom::Scene: setShadows has to be called before the first frame");
    state->shadowsWanted = enabled;
}

bool Scene::isRunning(){
    state->build();
    state->loom->pollEvents();
    return !state->loom->shouldClose();
}

float Scene::time() const{
    if(state->timeIsExplicit) return state->explicitTime;
    return state->built ? static_cast<float>(state->loom->getTime()) : 0.0f;
}

void Scene::setFrame(uint32_t frame, float framesPerSecond){
    if(framesPerSecond <= 0.0f) throw std::runtime_error("Loom::Scene: frames per second has to be above zero");
    state->frameIndex = frame;
    state->explicitTime = float(frame) / framesPerSecond;
    state->timeIsExplicit = true;
}

uint32_t Scene::frame() const{
    return state->frameIndex;
}

TextureHandle Scene::loadTexture(const std::string& path){
    state->build();

    //Spool decodes, Loom uploads. Neither knows the other exists; this line is the only
    //place they meet, and it is why the preset is a target of its own
    const Spool::Image image = Spool::loadImage(path);

    state->textures.push_back(std::make_unique<Texture>(state->loom->device, state->loom->command,
        image.pixels.data(), vk::Extent2D{image.width, image.height}));

    //Handles count from one so that a default constructed handle is invalid rather than
    //pointing at whatever was loaded first
    return TextureHandle{static_cast<uint32_t>(state->textures.size())};
}

TextureHandle Scene::createTexture(const void* pixels, uint32_t width, uint32_t height){
    state->build();

    if(pixels == nullptr) throw std::runtime_error("Loom::Scene: createTexture was given no pixels");
    if(width == 0 || height == 0) throw std::runtime_error("Loom::Scene: createTexture was given an image with no size");

    state->textures.push_back(std::make_unique<Texture>(state->loom->device, state->loom->command,
        pixels, vk::Extent2D{width, height}));

    return TextureHandle{static_cast<uint32_t>(state->textures.size())};
}

void Scene::startRendering(){
    state->build();

    if(state->frameOpen) throw std::runtime_error("Loom::Scene: startRendering was called twice (missing endRendering)");

    state->queue.clear();
    state->frameOpen = true;
    state->frameAcquired = state->loom->renderer.beginFrame();
}

void Scene::endRendering(){
    if(!state->frameOpen) throw std::runtime_error("Loom::Scene: endRendering without startRendering");
    state->frameOpen = false;

    //beginFrame refuses a frame whose swapchain went out of date. Everything queued is
    //dropped rather than drawn into an image that is about to be thrown away
    if(!state->frameAcquired) return;

    VulkanRenderer& renderer = state->loom->renderer;

    if(state->shadowMap && state->shadowLight){
        renderer.beginPass(*state->shadowMap, *state->shadowLight);
        state->replay(state->shadowMaterial.get());
        renderer.endPass();
    }

    //Six faces for a point light, and the same queue replayed into each. This is exactly why
    //the draws are queued rather than issued: nobody outside could have driven these passes
    if(state->shadowCube && state->shadowCubeLight){
        for(uint32_t face = 0; face < 6; ++face){
            renderer.beginPass(*state->shadowCube, *state->shadowCubeLight, face);
            state->replay(state->shadowMaterial.get());
            renderer.endPass();
        }
    }

    if(state->offscreen){
        renderer.beginPass(*state->offscreen);
        state->replay(nullptr);
        renderer.endPass();
    }
    else{
        renderer.beginPass();
        state->replay(nullptr);
        renderer.endPass();
    }

    renderer.endFrame();
}

void Scene::State::enqueue(Shape shape, TextureHandle texture, const glm::mat4& transform){
    if(!frameOpen){
        throw std::runtime_error("Loom::Scene: draw outside a frame - it belongs between startRendering and endRendering");
    }
    if(!texture.isValid() || texture.id > textures.size()){
        throw std::runtime_error("Loom::Scene: that texture handle was never handed out by loadTexture");
    }

    queue.push_back({shape, texture.id, transform});
}

void Scene::drawPlane(TextureHandle texture, const glm::mat4& transform){ state->enqueue(Shape::Plane, texture, transform); }
void Scene::drawCube(TextureHandle texture, const glm::mat4& transform){ state->enqueue(Shape::Cube, texture, transform); }
void Scene::drawSphere(TextureHandle texture, const glm::mat4& transform){ state->enqueue(Shape::Sphere, texture, transform); }
void Scene::drawPyramid(TextureHandle texture, const glm::mat4& transform){ state->enqueue(Shape::Pyramid, texture, transform); }
void Scene::drawSprite(TextureHandle texture, const glm::mat4& transform){ state->enqueue(Shape::Sprite, texture, transform); }

LoomInitializer& Scene::loom(){
    //build() rather than a throw: asking for the live objects is a perfectly good reason to
    //bring them into existence, and the alternative is an ordering rule nobody would guess
    state->build();
    return *state->loom;
}

const LoomInitializer& Scene::loom() const{
    if(!state->built){
        throw std::runtime_error("Loom::Scene: nothing has been built yet - call the non const loom(), or draw a frame first");
    }
    return *state->loom;
}

const LoomConfig& Scene::config() const{
    return state->config;
}

Light& Scene::addLight(const LightConfig& config, bool castsShadows){
    state->build();

    //Asked and answered before anything is touched. A throw halfway through used to leave a
    //light in the scene that the caller had been told was refused - it lit the picture, it
    //counted, and nobody had a reference to it
    if(castsShadows){
        if(config.type == LightType::Directional && state->shadowLight != nullptr){
            throw std::runtime_error("Loom::Scene: a directional shadow map is already in use by the preset's sun - "
                                     "turn it off with setShadows(false) before adding another shadow casting directional light");
        }
        if(config.type == LightType::Point && state->shadowCubeLight != nullptr){
            throw std::runtime_error("Loom::Scene: a shadow cube is already in use - set 0 has one binding for it, "
                                     "so only one point light can cast at a time");
        }
    }

    state->extraLights.push_back(std::make_unique<Light>(config));
    Light& light = *state->extraLights.back();

    state->loom->renderer.addLight(light);

    if(!castsShadows){
        return light;
    }

    if(config.type == LightType::Directional){
        state->shadowMap = std::make_unique<RenderTarget>(state->loom->device,
            vk::Extent2D{shadowMapSize, shadowMapSize}, makeShadowMapConfig());
        state->shadowLight = &light;
        state->ensureShadowPipeline(state->shadowMap->getDepthFormat());

        ShadowConfig shadow = presetShadow();
        shadow.viewportWidth = state->config.width;
        shadow.viewportHeight = state->config.height;
        state->loom->renderer.setShadowMap(*state->shadowMap, light, shadow);

        return light;
    }

    //A point light shines in every direction, so its map is a cube and its pass is six passes
    state->shadowCube = std::make_unique<RenderTarget>(state->loom->device,
        vk::Extent2D{shadowCubeSize, shadowCubeSize}, makeShadowCubeConfig());
    state->shadowCubeLight = &light;
    state->ensureShadowPipeline(state->shadowCube->getDepthFormat());

    ShadowConfig shadow;
    shadow.depthBias = 0.0025f;
    state->loom->renderer.setShadowCube(*state->shadowCube, light, shadow);

    return light;
}

uint32_t Scene::lightCount() const{
    //The preset's sun counts, and Flat2D has none at all
    const uint32_t fromPreset = (state->built && state->preset != Preset::Flat2D) ? 1u : 0u;
    return fromPreset + static_cast<uint32_t>(state->extraLights.size());
}

Camera& Scene::camera(){ state->build(); return state->camera; }
Light& Scene::sun(){ state->build(); return state->sun; }
Environment& Scene::environment(){ state->build(); return state->environment; }

std::vector<uint8_t> Scene::readPixels() const{
    if(!state->built) throw std::runtime_error("Loom::Scene: nothing has been drawn yet");
    if(state->frameOpen) throw std::runtime_error("Loom::Scene: readPixels between startRendering and endRendering");

    //Tier one speaks RGBA, always. The BGRA a Vulkan surface hands back is a detail of the
    //format the swapchain negotiated, and it does not belong up here
    if(state->offscreen){
        const ImageData shot = state->offscreen->readPixels(state->loom->command);
        return Spool::imageFromPixels(shot.pixels.data(), shot.extent.width, shot.extent.height,
            Spool::ChannelOrder::BGRA).pixels;
    }

    const ImageData shot = state->loom->renderer.readLastFrame();
    return Spool::imageFromPixels(shot.pixels.data(), shot.extent.width, shot.extent.height,
        Spool::ChannelOrder::BGRA).pixels;
}

uint32_t Scene::width() const{ return state->config.width; }
uint32_t Scene::height() const{ return state->config.height; }

// ---------------------------------------------------------------------------------------
// Sequence
// ---------------------------------------------------------------------------------------

struct Sequence::State{
    Spool::SequenceConfig config;
    std::unique_ptr<Spool::SequenceWriter> writer;

    Spool::SequenceWriter& get(){
        //Built on first write, so setDirectory and setPrefix can still be called after the
        //Sequence exists
        if(!writer) writer = std::make_unique<Spool::SequenceWriter>(config);
        return *writer;
    }
};

Sequence::Sequence() : state(new State()){}

Sequence::~Sequence(){
    delete state;
    state = nullptr;
}

void Sequence::setDirectory(const std::string& directory){
    if(state->writer) throw std::runtime_error("Loom::Sequence: setDirectory has to be called before the first frame is written");
    state->config.directory = directory;
}

void Sequence::setPrefix(const std::string& prefix){
    if(state->writer) throw std::runtime_error("Loom::Sequence: setPrefix has to be called before the first frame is written");
    state->config.prefix = prefix;
}

std::string Sequence::write(const Scene& scene){
    const std::vector<uint8_t> pixels = scene.readPixels();

    Spool::Image image;
    image.width = scene.width();
    image.height = scene.height();
    image.sourceChannels = 4;
    image.pixels = pixels;   //already RGBA: readPixels converted it

    return state->get().write(image);
}

uint32_t Sequence::frameCount() const{
    return state->writer ? state->writer->frameCount() : 0;
}

}
