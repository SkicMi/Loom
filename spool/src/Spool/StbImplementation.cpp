//The one translation unit that compiles stb_image itself.
//
//Same reasoning as Loom's VmaImplementation.cpp: third party code, parsed once instead of
//by every file that includes it, and built with warnings off (CMake gives this file -w) so
//that Spool's own -Wall -Wextra stays worth reading.
//
//STBI_FAILURE_USERMSG turns stb's terse internal tags into sentences, which is what ends up
//in the exception a caller sees.
#define STB_IMAGE_IMPLEMENTATION
#define STBI_FAILURE_USERMSG
#include <stb_image.h>
