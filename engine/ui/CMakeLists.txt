# Nimbus UI module.
add_library(nimbus_ui STATIC
  src/ui.cpp
)
target_include_directories(nimbus_ui PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(nimbus_ui PUBLIC nimbus_core)
target_compile_features(nimbus_ui PUBLIC cxx_std_20)
