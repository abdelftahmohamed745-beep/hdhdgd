# Nimbus scene module.
add_library(nimbus_scene STATIC
  src/serialize.cpp
)
target_include_directories(nimbus_scene PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(nimbus_scene PUBLIC nimbus_ecs nimbus_core)
target_compile_features(nimbus_scene PUBLIC cxx_std_20)
