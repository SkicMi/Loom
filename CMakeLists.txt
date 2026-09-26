# ============================================================================
#  Loom - Vulkan library
#  Struktura builda:
#    - "Loom"    -> staticka biblioteka (src/Core + src/Vulkan)
#    - "LoomApp" -> testni executable (src/main.cpp) koji linka Loom
#
#  Sistemske ovisnosti (sve ostalo se dovlaci FetchContentom):
#    - Vulkan drajver i validation layeri
#    - slangc (Vulkan SDK) - prevodi shadere
#    - FFmpeg razvojni paketi - Spool cita video; vidi spool/CMakeLists.txt
#    - ffmpeg CLI - samo za testove, koji sami prave testni materijal
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
# Build type, kad ga nitko ne zada.
#
# CMake bez CMAKE_BUILD_TYPE ne doda NIJEDAN -O, pa se sve gradi neoptimizirano.
# To se dugo nije vidjelo jer kadar drzi GPU, ali cim se izmjeri nesto sto je CPU
# posao razlika je velika: isto citanje .ply-a je 2.44 s s -O0 i 0.38 s s -O2.
#
# RelWithDebInfo a ne Release: -O2 ali sa simbolima, pa stack trace i dalje ima
# imena. CMake i u njega stavlja -DNDEBUG, ne samo u Release - nas kod to ne osjeti
# jer nema nijedan assert (guardovi bacaju recenicu), ali VMA-ine interne provjere
# time utihnu. Tko ih treba, gradi s -DCMAKE_BUILD_TYPE=Debug.
#
# Postavlja se samo ako korisnik nije rekao svoje, i samo za generatore s jednom
# konfiguracijom (Ninja Multi-Config i Visual Studio biraju u trenutku gradnje)
get_property(LOOM_MULTI_CONFIG GLOBAL PROPERTY GENERATOR_IS_MULTI_CONFIG)
if(NOT LOOM_MULTI_CONFIG AND NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "Build type" FORCE)
    message(STATUS "Loom: CMAKE_BUILD_TYPE nije zadan, postavljen na RelWithDebInfo")
endif()

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

# Jolt Physics - collideri (predmeti u ruci, prsti oko drske). Samo biblioteka: bez primjera,
#   testova i vlastitih zastavica prevoditelja (OVERRIDE_CXX_FLAGS OFF), da ne dira nase postavke.
set(TARGET_UNIT_TESTS          OFF CACHE BOOL "" FORCE)
set(TARGET_HELLO_WORLD         OFF CACHE BOOL "" FORCE)
set(TARGET_PERFORMANCE_TEST    OFF CACHE BOOL "" FORCE)
set(TARGET_SAMPLES             OFF CACHE BOOL "" FORCE)
set(TARGET_VIEWER              OFF CACHE BOOL "" FORCE)
set(ENABLE_ALL_WARNINGS        OFF CACHE BOOL "" FORCE)
set(OVERRIDE_CXX_FLAGS         OFF CACHE BOOL "" FORCE)
set(INTERPROCEDURAL_OPTIMIZATION OFF CACHE BOOL "" FORCE)
set(DEBUG_RENDERER_IN_DEBUG_AND_RELEASE OFF CACHE BOOL "" FORCE)
set(PROFILER_IN_DEBUG_AND_RELEASE OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
    JoltPhysics
    GIT_REPOSITORY https://github.com/jrouwe/JoltPhysics.git
    GIT_TAG        v5.2.0
    GIT_SHALLOW    TRUE
    SOURCE_SUBDIR  Build
)
FetchContent_MakeAvailable(JoltPhysics)

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

    # Stage moze biti napisan i u includeu. Bez ovoga bi takav shader tiho izgubio taj
    # stage - ne bi pao build nego bi nedostajao .spv, a stari bi jos lezao u build mapi i
    # sve bi radilo do prvog ciscenja. Jednom se dogodilo, vise ne moze
    foreach(SHADER_INCLUDE ${LOOM_SHADER_INCLUDES})
        file(READ ${SHADER_INCLUDE} INCLUDE_TEXT)
        string(APPEND SHADER_TEXT "${INCLUDE_TEXT}")
    endforeach()

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

# Engine - sto se crta: camera solve i sinteticka referenca za njega. Vlastiti folder i
# vlastiti CMakeLists, iz istog razloga kao Spool: ne zna ni za Loom ni za Spool
add_subdirectory(engine)

