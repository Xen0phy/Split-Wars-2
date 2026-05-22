// renderer.h
#pragma once

#include "imgui.h"
#include "imgui_internal.h"

void RenderTimerOverlay();
void RenderConfigWindow();
void RenderHistoryWindow();
void RenderRouteBrowserWindow();

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