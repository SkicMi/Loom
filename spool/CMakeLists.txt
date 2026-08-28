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

# FFmpeg preko pkg-configa. Trazi se izricito: Spool bez njega ne zna citati video, i
# bolje je da to padne ovdje nego da nedostaje tek na prvom pozivu.
#   sudo apt install libavformat-dev libavcodec-dev libavutil-dev libswscale-dev
find_package(PkgConfig REQUIRED)
pkg_check_modules(FFMPEG IMPORTED_TARGET
    libavformat
    libavcodec
    libavutil
    libswscale
)

# Vlastita poruka umjesto pkg-configove: kad build padne zbog sistemskog paketa, jedina
# korisna stvar u poruci je red koji ga instalira
if(NOT FFMPEG_FOUND)
    message(FATAL_ERROR
        "Spool treba FFmpeg razvojne pakete za citanje videa.\n"
        "  Debian/Ubuntu: sudo apt install libavformat-dev libavcodec-dev libavutil-dev libswscale-dev\n"
        "  Fedora:        sudo dnf install ffmpeg-devel\n"
        "  Arch:          sudo pacman -S ffmpeg")
endif()

add_library(Spool STATIC
    src/Spool/ImageFile.cpp
    src/Spool/Sequence.cpp
    src/Spool/DepthFile.cpp
    src/Spool/VideoFile.cpp
    src/Spool/StbImplementation.cpp
)

target_include_directories(Spool PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)

# stb je PRIVATE: Spoolovi headeri ga ne spominju, pa ga korisnik nikad ne vidi
target_include_directories(Spool PRIVATE ${stb_SOURCE_DIR})

# I FFmpeg je PRIVATE, iz istog razloga: VideoFile.h ne spominje nijedan njegov tip, nego
# ga skriva iza pimpla. Korisnik Spoola nikad ne vidi da libav postoji
target_link_libraries(Spool PRIVATE PkgConfig::FFMPEG)

target_compile_options(Spool PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall -Wextra>
)

# Jedini file koji prevodi samu stb-u. Tudi kod, bez nasih upozorenja.
set_source_files_properties(
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Spool/StbImplementation.cpp
    PROPERTIES COMPILE_OPTIONS "-w"
)
