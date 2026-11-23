# Nimbus core: platform-independent math, timing, jobs, and diagnostics.
add_library(nimbus_core STATIC
  src/log.cpp
  src/jobs.cpp
)
target_include_directories(nimbus_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_features(nimbus_core PUBLIC cxx_std_20)
find_package(Threads REQUIRED)
target_link_libraries(nimbus_core PUBLIC Threads::Threads)
