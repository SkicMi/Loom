#pragma once

#include "VulkanImage.h"
#include "SampledImage.h"
#include "VulkanCommand.h"
#include "ImageData.h"
#include <optional>

struct RenderTargetConfig{
//Must match the color format the pipeline was created with
vk::Format colorFormat = vk::Format::eB8G8R8A8Srgb;

vk::Filter filter = vk::Filter::eLinear;
vk::SamplerAddressMode addressMode = vk::SamplerAddressMode::eClampToEdge;


bool enableDepth = true;
ImageConfig depthConfig = {};

//A target with no colour attachment at all. A shadow map is depth and nothing else, so
//giving it a colour image would allocate a full screen of memory nobody ever reads
bool enableColor = true;

//Depth is normally scratch space: it exists so the depth test has somewhere to work, and
//it dies at endPass (storeOp eDontCare). A shadow map is the one case where the depth IS
//the result, so it has to survive the pass and be readable afterwards
bool keepDepth = false;

//Where the kept depth is left when the pass ends. Only means anything with keepDepth
vk::ImageLayout depthFinalLayout = vk::ImageLayout::eShaderReadOnlyOptimal;

//Ucitaj dubinu koja je vec unutra umjesto da je ocistis.
//
//To je ono sto od depth prepassa radi ustedu umjesto troska: prvi prolaz napise dubinu,
//drugi je UCITA i crta s eEqual i bez pisanja - pa se svaki piksel sjenca tocno jednom,
//bez obzira koliko se trokuta preko njega preklapa. Bez ovoga bi drugi prolaz obrisao
//sve sto je prvi napravio
bool loadDepth = false;

//A comparison sampler instead of an ordinary one. Sampling it does not return a depth - it
//returns how much of the 2x2 neighbourhood passed "is my reference closer than what is
//stored here". That is exactly a shadow lookup, and the filtering across the four texels
//is free: the same hardware that does bilinear does this
bool depthCompare = false;
vk::CompareOp depthCompareOp = vk::CompareOp::eLessOrEqual;

//Six faces instead of one image. A point light shines in every direction, so its shadow
//map is a cube: rendered one face at a time, sampled with a direction
bool cubeDepth = false;

//eColorAttachment always added. eSampled lats a later pass read result, eTransferSrc lets it be copied back to cpu
vk::ImageUsageFlags extraColorUsage = vk::ImageUsageFlagBits::eSampled;

vk::ImageLayout finalLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
};

class RenderTarget{
    public:
    RenderTarget(const VulkanDevice& device, vk::Extent2D extent, const RenderTargetConfig& config = {});

    RenderTarget(const RenderTarget&) = delete;
    RenderTarget& operator = (const RenderTarget&) = delete;
    RenderTarget(RenderTarget&&) = default;

    void resize(vk::Extent2D newExtent);
    ImageData readPixels(const VulkanCommand& command) const;

    //The kept depth buffer, back on the CPU. Only a target created with keepDepth has one
    //to give - anywhere else the depth was thrown away the moment the pass ended
    ImageData readDepthPixels(const VulkanCommand& command, uint32_t face = 0) const;

    //getters
    bool hasColor() const {return colorImage.has_value();}
    const VulkanImage& getColorImage() const {
        if(!colorImage){
            throw std::runtime_error("getColorImage: this target has no colour attachment (RenderTargetConfig::enableColor)");
        }
        return *colorImage;
    }
    const VulkanImage* getDepthImage() const {return depthImage ? &*depthImage : nullptr;}
    bool hasDepth() const {return depthImage.has_value();}
    bool keepsDepth() const {return config.keepDepth;}
    bool loadsDepth() const {return config.loadDepth;}
    bool hasCubeDepth() const {return config.cubeDepth;}

