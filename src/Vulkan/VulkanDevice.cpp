#include "VulkanDevice.h"

VulkanDevice::VulkanDevice(const VulkanInstance& instance, const DeviceConfig& config) : instance(instance), config(config){
    auto physicalDevices = instance.getInstance().enumeratePhysicalDevices();

    //Everything below asks this the same way: is there a surface at all
    needsPresent = instance.hasSurface();

    pickPhysicalDevice(physicalDevices);
    queueIndices = findQueueFamilies(physicalDevice);
    createLogicalDevice();
    createAllocator();





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

    //maintenance4 is what makes vkGetDeviceBufferMemoryRequirements callable, which is the
    //function VMA reaches for on 1.3. Every device that reports Vulkan 1.3 must support it,
    //so this filters nothing out in practice - it just says out loud what is being relied on
    return indices.isComplete(needsPresent) &&
    suported13.maintenance4 &&
    extensionSupported && 
    supportsRendering && 
    supported11.shaderDrawParameters &&
    supported10.fillModeNonSolid &&
    //Sjena se bira indeksom iz svjetla, a indeks se cita u shaderu. Bez ovoga polje
    //shadow karata se smije indeksirati samo konstantom, sto znaci jedna karta
    supported10.shaderSampledImageArrayDynamicIndexing;
    }



//Helper that checks for device extension support (almost same as in Instance where validation layer support check happens)
bool VulkanDevice::checkDeviceExtensionSupport(const vk::raii::PhysicalDevice& candidate){
    auto available = candidate.enumerateDeviceExtensionProperties();

    //Nothing is required of a headless device beyond what core Vulkan already promises
    if(!needsPresent){
        return true;
    }

    for(const char* required : surfaceDeviceExtensions){
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

//Same loop as the required check above, but the answer is allowed to be no
bool VulkanDevice::supportsExtension(const vk::raii::PhysicalDevice& candidate, const char* name){
    auto available = candidate.enumerateDeviceExtensionProperties();

    for(const auto& ext : available){
        if(strcmp(name, ext.extensionName) == 0){
            return true;
        }
    }
    return false;
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
        if(needsPresent && candidate.getSurfaceSupportKHR(i,*instance.getSurface())){
            indices.presentFamilies = i;
        }
        if(indices.isComplete(needsPresent)) break; //early exit as soon as we have what this device needs
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
    std::set<uint32_t> uniqueQueueFamilies = {queueIndices.graphicsFamilies.value()};
    if(queueIndices.presentFamilies.has_value()){
        uniqueQueueFamilies.insert(queueIndices.presentFamilies.value());
    }

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
    deviceFeatures.shaderSampledImageArrayDynamicIndexing = true; //indeks shadow karte dolazi iz svjetla, ne iz konstante

    //features needed for shaders inn vulan 1.1
    vk::PhysicalDeviceVulkan11Features features11;
    features11.shaderDrawParameters = true; //required for shaders to be able to use draw parameters, which is required for ray tracing and other features

    //features that are required for the device to be created, if they are not supported, device creation will fail
    vk::PhysicalDeviceVulkan13Features features13;
    features13.dynamicRendering = true; //required for dynamic rendering, which is required for ray tracing and to skip framebuffer
    features13.synchronization2 = true;
    features13.maintenance4 = true; //lets VMA call vkGetDeviceBufferMemoryRequirements, core in 1.3 but gated behind this feature

    features11.pNext = &features13; //pNext is used to chain additional structures to the features struct, in this case we are chaining the features13 struct to the features struct, so that the device will be created with the features specified in the features13 struct

    //The required extensions plus whichever optional ones this card offers. Asked once here
    //and remembered, because the allocator needs the same answer a moment later
    enabledExtensions = needsPresent ? surfaceDeviceExtensions : std::vector<const char*>{};
    hasMemoryBudget = supportsExtension(physicalDevice, "VK_EXT_memory_budget");
    hasMemoryPriority = supportsExtension(physicalDevice, "VK_EXT_memory_priority");

    if(hasMemoryBudget){
        enabledExtensions.push_back("VK_EXT_memory_budget");
    }

    //The extension only exposes the priority field; the feature is what makes the driver read
    //it, and the spec lets a driver ship the extension with the feature off. Asking first
    //costs one query and turns a failed vkCreateDevice into a quietly skipped optimisation
    vk::PhysicalDeviceMemoryPriorityFeaturesEXT memoryPriorityFeatures;
    if(hasMemoryPriority){
        auto supported = physicalDevice.getFeatures2<vk::PhysicalDeviceFeatures2,
            vk::PhysicalDeviceMemoryPriorityFeaturesEXT>();
        hasMemoryPriority = static_cast<bool>(supported.get<vk::PhysicalDeviceMemoryPriorityFeaturesEXT>().memoryPriority);
    }

    if(hasMemoryPriority){
        enabledExtensions.push_back("VK_EXT_memory_priority");
        memoryPriorityFeatures.memoryPriority = true;
        features13.pNext = &memoryPriorityFeatures;
    }

    //Creating device create info
    vk::DeviceCreateInfo deviceCreateInfo;
    deviceCreateInfo.queueCreateInfoCount = static_cast<uint32_t>(queueCreateInfos.size());    
    deviceCreateInfo.pQueueCreateInfos = queueCreateInfos.data();
    deviceCreateInfo.pEnabledFeatures = &deviceFeatures;
    deviceCreateInfo.enabledExtensionCount = static_cast<uint32_t>(enabledExtensions.size());
    deviceCreateInfo.ppEnabledExtensionNames = enabledExtensions.data();
    deviceCreateInfo.pNext = &features11; //pNext is used to chain additional structures to the device create info, in this case we are chaining the features13 struct to the device create info, so that the device will be created with the features specified in the features13 struct

    //Creating device using Physical Device and device create info
    device = vk::raii::Device(physicalDevice,deviceCreateInfo);

    graphicsQueue = device.getQueue(queueIndices.graphicsFamilies.value(),0);
    if(queueIndices.presentFamilies.has_value()){
        presentQueue = device.getQueue(queueIndices.presentFamilies.value(),0);
    }

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


void VulkanDevice::createAllocator(){
    AllocatorConfig allocatorConfig;
    allocatorConfig.useMemoryBudget = hasMemoryBudget;
    allocatorConfig.useMemoryPriority = hasMemoryPriority;

    //Loom already demands Vulkan 1.3 features (dynamic rendering, synchronization2), so 1.3
    //is what VMA is told. Deliberately not more: at 1.4 VMA would reach for maintenance5
    //entry points, and every entry point it reaches for is one more feature that has to be
    //switched on above, or the validation layer logs a message and the test suite counts it
    allocatorConfig.apiVersion = VK_API_VERSION_1_3;

    allocator = VulkanAllocator(instance.getInstance(), physicalDevice, device, allocatorConfig);
}
