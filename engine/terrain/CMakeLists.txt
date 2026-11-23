# Nimbus terrain module.
add_library(nimbus_terrain STATIC
  src/terrain.cpp
)
target_include_directories(nimbus_terrain PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(nimbus_terrain PUBLIC nimbus_render nimbus_core)
target_compile_features(nimbus_terrain PUBLIC cxx_std_20)
