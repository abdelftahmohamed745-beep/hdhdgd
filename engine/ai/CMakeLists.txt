# Nimbus AI module.
add_library(nimbus_ai STATIC
  src/ai.cpp
)
target_include_directories(nimbus_ai PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(nimbus_ai PUBLIC nimbus_ecs nimbus_core)
target_compile_features(nimbus_ai PUBLIC cxx_std_20)
