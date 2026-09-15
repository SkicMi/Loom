# ============================================================================
#  Treadle - suicelje. Loom crta, Spool cita i pise, Engine rjesava, Treadle je
#  ono na sto se klikne.
#
#  Papucica na tkalackom stanu: dio kojim tkalac stanom upravlja. Odatle ime -
#  ovo je povrsina preko koje covjek pokrece sve ostalo.
#
#  Treadle NE OVISI NI O CEMU. Ni o Loomu, ni o Vulkanu, ni o GLFW-u, ni o glm-u -
#  nema nijednu vanjsku ovisnost. Ulaz mu je zapis brojeva (Treadle::Input), izlaz
#  polje vrhova u pikselima (Treadle::DrawList), a tko to crta njegov je problem.
#
#  Tako je zato sto se UI inace ne da testirati: raspored, pogadjanje misem i
#  racun klizaca su cist racun, i test_treadle_ui ih provjerava bez ijednog
#  piksela, prozora i kartice.
#
#  Vlastiti CMakeLists da se ovaj folder moze odvojiti u vlastiti repo bez
#  ijedne izmjene - isto pravilo kao za Spool i Engine.
# ============================================================================
cmake_minimum_required(VERSION 3.20)

add_library(Treadle STATIC
    src/Treadle/Draw.cpp
    src/Treadle/Font.cpp
    src/Treadle/Ui.cpp
)

target_include_directories(Treadle PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)

target_compile_options(Treadle PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall -Wextra>
)
