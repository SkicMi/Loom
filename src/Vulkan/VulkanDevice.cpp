#include "VulkanDevice.h"

VulkanDevice::VulkanDevice(const VulkanInstance& instance, const DeviceConfig& config) : instance(instance), config(config){
    auto physicalDevices = instance.getInstance().enumeratePhysicalDevices();
    
    pickPhysicalDevice(physicalDevices);
    queueIndices = findQueueFamilies(physicalDevice);
    createLogicalDevice();





}



bool VulkanDevice::isDeviceSuitable(const vk::raii::PhysicalDevice& candidate){
    QueueFamilyIndices indices = findQueueFamilies(candidate);    
    bool extensionSupported = checkDeviceExtensionSupport(candidate);

    //checking if it supports features
    auto features = candidate.getFeatures2<vk::PhysicalDeviceFeatures2,
    vk::PhysicalDeviceVulkan11Features,
    vk::PhysicalDeviceVulkan13Features>();


    const auto& supported10 = features.get<vk::PhysicalDeviceFeatures2>().features;
    const auto& supported11 = features.get<vk::PhysicalDeviceVulkan11Features>();
    const auto& suported13 = features.get<vk::PhysicalDeviceVulkan13Features>();
    bool supportsRendering = suported13.dynamicRendering && suported13.synchronization2;
    
    return indices.isComplete() && 
    extensionSupported && 
    supportsRendering && 
    supported11.shaderDrawParameters &&
    supported10.fillModeNonSolid;
    }



//Helper that checks for device extension support (almost same as in Instance where validation layer support check happens)
bool VulkanDevice::checkDeviceExtensionSupport(const vk::raii::PhysicalDevice& candidate){
    auto available = candidate.enumerateDeviceExtensionProperties();

    for(const char* required : deviceExtensions){
        bool found = false;
        for(const auto& ext : available){
            if(strcmp(required,ext.extensionName) == 0){
                found = true;
                break;
            }
        }
        if(!found) return false;
    }
    return true;

}

void VulkanDevice::pickPhysicalDevice(const std::vector<vk::raii::PhysicalDevice>& candidates){

    vk::PhysicalDeviceType wanted = vk::PhysicalDeviceType::eOther;
    bool hasPreference = true;

    switch(config.preference){
        case DevicePreference::Discrete:
        wanted = vk::PhysicalDeviceType::eDiscreteGpu;
        break;
        case DevicePreference::Integrated:
        wanted = vk::PhysicalDeviceType::eIntegratedGpu;
        break;
        case DevicePreference::Any:
        hasPreference = false;
        break;
    }

    if(hasPreference){
        for(const auto& candidate : candidates){
            if(candidate.getProperties().deviceType == wanted && isDeviceSuitable(candidate)){
                physicalDevice = candidate;
                return;
            }
        }
    }

    for(const auto& candidate : candidates){
        if(isDeviceSuitable(candidate)){
            physicalDevice = candidate;
            return;
        }
    }
    throw std::runtime_error("Failed to find suitable GPU");
}

QueueFamilyIndices VulkanDevice::findQueueFamilies(const vk::raii::PhysicalDevice& candidate){
    QueueFamilyIndices indices;
    auto queueFamilies = candidate.getQueueFamilyProperties();

    for(uint32_t i = 0; i < queueFamilies.size(); i++){
        if(queueFamilies[i].queueFlags & vk::QueueFlagBits::eGraphics){
            indices.graphicsFamilies = i;
        }
        if(candidate.getSurfaceSupportKHR(i,*instance.getSurface())){
            indices.presentFamilies = i;
        }
        if(indices.isComplete()) break; //early exit as soon as we get both present and graphics families
    }

    return indices;
}


