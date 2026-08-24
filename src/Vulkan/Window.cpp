#include "Window.h"
#include <stdexcept>

std::atomic<uint32_t> GlfwContext::count{0};

GlfwContext::GlfwContext(){
    //glfwInit returns immediately when GLFW is already initialised, so calling it again
    //would be harmless - but the count is what decides who terminates, and it is only
    //honest if the initialisation is counted at the same moment
    if(count.fetch_add(1) == 0){
        if(!glfwInit()){
            count.fetch_sub(1);
            throw std::runtime_error("Glfw failed to initialize");
        }
    }
}

GlfwContext::~GlfwContext(){
    //Last one out turns off the lights, and only the last one
    if(count.fetch_sub(1) == 1){
        glfwTerminate();
    }
}


Window::Window(uint32_t width, uint32_t height, std::string appName) : width(width), height(height), appName(appName){

    //GLFW is already initialised by the glfw member above - it is a member precisely so that
    //a throw anywhere below still unwinds it correctly

    //Glfw supports openGL by default, this way we use no window hint api for VULKAN
    //MUST go before window creation
    glfwWindowHint(GLFW_CLIENT_API,GLFW_NO_API);

    //Creating window and assigning it to our variable -- glfw takes in C string so conversion is in need
    window = glfwCreateWindow(width,height,appName.c_str(), nullptr, nullptr);

    //Checking if window is null. No glfwTerminate here: another Window may well be alive,
    //and the glfw member unwinds this one's claim on its own
    if(!window){
        throw std::runtime_error("Window is null");
    }

   

    //Populating Glfw extensions vector
    glfwExtensions = populateGlfwExtensions();


}

Window::~Window(){
    glfwPollEvents();
    glfwDestroyWindow(window);
    //GLFW itself is left alone. The glfw member below decides whether this was the last one
}


//Helper Function to populate glfw extensions vector
std::vector<const char*> Window::populateGlfwExtensions(){
    uint32_t count;
    const char** exts = glfwGetRequiredInstanceExtensions(&count);
    return std::vector<const char*>(exts,exts+count);
}