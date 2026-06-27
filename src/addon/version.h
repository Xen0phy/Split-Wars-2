#pragma once

constexpr int Maj = 1;
constexpr int Min = 4;
constexpr int Bld = 6;
constexpr int Rev = 1;

static constexpr const char* VersionNotice =
    "Welcome! If you're new, I hope you enjoy Split Wars 2.\n\n"
    "New Features with 1.4.4.1:\n"
    "  * Speedometer overlay\n"
    "    * fully customizable in-game speedometer drawn with ImGui\n"
    "    * supports custom needle and gauge-face textures\n"
    "      * place textures in \"Split Wars 2\\textures\" folder\n"
    "    * per-mount visibility filtering\n"
    "  * Fractal Rota\n"
    "    * Compares your current Fractal run against your best time from a previous run on the same 15-day rotation slot, if one exists in your kept run history.\n"
    "    * Accessible from the Hotbar right-click menu.";