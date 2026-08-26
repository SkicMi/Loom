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

# VulkanMemoryAllocator - suballokator memorije (AMD GPUOpen).
#   SOURCE_SUBDIR pokazuje na nepostojeci folder kao i kod slanga: VMA-in
#   vlastiti CMakeLists radi find_package(Vulkan REQUIRED) i trazi SDK na
#   sustavu, a nama treba samo jedan header. Ovako ga povlacimo kao izvor i
#   sami kontroliramo sve VMA_* definicije.
FetchContent_Declare(
    VulkanMemoryAllocator
    GIT_REPOSITORY https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator.git
    GIT_TAG        v3.4.0
    GIT_SHALLOW    TRUE
    SOURCE_SUBDIR  no-cmake-here
)

FetchContent_MakeAvailable(VulkanHeaders glfw glm VulkanMemoryAllocator)

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
# ----------------------------------------------------------------------------
# Granice koje C++ i shaderi MORAJU dijeliti.
#   Odavde idu u C++ kroz generirani header, a u shadere kroz -D na slangc.
#   Prije su stajale na tri mjesta i sinkronizirale se rukom; da se raziđu,
#   pipeline bi se svejedno preveo a citanje bi otislo izvan granica polja.
# ----------------------------------------------------------------------------
set(LOOM_MAX_SHADOW_MAPS  4)
set(LOOM_MAX_SHADOW_CUBES 2)

configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/cmake/ShadowLimits.h.in
    ${CMAKE_BINARY_DIR}/generated/Core/ShadowLimits.h
    @ONLY
)

set(LOOM_SHADER_DEFINES
    -DLOOM_MAX_SHADOW_MAPS=${LOOM_MAX_SHADOW_MAPS}
    -DLOOM_MAX_SHADOW_CUBES=${LOOM_MAX_SHADOW_CUBES}
)

set(LOOM_SHADER_OUTPUT_DIR ${CMAKE_BINARY_DIR}/shaders)
file(MAKE_DIRECTORY ${LOOM_SHADER_OUTPUT_DIR})

