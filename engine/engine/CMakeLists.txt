# Nimbus top-level engine module.
add_library(nimbus_engine STATIC
  src/engine.cpp
  src/tests.cpp
)
target_include_directories(nimbus_engine PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(nimbus_engine PUBLIC
  nimbus_core nimbus_input nimbus_ecs nimbus_scene nimbus_render nimbus_rhi
  nimbus_physics nimbus_anim nimbus_terrain nimbus_particles
  nimbus_audio nimbus_ai nimbus_net nimbus_ui nimbus_asset nimbus_script
)
target_compile_features(nimbus_engine PUBLIC cxx_std_20)
