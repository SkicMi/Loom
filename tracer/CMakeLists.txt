# ============================================================================
#  Tracer (LoomTracer) - fizikalni path tracer za konacni render.
#
#  Pogled editora je raster: brz, ali svjetlo u njemu je dogovor (kljucno i
#  popunjavajuce svjetlo, nebo kao dvije boje). Render iz kamere mora odgovarati
#  na drugo pitanje - kako bi taj objekt STVARNO izgledao u toj sceni, pod tim
#  svjetlom, preko te snimke. To je Monte Carlo integral jednadzbe renderiranja
#  i ovdje se racuna bez precaca: globalno osvjetljenje, meke sjene, refrakcija,
#  hrapave refleksije s ocuvanjem energije, sjena na snimci (shadow catcher).
#
#  Ovisi SAMO O GLM-u i dretvama - isto pravilo kao Warp. Ne zna za Vulkan, ni
#  za datoteke, ni za scenu editora: dobije trokute, materijale, svjetla i
#  kameru, a vrati polja floatova. Zato se testira bez prozora i bez kartice,
#  protiv analiticki poznatih odgovora (test_tracer).
#
#  Vlastiti CMakeLists - isto pravilo kao za Spool, Engine, Treadle i Warp.
# ============================================================================
cmake_minimum_required(VERSION 3.20)

if(NOT TARGET glm::glm)
    include(FetchContent)
    FetchContent_Declare(
        glm
        GIT_REPOSITORY https://github.com/g-truc/glm.git
        GIT_TAG        1.0.1
    )
    FetchContent_MakeAvailable(glm)
endif()

find_package(Threads REQUIRED)

add_library(Tracer STATIC
    src/Tracer/Bvh.cpp
    src/Tracer/Bsdf.cpp
    src/Tracer/Environment.cpp
    src/Tracer/Scene.cpp
    src/Tracer/Renderer.cpp
    src/Tracer/Denoise.cpp
    src/Tracer/Film.cpp
)

target_include_directories(Tracer PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(Tracer PUBLIC glm::glm Threads::Threads)
target_compile_definitions(Tracer PRIVATE GLM_FORCE_RADIANS)

target_compile_options(Tracer PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall -Wextra>
)
