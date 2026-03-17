cmake_minimum_required(VERSION 3.18)

set(PROJECT_NAME "flutter_native_texture")
project(${PROJECT_NAME} LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(PLUGIN_NAME "flutter_native_texture_plugin")

list(APPEND PLUGIN_SOURCES
  "flutter_native_texture_plugin.cpp"
  "flutter_native_texture_plugin.h"
  "flutter_native_texture_plugin_c_api.cpp"
)

add_library(${PLUGIN_NAME} SHARED
  "include/flutter_native_texture/flutter_native_texture_plugin_c_api.h"
  "../src/flutter_native_texture_api.h"
  ${PLUGIN_SOURCES}
)

target_include_directories(${PLUGIN_NAME} INTERFACE
  "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

target_include_directories(${PLUGIN_NAME} PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}/../src"
)

target_compile_definitions(${PLUGIN_NAME} PRIVATE FLUTTER_PLUGIN_IMPL)

target_link_libraries(${PLUGIN_NAME} PRIVATE
  flutter
  flutter_wrapper_plugin
  d3d11
  d3dcompiler
  dxgi
  dxguid
)