//The one translation unit that compiles VulkanMemoryAllocator itself.
//
//It is on its own for two reasons: VMA is ~16000 lines and would be re-parsed by every file
//that includes it, and it is third party code compiled with warnings off (CMake gives this
//file -w) so Loom's own -Wall -Wextra stays honest.
//
//The VMA_* configuration macros are NOT here on purpose - they live in CMakeLists.txt as
//target_compile_definitions, because VMA_VULKAN_VERSION changes the layout of
//VmaVulkanFunctions. If this file said one version and VulkanAllocator.cpp another, the
//struct would be filled in one shape and read in another
#define VMA_IMPLEMENTATION
#include <vk_mem_alloc.h>
