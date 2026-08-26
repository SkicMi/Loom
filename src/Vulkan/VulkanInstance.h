#pragma once
#include <vulkan/vulkan_raii.hpp>
#include "Window.h"
#include <cstring>
#include <iostream>
#include <atomic>


class VulkanInstance{

    public:
    //The window is optional. Without one there is no surface, no swapchain and no
    //presentation - which is exactly what a sequence export or a machine with no display
    //needs. Everything else Loom does works the same either way
    VulkanInstance(const std::string appName, const std::string engineName, const Window* window = nullptr);
 

    //getters
    const vk::raii::Context& getContext() const {return context;}
    const vk::raii::Instance& getInstance() const {return instance;}
    const vk::raii::SurfaceKHR& getSurface() const {return surface;}
    bool hasSurface() const {return *surface != VK_NULL_HANDLE;}
    const vk::raii::DebugUtilsMessengerEXT& getMessenger() const {return debugMessenger;}

    //How many warnings and errors the validation layers have reported since the counter was
    //last reset. A test can assert on this instead of a human reading stderr
    static uint32_t getValidationMessageCount() {return validationMessages.load();}
    static void resetValidationMessages() {validationMessages.store(0);}
    
    private:
    const Window* window = nullptr;
    const std::string appName;
    const std::string engineName;
    vk::raii::Context context;
    vk::raii::Instance instance = nullptr;
    vk::raii::SurfaceKHR surface = nullptr;
    vk::raii::DebugUtilsMessengerEXT debugMessenger = nullptr;


    
    

    std::vector<const char*> getExtensions();


    static inline std::atomic<uint32_t> validationMessages{0};

    static inline const std::vector<const char*> validationLayers = {
        "VK_LAYER_KHRONOS_validation"};
   

    //Static debug callback function 
    //vk:: tipovi, ne C tipovi. Clan pfnUserCallback ocekuje bas taj potpis, a setter koji
    //prima C verziju je oznacen kao zastario
    static VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(
    vk::DebugUtilsMessageSeverityFlagBitsEXT severity,
    vk::DebugUtilsMessageTypeFlagsEXT type,
    const vk::DebugUtilsMessengerCallbackDataEXT* pCallbackData,
    void* pUserData);

 bool checkValidationLayerSupport();
    vk::DebugUtilsMessengerCreateInfoEXT populateDebugMessengerCreateInfo();

};