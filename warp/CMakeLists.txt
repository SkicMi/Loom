# ============================================================================
#  Warp - scena. Loom crta, Spool cita i pise, Engine rjesava, Treadle je ono na
#  sto se klikne, a Warp je ono STO se crta, rjesava i klika: kamere, oblaci,
#  kocke i splatovi, u stablu i kroz vrijeme.
#
#  Osnova na tkalackom stanu: niti napete po duljini, na koje se sve ostalo
#  tka. Odatle ime - sve ostalo se veze za ovo.
#
#  Warp ovisi SAMO O GLM-u. Scena je cisti podatak: nema crtanja, nema datoteka
#  slika, nema solvera. Zato se stablo, kljucevi i transformacije testiraju bez
#  prozora (test_warp_stage).
#
#  Vlastiti CMakeLists - isto pravilo kao za Spool, Engine i Treadle.
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

add_library(Warp STATIC
    src/Warp/Stage.cpp
    src/Warp/UsdCamera.cpp
    src/Warp/Usda.cpp
    src/Warp/Project.cpp
)

target_include_directories(Warp PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(Warp PUBLIC glm::glm)
target_compile_definitions(Warp PRIVATE GLM_FORCE_RADIANS)

target_compile_options(Warp PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall -Wextra>
)