# Treadle - suicelje. Isti odnos kao Spool i Engine, samo jos strozi: Treadle nema NIJEDNU
# vanjsku ovisnost. Ulaz mu je zapis brojeva, izlaz polje vrhova u pikselima, a tko to crta
# njegov je problem - vidi treadle/CMakeLists.txt
add_subdirectory(treadle)

# Warp - scena: stablo entiteta s komponentama i kljucevima kroz vrijeme. Ovisi samo o glm-u -
# vidi warp/CMakeLists.txt
add_subdirectory(warp)

# Tracer (LoomTracer) - fizikalni path tracer za konacni render iz kamere. Ovisi samo o glm-u i
# dretvama, pa se testira bez prozora i kartice - vidi tracer/CMakeLists.txt
add_subdirectory(tracer)

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
    src/Vulkan/RadixSort.cpp
    src/Vulkan/PrefixSum.cpp
    src/Vulkan/DescriptorMatcher.cpp
    src/Vulkan/SiftDescriber.cpp
    src/Vulkan/SplatRenderer.cpp
    src/Vulkan/VulkanAllocator.cpp
    src/Vulkan/VmaImplementation.cpp
    src/Vulkan/VulkanBuffer.cpp
    src/Vulkan/Mesh.cpp
    src/Vulkan/VulkanImage.cpp
    src/Vulkan/Texture.cpp
    src/Vulkan/StreamingTexture.cpp
    src/Vulkan/Material.cpp
    src/Vulkan/RenderTarget.cpp
    src/Vulkan/ShadingRateMap.cpp
    src/Vulkan/PositionMap.cpp
    src/Vulkan/NormalMap.cpp
    src/Vulkan/Relight.cpp
    src/Core/Camera.cpp
    src/Core/Splat.cpp
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
# TreadlePaint - suicelje nacrtano Loomom.
#   Zaseban target iz istog razloga kao LoomPreset: jedini je koji zna i za Loom
#   i za Treadle. Da je unutra, Treadle bi ovisio o Vulkanu i vise se ne bi dao
#   testirati bez kartice, a Loom bi znao sto je gumb.
# ----------------------------------------------------------------------------
add_library(TreadlePaint STATIC
    src/TreadlePaint/UiPainter.cpp
)

target_link_libraries(TreadlePaint PUBLIC Loom Treadle)
target_include_directories(TreadlePaint PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)

target_compile_options(TreadlePaint PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall -Wextra>
)

add_dependencies(TreadlePaint LoomShaders)   # crta iz ui.vert.spv i ui.frag.spv

# ----------------------------------------------------------------------------
# TracerGpu - LoomTracer na kartici (Vulkan compute).
#   Zaseban target iz istog razloga kao LoomPreset i TreadlePaint: jedini zna i za
#   Loom i za Tracer. Tracer tako ostaje bez Vulkana i testira se bez kartice
# ----------------------------------------------------------------------------
add_library(TracerGpu STATIC
    src/TracerGpu/GpuTracer.cpp
)

target_link_libraries(TracerGpu PUBLIC Loom Tracer)
target_include_directories(TracerGpu PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)

target_compile_options(TracerGpu PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall -Wextra>
)

add_dependencies(TracerGpu LoomShaders)   # tracer.comp.spv, tracer_resolve.comp.spv

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
# main.cpp je trenutno STEPENICA 2 i linka Loom i Spool izravno: prikaz snimke
# trazi StreamingTexture (Loom) i VideoReader (Spool), a stepenica 1 jos nema
# gdje drzati plate. To pise i u samom fileu - spust koji se ne vidi je spust
# koji se zaboravi.
target_link_libraries(LoomApp PRIVATE Loom Spool)
add_dependencies(LoomApp LoomShaders)   # shaderi se prevedu prije exea

# SplatViewer: .ply s gaussianima u prozoru. Isto stepenica 2 i iz istog razloga -
# oblak od tri cetvrt milijuna gaussiana nije oblik koji Scene zna staviti u svoj red.
add_executable(SplatViewer src/splat_viewer.cpp)
target_link_libraries(SplatViewer PRIVATE Loom Spool Engine TreadlePaint)

# SolveViewer - camera solve nacrtan: prave kamere protiv onih koje je solver nasao.
# Jedini program koji linka i preset i Engine, jer je usporedba njegov cijeli posao
# VideoSolve - camera solve na pravoj snimci. Linka Spool (cita video) i Engine (rjesava);
# Looma ovdje nema, jer se nista ne crta
# VideoInfo - sto snimka o sebi govori. Spool cita, Engine tumaci
add_executable(VideoInfo src/video_info.cpp)
target_link_libraries(VideoInfo PRIVATE Spool Engine)

