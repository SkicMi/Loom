# ============================================================================
#  Provjerava propusta li jedan prevodilacki file Vulkan.
#
#  Ne cita se izvorni kod nego se file PREPROCESIRA i trazi se u rezultatu -
#  jer curenje nikad nije u fileu koji gledas, nego u onome sto je on ukljucio,
#  pa je jedini posten test onaj koji gleda sve zajedno.
#
#  Zove se s:
#    cmake -DCOMPILER=.. -DSOURCE=.. -DINCLUDES=a;b;c -DEXPECT=clean|dirty
#          -P HeaderIsClean.cmake
# ============================================================================

if(NOT DEFINED COMPILER OR NOT DEFINED SOURCE OR NOT DEFINED EXPECT)
    message(FATAL_ERROR "HeaderIsClean: treba COMPILER, SOURCE i EXPECT")
endif()

set(INCLUDE_FLAGS "")
foreach(DIR ${INCLUDES})
    list(APPEND INCLUDE_FLAGS "-I${DIR}")
endforeach()

execute_process(
    COMMAND ${COMPILER} -std=c++17 -E ${INCLUDE_FLAGS} ${SOURCE}
    OUTPUT_VARIABLE PREPROCESSED
    ERROR_VARIABLE  COMPILER_ERRORS
    RESULT_VARIABLE COMPILER_RESULT
)

if(NOT COMPILER_RESULT EQUAL 0)
    message(FATAL_ERROR "HeaderIsClean: ${SOURCE} se ne da preprocesirati:\n${COMPILER_ERRORS}")
endif()

# Dva traga: tipovi (vk::) i putanje headera (vulkan). Prvi hvata simbole koji
# su dosli u nas potpis, drugi hvata i sam include koji jos nista ne koristi.
string(REGEX MATCHALL "vk::" TYPE_HITS "${PREPROCESSED}")
string(REGEX MATCHALL "[Vv]ulkan" PATH_HITS "${PREPROCESSED}")

list(LENGTH TYPE_HITS TYPE_COUNT)
list(LENGTH PATH_HITS PATH_COUNT)
math(EXPR TOTAL "${TYPE_COUNT} + ${PATH_COUNT}")

string(LENGTH "${PREPROCESSED}" SIZE)

if(EXPECT STREQUAL "clean")
    if(TOTAL GREATER 0)
        message(FATAL_ERROR
            "curi Vulkan: ${SOURCE} nakon preprocesiranja sadrzi ${TYPE_COUNT} 'vk::' i ${PATH_COUNT} 'vulkan'")
    endif()
    message(STATUS "cisto: ${SOURCE} -> ${SIZE} znakova, 0 'vk::', 0 'vulkan'")

elseif(EXPECT STREQUAL "dirty")
    # Kontrola. Bez ovoga detektor koji nista ne nalazi izgleda isto kao detektor
    # koji ne radi
    if(TOTAL EQUAL 0)
        message(FATAL_ERROR
            "detektor ne radi: ${SOURCE} bi trebao vidjeti Vulkan, a nije nasao nista")
    endif()
    message(STATUS "detektor radi: ${SOURCE} -> ${TYPE_COUNT} 'vk::', ${PATH_COUNT} 'vulkan'")

else()
    message(FATAL_ERROR "HeaderIsClean: EXPECT mora biti clean ili dirty, dobio '${EXPECT}'")
endif()
