# Nimbus physics module.
add_library(nimbus_physics STATIC
  src/world.cpp
)
target_include_directories(nimbus_physics PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(nimbus_physics PUBLIC nimbus_scene nimbus_ecs nimbus_core)
target_compile_features(nimbus_physics PUBLIC cxx_std_20)
