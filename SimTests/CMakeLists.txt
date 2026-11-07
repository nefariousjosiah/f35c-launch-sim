# Standalone build of the LaunchSim module and its tests, without Unreal.
# This is what CI runs: the launch physics is pure C++ by design.
cmake_minimum_required(VERSION 3.25)
project(LaunchSimTests LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

set(LAUNCHSIM_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../Source/LaunchSim)

# Everything except LaunchSimModule.cpp, the module's only Unreal-specific file.
add_library(launchsim STATIC ${LAUNCHSIM_DIR}/Private/LaunchSimulation.cpp)
target_include_directories(launchsim PUBLIC ${LAUNCHSIM_DIR}/Public)
if(MSVC)
  target_compile_options(launchsim PRIVATE /W4 /permissive-)
else()
  target_compile_options(launchsim PRIVATE -Wall -Wextra -Wpedantic -Wshadow -Wconversion)
endif()

include(FetchContent)
FetchContent_Declare(Catch2
  URL https://github.com/catchorg/Catch2/archive/refs/tags/v3.7.1.tar.gz
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE
  SYSTEM
)
FetchContent_MakeAvailable(Catch2)

enable_testing()
add_executable(launchsim_tests test_launch.cpp)
target_link_libraries(launchsim_tests PRIVATE launchsim Catch2::Catch2WithMain)
if(MINGW)
  target_link_options(launchsim_tests PRIVATE -static)
endif()
list(APPEND CMAKE_MODULE_PATH ${catch2_SOURCE_DIR}/extras)
include(Catch)
catch_discover_tests(launchsim_tests)
