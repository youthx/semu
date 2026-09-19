cmake_minimum_required(VERSION 3.20)
project(sere_native LANGUAGES C CXX)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

add_library(sere_native STATIC native.cpp)

# Your existing include
target_include_directories(sere_native PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/../../venv/include"
)

# MSYS2 SDL3 include
target_include_directories(sere_native PUBLIC
    "C:/msys64/mingw64/include"
)

# MSYS2 SDL3 lib
target_link_directories(sere_native PUBLIC
    "C:/msys64/mingw64/lib"
)

# Link SDL3
target_link_libraries(sere_native PUBLIC SDL3)
