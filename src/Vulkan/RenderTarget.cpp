#include "RenderTarget.h"

RenderTarget::RenderTarget(const VulkanDevice& device, vk::Extent2D extent, const RenderTargetConfig& config) :
device(device),
config(config),
extent(extent),
colorImage(device,extent, makeColorConfig(config)),
depthImage(config.enableDepth ? std::optional<VulkanImage>(std::in_place, device,  extent, makeDepthConfig(device, config.depthConfig)) : std::nullopt){


}

void RenderTarget::resize(vk::Extent2D newExtent){
    extent = newExtent;
    colorImage.recreate(newExtent);
    if(depthImage){
        depthImage->recreate(newExtent);
    }
}