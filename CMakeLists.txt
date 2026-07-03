cmake_minimum_required(VERSION 3.28)
project(${SKBUILD_PROJECT_NAME} LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(pybind11 CONFIG REQUIRED)

find_package(Vulkan REQUIRED COMPONENTS shaderc_combined)

include(FetchContent)

# glfw
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
  glfw
  GIT_REPOSITORY https://github.com/glfw/glfw.git
  GIT_TAG 3.4
  GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(glfw)

#volk
FetchContent_Declare(
    volk
    GIT_REPOSITORY https://github.com/zeux/volk.git
    GIT_TAG 1.4.350
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(volk)

#vma
FetchContent_Declare(
    vma
    GIT_REPOSITORY https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator.git
    GIT_TAG v3.4.0
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(vma)

if(NOT TARGET vma)
    add_library(vma INTERFACE)
    target_include_directories(vma INTERFACE ${vma_SOURCE_DIR}/include)
    target_compile_definitions(vma INTERFACE VMA_STATIC_VULKAN_FUNCTIONS=0 VMA_DYNAMIC_VULKAN_FUNCTIONS=1)
endif()

#vk-bootstrap
FetchContent_Declare(
    vk-bootstrap
    GIT_REPOSITORY https://github.com/charles-lunarg/vk-bootstrap.git
    GIT_TAG v1.4.350
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(vk-bootstrap)

add_library(lumapy MODULE src/main.cpp src/MpscQueue.hpp src/Logger.hpp src/window.hpp src/Renderer.hpp src/vma_impl.cpp)

target_link_libraries(lumapy PRIVATE 
    pybind11::module
    glfw
    volk
    vma
    vk-bootstrap::vk-bootstrap
    Vulkan::shaderc_combined
)
set_target_properties(lumapy PROPERTIES PREFIX "${PYTHON_MODULE_PREFIX}" SUFFIX "${PYTHON_MODULE_EXTENSION}")

target_compile_features(lumapy PRIVATE cxx_std_23)
if(MSVC)
    target_compile_options(lumapy PRIVATE /Zc:__cplusplus)
endif()

install(TARGETS lumapy DESTINATION .)