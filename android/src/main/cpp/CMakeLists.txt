cmake_minimum_required(VERSION 3.18)
project(flutter_native_texture_android LANGUAGES CXX)

add_library(flutter_native_texture_android SHARED 
    flutter_native_texture_android_api.cpp
)

# Go up 4 levels to reach the project root, then into 'src'
target_include_directories(flutter_native_texture_android PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../../src
)

find_library(log-lib log)
find_library(android-lib android)
find_library(egl-lib EGL)
find_library(gles3-lib GLESv3)

target_link_libraries(flutter_native_texture_android PRIVATE
    ${log-lib}
    ${android-lib}
    ${egl-lib}
    ${gles3-lib}
)