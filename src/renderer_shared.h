// renderer_shared.h
#pragma once

#include "imgui.h"
#include "imgui_internal.h"
#include "shared.h"
#include "storage.h"
#include <cstring>
#include <cstdio>
#include <cmath>
#include <string>
#include <filesystem>

void RenderTimerOverlay();
void RenderConfigWindow();
void RenderHistoryWindow();
void RenderRouteBrowserWindow();

// Time formatting helpers
void   FormatTime(char* buf, int bufSize, double elapsed, bool showMillis = true);
bool   FormatDiff(char* buf, int bufSize, double diff, bool isSplit = false);
ImVec4 TimeColor(double current, double best, bool running);

// Loads a route file and updates all shared state (CurrentRoute, history, etc.)
void LoadRouteFile(const RouteFile& rf);

// Add tooltips for mouse hover with small delay
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
