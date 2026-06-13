// render_speedo.h
// Speedometer overlay for Split Wars 2.
//
// Derives the player's movement speed each frame from the delta between
// consecutive GS.PlayerX/Y/Z samples and displays it in a small ImGui window.
//
// Unit toggle: SpeedUnitMph (bool, persisted in settings_table.h)
//   false — km/h (default)
//   true  — mph
//
// Adding the speedo to the render loop:
//   1. #include "render_speedo.h" in render_shared.h
//   2. Declare RenderSpeedoWindow() in render_shared.h alongside the others
//   3. Call RenderSpeedoWindow() at the bottom of AddonRender() in addon.cpp

#pragma once

// ---------------------------------------------------------------------------
// Render entry point — call once per frame from AddonRender()
// ---------------------------------------------------------------------------
void RenderSpeedoWindow();