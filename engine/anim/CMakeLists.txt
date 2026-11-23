# Nimbus animation module.
add_library(nimbus_anim STATIC
  src/animator.cpp
)
target_include_directories(nimbus_anim PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(nimbus_anim PUBLIC nimbus_ecs nimbus_core)
target_compile_features(nimbus_anim PUBLIC cxx_std_20)