#   shaders/       - shaders the library ships
#   shaders/tests/ - shaders only the test harnesses use
file(GLOB LOOM_SHADER_SOURCES CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/shaders/*.slang
    ${CMAKE_CURRENT_SOURCE_DIR}/shaders/tests/*.slang
)

#   shaders/include/ - dijelovi koje shaderi ukljucuju, a sami nisu shader.
# Nisu u popisu iznad (nemaju entry point), ali svaki shader ovisi o svima njima: bez toga
# promjena formule ne bi ponovno prevela nista i .spv bi ostao od jucer.
file(GLOB LOOM_SHADER_INCLUDES CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/shaders/include/*.slang
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
                    ${LOOM_SHADER_DEFINES}
                    -o ${SPV_OUTPUT}
            DEPENDS ${SHADER} ${LOOM_SHADER_INCLUDES}
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
# Spool - import/export biblioteka. Vlastiti folder i vlastiti CMakeLists, jer
# ne ovisi o Loomu ni Loom o njemu - smjer je uvijek aplikacija -> jedno od njih.
# ----------------------------------------------------------------------------
add_subdirectory(spool)

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
    src/Vulkan/VulkanAllocator.cpp
    src/Vulkan/VmaImplementation.cpp
    src/Vulkan/VulkanBuffer.cpp
    src/Vulkan/Mesh.cpp
    src/Vulkan/VulkanImage.cpp
    src/Vulkan/Texture.cpp
    src/Vulkan/Material.cpp
    src/Vulkan/RenderTarget.cpp
    src/Vulkan/ShadingRateMap.cpp
    src/Vulkan/PositionMap.cpp
    src/Vulkan/NormalMap.cpp
    src/Core/Camera.cpp
    src/Core/CameraIntrinsics.cpp
    src/Core/Light.cpp
    src/Core/LoomShapes.cpp
)

# PUBLIC: i Loom i sve sto ga linka vidi ove headere/dependencije,
# jer su Vulkan/GLFW tipovi dio javnih Loom headera
target_include_directories(Loom PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src ${CMAKE_BINARY_DIR}/generated)
target_link_libraries(Loom PUBLIC Vulkan::Headers glfw glm::glm)

# VMA je PRIVATE: samo .cpp fileovi koji stvarno alociraju ga includeaju, a Loomovi
# headeri handleove deklariraju sami (VK_DEFINE_HANDLE), pa korisnikov kod nikad ne
# mora vidjeti vk_mem_alloc.h.
target_include_directories(Loom PRIVATE ${vulkanmemoryallocator_SOURCE_DIR}/include)

# VMA_STATIC_VULKAN_FUNCTIONS=0  -> Loom ne linka libvulkan (vk::raii sam otvara loader),
#                                   pa VMA ne smije traziti simbole u linkanju.
# VMA_DYNAMIC_VULKAN_FUNCTIONS=1 -> umjesto toga dobije vkGetInstanceProcAddr i
#                                   vkGetDeviceProcAddr iz raii dispatchera i sve ostalo
#                                   dohvati sam.
# VMA_VULKAN_VERSION=1003000     -> Vulkan 1.3, isto sto trazi i VulkanDevice. Headeri su
#                                   1.4 pa bi default bio 1004000; nizi broj drzi VMA
#                                   podalje od maintenance5 poziva.
# Definicije MORAJU biti na targetu, ne u jednom fileu: VMA_VULKAN_VERSION mijenja izgled
# strukture VmaVulkanFunctions, pa bi se dva TU-a razisla u tome tko koje polje cita.
target_compile_definitions(Loom PRIVATE
    VMA_STATIC_VULKAN_FUNCTIONS=0
    VMA_DYNAMIC_VULKAN_FUNCTIONS=1
    VMA_VULKAN_VERSION=1003000
)

# Jedini file koji prevodi samu VMA-u. Tudi kod, pa bez Loomovih -Wall -Wextra: inace
# upozorenja iz 16000 tudih linija zatrpaju ona koja se nas ticu.
set_source_files_properties(
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Vulkan/VmaImplementation.cpp
    PROPERTIES COMPILE_OPTIONS "-w"
)

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
# LoomPreset - stepenica 1.
#   Zaseban target, ne dio Looma, jer je jedini koji zna i za Loom i za Spool.
#   Da je unutra, Loom bi ovisio o Spoolu i granica bi nestala.
# ----------------------------------------------------------------------------
add_library(LoomPreset STATIC
    src/Loom/Scene.cpp
)

target_link_libraries(LoomPreset PUBLIC Loom PRIVATE Spool)

target_compile_options(LoomPreset PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall -Wextra>
)

add_dependencies(LoomPreset LoomShaders)   # preset gradi shadow pipeline iz shadow.vert.spv

# ----------------------------------------------------------------------------
# Primjeri - specifikacija stepenice 1.
#   Grade se s ostatkom da bi se znalo da se JOS UVIJEK prevode: primjer koji
#   se ne prevodi je specifikacija koja laze. Ne pokrecu se kao testovi jer
#   traze assete i prozor.
# ----------------------------------------------------------------------------
option(LOOM_BUILD_EXAMPLES "Build the tier 1 examples" ON)

if(LOOM_BUILD_EXAMPLES)
    file(GLOB LOOM_EXAMPLE_SOURCES CONFIGURE_DEPENDS
        ${CMAKE_CURRENT_SOURCE_DIR}/examples/*.cpp
    )

    add_custom_target(LoomExamples)

    foreach(EXAMPLE_SOURCE ${LOOM_EXAMPLE_SOURCES})
        get_filename_component(EXAMPLE_NAME ${EXAMPLE_SOURCE} NAME_WE)

        add_executable(example_${EXAMPLE_NAME} ${EXAMPLE_SOURCE})

        # Samo LoomPreset. Primjer koji bi trebao linkati Loom ili Spool izravno
        # bio bi primjer koji je sisao stepenicu, a da to nigdje ne pise
        target_link_libraries(example_${EXAMPLE_NAME} PRIVATE LoomPreset)

        add_dependencies(LoomExamples example_${EXAMPLE_NAME})
    endforeach()
endif()

# ----------------------------------------------------------------------------
# Testni executable
# ----------------------------------------------------------------------------
add_executable(LoomApp src/main.cpp)
# Samo LoomPreset. main.cpp je od sada stepenica 1 plus jedan svjesni spust,
# a ni jedno ni drugo ne trazi da se Loom ili Spool linkaju izravno.
target_link_libraries(LoomApp PRIVATE LoomPreset)
add_dependencies(LoomApp LoomShaders)   # shaderi se prevedu prije exea

# ----------------------------------------------------------------------------
# Testovi
#   Svaki tests/test_*.cpp je zaseban executable koji vraca 0 samo ako sve
#   provjere prodju, pa "ctest" ili "cmake --build build --target LoomTests"
#   daje odgovor bez citanja ispisa.
#   Testovi otvaraju prozor i traze GPU - nisu za headless CI bez displaya.
# ----------------------------------------------------------------------------
option(LOOM_BUILD_TESTS "Build Loom's test executables" ON)

if(LOOM_BUILD_TESTS)
    enable_testing()

    # ------------------------------------------------------------------------
    # Granica stepenice 1: provjerava se preprocesiranjem, ne citanjem.
    #   tier1_header_is_clean - <Loom/Loom.h> ne smije vidjeti Vulkan
    #   tier1_leak_detector   - kontrola: stepenica 2 mora ga vidjeti, inace
    #                           detektor koji nista ne nalazi izgleda ispravno
    # ------------------------------------------------------------------------
    set(LOOM_HEADER_INCLUDES
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${CMAKE_BINARY_DIR}/generated
        ${glm_SOURCE_DIR}
        ${vulkanheaders_SOURCE_DIR}/include
        ${glfw_SOURCE_DIR}/include
    )
    string(REPLACE ";" "$<SEMICOLON>" LOOM_HEADER_INCLUDES_ARG "${LOOM_HEADER_INCLUDES}")

    add_test(NAME tier1_header_is_clean COMMAND ${CMAKE_COMMAND}
        -DCOMPILER=${CMAKE_CXX_COMPILER}
        -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/headers/tier1.cpp
        "-DINCLUDES=${LOOM_HEADER_INCLUDES}"
        -DEXPECT=clean
        -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/HeaderIsClean.cmake)

    add_test(NAME tier1_leak_detector COMMAND ${CMAKE_COMMAND}
        -DCOMPILER=${CMAKE_CXX_COMPILER}
        -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/headers/tier2.cpp
        "-DINCLUDES=${LOOM_HEADER_INCLUDES}"
        -DEXPECT=dirty
        -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/HeaderIsClean.cmake)

    add_test(NAME tier1_advanced_is_dirty COMMAND ${CMAKE_COMMAND}
        -DCOMPILER=${CMAKE_CXX_COMPILER}
        -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/headers/tier1_advanced.cpp
        "-DINCLUDES=${LOOM_HEADER_INCLUDES}"
        -DEXPECT=dirty
        -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/HeaderIsClean.cmake)

    file(GLOB LOOM_TEST_SOURCES CONFIGURE_DEPENDS
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_*.cpp
    )

    add_custom_target(LoomTests)

    foreach(TEST_SOURCE ${LOOM_TEST_SOURCES})
        get_filename_component(TEST_NAME ${TEST_SOURCE} NAME_WE)

        add_executable(${TEST_NAME} ${TEST_SOURCE})
        # Testovi linkaju obje biblioteke. To je dozvoljeno i tu prestaje: same
        # biblioteke jedna o drugoj ne znaju nista.
        target_link_libraries(${TEST_NAME} PRIVATE Loom Spool LoomPreset)
        target_include_directories(${TEST_NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/tests)
        add_dependencies(${TEST_NAME} LoomShaders)
        add_dependencies(LoomTests ${TEST_NAME})

        add_test(NAME ${TEST_NAME} COMMAND ${TEST_NAME})
    endforeach()
endif()
