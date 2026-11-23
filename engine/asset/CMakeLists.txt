# Nimbus asset module.
add_library(nimbus_asset STATIC
  src/asset.cpp
)
target_include_directories(nimbus_asset PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(nimbus_asset PUBLIC nimbus_core)
target_compile_features(nimbus_asset PUBLIC cxx_std_20)
