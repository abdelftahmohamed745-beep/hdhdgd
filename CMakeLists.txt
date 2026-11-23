# Nimbus Engine — portable C++20 build with optional platform backends.
cmake_minimum_required(VERSION 3.20)
project(nimbus VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(NIMBUS_ANDROID "Build using the Android NDK toolchain" OFF)
option(NIMBUS_BUILD_TESTS "Build the test runner" ON)
option(NIMBUS_BUILD_EDITOR "Build the editor and MCP server" ON)
option(NIMBUS_ENABLE_LUA "Enable Lua 5.4 scripting when its runtime library is available" ON)
option(NIMBUS_ENABLE_VULKAN "Build the Vulkan backend when development headers are available" ON)
option(NIMBUS_ENABLE_GLES "Build the OpenGL ES backend when development headers are available" ON)
option(NIMBUS_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" OFF)

if(NIMBUS_ANDROID AND NOT ANDROID)
  message(FATAL_ERROR
    "NIMBUS_ANDROID=ON requires the Android NDK toolchain at configure time. Use: "
    "cmake -S . -B build-android -DCMAKE_TOOLCHAIN_FILE=$ANDROID_NDK_HOME/build/cmake/android.toolchain.cmake "
    "-DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-24 -DNIMBUS_ANDROID=ON")
endif()
if(ANDROID)
  set(NIMBUS_ANDROID ON CACHE BOOL "Android build" FORCE)
endif()

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  add_compile_options(-Wall -Wextra -Wpedantic)
  if(NIMBUS_WARNINGS_AS_ERRORS)
    add_compile_options(-Werror)
  endif()
endif()

# Lua has a stable C ABI. The engine carries a small declaration-only fallback
# for environments that ship liblua5.4 but omit development headers.
set(NIMBUS_HAS_LUA OFF)
if(NIMBUS_ENABLE_LUA)
  find_library(LUA_LIBRARY NAMES lua5.4 lua54)
  if(NOT LUA_LIBRARY AND NOT ANDROID)
    find_file(LUA_LIBRARY NAMES liblua5.4.so.0 libluajit-5.1.so.2
      PATHS /lib /usr/lib /usr/local/lib
      PATH_SUFFIXES x86_64-linux-gnu aarch64-linux-gnu)
  endif()
  if(LUA_LIBRARY)
    set(NIMBUS_HAS_LUA ON)
    message(STATUS "nimbus: Lua runtime: ${LUA_LIBRARY}")
  else()
    message(WARNING "nimbus: Lua runtime not found; dynamic loader will still probe at runtime")
  endif()
endif()

add_subdirectory(engine/core)
add_subdirectory(engine/input)
add_subdirectory(engine/ecs)
add_subdirectory(engine/scene)
add_subdirectory(engine/rhi)
add_subdirectory(engine/render)
add_subdirectory(engine/physics)
add_subdirectory(engine/anim)
add_subdirectory(engine/terrain)
add_subdirectory(engine/particles)
add_subdirectory(engine/audio)
add_subdirectory(engine/ai)
add_subdirectory(engine/net)
add_subdirectory(engine/ui)
add_subdirectory(engine/asset)
add_subdirectory(engine/script)
add_subdirectory(engine/engine)

if(NIMBUS_BUILD_EDITOR)
  add_subdirectory(editor)
endif()

if(NIMBUS_BUILD_TESTS)
  enable_testing()
  add_executable(nimbus_tests tests/main.cpp)
  target_link_libraries(nimbus_tests PRIVATE nimbus_engine)
  if(NIMBUS_BUILD_EDITOR)
    target_link_libraries(nimbus_tests PRIVATE nimbus_mcp)
  endif()
  add_test(NAME nimbus_engine_tests COMMAND nimbus_tests)
endif()

if(NIMBUS_BUILD_EDITOR)
  add_executable(nimbus_mcp_server editor/mcp/main.cpp)
  target_link_libraries(nimbus_mcp_server PRIVATE nimbus_mcp)
endif()
