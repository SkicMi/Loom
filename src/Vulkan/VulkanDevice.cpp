#include "VulkanDevice.h"

VulkanDevice::VulkanDevice(const VulkanInstance& instance) : instance(instance){
    auto physicalDevices = instance.getInstance().enumeratePhysicalDevices();
    

    //Pick physical device

    //create queries

    //create logical device and present queues


}