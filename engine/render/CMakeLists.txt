# Nimbus render module.
add_library(nimbus_render STATIC
  src/renderer.cpp
  src/camera.cpp
  src/mesh_factory.cpp
)
target_include_directories(nimbus_render PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(nimbus_render PUBLIC nimbus_rhi nimbus_scene nimbus_core)
target_compile_features(nimbus_render PUBLIC cxx_std_20)
