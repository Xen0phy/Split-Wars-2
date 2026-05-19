cmake_minimum_required(VERSION 3.20)
project(gw2-speedrun)

set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_C_COMPILER x86_64-w64-mingw32-gcc)
set(CMAKE_CXX_COMPILER x86_64-w64-mingw32-g++)
set(CMAKE_CXX_STANDARD 17)

add_library(gw2-speedrun SHARED
    src/entry.cpp
    src/shared.cpp
    src/timer.cpp
    src/route.cpp
    src/renderer.cpp
    src/worldrender.cpp
    src/storage.cpp
    include/imgui.cpp
    include/imgui_draw.cpp
    include/imgui_tables.cpp
    include/imgui_widgets.cpp
)

target_include_directories(gw2-speedrun PRIVATE include)

set_target_properties(gw2-speedrun PROPERTIES
    OUTPUT_NAME "SplitWars2"
    PREFIX ""
    SUFFIX ".dll"
)

target_link_options(gw2-speedrun PRIVATE
    -static-libgcc
    -static-libstdc++
    -static
)