// renderer_config.cpp
// Implements the "Speedrun Config" window — the main route editor UI.
//
// From this window the player can:
//   • Name the route and choose a save directory
//   • Save the route (and its history) to disk
//   • Add, edit, reorder, or remove checkpoints in a scrollable table
//   • Capture the player's current in-game position directly into a checkpoint
//   • Set each checkpoint's trigger type, map ID, coordinates, radius/width, and angle
//   • Activate the route so the timer system starts using it
//   • Clear the entire route and start fresh

#include "renderer_shared.h"

namespace fs = std::filesystem;

void RenderConfigWindow()
{
    if (!ShowConfig) return;

    ImGui::Begin("Speedrun Config", &ShowConfig);

    // -------------------------------------------------------------------------
    // Route name field
    // Uses a character-filter callback to block characters that are illegal in
    // Windows file names (\ / : * ? " < > |) as well as '.' to prevent path
    // traversal tricks like ".." in saved file names.
    // -------------------------------------------------------------------------
    ImGui::Text("Route:");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(200.0f);
    static char routeNameBuf[64] = "New Route";
    if (CurrentRouteName.size() >= 64)
        CurrentRouteName.resize(63);
    strncpy(routeNameBuf, CurrentRouteName.c_str(), 63);
    routeNameBuf[63] = '\0';

    // Callback that rejects any character that would make an invalid file name.
    auto routeNameFilter = [](ImGuiInputTextCallbackData* data) -> int
    {
        char c = (char)data->EventChar;
        // Block: Windows-illegal chars  \ / : * ? " < > |
        //        dot (.)  to prevent .  and .. path traversal
        //        null byte
        if (c == '\\' || c == '/' || c == ':' || c == '*' ||
            c == '?' || c == '"' || c == '<' || c == '>' || c == '|' ||
            c == '.' || c == '\0')
            return 1; // 1 = reject the character
        return 0;     // 0 = accept
    };
    if (ImGui::InputText("##routename", routeNameBuf, 64,
        ImGuiInputTextFlags_CallbackCharFilter, routeNameFilter))
        CurrentRouteName = routeNameBuf;
    Tooltip("Tip: \\ / : * ? \" < > | . are not allowed in route names.");
    ImGui::SameLine();

    // -------------------------------------------------------------------------
    // Save directory field
    // Initialises from the directory of the currently loaded route file, or
    // falls back to the addon directory when no file is loaded.  It auto-updates
    // whenever the active route file path changes (e.g. after loading a route
    // from the browser).
    // -------------------------------------------------------------------------
    static char saveDirBuf[512]         = "";
    static std::string lastSeenFilepath = "";
    if (CurrentRouteFilepath != lastSeenFilepath)
    {
        // The loaded route changed — refresh the directory shown in the field.
        lastSeenFilepath = CurrentRouteFilepath;
        std::string dir = CurrentRouteFilepath.empty()
            ? AddonDir
            : fs::path(CurrentRouteFilepath).parent_path().string();
        strncpy(saveDirBuf, dir.c_str(), sizeof(saveDirBuf) - 1);
        saveDirBuf[sizeof(saveDirBuf) - 1] = '\0';
    }
    // Fallback: if the buffer is still empty (very first draw), fill it now.
    if (saveDirBuf[0] == '\0' && !AddonDir.empty())
    {
        strncpy(saveDirBuf, AddonDir.c_str(), sizeof(saveDirBuf) - 1);
        saveDirBuf[sizeof(saveDirBuf) - 1] = '\0';
    }

    // -------------------------------------------------------------------------
    // Save Route button
    // Two behaviours:
    //   • Same path as current file → overwrite in place (also saves history).
    //   • Different path / new file → save to the new path, clear history
    //     (history from the old file wouldn't be valid for the renamed route).
    // -------------------------------------------------------------------------
    if (ImGui::Button("Save Route"))
    {
        std::string dir   = saveDirBuf;
        std::string newFP = dir + "\\" + CurrentRouteName + ".json";
        std::string newHP = dir + "\\" + CurrentRouteName + ".history";

        if (!CurrentRouteFilepath.empty() && newFP == CurrentRouteFilepath)
        {
            // Overwrite the existing file and keep history intact.
            SaveRoute(CurrentRouteFilepath, CurrentRoute);
            if (!CurrentHistoryPath.empty())
                SaveHistory(CurrentHistoryPath, BestRun, HistoryRuns);
        }
        else
        {
            // New file path — save fresh and clear the in-memory history so
            // stale splits from a differently-named route don't carry over.
            SaveRoute(newFP, CurrentRoute);
            CurrentRouteFilepath = newFP;
            CurrentHistoryPath   = newHP;
            lastSeenFilepath     = newFP;
            BestRun.clear();
            HistoryRuns.clear();
        }
    }
    ImGui::SameLine();

    if (ImGui::Button("Browse Routes"))
        ShowRouteBrowser = !ShowRouteBrowser;
    ImGui::SameLine();

    if (ImGui::Button("History"))
        ShowHistory = !ShowHistory;

    // Editable save directory — lets the player type or paste a custom path.
    ImGui::Text("Dir:"); ImGui::SameLine();
    ImGui::SetNextItemWidth(-1.0f);
    ImGui::InputText("##savedir", saveDirBuf, sizeof(saveDirBuf));

    ImGui::Separator();

    // -------------------------------------------------------------------------
    // Footer height reservation
    // We need to know the footer height before drawing the scrollable table so
    // ImGui::BeginChild() can reserve the right amount of space at the bottom.
    // The footer is taller when an Interact trigger is present because an extra
    // warning line is appended.
    // -------------------------------------------------------------------------
    bool hasInteract = false;
    for (const auto& cp : CurrentRoute.Checkpoints)
        if (cp.Point.TriggerType == ETriggerType::CircleInteract)
            { hasInteract = true; break; }

    float footerReserve = ImGui::GetFrameHeightWithSpacing() * 2.0f
                        + ImGui::GetStyle().ItemSpacing.y * 3.0f + 1.0f;
    if (hasInteract)
        footerReserve += ImGui::GetFrameHeightWithSpacing() * 1.0f;

    // -------------------------------------------------------------------------
    // Checkpoint table (scrollable)
    // Each row represents one checkpoint.  Columns:
    //   Name           — editable text field
    //   S              — "Is Start" checkbox (only one checkpoint may be start)
    //   G              — "Is Goal" checkbox (only one checkpoint may be goal)
    //   Trigger        — dropdown: Circle / Plane / Map Change / Interact / Combat
    //                    (Goal checkpoints additionally offer "All Checkpoints")
    //   MapID          — the GW2 map ID the trigger is scoped to (0 = any map)
    //   X/Y/Z          — world-space coordinates (hidden for Map Change / All Checkpoints)
    //   R/W            — Radius for circle types; PlaneWidth for Plane type
    //   Angle/Sphere   — PlaneAngle for Plane; dot count for Circle/Interact; Re-arm for Combat
    //   Capture        — snaps all spatial fields from the player's current position
    //   Remove         — marks this row for deletion (applied after the table loop)
    // -------------------------------------------------------------------------
    ImGui::BeginChild("##route_scroll", ImVec2(0, -footerReserve));
    if (ImGui::BeginTable("route_table", 12,
        ImGuiTableFlags_Borders |
        ImGuiTableFlags_Resizable |
        ImGuiTableFlags_SizingStretchProp |
        ImGuiTableFlags_ScrollY))
    {
        ImGui::TableSetupColumn("Name",         ImGuiTableColumnFlags_WidthFixed,    100.0f);
        ImGui::TableSetupColumn("S",            ImGuiTableColumnFlags_WidthFixed,    20.0f);
        ImGui::TableSetupColumn("G",            ImGuiTableColumnFlags_WidthFixed,    20.0f);
        ImGui::TableSetupColumn("Trigger",      ImGuiTableColumnFlags_WidthFixed,    90.0f);
        ImGui::TableSetupColumn("MapID",        ImGuiTableColumnFlags_WidthFixed,    60.0f);
        ImGui::TableSetupColumn("X",            ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Y",            ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Z",            ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("R/W",          ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Angle/Sphere", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Capture",      ImGuiTableColumnFlags_WidthFixed,    50.0f);
        ImGui::TableSetupColumn("Remove",       ImGuiTableColumnFlags_WidthFixed,    50.0f);
        ImGui::TableHeadersRow();

        int removeIndex = -1; // Set to a row index if the player clicks a Remove button
        for (int i = 0; i < (int)CurrentRoute.Checkpoints.size(); i++)
        {
            ImGui::TableNextRow();
            Checkpoint& cp        = CurrentRoute.Checkpoints[i];
            RoutePoint& point     = cp.Point;
            bool isMapChange      = point.TriggerType == ETriggerType::MapChange;
            bool isAllCheckpoints = point.TriggerType == ETriggerType::AllCheckpoints;
            bool isGoal           = cp.IsGoal;

            // --- Name ---
            ImGui::TableSetColumnIndex(0);
            ImGui::SetNextItemWidth(-1);
            char nameLabel[32]; snprintf(nameLabel, sizeof(nameLabel), "##cpname_%d", i);
            ImGui::InputText(nameLabel, cp.Name, sizeof(cp.Name));

            // --- Start checkbox ---
            // Enforces that only one checkpoint can be the start: ticking a new
            // one clears the IsStart flag on all other checkpoints first.
            ImGui::TableSetColumnIndex(1);
            bool isStart = cp.IsStart;
            char startLabel[32]; snprintf(startLabel, sizeof(startLabel), "##s_%d", i);
            if (ImGui::Checkbox(startLabel, &isStart))
            {
                if (isStart)
                {
                    for (auto& other : CurrentRoute.Checkpoints)
                        other.IsStart = false;
                }
                cp.IsStart = isStart;
            
                // Make sure Start and Goal can't be set at the same row with those triggers
                if (cp.IsStart)
                {
                    ETriggerType t = point.TriggerType;
                    if (t == ETriggerType::AllCheckpoints)
                        cp.IsStart = false; // AllCheckpoints is goal-only, never start
                    else if (t == ETriggerType::MapChange      ||
                             t == ETriggerType::CircleInteract ||
                             t == ETriggerType::Plane)
                        cp.IsGoal = false; // Can't have both — setting start clears goal
                }
            }

            // --- Goal checkbox ---
            // Same single-selection enforcement as the Start checkbox.
            ImGui::TableSetColumnIndex(2);
            bool isGoalVal = cp.IsGoal;
            char goalLabel[32]; snprintf(goalLabel, sizeof(goalLabel), "##g_%d", i);
            if (ImGui::Checkbox(goalLabel, &isGoalVal))
            {
                if (isGoalVal)
                {
                    for (auto& other : CurrentRoute.Checkpoints)
                        other.IsGoal = false;
                }
                cp.IsGoal = isGoalVal;
            
                // Goal is incompatible with these trigger types — clear it immediately.
                if (cp.IsGoal)
                {
                    ETriggerType t = point.TriggerType;
                    if (t == ETriggerType::MapChange      ||
                        t == ETriggerType::CircleInteract ||
                        t == ETriggerType::Plane)
                        cp.IsStart = false; // Can't have both — setting goal clears start
                }
            }

            // --- Trigger type dropdown ---
            // Goal checkpoints get an extra option: "All Checkpoints", which fires
            // once every other checkpoint in the route has been triggered.
            // Non-goal checkpoints use a 5-item list (no "All Checkpoints" entry).
            ImGui::TableSetColumnIndex(3);
            const char* triggerTypes[]     = { "Circle", "Plane", "Map Change", "Interact", "Combat(Mumble)" };
            const char* goalTriggerTypes[] = { "Circle", "Plane", "Map Change", "Interact", "Combat(Mumble)", "All Checkpoints" };
            ImGui::SetNextItemWidth(-1);
            char comboLabel[32]; snprintf(comboLabel, sizeof(comboLabel), "##type_%d", i);
            if (isGoal)
            {
                int currentType = (int)point.TriggerType;
                if (ImGui::Combo(comboLabel, &currentType, goalTriggerTypes, 6))
                    point.TriggerType = (ETriggerType)currentType;
            }
            else
            {
                // CombatArena maps to display index 4; AllCheckpoints is goal-only
                // so clamp it to index 0 if somehow set on a non-goal checkpoint.
                int displayIndex = (point.TriggerType == ETriggerType::CombatArena) ? 4 : (int)point.TriggerType;
                if (point.TriggerType == ETriggerType::AllCheckpoints) displayIndex = 0;
                if (ImGui::Combo(comboLabel, &displayIndex, triggerTypes, 5))
                    point.TriggerType = (displayIndex == 4) ? ETriggerType::CombatArena : (ETriggerType)displayIndex;
            }

            // Enforce start/goal compatibility whenever trigger type may have changed
            ETriggerType t = point.TriggerType;
            if (t == ETriggerType::AllCheckpoints)
                cp.IsStart = false;
            else if (t == ETriggerType::MapChange      ||
                     t == ETriggerType::CircleInteract ||
                     t == ETriggerType::Plane)
            {
                if (cp.IsStart && cp.IsGoal) cp.IsGoal = false; // resolve conflict, keep start
            }
            if (t == ETriggerType::MapChange      ||
                t == ETriggerType::CircleInteract ||
                t == ETriggerType::Plane)
            {
                if (cp.IsStart && cp.IsGoal) cp.IsGoal = false;
            }

            // --- MapID ---
            // Hidden for "All Checkpoints" since that trigger has no spatial context.
            ImGui::TableSetColumnIndex(4);
            if (!isAllCheckpoints)
            {
                ImGui::SetNextItemWidth(-1);
                char mapIdLabel[32]; snprintf(mapIdLabel, sizeof(mapIdLabel), "##mapid_%d", i);
                int mapId = (int)point.MapID;
                if (ImGui::InputInt(mapIdLabel, &mapId, 0, 0))
                    point.MapID = (unsigned int)mapId;
            }

            // --- X / Y / Z coordinates ---
            // Hidden for Map Change (position is irrelevant — only the map ID matters)
            // and for All Checkpoints (no spatial component at all).
            ImGui::TableSetColumnIndex(5);
            if (!isMapChange && !isAllCheckpoints)
            {
                ImGui::SetNextItemWidth(-1);
                char l[32]; snprintf(l, sizeof(l), "##x_%d", i);
                ImGui::InputFloat(l, &point.X, 0.0f, 0.0f, "%.2f");
            }

            ImGui::TableSetColumnIndex(6);
            if (!isMapChange && !isAllCheckpoints)
            {
                ImGui::SetNextItemWidth(-1);
                char l[32]; snprintf(l, sizeof(l), "##y_%d", i);
                ImGui::InputFloat(l, &point.Y, 0.0f, 0.0f, "%.2f");
            }

            ImGui::TableSetColumnIndex(7);
            if (!isMapChange && !isAllCheckpoints)
            {
                ImGui::SetNextItemWidth(-1);
                char l[32]; snprintf(l, sizeof(l), "##z_%d", i);
                ImGui::InputFloat(l, &point.Z, 0.0f, 0.0f, "%.2f");
            }

            // --- R/W (Radius or Plane Width) ---
            // For Plane triggers this column shows PlaneWidth (the half-width of the
            // trigger plane perpendicular to its facing angle).
            // For all other spatial triggers it shows the circle Radius.
            ImGui::TableSetColumnIndex(8);
            if (!isMapChange && !isAllCheckpoints)
            {
                ImGui::SetNextItemWidth(-1);
                if (point.TriggerType == ETriggerType::Plane)
                {
                    char l[32]; snprintf(l, sizeof(l), "##w_%d", i);
                    ImGui::InputFloat(l, &point.PlaneWidth, 0.0f, 0.0f, "%.2f");
                }
                else
                {
                    char l[32]; snprintf(l, sizeof(l), "##r_%d", i);
                    ImGui::InputFloat(l, &point.Radius, 0.0f, 0.0f, "%.2f");
                }
            }

            // --- Angle / Re-arm / Billboard ---
            // For Plane triggers: editable PlaneAngle (degrees, 0° = north).
            // For CombatArena triggers: a "Re-arm" button that clears the finished
            //   state so the same combat zone can trigger again in the same run
            //   (useful for routes that loop back through a combat arena).
            // For Circle / CircleInteract: an unnamed checkbox that switches the
            //   world render from flat rings + base band to a billboard ring with
            //   inward fade (IsBillboardCircle). Display-only — no trigger change.
            // Hidden for all other trigger types.
            ImGui::TableSetColumnIndex(9);
            if (point.TriggerType == ETriggerType::CombatArena)
            {
                if (i < (int)CombatCheckpoints.size() && CombatCheckpoints[i].finished)
                {
                    char rearmLabel[32]; snprintf(rearmLabel, sizeof(rearmLabel), "Re-arm##%d", i);
                    if (ImGui::Button(rearmLabel))
                    {
                        CombatCheckpoints[i] = {};           // Reset the combat state machine
                        if (i < (int)checkpointTriggered.size())
                            checkpointTriggered[i] = false;  // Allow the split to fire again
                    }
                }
            }
            else if (!isMapChange && !isAllCheckpoints && point.TriggerType == ETriggerType::Plane)
            {
                ImGui::SetNextItemWidth(-1);
                char l[32]; snprintf(l, sizeof(l), "##angle_%d", i);
                ImGui::InputFloat(l, &point.PlaneAngle, 0.0f, 0.0f, "%.1f");
            }
            else if (point.TriggerType == ETriggerType::Circle ||
                     point.TriggerType == ETriggerType::CircleInteract)
            {
                char l[32]; snprintf(l, sizeof(l), "##dotsphere_%d", i);
                ImGui::SetNextItemWidth(-1);
                ImGui::InputInt(l, &point.DotSphereCount, 0, 0);
                if (point.DotSphereCount < 0) point.DotSphereCount = 0;
            }

            // --- Capture button ---
            // Snaps the checkpoint's map ID and world coordinates from the player's
            // current MumbleLink position in one click.  For Plane triggers it also
            // calculates the facing angle from the camera direction so the plane
            // is perpendicular to the direction the player is looking.
            // Hidden for "All Checkpoints" (no position to capture).
            ImGui::TableSetColumnIndex(10);
            if (!isAllCheckpoints)
            {
                char capLabel[32]; snprintf(capLabel, sizeof(capLabel), "Cap##%d", i);
                if (ImGui::Button(capLabel) && MumbleLink)
                {
                    point.MapID = MumbleLink->Context.MapID;
                    if (!isMapChange)
                    {
                        point.X = MumbleLink->AvatarPosition.X;
                        point.Y = MumbleLink->AvatarPosition.Y;
                        point.Z = MumbleLink->AvatarPosition.Z;
                    }
                    if (point.TriggerType == ETriggerType::Plane)
                    {
                        // Convert the camera's 2D forward vector (X/Z plane) to a
                        // compass angle in degrees, then rotate 90° so the plane
                        // faces the player rather than running parallel to their gaze.
                        float fx = MumbleLink->CameraFront.X;
                        float fz = MumbleLink->CameraFront.Z;
                        point.PlaneAngle = -(std::atan2(fx, fz) * 180.0f / 3.14159265f) + 90.0f;
                    }
                }
            }
            else
            {
                ImGui::TextDisabled("—"); // No capture for All Checkpoints
            }

            // --- Remove button ---
            // We can't erase from the vector while iterating it, so we just
            // record the index and remove it after the loop finishes.
            ImGui::TableSetColumnIndex(11);
            char removeLabel[32]; snprintf(removeLabel, sizeof(removeLabel), "X##rm_%d", i);
            if (ImGui::Button(removeLabel))
                removeIndex = i;
        }

        ImGui::EndTable();

        // Deferred removal — safe to do here because the table loop is done.
        if (removeIndex >= 0)
        {        
            CurrentRoute.Checkpoints.erase(CurrentRoute.Checkpoints.begin() + removeIndex);
            FullReset(); // Resync trigger-state arrays and reset the timer
        }
    }

    ImGui::EndChild();

    // -------------------------------------------------------------------------
    // Footer buttons
    // -------------------------------------------------------------------------

    ImGui::Spacing();

    // Add Checkpoint — appends a new checkpoint pre-filled with the player's
    // current position (if MumbleLink is available).
    if (ImGui::Button("Add Checkpoint"))
    {
        Checkpoint cp;
        snprintf(cp.Name, sizeof(cp.Name), "Checkpoint %d", (int)CurrentRoute.Checkpoints.size() + 1);
        if (MumbleLink)
        {
            cp.Point.X     = MumbleLink->AvatarPosition.X;
            cp.Point.Y     = MumbleLink->AvatarPosition.Y;
            cp.Point.Z     = MumbleLink->AvatarPosition.Z;
            cp.Point.MapID = MumbleLink->Context.MapID;
        }
        CurrentRoute.Checkpoints.push_back(cp);
        FullReset();
    }
    ImGui::SameLine();

    // Clear Route — wipes everything: checkpoints, history, file paths.
    if (ImGui::Button("Clear Route"))
    {
        CurrentRoute.Checkpoints.clear();
        CurrentRoute.IsValid = false;
        CurrentRouteName     = "New Route";
        CurrentRouteFilepath.clear();
        CurrentHistoryPath.clear();
        BestRun.clear();
        HistoryRuns.clear();
        FullReset();
    }

    ImGui::Spacing();
    ImGui::Separator();

    // Activate Route — marks the route as valid so the trigger system in
    // AddonRender() will start evaluating it.  Required after editing a route
    // that was loaded from disk (loaded routes start as active already, but
    // new/cleared routes need an explicit activation).
    if (ImGui::Button("Activate Route"))
    {
        CurrentRoute.IsValid = true;
        FullReset();
    }
    ImGui::SameLine();
    if (ImGui::Button("Reset Timer"))
        FullReset();

    // Warning shown when any checkpoint uses the CircleInteract trigger type,
    // because that trigger requires the GW2 Interact key to be bound in Nexus
    // *with* passthrough enabled so the game still receives the keypress.
    if (hasInteract)
    {
        ImGui::Spacing();
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.6f, 0.0f, 1.0f)); // Orange text
        ImGui::TextWrapped("Interact trigger detected - ensure Interact button is set and passthrough is enabled in Nexus -> Keybinds.");
        ImGui::PopStyleColor();
    }

    ImGui::End();
}