# ModelInfo - sto vrijedi jedna rekonstrukcija, bez poznate istine. Isti racun za nas model i za
# COLMAP-ov, jer se inace usporedjuju dvije definicije umjesto dva rjesenja
# TruthBench - apsolutna greska solvera na snimci koju sami nacrtamo. Loom vodi kameru poznatim
# putem, solver dobije samo piksele, greska se mjeri protiv istine a ne protiv drugog alata
add_executable(TruthBench src/truth_bench.cpp)
target_link_libraries(TruthBench PRIVATE LoomPreset Engine)
add_dependencies(TruthBench LoomShaders)

add_executable(ModelInfo src/model_info.cpp)
target_link_libraries(ModelInfo PRIVATE Engine)

# OverlayBox - kocka na fiksnom mjestu u svijetu, nacrtana preko pravih kadrova. Provjera koju
# brojke ne mogu dati: stoji li rjesenje mirno kroz snimku
add_executable(OverlayBox src/overlay_box.cpp)
target_link_libraries(OverlayBox PRIVATE Engine Spool)

# MotionScore - ocjena generiranog pokreta bez gledanja: klizanje stopala, propadanje, trzaji,
# skok korijena (Engine/MotionQuality.h). Za varijante istog opisa kaze koja je najbolja
add_executable(MotionScore src/motion_score.cpp)
target_link_libraries(MotionScore PRIVATE Engine)

add_executable(VideoSolve src/video_solve.cpp)
# Loom samo za --gpu-match (DescriptorMatcher na kartici); bez zastavice se kartica ne dira
target_link_libraries(VideoSolve PRIVATE Spool Engine Loom)
add_dependencies(VideoSolve LoomShaders)

# RollingShutterProbe - koliko rolling shutter kvari gotovo rjesenje i koliko traje citanje senzora
# (Bundle.h, BundleConfig::rowTime). Ne solva iznova: zavrsni bundle nad rezultatom, po vremenu citanja
add_executable(RollingShutterProbe src/rolling_shutter_probe.cpp)
target_link_libraries(RollingShutterProbe PRIVATE Engine)

