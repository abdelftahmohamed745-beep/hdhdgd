# Nimbus editor module.
add_library(nimbus_editor STATIC
  src/editor.cpp
)
target_include_directories(nimbus_editor PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(nimbus_editor PUBLIC nimbus_engine)
target_compile_features(nimbus_editor PUBLIC cxx_std_20)

# Nimbus MCP server module.
add_library(nimbus_mcp STATIC
  mcp/src/server.cpp
)
target_include_directories(nimbus_mcp PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/mcp/include)
target_link_libraries(nimbus_mcp PUBLIC nimbus_editor)
target_compile_features(nimbus_mcp PUBLIC cxx_std_20)
