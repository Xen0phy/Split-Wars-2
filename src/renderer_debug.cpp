// renderer_debug.cpp
// Implements the "Debug" window — a live inspection panel for route and
// player state, intended for route authors and developers.
//
// Layout:
//   Top section  — live player and timer state (always visible)
//   Bottom left  — selectable list of all checkpoints in the active route
//   Bottom right — detailed state for the selected checkpoint, including
//                  world-to-screen projection data for spatial trigger types
//
// The window is only drawn when ShowDebug is true.
// All data is read directly from shared globals — no copies are made.

#include "imgui.h"
#include "renderer_shared.h"
#include "shared.h"
#include "worldrender.h"
#include <algorithm>

// Index of the checkpoint currently selected in the left panel.
// Persists across frames so the selection survives redraws.
// -1 = nothing selected.
static int s_SelectedCheckpoint = -1;

// ---------------------------------------------------------------------------
// TriggerTypeName  (file-private helper)
// ---------------------------------------------------------------------------
// Returns a human-readable label for an ETriggerType value.
// ---------------------------------------------------------------------------
static const char* TriggerTypeName(ETriggerType t)
{
    switch (t)
    {
        case ETriggerType::Circle:         return "Circle";
        case ETriggerType::Plane:          return "Plane";
        case ETriggerType::MapChange:      return "Map Change";
        case ETriggerType::CircleInteract: return "Circle Interact";
        case ETriggerType::CombatArena:    return "Combat (Native)";
        case ETriggerType::AllCheckpoints: return "All Checkpoints";
        default:                           return "Unknown";
    }
}

// ---------------------------------------------------------------------------
// CombatStateName  (file-private helper)
// ---------------------------------------------------------------------------
// Returns a human-readable label for an ECombatState value.
// ---------------------------------------------------------------------------
static const char* CombatStateName(ECombatState s)
{
    switch (s)
    {
        case ECombatState::Armed:        return "Armed";
        case ECombatState::GracePending: return "Grace Pending";
        case ECombatState::Finished:     return "Finished";
        default:                         return "Unknown";
    }
}

