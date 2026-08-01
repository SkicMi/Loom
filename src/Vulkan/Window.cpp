#include "Window.h"
#include <stdexcept>


Window::Window(uint32_t width, uint32_t height, std::string appName) : width(width), height(height), appName(appName){

    //Trying to initialize glfw, if it fails, throw error and return

    if(!glfwInit()){
        throw std::runtime_error("Glfw failed to initialize");
        return;
    }

    //Glfw supports openGL by default, this way we use no window hint api for VULKAN
    //MUST go before window creation
    glfwWindowHint(GLFW_CLIENT_API,GLFW_NO_API);

    //Creating window and assigning it to our variable -- glfw takes in C string so conversion is in need
    window = glfwCreateWindow(width,height,appName.c_str(), nullptr, nullptr);

    //Checking if window is null, if it is, throw error and terminate
    if(!window){
        glfwTerminate();
        throw std::runtime_error("Window is null");
        
    }

   

    //Populating Glfw extensions vector
    glfwExtensions = populateGlfwExtensions();


}

Window::~Window(){
    glfwDestroyWindow(window);
    glfwTerminate();

}


//Helper Function to populate glfw extensions vector
std::vector<const char*> Window::populateGlfwExtensions(){
    uint32_t count;
    const char** exts = glfwGetRequiredInstanceExtensions(&count);
    return std::vector<const char*>(exts,exts+count);
}