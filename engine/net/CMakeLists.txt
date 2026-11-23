# Nimbus networking module.
add_library(nimbus_net STATIC
  src/net.cpp
  src/socket.cpp
)
target_include_directories(nimbus_net PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(nimbus_net PUBLIC nimbus_core)
target_compile_features(nimbus_net PUBLIC cxx_std_20)
