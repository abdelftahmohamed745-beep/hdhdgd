# Nimbus input subsystem: keyboard, pointer, multi-touch, gestures, gamepad, action mapping.
add_library(nimbus_input STATIC src/input.cpp)
target_include_directories(nimbus_input PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(nimbus_input PUBLIC nimbus_core)
target_compile_features(nimbus_input PUBLIC cxx_std_20)
