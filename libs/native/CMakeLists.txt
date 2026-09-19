cmake_minimum_required(VERSION 3.20)

project(sere_native LANGUAGES C CXX)

add_library(sere_native STATIC native.cpp semu.rc)

# Your existing include
target_include_directories(sere_native PUBLIC
    "C:\\Users\\jackw\\OneDrive\\Desktop\\git-projects\\sere-projects\\semu\\venv\\include"
)

# MSYS2 SDL3 include + lib
target_include_directories(sere_native PUBLIC
    "C:/msys64/mingw64/include"
)

target_link_directories(sere_native PUBLIC
    "C:/msys64/mingw64/lib"
)

# Link SDL3
target_link_libraries(sere_native PUBLIC SDL3)
