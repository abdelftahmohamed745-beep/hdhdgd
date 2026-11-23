# Nimbus audio module.
add_library(nimbus_audio STATIC
  src/mixer.cpp
  src/opensles_backend.cpp
)
target_include_directories(nimbus_audio PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(nimbus_audio PUBLIC nimbus_core)
target_compile_features(nimbus_audio PUBLIC cxx_std_20)
if(NIMBUS_ANDROID)
  target_link_libraries(nimbus_audio PUBLIC OpenSLES)
endif()
