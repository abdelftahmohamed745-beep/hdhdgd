cmake_minimum_required(VERSION 3.22.1)
project(nimbus_android LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

set(NIMBUS_ANDROID ON CACHE BOOL "Build Nimbus for Android" FORCE)
set(NIMBUS_BUILD_TESTS OFF CACHE BOOL "Do not build host tests in the APK" FORCE)
option(NIMBUS_BUILD_EDITOR "Package Nimbus Editor core in the APK" OFF)
set(NIMBUS_ENABLE_LUA OFF CACHE BOOL "Lua runtime is optional on Android" FORCE)
set(NIMBUS_ENABLE_VULKAN OFF CACHE BOOL "Use the GLES backend for Android runtime" FORCE)
set(NIMBUS_ENABLE_GLES ON CACHE BOOL "Build the Android GLES backend" FORCE)

get_filename_component(NIMBUS_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../../../../" ABSOLUTE)
add_subdirectory("${NIMBUS_ROOT}" "${CMAKE_CURRENT_BINARY_DIR}/nimbus-engine")

add_library(nimbus_android SHARED native_engine.cpp)
target_compile_features(nimbus_android PRIVATE cxx_std_20)
target_link_libraries(nimbus_android PRIVATE nimbus_engine android log EGL GLESv3 OpenSLES)

if(NIMBUS_BUILD_EDITOR AND TARGET nimbus_editor)
  target_compile_definitions(nimbus_android PRIVATE NIMBUS_HAS_EDITOR=1)
  target_link_libraries(nimbus_android PRIVATE nimbus_editor)
endif()