// ---------------------------------------------------------------------------
// RenderDebugWindow
// ---------------------------------------------------------------------------
void RenderDebugWindow()
{
    if (!ShowDebug) return;

    ImGui::SetNextWindowSize(ImVec2(700.0f, 450.0f), ImGuiCond_FirstUseEver);
    ImGui::Begin("Split Wars 2 - Debug", &ShowDebug);

    // -------------------------------------------------------------------------
    // Top section — live player and timer state
    // -------------------------------------------------------------------------
    if (ImGui::BeginTable("##debugtop", 3, ImGuiTableFlags_Borders | ImGuiTableFlags_SizingStretchSame))
    {
        ImGui::TableSetupColumn("Player");
        ImGui::TableSetupColumn("Timer");
        ImGui::TableSetupColumn("Run State");
        ImGui::TableHeadersRow();

        ImGui::TableNextRow();

        // --- Player column ---
        ImGui::TableSetColumnIndex(0);
        ImGui::Text("X: %.3f", GS.PlayerX);
        ImGui::Text("Y: %.3f", GS.PlayerY);
        ImGui::Text("Z: %.3f", GS.PlayerZ);
        ImGui::Text("MapID: %u",  GS.MapID);
        ImGui::Text("Combat: %s", GS.IsInCombat ? "Yes" : "No");
        // Source diagnostics — shows which data source is currently active and
        // whether RTAPI is connected, useful when debugging position discrepancies.
        ImGui::Text("Source: %s", GS.ActiveSource == EDataSource::RTAPI ? "RTAPI" : "Mumble");
        ImGui::Text("RTAPI: %s",  GS.RTAPIAvailable ? "Available" : "Unavailable");
        ImGui::Text("FOV: %f", GS.FOV);

        // --- Timer column ---
        ImGui::TableSetColumnIndex(1);
        {
            char buf[32];
            FormatTime(buf, sizeof(buf), SpeedrunTimer.GetElapsedSeconds());
            ImGui::Text("Speedrun: %s", buf);
            FormatTime(buf, sizeof(buf), GrandTimer.GetElapsedSeconds());
            ImGui::Text("Grand:    %s", buf);
            ImGui::Text("Running:  %s", SpeedrunTimer.IsRunning()  ? "Yes" : "No");
            ImGui::Text("Paused:   %s", SpeedrunTimer.IsPaused()   ? "Yes" : "No");
            ImGui::Text("Finished: %s", SpeedrunTimer.IsFinished() ? "Yes" : "No");
        }

        // --- Run State column ---
        ImGui::TableSetColumnIndex(2);
        ImGui::Text("Route Valid:    %s", CurrentRoute.IsValid ? "Yes" : "No");
        ImGui::Text("Run Finished:   %s", RunFinished          ? "Yes" : "No");
        ImGui::Text("Pending Start:  %s", PendingStart         ? "Yes" : "No");
        ImGui::Text("Checkpoints:    %d", (int)CurrentRoute.Checkpoints.size());
        ImGui::Text("Best Run Index: %d", BestRunIndex);
        ImGui::Text("History Runs:   %d", (int)HistoryRuns.size());

        ImGui::EndTable();
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // -------------------------------------------------------------------------
    // Bottom section — checkpoint list (left) + checkpoint detail (right)
    // -------------------------------------------------------------------------
    if (CurrentRoute.Checkpoints.empty())
    {
        ImGui::TextDisabled("No checkpoints in active route.");
        ImGui::End();
        return;
    }

    // Clamp selection in case checkpoints were removed since last frame.
    if (s_SelectedCheckpoint >= (int)CurrentRoute.Checkpoints.size())
        s_SelectedCheckpoint = (int)CurrentRoute.Checkpoints.size() - 1;

    float availWidth = ImGui::GetContentRegionAvail().x;
    float leftWidth  = availWidth * 0.35f;
    float rightWidth = availWidth * 0.65f - ImGui::GetStyle().ItemSpacing.x - 1.0f;

    // --- Left panel: checkpoint list ---
    ImGui::BeginChild("##debuglist", ImVec2(leftWidth, 0), true);
    for (int i = 0; i < (int)CurrentRoute.Checkpoints.size(); i++)
    {
        const Checkpoint& cp = CurrentRoute.Checkpoints[i];

        // Build a label that shows the role (S = start, G = goal) and name.
        std::string label;
        if (cp.IsStart) label += "[S] ";
        if (cp.IsGoal)  label += "[G] ";
        label += cp.Name;

        bool selected = (s_SelectedCheckpoint == i);
        if (ImGui::Selectable(label.c_str(), selected))
        {
            s_SelectedCheckpoint    = i;
            ZoneRenderSelectedIndex = i; // tell worldrender which checkpoint to time
        }
    }
    ImGui::EndChild();

    ImGui::SameLine();

    // Vertical separator between the checkpoint list and the detail panel.
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.3f, 0.3f, 0.3f, 1.0f));
    ImGui::BeginChild("##debugsep", ImVec2(1.0f, 0), false);
    ImGui::EndChild();
    ImGui::PopStyleColor();
    
    ImGui::SameLine();

    // --- Right panel: checkpoint detail ---
    ImGui::BeginChild("##debugdetail", ImVec2(rightWidth, 0), true);

    if (s_SelectedCheckpoint < 0)
    {
        ImGui::TextDisabled("Select a checkpoint from the list.");
    }
    else
    {
        const Checkpoint& cp    = CurrentRoute.Checkpoints[s_SelectedCheckpoint];
        const RoutePoint& point = cp.Point;

        ImGui::Text("Name:    %s", cp.Name);
        ImGui::Text("Role:    %s%s%s",
            cp.IsStart ? "Start " : "",
            cp.IsGoal  ? "Goal "  : "",
            (!cp.IsStart && !cp.IsGoal) ? "Checkpoint" : "");
        ImGui::Text("Trigger: %s", TriggerTypeName(point.TriggerType));
        ImGui::Text("MapID:   %u", point.MapID);

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        // Spatial fields — hidden for types that have no position.
        bool hasCoords = point.TriggerType != ETriggerType::AllCheckpoints &&
                         point.TriggerType != ETriggerType::MapChange;

        if (hasCoords)
        {
            ImGui::Text("X: %.3f  Y: %.3f  Z: %.3f", point.X, point.Y, point.Z);

            if (point.TriggerType == ETriggerType::Plane)
            {
                ImGui::Text("Plane Width: %.2f m", point.PlaneWidth);
                ImGui::Text("Plane Angle: %.1f deg", point.PlaneAngle);
            }
            else
            {
                ImGui::Text("Radius: %.2f m", point.Radius);
            }

            // Live distance and in-zone status.
            if (MumbleLink || GS.RTAPIAvailable)
            {
                Vector3 playerPos = { GS.PlayerX, GS.PlayerY, GS.PlayerZ };
                float dist   = DistanceTo(playerPos, point);
                bool  inZone = IsWithinRange(playerPos, point);
                ImGui::Spacing();
                ImGui::Text("Distance: %.2f m", dist);
                ImGui::TextColored(
                    inZone ? ImVec4(0.2f, 1.0f, 0.2f, 1.0f)
                           : ImVec4(1.0f, 0.3f, 0.3f, 1.0f),
                    "In Zone: %s", inZone ? "Yes" : "No");
                // -------------------------------------------------------------------------
                // World-to-screen projection
                // Shows where the checkpoint centre projects onto the screen and whether
                // it is currently visible.  Also draws a confirmation dot in the game world.
                // Camera data comes from GS regardless of whether RTAPI or Mumble is active.
                // -------------------------------------------------------------------------
                ImGui::Spacing();
                ImGui::Separator();
                ImGui::Spacing();
                ImGui::Text("Projection:");
            
                float sx, sy;
                bool  valid = WorldToScreen(point.X, point.Y, point.Z, sx, sy);
            
                ImGui::Text("  Cam X: %.3f  Y: %.3f  Z: %.3f", GS.CameraX, GS.CameraY, GS.CameraZ);
                ImGui::Text("  Screen: %.1f  %.1f", sx, sy);
                ImGui::TextColored(
                    valid ? ImVec4(0.2f, 1.0f, 0.2f, 1.0f)
                          : ImVec4(1.0f, 0.3f, 0.3f, 1.0f),
                    "  Center: %s", valid ? "VALID" : "INVALID");
            
                if (valid)
                {
                    ImDrawList* dl = ImGui::GetForegroundDrawList();
                    dl->AddCircleFilled(ImVec2(sx, sy), 10.0f, IM_COL32(255, 255, 0, 255));
                    dl->AddCircle(ImVec2(sx, sy), 12.0f, IM_COL32(0, 0, 0, 200), 0, 2.0f);
                }
            
                ImGui::Spacing();
                ImGui::Separator();
                ImGui::Spacing();
                ImGui::Text("Occlusion:");
            
                float playerSx, playerSy;
                bool  playerValid = WorldToScreen(
                    GS.PlayerX,
                    GS.PlayerY + 1.0f,
                    GS.PlayerZ,
                    playerSx, playerSy);
            
                float camToPlayerX = GS.CameraX - GS.PlayerX;
                float camToPlayerY = GS.CameraY - GS.PlayerY;
                float camToPlayerZ = GS.CameraZ - GS.PlayerZ;
                float camToPlayer  = std::sqrt(camToPlayerX*camToPlayerX + camToPlayerY*camToPlayerY + camToPlayerZ*camToPlayerZ);
                float occludeRadius = std::clamp(occludePixelRadius / (camToPlayer * 0.5f), 30.0f, occludePixelClamp);
            
                ImGui::Text("  Player Screen: %.1f  %.1f", playerSx, playerSy);
                ImGui::Text("  Player Valid:  %s", playerValid ? "Yes" : "No");
                ImGui::Text("  Cam->Player:   %.3f m", camToPlayer);
                ImGui::Text("  Occlude Radius: %.1f px", occludeRadius);
                ImGui::SetNextItemWidth(80.0f);
                ImGui::DragFloat("Pixel Radius", &occludePixelRadius, 1.0f, 0.0f, 0.0f, "%.0f");
                ImGui::SameLine();
                ImGui::SetNextItemWidth(80.0f);
                ImGui::DragFloat("Clamp High", &occludePixelClamp, 1.0f, 0.0f, 0.0f, "%.0f");
            
                if (playerValid)
                {
                    ImDrawList* dl = ImGui::GetForegroundDrawList();
                    dl->AddCircle(ImVec2(playerSx, playerSy), occludeRadius,
                        IM_COL32(255, 100, 0, 180), 0, 1.5f);
                }
            }

            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();
            ImGui::Text("Render Time (1s avg):");
            if (ZoneRenderSelectedIndex == s_SelectedCheckpoint)
                ImGui::Text("  %.4f ms", ZoneRenderAvgMs);
            else
                ImGui::TextDisabled("  (select a checkpoint to measure)");
        }

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        // Per-checkpoint runtime trigger state.
        int i = s_SelectedCheckpoint;

        if (cp.IsStart)
        {
            ImGui::Text("Trigger State:");
            if (point.TriggerType == ETriggerType::CombatArena)
            {
                ImGui::Text("  Active:   %s", CombatStart.active   ? "Yes" : "No");
                ImGui::Text("  Finished: %s", CombatStart.finished ? "Yes" : "No");
                if (CombatStart.active)
                    ImGui::Text("  State:    %s", CombatStateName(CombatStart.state));
                if (CombatStart.dropTime > 0.0)
                    ImGui::Text("  Drop Time: %.3fs", CombatStart.dropTime);
            }
            else
            {
                ImGui::Text("  In Circle Last Frame: %s", WasInCircleStart ? "Yes" : "No");
            }
        }
        else if (cp.IsGoal)
        {
            ImGui::Text("Trigger State:");
            if (point.TriggerType == ETriggerType::CombatArena)
            {
                ImGui::Text("  Active:   %s", CombatGoal.active   ? "Yes" : "No");
                ImGui::Text("  Finished: %s", CombatGoal.finished ? "Yes" : "No");
                if (CombatGoal.active)
                    ImGui::Text("  State:    %s", CombatStateName(CombatGoal.state));
                if (CombatGoal.dropTime > 0.0)
                    ImGui::Text("  Drop Time: %.3fs", CombatGoal.dropTime);
            }
            else if (point.TriggerType == ETriggerType::AllCheckpoints)
            {
                // Show which intermediate checkpoints have and haven't fired.
                ImGui::Text("  Checkpoint Progress:");
                for (int j = 0; j < (int)CurrentRoute.Checkpoints.size(); j++)
                {
                    const Checkpoint& other = CurrentRoute.Checkpoints[j];
                    if (other.IsStart || other.IsGoal) continue;
                    bool done = j < (int)checkpointTriggered.size() && checkpointTriggered[j];
                    ImGui::TextColored(
                        done ? ImVec4(0.2f, 1.0f, 0.2f, 1.0f)
                             : ImVec4(1.0f, 0.3f, 0.3f, 1.0f),
                        "    [%s] %s", done ? "X" : " ", other.Name);
                }
            }
        }
        else
        {
            // Intermediate checkpoint.
            ImGui::Text("Trigger State:");
            bool triggered = i < (int)checkpointTriggered.size() && checkpointTriggered[i];
            ImGui::TextColored(
                triggered ? ImVec4(0.2f, 1.0f, 0.2f, 1.0f)
                          : ImVec4(1.0f, 0.3f, 0.3f, 1.0f),
                "  Triggered: %s", triggered ? "Yes" : "No");

            if (point.TriggerType == ETriggerType::Circle ||
                point.TriggerType == ETriggerType::CircleInteract)
            {
                bool wasIn = i < (int)WasInCheckpoint.size() && WasInCheckpoint[i];
                ImGui::Text("  In Circle Last Frame: %s", wasIn ? "Yes" : "No");
            }

            if (point.TriggerType == ETriggerType::CombatArena &&
                i < (int)CombatCheckpoints.size())
            {
                const CombatTriggerState& cs = CombatCheckpoints[i];
                ImGui::Text("  Active:   %s", cs.active   ? "Yes" : "No");
                ImGui::Text("  Finished: %s", cs.finished ? "Yes" : "No");
                if (cs.active)
                    ImGui::Text("  State:    %s", CombatStateName(cs.state));
                if (cs.dropTime > 0.0)
                    ImGui::Text("  Drop Time: %.3fs", cs.dropTime);
            }
        }
    }

    ImGui::EndChild();
    ImGui::End();
}