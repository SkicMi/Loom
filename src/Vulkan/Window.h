#pragma once
#include <GLFW/glfw3.h>
#include <atomic>
#include <vector>
#include <string>
#include <cstring>


//GLFW's initialisation is process wide, not per window: glfwTerminate destroys every window
//that exists and invalidates every handle to one. A Window that called it in its own
//destructor therefore took every other Window down with it, and the next destructor ran
//glfwDestroyWindow on a dangling pointer.
//
//So nobody owns GLFW - the count does. The first context in initialises it and the last one
//out terminates it, which is the only lifetime that matches what GLFW actually is
class GlfwContext{
    public:
    GlfwContext();
    ~GlfwContext();

    GlfwContext(const GlfwContext&) = delete;
    GlfwContext& operator=(const GlfwContext&) = delete;

    //How many things are currently holding GLFW open. Zero means it is not initialised
    static uint32_t liveCount() {return count.load();}

    private:
    static std::atomic<uint32_t> count;
};


class Window{

public:
Window(uint32_t width, uint32_t height, std::string appName);
~Window();

Window(const Window&) = delete;
Window& operator = (const Window&) = delete;


//Getters
GLFWwindow* getWindow() const {return window;}
std::vector<const char*> getGlfwExtensions() const{return glfwExtensions;}
double getTime() const {return glfwGetTime();}



bool shouldClose() const {return glfwWindowShouldClose(window);}
void pollEvents() const {glfwPollEvents();}




private:
//Declared first so it is destroyed last: the window handle above has to be gone before
//GLFW is allowed to shut down
GlfwContext glfw;

GLFWwindow* window = nullptr;
uint32_t width = 1080;
uint32_t height = 720;
std::string appName = "";

std::vector<const char*> glfwExtensions = {};
std::vector<const char*> populateGlfwExtensions();



};