// renderer_shared.h
// Central header included by every renderer_*.cpp file.
//
// Pulls in all ImGui headers, the shared game-state header, and the storage
// header so each renderer only needs one include line.  Also declares the
// utility functions that are shared across renderers (time formatting,
// color selection, route loading) and the inline Tooltip helper.

#pragma once

#include "imgui.h"
#include "imgui_internal.h"
#include "shared.h"
#include <algorithm>
#include <cstring>
#include <cstdio>
#include <cmath>
#include <string>
#include <filesystem>

// ---------------------------------------------------------------------------
// Render entry points — one per UI window, each implemented in its own .cpp
// ---------------------------------------------------------------------------
void RenderTimerOverlay();
void RenderTimerOverlayStream();
void RenderConfigWindow();
void RenderHistoryWindow();
void RenderRouteBrowserWindow();
void RenderDebugWindow();

// ---------------------------------------------------------------------------
// Time formatting helpers (implemented in renderer_shared.cpp)
// ---------------------------------------------------------------------------

// Formats a seconds value as "HH:MM:SS[.mmm]".
// showMillis = true  → includes milliseconds  (default, used on the live timer)
// showMillis = false → whole seconds only      (used in history/tooltip tables)
void FormatTime(char* buf, int bufSize, double elapsed, bool showMillis = true);
void FormatTimeExport(char* buf, int bufSize, double elapsed);

// Formats a signed time delta (current - best) as a compact "+/-" string.
// isSplit = false (default) → live comparison mode: hides the diff when more
//   than 60 s ahead and reduces precision at large margins to reduce noise.
// isSplit = true            → completed-split mode: always shown, full precision.
// Returns false when the diff should be hidden entirely (live mode, far ahead).
bool FormatDiff(char* buf, int bufSize, double diff, bool isSplit = false);

// Returns the ImGui color for a split time cell:
//   White — segment still in progress  (running = true)
//   Green — no best time yet, or current <= best
//   Red   — current time is slower than best
ImVec4 TimeColor(double current, double best, bool running);

// Loads a route file from disk and updates all shared global state
// (CurrentRoute, history, file paths, timer reset).
void LoadRouteFile(const RouteFile& rf);

// ---------------------------------------------------------------------------
// Tooltip  (inline helper)
// ---------------------------------------------------------------------------
// Displays a plain-text tooltip for the last ImGui item, but only after the
// cursor has been hovering for delaySeconds (default 0.5 s).  The delay
// prevents tooltips from flickering up while the player is just moving the
// mouse across the window.
//
// Uses GImGui->HoveredIdTimer from imgui_internal.h to read how long the
// current item has been hovered — this is an ImGui internal, but it's the
// standard approach for hover-delay tooltips prior to ImGui's built-in
// SetNextWindowContentSize hover-delay API landing in later versions.
// ---------------------------------------------------------------------------
inline void Tooltip(const char* text, float delaySeconds = 0.5f)
{
    if (ImGui::IsItemHovered())
    {
        if (GImGui->HoveredIdTimer >= delaySeconds)
        {
            ImGui::BeginTooltip();
            ImGui::Text("%s", text);
            ImGui::EndTooltip();
        }
    }
}