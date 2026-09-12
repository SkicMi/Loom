# ============================================================================
#  Engine - sto se crta. Loom crta, Spool ucitava i sprema, Engine racuna gdje je
#  kamera bila: sinteticka referenca, pa triangulacija, PnP i bundle adjustment.
#
#  Engine NE ovisi ni o Loomu ni o Spoolu, i oni ne ovise o njemu. Smjer je uvijek
#  aplikacija -> Engine. Zato Engine ima vlastiti tip poze i vlastite intrinsike, a
#  ne Loomove: cim bi vratio Loomov tip, granica "sto se crta" / "kako se crta" bi
#  nestala. Konvencija je ipak ista kao Loomova (kamera gleda niz -Z, kvaternion za
#  orijentaciju) - i to test provjerava, jer poze na kraju idu upravo Loomu.
#
#  Vlastiti CMakeLists da se ovaj folder moze odvojiti u vlastiti repo bez ijedne
#  izmjene - isto pravilo kao za spool/.
# ============================================================================
cmake_minimum_required(VERSION 3.20)

# glm dolazi iz krovnog projekta kad ga ima; sam ga dovlaci kad Engine stoji sam.
# Isti pinnani tag kao gore, da dvije putanje ne mogu dati dvije verzije.
if(NOT TARGET glm::glm)
    include(FetchContent)
    FetchContent_Declare(
        glm
        GIT_REPOSITORY https://github.com/g-truc/glm.git
        GIT_TAG        1.0.1
    )
    FetchContent_MakeAvailable(glm)
endif()

add_library(Engine STATIC
    src/Engine/SyntheticScene.cpp
    src/Engine/Triangulate.cpp
    src/Engine/SolvePose.cpp
    src/Engine/Bundle.cpp
)

target_include_directories(Engine PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)

# glm je PUBLIC jer ga Engineovi headeri koriste u potpisima (vec3, quat)
target_link_libraries(Engine PUBLIC glm::glm)

# gtx/ su glm-ove "experimental" ekstenzije i bez ovoga se odbijaju prevesti.
# Treba nam glm::quatLookAt, ista funkcija koju koristi Loomova Camera. PRIVATE jer
# je spominje samo .cpp - Engineovi headeri ostaju bez nje
target_compile_definitions(Engine PRIVATE GLM_ENABLE_EXPERIMENTAL GLM_FORCE_RADIANS)

target_compile_options(Engine PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall -Wextra>
)
