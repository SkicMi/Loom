# ============================================================================
#  Loom - Vulkan library
#  Struktura builda:
#    - "Loom"    -> staticka biblioteka (src/Core + src/Vulkan)
#    - "LoomApp" -> testni executable (src/main.cpp) koji linka Loom
# ============================================================================
cmake_minimum_required(VERSION 3.20)

project(Loom
    VERSION 0.1.0
    LANGUAGES C CXX   # C je potreban za GLFW koji se gradi iz izvornog koda
)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ----------------------------------------------------------------------------
# Dependencije (FetchContent) - pinnane na konkretne tagove radi
# ponovljivog builda (branch "main"/"master" se mijenja svaki dan)
# ----------------------------------------------------------------------------
include(FetchContent)

FetchContent_Declare(
    VulkanHeaders
    GIT_REPOSITORY https://github.com/KhronosGroup/Vulkan-Headers.git
    GIT_TAG        v1.4.309
    GIT_SHALLOW    TRUE
)

set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS    OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_DOCS     OFF CACHE BOOL "" FORCE)

FetchContent_Declare(
    glfw
    GIT_REPOSITORY https://github.com/glfw/glfw.git
    GIT_TAG        3.4
    GIT_SHALLOW    TRUE
)

FetchContent_MakeAvailable(VulkanHeaders glfw)

# ----------------------------------------------------------------------------
# Loom biblioteka
# ----------------------------------------------------------------------------
add_library(Loom STATIC
    src/Vulkan/Window.cpp
    src/Vulkan/VulkanInstance.cpp
    src/Vulkan/VulkanDevice.cpp
    src/Vulkan/VulkanSwapchain.cpp
    src/Vulkan/VulkanCommand.cpp
    src/Vulkan/VulkanRenderer.cpp
)

# PUBLIC: i Loom i sve sto ga linka vidi ove headere/dependencije,
# jer su Vulkan/GLFW tipovi dio javnih Loom headera
target_include_directories(Loom PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(Loom PUBLIC Vulkan::Headers glfw)

target_compile_options(Loom PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall -Wextra>
)

# ----------------------------------------------------------------------------
# Testni executable
# ----------------------------------------------------------------------------
add_executable(LoomApp src/main.cpp)
target_link_libraries(LoomApp PRIVATE Loom)
