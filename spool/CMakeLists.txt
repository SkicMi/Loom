# ============================================================================
#  Spool - import i export. Loom crta; Spool je odakle stvari dolaze i kamo idu.
#
#  Spool NE ovisi o Loomu i Loom NE ovisi o Spoolu. Smjer je uvijek
#  aplikacija -> Spool i aplikacija -> Loom. Zato slika izlazi kao obicni
#  bajtovi, a ne kao nesto sto Loom definira: cim bi Spool vratio Loomov tip,
#  granica "kako se crta" / "odakle je doslo" bi nestala.
#
#  Vlastiti CMakeLists da se ovaj folder moze odvojiti u vlastiti repo bez
#  ijedne izmjene.
# ============================================================================
cmake_minimum_required(VERSION 3.20)
include(FetchContent)

# stb nema tagove, pa je pinnan na konkretan commit - isto pravilo kao za
# ostale dependencije. SOURCE_SUBDIR pokazuje na nepostojeci folder jer je
# stb samo hrpa headera bez CMake projekta.
FetchContent_Declare(
    stb
    GIT_REPOSITORY https://github.com/nothings/stb.git
    GIT_TAG        2c980bb59875b0d32144a71867fbdebb2f77cd20
    SOURCE_SUBDIR  no-cmake-here
)
FetchContent_MakeAvailable(stb)

add_library(Spool STATIC
    src/Spool/ImageFile.cpp
    src/Spool/StbImplementation.cpp
)

target_include_directories(Spool PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)

# stb je PRIVATE: Spoolovi headeri ga ne spominju, pa ga korisnik nikad ne vidi
target_include_directories(Spool PRIVATE ${stb_SOURCE_DIR})

target_compile_options(Spool PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall -Wextra>
)

# Jedini file koji prevodi samu stb-u. Tudi kod, bez nasih upozorenja.
set_source_files_properties(
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Spool/StbImplementation.cpp
    PROPERTIES COMPILE_OPTIONS "-w"
)
