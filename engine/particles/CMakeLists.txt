# Nimbus particles module.
add_library(nimbus_particles STATIC
  src/system.cpp
)
target_include_directories(nimbus_particles PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(nimbus_particles PUBLIC nimbus_core)
target_compile_features(nimbus_particles PUBLIC cxx_std_20)
