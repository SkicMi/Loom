#include "VulkanInstance.h"

VulkanInstance::VulkanInstance(const Window& window) : window(window){


    //Creating App Info
    //Later I want to abstract app name and engine name from Uptop(INITIALIZER) for now I will leave it hardcoded
    vk::ApplicationInfo appInfo;
    appInfo.pApplicationName = "LoomPlaceHolder";
    appInfo.pEngineName = "LoomPlaceHolder";
    appInfo.apiVersion = vk::makeApiVersion(0,1,4,0);
    appInfo.engineVersion = vk::makeApiVersion(0,0,0,1);

    //Creating instance info
    vk::InstanceCreateInfo instanceInfo;
    instanceInfo.pApplicationInfo = &appInfo;

    auto exts = getExtensions();

    //Enabled extansion count ( glfw extensions, debug utils, ..)
    instanceInfo.enabledExtensionCount = static_cast<uint32_t>(exts.size());
    instanceInfo.ppEnabledExtensionNames = exts.data();
    
    //0 for now, I will add Validation layers in next steps of this script

    vk::DebugUtilsMessengerCreateInfoEXT debugCreateInfo;


    /*If in debug mode, we check for validation layers support 
    and if they exist we apply them to create info
    of our instance
    */
    #ifndef NDEBUG
    if(!checkValidationLayerSupport()){
        throw std::runtime_error("Validation layers requested but not available!");
    }
    instanceInfo.enabledLayerCount = static_cast<uint32_t>(validationLayers.size());
    instanceInfo.ppEnabledLayerNames = validationLayers.data();

    debugCreateInfo = populateDebugMessengerCreateInfo();

    //Catches exceptions from validation layers on creation of validation layers
    instanceInfo.pNext = &debugCreateInfo;

    #else 
    instanceInfo.enabledLayerCount = 0;
    instanceInfo.ppEnabledLayerNames = {};

    #endif

    //Creating instance and saving to member variable;
    instance = vk::raii::Instance(context, instanceInfo);


    //Creating real debug messanger
    #ifndef NDEBUG
    debugMessenger = vk::raii::DebugUtilsMessengerEXT(instance, populateDebugMessengerCreateInfo());
    #endif



    //Since using raii, I need to wrap surface in rawSurface then use RAII method to create raii SurfaceKHR member

    //Creating non raii surface
    VkSurfaceKHR rawSurface;

    //Trying to create window surface, if it works, it will save it to rawSurface
    if(glfwCreateWindowSurface(*instance ,window.getWindow(),nullptr ,&rawSurface) != VK_SUCCESS){
        throw std::runtime_error("Failed to create Surface");
    }

    //Then I can make from that rawSurface a RAII version
    surface = vk::raii::SurfaceKHR(instance,rawSurface);

}


//Helper that gets glfw extensions from window and adds utils for debug
std::vector<const char*> VulkanInstance::getExtensions(){
    auto exts = window.getGlfwExtensions();
    #ifndef NDEBUG
    exts.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
    #endif

    return exts;

}

//Check to see if user handling development has Vulkan SDK and validation layers support
bool VulkanInstance::checkValidationLayerSupport(){
    auto availableLayers = context.enumerateInstanceLayerProperties();

    for(const char* layerName : validationLayers){
        bool found = false;
        for(const auto& props : availableLayers){
            if(strcmp(layerName, props.layerName) == 0){
                found = true;
                break;

            }
        }
        if(!found) return false;
    }
    return true;
}

//Debug callback function
VKAPI_ATTR VkBool32 VKAPI_CALL VulkanInstance::debugCallback(
    VkDebugUtilsMessageSeverityFlagBitsEXT severity,
    VkDebugUtilsMessageTypeFlagsEXT type,
    const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData,
    void* pUserData)
{
    std::cerr << "[validation] " << pCallbackData->pMessage << std::endl;
    return VK_FALSE;
}



//Populating create info helper for debug messenger
vk::DebugUtilsMessengerCreateInfoEXT VulkanInstance::populateDebugMessengerCreateInfo() {
vk::DebugUtilsMessengerCreateInfoEXT createInfo{};
createInfo.setMessageSeverity(
    vk::DebugUtilsMessageSeverityFlagBitsEXT::eVerbose |
    vk::DebugUtilsMessageSeverityFlagBitsEXT::eWarning |
    vk::DebugUtilsMessageSeverityFlagBitsEXT::eError
);
createInfo.setMessageType(
    vk::DebugUtilsMessageTypeFlagBitsEXT::eGeneral |
    vk::DebugUtilsMessageTypeFlagBitsEXT::eValidation |
    vk::DebugUtilsMessageTypeFlagBitsEXT::ePerformance
);
createInfo.setPfnUserCallback(debugCallback);
createInfo.setPUserData(nullptr);
return createInfo;
}
