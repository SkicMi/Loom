#pragma once
#include <GLFW/glfw3.h>
#include <iostream>
#include <vector>
#include <string>
#include <cstring>


class Window{

public:
Window(int width, int height, std::string appName);
~Window();

Window(const Window&) = delete;
Window& operator = (const Window&) = delete;


//Getters
GLFWwindow* getWindow() const {return window;}
std::vector<const char*> getGlfwExtensions() const{return glfwExtensions;}



private:
GLFWwindow* window = nullptr;
int width = 1080;
int height = 720;
std::string appName = "";

std::vector<const char*> glfwExtensions = {};
std::vector<const char*> populateGlfwExtensions();



};