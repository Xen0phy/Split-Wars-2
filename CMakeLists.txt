cmake_minimum_required(VERSION 3.20)
project(SplitWars2)

set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_C_COMPILER x86_64-w64-mingw32-gcc)
set(CMAKE_CXX_COMPILER x86_64-w64-mingw32-g++)
set(CMAKE_CXX_STANDARD 17)

add_library(SplitWars2 SHARED
    src/addon.cpp
    src/shared.cpp
    src/timer.cpp
    src/route.cpp
    src/renderer_shared.cpp
    src/renderer_timer.cpp
    src/renderer_config.cpp
    src/renderer_history.cpp
    src/renderer_browser.cpp
    src/worldrender.cpp
    src/storage.cpp
    include/imgui.cpp
    include/imgui_draw.cpp
    include/imgui_tables.cpp
    include/imgui_widgets.cpp
)

target_include_directories(SplitWars2 PRIVATE include)

set_target_properties(SplitWars2 PROPERTIES
    OUTPUT_NAME "SplitWars2"
    PREFIX ""
    SUFFIX ".dll"
)

target_compile_options(SplitWars2 PRIVATE
    -Os                  # optimise for size (smaller than -O2)
    -ffunction-sections  # put each function in its own section
    -fdata-sections      # put each variable in its own section
    -fno-rtti            # no dynamic_cast/typeid — safe, nothing in this codebase uses it
)

target_link_options(SplitWars2 PRIVATE
    -static-libgcc
    -static-libstdc++
    -static
    -s                        # strip all debug symbols
    -Wl,--gc-sections         # dead-code elimination: drop anything not reachable
    -Wl,--exclude-libs,ALL    # don't re-export symbols from static libs into the DLL
)