#pragma once
#include <GLFW/glfw3.h>
#include <vector>
#include <string>
#include <cstring>


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
GLFWwindow* window = nullptr;
uint32_t width = 1080;
uint32_t height = 720;
std::string appName = "";

std::vector<const char*> glfwExtensions = {};
std::vector<const char*> populateGlfwExtensions();



};