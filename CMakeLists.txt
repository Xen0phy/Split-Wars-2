cmake_minimum_required(VERSION 3.20)
project(SplitWars2)

set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_C_COMPILER x86_64-w64-mingw32-gcc)
set(CMAKE_CXX_COMPILER x86_64-w64-mingw32-g++)
set(CMAKE_CXX_STANDARD 17)

find_package(Python3 REQUIRED COMPONENTS Interpreter)

add_custom_target(bump_rev
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/bump_rev.py
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    COMMENT "Bumping revision..."
    VERBATIM
)

add_library(SplitWars2 SHARED
    src/entry.cpp
    src/addon.cpp
    src/addon_options.cpp
    src/arcdps_events.cpp
    src/shared.cpp
    src/timer.cpp
    src/route.cpp
    src/render_browser.cpp
    src/render_config.cpp
    src/render_debug_arcdps_dump.cpp
    src/render_debug_mumble_dump.cpp
    src/render_debug_rtapi_dump.cpp
    src/render_debug.cpp
    src/render_history.cpp
    src/render_shared.cpp
    src/render_timer.cpp
    src/worldrender.cpp
    src/storage.cpp
    include/imgui.cpp
    include/imgui_draw.cpp
    include/imgui_tables.cpp
    include/imgui_widgets.cpp
)

add_dependencies(SplitWars2 bump_rev)

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