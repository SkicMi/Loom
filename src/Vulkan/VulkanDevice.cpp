#include "VulkanDevice.h"

VulkanDevice::VulkanDevice(const VulkanInstance& instance) : instance(instance){
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
    vk::PhysicalDeviceVulkan13Features>();

    const auto& suported13 = features.get<vk::PhysicalDeviceVulkan13Features>();
    bool supportsRendering = suported13.dynamicRendering && suported13.synchronization2;
    
    return indices.isComplete() && extensionSupported && supportsRendering;
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
    vk::PhysicalDeviceFeatures deviceFeatures {}; //empty for now, I will add them later 

    //features that are required for the device to be created, if they are not supported, device creation will fail
    vk::PhysicalDeviceVulkan13Features features13;
    features13.dynamicRendering = true; //required for dynamic rendering, which is required for ray tracing and to skip framebuffer
    features13.synchronization2 = true;




    //Creating device create info
    vk::DeviceCreateInfo deviceCreateInfo;
    deviceCreateInfo.queueCreateInfoCount = static_cast<uint32_t>(queueCreateInfos.size());    
    deviceCreateInfo.pQueueCreateInfos = queueCreateInfos.data();
    deviceCreateInfo.pEnabledFeatures = &deviceFeatures;
    deviceCreateInfo.enabledExtensionCount = static_cast<uint32_t>(deviceExtensions.size());
    deviceCreateInfo.ppEnabledExtensionNames = deviceExtensions.data();
    deviceCreateInfo.pNext = &features13; //pNext is used to chain additional structures to the device create info, in this case we are chaining the features13 struct to the device create info, so that the device will be created with the features specified in the features13 struct

    //Creating device using Physical Device and device create info
    device = vk::raii::Device(physicalDevice,deviceCreateInfo);

    graphicsQueue = device.getQueue(queueIndices.graphicsFamilies.value(),0);
    presentQueue = device.getQueue(queueIndices.presentFamilies.value(),0);

   }




