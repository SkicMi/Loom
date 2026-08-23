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

# glm - matematika (matrice, vektori). Header-only, ali ima CMake target glm::glm.
FetchContent_Declare(
    glm
    GIT_REPOSITORY https://github.com/g-truc/glm.git
    GIT_TAG        1.0.1
    GIT_SHALLOW    TRUE
)

FetchContent_MakeAvailable(VulkanHeaders glfw glm)

# ----------------------------------------------------------------------------
# Slang shader compiler
#   Ako "slangc" postoji na sustavu, koristi se on. Inace se skida sluzbeni
#   prebuilt binary (pinnan + provjeren hashom, isto kao ostale dependencije).
#   SOURCE_SUBDIR pokazuje na nepostojeci folder da MakeAvailable ne pokusa
#   add_subdirectory - arhiva je binary, nema CMakeLists.txt.
# ----------------------------------------------------------------------------
find_program(SLANGC_EXECUTABLE slangc)

if(NOT SLANGC_EXECUTABLE)
    set(SLANG_VERSION 2026.14.1)

    FetchContent_Declare(
        slang
        URL      https://github.com/shader-slang/slang/releases/download/v${SLANG_VERSION}/slang-${SLANG_VERSION}-linux-x86_64.tar.gz
        URL_HASH SHA256=21f2d7847385a770e569fb61b1507a7794d742d97850bce0432bff0032ca005f
        SOURCE_SUBDIR no-cmake-here
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    )
    FetchContent_MakeAvailable(slang)

    set(SLANGC_EXECUTABLE ${slang_SOURCE_DIR}/bin/slangc)
endif()

message(STATUS "Slang compiler: ${SLANGC_EXECUTABLE}")

# ----------------------------------------------------------------------------
# Kompilacija shadera: shaders/*.slang -> build/shaders/*.{vert,frag,comp}.spv
#   Jedan .slang file moze drzati vise entry pointa. Koje stageove file stvarno
#   sadrzi otkriva se citanjem [shader("...")] atributa, pa se prevodi samo to -
#   compute-only shader nema vertexMain i pokusaj prevodenja bi pukao.
#   Imena entry pointa su konvencija: vertexMain / fragmentMain / computeMain.
#
#   CONFIGURE_DEPENDS znaci da CMake sam primijeti novi .slang file. Ako
#   POSTOJECEM fileu dodas novi stage, treba rucni "cmake -S . -B build",
#   jer se sadrzaj cita u configure koraku, a ne u buildu.
# ----------------------------------------------------------------------------
set(LOOM_SHADER_OUTPUT_DIR ${CMAKE_BINARY_DIR}/shaders)
file(MAKE_DIRECTORY ${LOOM_SHADER_OUTPUT_DIR})