vk::Format VulkanDevice::findSupportedFormat(const std::vector<vk::Format>& candidates, vk::ImageTiling tiling, vk::FormatFeatureFlags features) const{
    for(vk::Format format : candidates){
        vk::FormatProperties properties = physicalDevice.getFormatProperties(format);

        if(tiling == vk::ImageTiling::eOptimal && (properties.optimalTilingFeatures & features) == features) {
            return format;
        }

        if(tiling == vk::ImageTiling::eLinear && (properties.linearTilingFeatures & features) == features){
            return format;
        }
    }

    throw std::runtime_error("findSupportedFormat: no candidate supports format");
}


void VulkanDevice::createLogicalDevice(){
    //Creating a set that holds bot queue and present families value
    std::set<uint32_t> uniqueQueueFamilies = {
        queueIndices.graphicsFamilies.value(),
        queueIndices.presentFamilies.value()
 };

    //Vector that holds al queueCreateInfos
    std::vector<vk::DeviceQueueCreateInfo> queueCreateInfos;
    float queuePriority = 1.0f;


    //for each queueFamily in uniqueQueueFamilies we create create info and push it back to*********
    for(uint32_t family : uniqueQueueFamilies){
        vk::DeviceQueueCreateInfo queueCreateInfo;
        queueCreateInfo.queueFamilyIndex = family;
        queueCreateInfo.queueCount = 1;
        queueCreateInfo.pQueuePriorities = &queuePriority;


        queueCreateInfos.push_back(queueCreateInfo);
    }


    //Now we check device features
    vk::PhysicalDeviceFeatures deviceFeatures {};
    deviceFeatures.fillModeNonSolid = true; //vulkan 1.0 feature that allows vk::PolygonMode::eLine and ePoint (needed for wireframe)

    //features needed for shaders inn vulan 1.1
    vk::PhysicalDeviceVulkan11Features features11;
    features11.shaderDrawParameters = true; //required for shaders to be able to use draw parameters, which is required for ray tracing and other features

    //features that are required for the device to be created, if they are not supported, device creation will fail
    vk::PhysicalDeviceVulkan13Features features13;
    features13.dynamicRendering = true; //required for dynamic rendering, which is required for ray tracing and to skip framebuffer
    features13.synchronization2 = true;

    features11.pNext = &features13; //pNext is used to chain additional structures to the features struct, in this case we are chaining the features13 struct to the features struct, so that the device will be created with the features specified in the features13 struct




    //Creating device create info
    vk::DeviceCreateInfo deviceCreateInfo;
    deviceCreateInfo.queueCreateInfoCount = static_cast<uint32_t>(queueCreateInfos.size());    
    deviceCreateInfo.pQueueCreateInfos = queueCreateInfos.data();
    deviceCreateInfo.pEnabledFeatures = &deviceFeatures;
    deviceCreateInfo.enabledExtensionCount = static_cast<uint32_t>(deviceExtensions.size());
    deviceCreateInfo.ppEnabledExtensionNames = deviceExtensions.data();
    deviceCreateInfo.pNext = &features11; //pNext is used to chain additional structures to the device create info, in this case we are chaining the features13 struct to the device create info, so that the device will be created with the features specified in the features13 struct

    //Creating device using Physical Device and device create info
    device = vk::raii::Device(physicalDevice,deviceCreateInfo);

    graphicsQueue = device.getQueue(queueIndices.graphicsFamilies.value(),0);
    presentQueue = device.getQueue(queueIndices.presentFamilies.value(),0);

   }

   uint32_t VulkanDevice::findMemoryType(uint32_t typeFilter, vk::MemoryPropertyFlags properties) const{
    vk::PhysicalDeviceMemoryProperties memProperties = getPhysicalDevice().getMemoryProperties();

    for(uint32_t i = 0; i < memProperties.memoryTypeCount; i++){
        if((typeFilter & (1u << i)) &&
        (memProperties.memoryTypes[i].propertyFlags & properties) == properties){
            return i;
        }
    }
    throw std::runtime_error("Failed to find suitable memory type");
}





