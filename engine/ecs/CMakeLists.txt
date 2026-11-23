# Nimbus ECS module (header-only).
add_library(nimbus_ecs INTERFACE)
target_include_directories(nimbus_ecs INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(nimbus_ecs INTERFACE nimbus_core)
target_compile_features(nimbus_ecs INTERFACE cxx_std_20)
