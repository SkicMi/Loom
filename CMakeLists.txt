# ============================================================================
#  CMakeLists.txt - projekt "Loom"
#
#  Ovo je jedina datoteka u ovoj mapi i ona je ROOT (korijenska) CMake
#  datoteka projekta. CMake nije kompajler - to je alat koji CITA ovakve
#  datoteke i na temelju njih GENERIRA pravi build sustav (npr. Makefile
#  za "make", ili Ninja datoteke, ili Visual Studio .sln projekt...).
#  Datoteka MORA se zvati tocno "CMakeLists.txt" (velika/mala slova bitna
#  su na Linuxu) da bi je CMake uopce prepoznao kad pokrenes `cmake .`.
# ============================================================================


# ----------------------------------------------------------------------------
# 1) Minimalna verzija CMake-a
# ----------------------------------------------------------------------------
# Svaka CMakeLists.txt na svom pocetku MORA reci koju najstariju verziju
# CMake-a ocekuje. Ako korisnik ima stariju verziju instaliranu, CMake ce
# odmah javiti jasnu gresku ("potrebna je verzija X, imas Y") umjesto da
# se konfiguracija cudno slomi kasnije zbog nepostojece komande.
# Trazimo 3.20 jer nam treba moderna naredba FetchContent_MakeAvailable().
cmake_minimum_required(VERSION 3.20)


# ----------------------------------------------------------------------------
# 2) project() - definicija projekta i ROOT statusa
# ----------------------------------------------------------------------------
# project() mora doci ODMAH nakon cmake_minimum_required(). Ona postavlja
# ime projekta, njegovu verziju i programske jezike koje CMake treba
# pripremiti (provjeriti kompajlere za njih itd).
#
# Kad se project() prvi put pozove u nekom CMakeLists.txt-u, i ta datoteka
# nije ukljucena (preko add_subdirectory) iz neke NADREDJENE CMakeLists.txt
# datoteke, onda taj projekt automatski postaje "root" (korijenski) projekt
# cijelog builda. CMake tada interno postavi varijable poput
# CMAKE_SOURCE_DIR (put do ove mape) i CMAKE_PROJECT_NAME na temelju ovoga -
# to je ono sto se u zadatku misli pod "root".
project(Loom
    VERSION 0.1.0     # verzija naseg projekta - broj je proizvoljan, mi ga biramo
    LANGUAGES C CXX   # kojim jezicima ce se ovaj projekt gradati (C i C++)
)


# ----------------------------------------------------------------------------
# 3) Standard jezika
# ----------------------------------------------------------------------------
# Ovime kazemo CMake-u koju verziju C++ standarda ocekujemo (17), a
# CXX_STANDARD_REQUIRED ON znaci "ako kompajler ne podrzava C++17, prekini
# konfiguraciju s greskom" umjesto da CMake tiho padne na stariji standard.
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)


# ----------------------------------------------------------------------------
# 4) include(FetchContent)
# ----------------------------------------------------------------------------
# FetchContent je modul koji vec dolazi ugradjen u CMake (ne treba ga
# posebno instalirati). On omogucuje da CMake sam, tijekom faze
# konfiguracije, preuzme (klonira s Git-a) izvorni kod neke vanjske
# biblioteke i ukljuci ga u nas build - bez da korisnik mora rucno
# instalirati tu biblioteku na svoj sustav. include() ovdje samo ucitava
# taj modul kako bismo ispod mogli koristiti njegove naredbe.
include(FetchContent)


# ----------------------------------------------------------------------------
# 5) Dohvacanje najnovijeg Vulkana (Vulkan-Headers) preko fetch-a
# ----------------------------------------------------------------------------
# FetchContent_Declare() samo OPISUJE odakle nesto treba povuci i koju
# verziju/granu - ovime se jos NISTA ne preuzima, to je tek "recept".
#
# Koristimo sluzbeni Khronos Group repozitorij "Vulkan-Headers" jer on
# sadrzi najnovije Vulkan API definicije (vulkan.h i ostalo), tako da
# projekt uvijek koristi najnoviji Vulkan API, neovisno o tome koja je
# (mozda starija) verzija instalirana na ovom racunalu.
FetchContent_Declare(
    VulkanHeaders
    GIT_REPOSITORY https://github.com/KhronosGroup/Vulkan-Headers.git
    GIT_TAG        main    # "main" = uvijek najnovija verzija; za stabilniji/
                            # ponovljiv build moze se umjesto toga staviti
                            # konkretan tag, npr. "v1.4.350"
    GIT_SHALLOW    TRUE    # povuci samo zadnji commit, ne cijelu povijest
                            # (brze preuzimanje, manje mjesta na disku)
)


# ----------------------------------------------------------------------------
# 6) Dohvacanje GLFW-a preko fetch-a
# ----------------------------------------------------------------------------
# Isti princip kao gore, ali za GLFW - biblioteku koja otvara prozor
# operacijskog sustava i hvata unos s tipkovnice/misa, a inace se koristi
# zajedno s Vulkanom za prikazivanje sadrzaja na ekran.
FetchContent_Declare(
    glfw
    GIT_REPOSITORY https://github.com/glfw/glfw.git
    GIT_TAG        master
    GIT_SHALLOW    TRUE
)

# GLFW po defaultu, kad se gradi iz izvornog koda, pokusa graditi i svoje
# primjere, testove i dokumentaciju. To nama ovdje ne treba, pa te opcije
# gasimo PRIJE nego sto GLFW stvarno preuzmemo i ukljucimo u build -
# rezultat je brzi i cisci build.
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS    OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_DOCS     OFF CACHE BOOL "" FORCE)


# ----------------------------------------------------------------------------
# 7) FetchContent_MakeAvailable() - stvarno preuzimanje
# ----------------------------------------------------------------------------
# Ovo je trenutak kad CMake STVARNO (ako vec nije prije preuzeto i
# spremljeno u cache) klonira gore deklarirane repozitorije s Git-a, i
# zatim ih doda u nas projekt kao da smo ih rucno stavili u pod-mapu i
# pozvali add_subdirectory() na njih. Nakon ove linije, mozemo koristiti
# njihove CMake "targete" (Vulkan-Headers i glfw) u ostatku projekta.
FetchContent_MakeAvailable(VulkanHeaders glfw)


# ============================================================================
# Napomena: ovo je namjerno SAMO CMake konfiguracija, bez izvornog koda
# (nema .c/.cpp datoteka u mapi). Kad se doda npr. main.c, ispod bi doslo:
#
#   add_executable(Loom main.c)
#   target_link_libraries(Loom PRIVATE Vulkan-Headers glfw)
#
# add_executable() definira STO se gradi (izvrsna datoteka "Loom" iz
# main.c), a target_link_libraries() kaze toj izvrsnoj datoteci koje
# vanjske "targete" (biblioteke) smije koristiti - ovdje Vulkan headere
# i GLFW koje smo gore preuzeli.
# ============================================================================