# Loom - desktop alat. `Loom` otvori prozor i odande se bira sto alat radi. Solver se pokrece kao
# zaseban proces, pa se nijedna linija VideoSolvea ne mijenja da bi suicelje postojalo
# Meta se zove LoomDesk jer su "Loom" (staticka biblioteka) i "LoomApp" (src/main.cpp) vec
# zauzeti; BINARY se zove loom, malim slovom, jer se tako pokrece iz terminala
add_executable(LoomDesk src/loom_app.cpp)
set_target_properties(LoomDesk PROPERTIES OUTPUT_NAME loom)
target_link_libraries(LoomDesk PRIVATE Loom TreadlePaint Engine Warp Spool Tracer TracerGpu)
# KORIJEN PROJEKTA U KODU. Aplikacija se pokrece iz build mape, a trener splatova i njegov venv
# zive u korijenu - bez ovoga bi radila samo kad se slucajno pokrene s pravog mjesta
target_compile_definitions(LoomDesk PRIVATE LOOM_ROOT_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
add_dependencies(LoomDesk LoomShaders VideoSolve)

# loom-render - render projekta iz kamere bez prozora (LoomTracer). Isti most kao gumb Render u
# editoru (src/LoomRender.h), ali bez Vulkana: radi i na stroju bez kartice i zaslona
#AgentOfWeavers: podaci za trening iz Procedura pravila, bez prozora (src/procedura_gen.cpp)
add_executable(ProceduraGen src/procedura_gen.cpp)
set_target_properties(ProceduraGen PROPERTIES OUTPUT_NAME procedura-gen)
target_link_libraries(ProceduraGen PRIVATE Engine Warp Spool Treadle)
target_compile_definitions(ProceduraGen PRIVATE LOOM_ROOT_DIR="${CMAKE_CURRENT_SOURCE_DIR}")

add_executable(LoomRender src/loom_render.cpp)
set_target_properties(LoomRender PROPERTIES OUTPUT_NAME loom-render)
target_link_libraries(LoomRender PRIVATE Warp Spool Tracer TracerGpu)
add_dependencies(LoomRender LoomShaders)
target_compile_options(LoomRender PRIVATE $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall -Wextra>)

add_executable(SolveViewer src/solve_viewer.cpp)
target_link_libraries(SolveViewer PRIVATE LoomPreset Engine)
add_dependencies(SolveViewer LoomShaders)

# SolveMovie - solve prave snimke kao let kroz rezultat. Jedini program koji linka sva tri
# modula: Spool cita snimku, Engine je rjesava, Loom crta ono sto je naslo
add_executable(SolveMovie src/solve_movie.cpp)
target_link_libraries(SolveMovie PRIVATE LoomPreset Spool Engine)
add_dependencies(SolveMovie LoomShaders)
add_dependencies(SplatViewer LoomShaders)

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
        if(TEST_NAME STREQUAL "test_motion_panel")
            target_compile_definitions(${TEST_NAME} PRIVATE LOOM_WEAVERMOTION_FIXTURE="${CMAKE_CURRENT_BINARY_DIR}/weavermotion_constraints_fixture.json")
        endif()
        # Testovi linkaju obje biblioteke. To je dozvoljeno i tu prestaje: same
        # biblioteke jedna o drugoj ne znaju nista.
        target_link_libraries(${TEST_NAME} PRIVATE Loom Spool Engine Treadle Warp Tracer TracerGpu LoomPreset TreadlePaint)
        target_include_directories(${TEST_NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/tests)
        add_dependencies(${TEST_NAME} LoomShaders)
        add_dependencies(LoomTests ${TEST_NAME})

        add_test(NAME ${TEST_NAME} COMMAND ${TEST_NAME})
    endforeach()

    # test_budget measures frame time, so it only means something when nothing else is running:
    # under ctest -j it failed on load, never on the renderer
    set_tests_properties(test_budget PROPERTIES RUN_SERIAL TRUE)

    find_package(Python3 COMPONENTS Interpreter QUIET)
    if(Python3_Interpreter_FOUND)
        add_test(NAME test_weavermotion_cli
            COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_weavermotion_cli.py)
    endif()

    set(LOOM_KIMODO_PYTHON "${CMAKE_CURRENT_SOURCE_DIR}/tools/weavermotion/.venv-clean/bin/python")
    if(EXISTS "${LOOM_KIMODO_PYTHON}")
        add_test(NAME test_weavermotion_constraint_schema
            COMMAND "${LOOM_KIMODO_PYTHON}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_weavermotion_constraints.py"
                    "${CMAKE_CURRENT_BINARY_DIR}/weavermotion_constraints_fixture.json")
        set_tests_properties(test_weavermotion_constraint_schema PROPERTIES DEPENDS test_motion_panel)
        # Ponovno generiranje dijela takea: ogranicenja i spoj, bez kartice (tools/weavermotion/kimodo_range.py)
        add_test(NAME test_kimodo_range
            COMMAND "${LOOM_KIMODO_PYTHON}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_kimodo_range.py")
        # Kimodo servis (modeli ostaju u memoriji): protokol, ponovna upotreba radnika, pad - bez modela
        add_test(NAME test_kimodo_service
            COMMAND "${LOOM_KIMODO_PYTHON}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_kimodo_service.py")
    endif()


    # Polje ostataka mora proci i kroz STVARNI put do korisnika, ne samo kroz cisti racun u
    # test_residual_field: Loom nacrta kadrove, oba grafa ih ponovno pronadju, rekonstrukcija
    # procijeni poze i tek se nad njezinim prezivjelim opazanjima donese dijagnoza.
    #
    # Namjerno su spori i nisu iskljuceni iz obicnog ctesta. Test koji se nikad ne vrti ne brani
    # alat. Ovo su tocno dva unaprijed zapisana kriterija iz HANDOVER-a.
    add_test(NAME test_residual_field_truthbench_clean
        COMMAND TruthBench luk 30 0 1280 0)
    set_tests_properties(test_residual_field_truthbench_clean PROPERTIES
        PASS_REGULAR_EXPRESSION "POLJE OSTATAKA: bijelo"
        TIMEOUT 600
        LABELS "slow;integration")

    add_test(NAME test_residual_field_truthbench_warp
        COMMAND TruthBench luk 30 0 1280 5)
    set_tests_properties(test_residual_field_truthbench_warp PROPERTIES
        PASS_REGULAR_EXPRESSION "POLJE OSTATAKA: polje se mijenja po kadru"
        TIMEOUT 600
        LABELS "slow;integration")
endif()