#   shaders/       - shaders the library ships
#   shaders/tests/ - shaders only the test harnesses use
file(GLOB LOOM_SHADER_SOURCES CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/shaders/*.slang
    ${CMAKE_CURRENT_SOURCE_DIR}/shaders/tests/*.slang
)

set(LOOM_COMPILED_SHADERS "")

foreach(SHADER ${LOOM_SHADER_SOURCES})
    get_filename_component(SHADER_NAME ${SHADER} NAME_WE)
    file(READ ${SHADER} SHADER_TEXT)

    # dvije paralelne liste: izlazni sufiks i ime entry pointa
    set(STAGE_SUFFIXES "")
    set(STAGE_ENTRIES  "")

    if(SHADER_TEXT MATCHES "shader\\(\"vertex\"\\)")
        list(APPEND STAGE_SUFFIXES vert)
        list(APPEND STAGE_ENTRIES  vertexMain)
    endif()
    if(SHADER_TEXT MATCHES "shader\\(\"fragment\"\\)")
        list(APPEND STAGE_SUFFIXES frag)
        list(APPEND STAGE_ENTRIES  fragmentMain)
    endif()
    if(SHADER_TEXT MATCHES "shader\\(\"compute\"\\)")
        list(APPEND STAGE_SUFFIXES comp)
        list(APPEND STAGE_ENTRIES  computeMain)
    endif()

    list(LENGTH STAGE_SUFFIXES STAGE_COUNT)

    if(STAGE_COUNT EQUAL 0)
        message(WARNING "Shader ${SHADER_NAME}.slang nema nijedan [shader(...)] entry point - preskacem")
    endif()

    math(EXPR STAGE_LAST "${STAGE_COUNT} - 1")
    foreach(STAGE_INDEX RANGE 0 ${STAGE_LAST})
        if(STAGE_COUNT EQUAL 0)
            break()
        endif()

        list(GET STAGE_SUFFIXES ${STAGE_INDEX} STAGE_SUFFIX)
        list(GET STAGE_ENTRIES  ${STAGE_INDEX} STAGE_ENTRY)

        set(SPV_OUTPUT ${LOOM_SHADER_OUTPUT_DIR}/${SHADER_NAME}.${STAGE_SUFFIX}.spv)

        add_custom_command(
            OUTPUT  ${SPV_OUTPUT}
            COMMAND ${SLANGC_EXECUTABLE} ${SHADER}
                    -target spirv
                    -profile spirv_1_5
                    -entry ${STAGE_ENTRY}
                    -o ${SPV_OUTPUT}
            DEPENDS ${SHADER}
            COMMENT "Slang -> SPIR-V: ${SHADER_NAME}.${STAGE_SUFFIX}.spv"
            VERBATIM
        )

        list(APPEND LOOM_COMPILED_SHADERS ${SPV_OUTPUT})
    endforeach()
endforeach()

if(LOOM_COMPILED_SHADERS)
    add_custom_target(LoomShaders ALL DEPENDS ${LOOM_COMPILED_SHADERS})
else()
    add_custom_target(LoomShaders)
    message(STATUS "Nema .slang fileova u shaders/ - preskacem kompilaciju shadera")
endif()

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
    src/Vulkan/VulkanGraphicsPipeline.cpp
    src/Vulkan/VulkanComputePipeline.cpp
    src/Vulkan/ComputeMaterial.cpp
    src/Vulkan/VulkanBuffer.cpp
    src/Vulkan/Mesh.cpp
    src/Vulkan/VulkanImage.cpp
    src/Vulkan/Texture.cpp
    src/Vulkan/Material.cpp
    src/Vulkan/RenderTarget.cpp
    src/Core/Camera.cpp
)

# PUBLIC: i Loom i sve sto ga linka vidi ove headere/dependencije,
# jer su Vulkan/GLFW tipovi dio javnih Loom headera
target_include_directories(Loom PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(Loom PUBLIC Vulkan::Headers glfw glm::glm)

# glm defaults za Vulkan:
#   DEPTH_ZERO_TO_ONE  -> Vulkanov depth raspon je [0,1], ne OpenGL-ov [-1,1]
#   RADIANS            -> kutovi u radijanima (u glm 1.0 je ionako default, ali eksplicitno)
target_compile_definitions(Loom PUBLIC
    GLM_FORCE_DEPTH_ZERO_TO_ONE
    GLM_FORCE_RADIANS
    # gtx/ su glm-ove "experimental" ekstenzije i bez ovoga se odbijaju prevesti.
    # Treba nam glm::quatLookAt (gtx/quaternion.hpp) u Camera.cpp.
    # Rizik je pod kontrolom jer je glm pinnan na 1.0.1 preko FetchContenta.
    GLM_ENABLE_EXPERIMENTAL
)

target_compile_options(Loom PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall -Wextra>
)

# Apsolutna putanja do prevedenih shadera - dostupna u C++ kao makro, pa
# ucitavanje shadera radi bez obzira iz kojeg foldera se program pokrece:
#     std::string path = std::string(LOOM_SHADER_DIR) + "/triangle.vert.spv";
target_compile_definitions(Loom PUBLIC
    LOOM_SHADER_DIR="${LOOM_SHADER_OUTPUT_DIR}"
)

# ----------------------------------------------------------------------------
# Testni executable
# ----------------------------------------------------------------------------
add_executable(LoomApp src/main.cpp)
target_link_libraries(LoomApp PRIVATE Loom)
add_dependencies(LoomApp LoomShaders)   # shaderi se prevedu prije exea