    //The view a pass attaches when it renders one face. The sampled view is the whole cube
    const vk::raii::ImageView& getDepthFaceView(uint32_t face) const {
        if(!depthImage){
            throw std::runtime_error("getDepthFaceView: this target has no depth attachment");
        }
        if(!config.cubeDepth){
            throw std::runtime_error("getDepthFaceView: this target's depth is a single image, not a cube (RenderTargetConfig::cubeDepth)");
        }
        return depthImage->getLayerView(face);
    }
    //The depth buffer as something a shader can read. With depthCompare the sampler compares
    //rather than fetches, which is what a shadow lookup needs
    SampledImage getDepthSampled() const {
        if(!depthImage){
            throw std::runtime_error("getDepthSampled: this target has no depth attachment (RenderTargetConfig::enableDepth)");
        }
        if(!config.keepDepth){
            throw std::runtime_error("getDepthSampled: the target does not keep its depth, so there would be nothing to sample (RenderTargetConfig::keepDepth)");
        }
        return SampledImage{*depthImage->getImageView(), *depthSampler, &*depthImage, depthImage->getGeneration()};
    }

    SampledImage getSampled() const {
        const VulkanImage& color = getColorImage();
        return SampledImage{*color.getImageView(), *sampler, &color, color.getGeneration()};
    }

    vk::Extent2D getExtent() const {return extent;}
    vk::Format getColorFormat() const {return colorImage ? colorImage->getFormat() : vk::Format::eUndefined;}
    vk::Format getDepthFormat() const {return depthImage ? depthImage->getFormat() : vk::Format::eUndefined;}
    vk::ImageLayout getFinalLayout() const {return config.finalLayout;}
    vk::ImageLayout getDepthFinalLayout() const {return config.depthFinalLayout;}


    private:
    const VulkanDevice& device;
    RenderTargetConfig config;
    vk::Extent2D extent;
    std::optional<VulkanImage> colorImage;
    std::optional<VulkanImage> depthImage;
    vk::raii::Sampler sampler = nullptr;
    vk::raii::Sampler depthSampler = nullptr;

};

inline ImageConfig makeColorConfig(const RenderTargetConfig& config){
        ImageConfig imageConfig;
        imageConfig.format = config.colorFormat;
        imageConfig.usage = vk::ImageUsageFlagBits::eColorAttachment | config.extraColorUsage;

        if(config.finalLayout == vk::ImageLayout::eShaderReadOnlyOptimal){
            imageConfig.usage |= vk::ImageUsageFlagBits::eSampled;
        }
        if(config.finalLayout ==vk::ImageLayout::eTransferSrcOptimal){
            imageConfig.usage |= vk::ImageUsageFlagBits::eTransferSrc;
        }

        imageConfig.aspect = vk::ImageAspectFlagBits::eColor;

        //Same reasoning as the depth buffer in makeDepthConfig: a full screen attachment is
        //big, lives as long as the target, and must be the last thing evicted
        imageConfig.dedicated = true;
        imageConfig.priority = 1.0f;
        return imageConfig;

        
    }

//Depth that is kept is depth somebody is going to read - as a texture in a later pass, or
//straight back to the CPU. Scratch depth needs neither usage and does not get them
inline ImageConfig makeTargetDepthConfig(const VulkanDevice& device, const RenderTargetConfig& config){
    ImageConfig depth = config.depthConfig;
    if(config.keepDepth){
        depth.usage |= vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferSrc;
    }
    depth.cube = config.cubeDepth;
    return makeDepthConfig(device, depth);
}

//A depth only target: no colour image, depth stored and left where a shader can sample it.
//Everything a shadow map is, and nothing else
inline RenderTargetConfig makeShadowMapConfig(RenderTargetConfig config = {}){
    config.enableColor = false;
    config.enableDepth = true;
    config.keepDepth = true;
    config.depthFinalLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
    config.depthCompare = true;
    return config;
}

//A point light's shadow map: the same depth only target, six faces deep
inline RenderTargetConfig makeShadowCubeConfig(RenderTargetConfig config = {}){
    config = makeShadowMapConfig(config);
    config.cubeDepth = true;
    return config;
}