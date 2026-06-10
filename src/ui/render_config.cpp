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

#include "render_shared.h"
#include "route.h"
#include <algorithm>
#include <cmath>

namespace fs = std::filesystem;

void RenderConfigWindow()
{
    if (!ShowConfig) return;

    // Window size
    static bool firstFrame = true;
    if (firstFrame) {
        ImGui::SetNextWindowSize(ImVec2(ConfigWindowW, ConfigWindowH), ImGuiCond_Always);
        firstFrame = false;
    }
    ImGui::Begin("Split Wars 2 - Route Config", &ShowConfig);
    ImVec2 sz = ImGui::GetWindowSize();
    ConfigWindowW = sz.x;
    ConfigWindowH = sz.y;

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
    // Only the relative subfolder path (beneath AddonDir) is editable.
    // The AddonDir prefix is shown as read-only grayed text so the user can
    // see where routes are rooted but can't accidentally redirect saves
    // outside the addon directory.
    //
    // subfolderBuf holds just the relative part, e.g. "Fractals\\Daily".
    // Full save path = AddonDir + "\\" + subfolderBuf (or just AddonDir if empty).
    //
    // Auto-updates when the active route file changes: strips AddonDir from
    // the loaded file's parent directory to recover the relative subfolder.
    // -------------------------------------------------------------------------
    static char subfolderBuf[256]        = "";
    static std::string lastSeenFilepath  = "";
    if (CurrentRouteFilepath != lastSeenFilepath)
    {
        lastSeenFilepath = CurrentRouteFilepath;
        if (CurrentRouteFilepath.empty())
        {
            subfolderBuf[0] = '\0'; // No route loaded — default to root
        }
        else
        {
            // Strip AddonDir prefix to get the relative subfolder.
            // e.g. AddonDir="...\\Split Wars 2", parent="...\\Split Wars 2\\Fractals"
            // → subfolderBuf = "Fractals". Empty when file is in AddonDir root.
            std::string parentDir = fs::path(CurrentRouteFilepath).parent_path().string();
            if (parentDir.size() > AddonDir.size() &&
                parentDir.substr(0, AddonDir.size()) == AddonDir)
            {
                std::string rel = parentDir.substr(AddonDir.size() + 1); // +1 skips the backslash
                strncpy(subfolderBuf, rel.c_str(), sizeof(subfolderBuf) - 1);
                subfolderBuf[sizeof(subfolderBuf) - 1] = '\0';
            }
            else
            {
                subfolderBuf[0] = '\0'; // Outside AddonDir — fall back to root
            }
        }
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
        std::string dir   = (subfolderBuf[0] != '\0')
                            ? AddonDir + "\\" + subfolderBuf
                            : AddonDir;
        std::string newFP = dir + "\\" + CurrentRouteName + ".json";
        std::string newHP = dir + "\\" + CurrentRouteName + ".history";

        if (!CurrentRouteFilepath.empty() && newFP == CurrentRouteFilepath)
        {
            // Overwrite the existing file and keep history intact.
            SaveRoute(CurrentRouteFilepath, CurrentRoute);
            if (!CurrentHistoryPath.empty())
                SaveHistory(CurrentHistoryPath, HistoryRuns, SegmentRecords, BestRunIndex);
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
    ImGui::SameLine();

    // Folder field — same row as the buttons, fixed 200px to match the route
    // name field width. "Folder:" label makes it visually distinct from Route:.
    // The grayed prefix shows the addon root; only the relative subfolder is editable.
    // Hovering the prefix shows the full absolute path as a tooltip.
    ImGui::Spacing(); ImGui::SameLine();
    ImGui::Text("Folder:"); ImGui::SameLine();
    {
        std::string displayPrefix = "...\\" + fs::path(AddonDir).filename().string() + "\\";
        ImGui::TextDisabled("%s", displayPrefix.c_str());
        Tooltip(AddonDir.c_str());
        ImGui::SameLine(0, 0);
        ImGui::SetNextItemWidth(200.0f);
        ImGui::InputText("##subfolder", subfolderBuf, sizeof(subfolderBuf));
        Tooltip("Optional subfolder beneath the addon directory.\nLeave empty to save in the root. Example: Fractals\\Daily");
    }

    ImGui::Separator();

    // -------------------------------------------------------------------------
    // Footer height reservation
    // We need to know the footer height before drawing the scrollable table so
    // ImGui::BeginChild() can reserve the right amount of space at the bottom.
    // The footer is taller when an Interact trigger is present because an extra
    // warning line is appended.
    // -------------------------------------------------------------------------
    bool hasInteract = false;
    bool hasCombat   = false;
    for (const auto& cp : CurrentRoute.Checkpoints)
    {
        if (cp.Point.TriggerType == ETriggerType::CircleInteract) hasInteract = true;
        if (cp.Point.TriggerType == ETriggerType::CombatArena)    hasCombat   = true;
    }
    bool showCombatWarning = hasCombat && (GS.ActiveSource != EDataSource::RTAPI);

    // Calculator state — persisted across frames, cleared on map change.
    static bool  showCalculator = false;
    static float calcP1X = 0, calcP1Y = 0, calcP1Z = 0;
    static float calcP2X = 0, calcP2Y = 0, calcP2Z = 0;
    static unsigned int calcMapID = 0;
    static float calcCX = 0, calcCY = 0, calcCZ = 0, calcRadius = 0;
    static bool  calcHasResult = false;
    static unsigned int calcLastMapID = 0;
    if (GS.MapID != calcLastMapID && calcLastMapID != 0)
    {
        // Map changed — clear points and result.
        calcP1X = calcP1Y = calcP1Z = 0;
        calcP2X = calcP2Y = calcP2Z = 0;
        calcMapID = 0;
        calcHasResult = false;
    }
    calcLastMapID = GS.MapID;

    float footerReserve = ImGui::GetFrameHeightWithSpacing() * 2.0f
                        + ImGui::GetStyle().ItemSpacing.y * 3.0f + 1.0f;
    if (hasInteract)
        footerReserve += ImGui::GetFrameHeightWithSpacing() * 1.0f;
    if (showCombatWarning)
        footerReserve += ImGui::GetFrameHeightWithSpacing() * 2.0f;
    if (showCalculator)
    {
        footerReserve += ImGui::GetFrameHeightWithSpacing() * 2.0f + 4.0f; // two rows + separator
    }

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
    //   R/W            — Radius for circle types; Width for Plane type (RadiusWidth)
    //   Angle/Arm      — PlaneAngle for Plane; dot count for Circle/Interact; Re-arm for Combat
    //   Capture        — snaps all spatial fields from the player's current position
    //   Remove         — marks this row for deletion (applied after the table loop)
    // -------------------------------------------------------------------------
    ImGui::BeginChild("##route_scroll", ImVec2(0, -footerReserve));
    if (ImGui::BeginTable("route_table", 16,
        ImGuiTableFlags_Borders |
        ImGuiTableFlags_Resizable |
        ImGuiTableFlags_SizingStretchProp |
        ImGuiTableFlags_ScrollY))
    {
        ImGui::TableSetupColumn("Name",          ImGuiTableColumnFlags_WidthFixed,    100.0f);
        ImGui::TableSetupColumn("Start",         ImGuiTableColumnFlags_WidthFixed,    20.0f);
        ImGui::TableSetupColumn("Goal",          ImGuiTableColumnFlags_WidthFixed,    20.0f);
        ImGui::TableSetupColumn("Trigger",       ImGuiTableColumnFlags_WidthFixed,    90.0f);
        ImGui::TableSetupColumn("MapID",         ImGuiTableColumnFlags_WidthFixed,    60.0f);
        ImGui::TableSetupColumn("X",             ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Y",             ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Z",             ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Radius\nWidth", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Density",       ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Center",        ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Up",            ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Down",          ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Angle\nRe-Arm", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Capture",       ImGuiTableColumnFlags_WidthFixed,    30.0f);
        ImGui::TableSetupColumn("Menu\nDrag",    ImGuiTableColumnFlags_WidthFixed,    20.0f);
        ImGui::TableNextRow(ImGuiTableRowFlags_Headers);
        for (int col = 0; col < 16; col++)
        {
            ImGui::TableSetColumnIndex(col);
            ImGui::TableHeader(ImGui::TableGetColumnName(col));
            switch (col)
            {
                case 0:  Tooltip("Name your checkpoint."); break;
                case 1:  Tooltip("Set checkpoint as starting point.\nOnly one possible.\nWill start the timer."); break;
                case 2:  Tooltip("Set your checkpoint as Goal point.\nMultiple possble.\nWill end the timer."); break;
                case 3:  Tooltip("Trigger types that create a checkpoint:\n"
                                       "  * Sphere:          Fires when you enter it. If Start, fires when you leave.\n"
                                       "  * Plane:           Fires when you walk through it. Infinite height.\n"
                                       "  * Interact:        Fires when you interact while in sphere. Check warning when set.\n"
                                       "  * Combat(Native):  Fires twice.\n"
                                       "                     Combat start while in sphere.\n"
                                       "                     Combat end when out of combat or leaving sphere.\n"
                                       "  * MapChange:       Fires when leaving the selected map.\n"
                                       "  * AllCheckpoints:  Fires when all other Checkpoints in the list have been triggered."); break;
                case 4:  Tooltip("Enter MapID here.\nEither you know or you press the capture button."); break;
                case 5:  Tooltip("Enter X Coordinate here.\nEither you know or you press the capture button."); break;
                case 6:  Tooltip("Enter Y Coordinate here.\nEither you know or you press the capture button."); break;
                case 7:  Tooltip("Enter Z Coordinate here.\nEither you know or you press the capture button."); break;
                case 8:  Tooltip("Sphere radiues or plane width."); break;
                case 9:  Tooltip("The amount of dots you want on your sphere or plane.\nBigger numbers need more render time."); break;
                case 10: Tooltip("A center line for Up and Down.\n Sphere uses longitude degrees, plane uses meter."); break;
                case 11: Tooltip("How far up you want the dots to extend from center."); break;
                case 12: Tooltip("How far down you want the dots to extend from center."); break;
                case 13: Tooltip("Defines plane angle. Use capture button or adjust manually.\n"
                                       "Shows a Re-Arm button for combat trigger.\n"
                                       "Adjust MapChange indicators hyperbole strength."); break;
                case 14: Tooltip("Captures current MapID, XYZ location and plane angle if available."); break;
                case 15: Tooltip("Right-click for copy / paste / delete.\nLeft click and drag to reorder row."); break;
            }
        }

        int removeIndex   = -1; // Set if Delete is chosen from context menu
        int dragReorderFrom = -1;
        int dragReorderTo   = -1;
        static CheckpointState clipboard;
        static bool       hasClipboard = false;
        for (int i = 0; i < (int)CurrentRoute.Checkpoints.size(); i++)
        {
            ImGui::TableNextRow();
            CheckpointState& cp        = CurrentRoute.Checkpoints[i];
            RoutePoint& point     = cp.Point;
            bool isMapChange      = point.TriggerType == ETriggerType::MapChange;
            bool isAllCheckpoints = point.TriggerType == ETriggerType::AllCheckpoints;
            bool isNullCircle     = point.TriggerType == ETriggerType::NullCircle;
            bool isNullPlane      = point.TriggerType == ETriggerType::NullPlane;
            bool isNull           = isNullCircle || isNullPlane;

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
            // Multiple goals are allowed right now, see commented if(isGoalVal)
            ImGui::TableSetColumnIndex(2);
            bool isGoalVal = cp.IsGoal;
            char goalLabel[32]; snprintf(goalLabel, sizeof(goalLabel), "##g_%d", i);
            if (ImGui::Checkbox(goalLabel, &isGoalVal))
            {
                // Uncomment to bring back singular goal
                //if (isGoalVal)
                //{
                //    for (auto& other : CurrentRoute.Checkpoints)
                //        other.IsGoal = false;
                //}
                cp.IsGoal = isGoalVal;
            
                // Goal is incompatible with these trigger types set to start — clear it immediately.
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
            // AllCheckpoints is always available — selecting it automatically locks
            // this checkpoint as a goal since it has no meaning otherwise.
            // NullCircle and NullPlane are decorative — they render in the world but
            // never fire as triggers. Their display color is set separately in options.
            ImGui::TableSetColumnIndex(3);
            ImGui::SetNextItemWidth(-1);
            char comboLabel[32]; snprintf(comboLabel, sizeof(comboLabel), "##type_%d", i);
            const char* allTriggerTypes[] = { "Circle", "Plane", "Map Change", "Interact", "Combat(Native)", "All Checkpoints", "Null (Circle)", "Null (Plane)" };
            int currentType = (int)point.TriggerType;
            if (ImGui::Combo(comboLabel, &currentType, allTriggerTypes, 8))
            {
                point.TriggerType = (ETriggerType)currentType;
                if (point.TriggerType == ETriggerType::AllCheckpoints)
                    cp.IsGoal = true;
                // Changing trigger type can invalidate CombatCheckpoints state
                // (e.g. Circle → CombatArena), so reset to avoid stale trigger data
                FullReset();
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
                ImGui::DragFloat(l, &point.X, 0.1f, 0.0f, 0.0f, "%.2f");
            }

            ImGui::TableSetColumnIndex(6);
            if (!isMapChange && !isAllCheckpoints)
            {
                ImGui::SetNextItemWidth(-1);
                char l[32]; snprintf(l, sizeof(l), "##y_%d", i);
                ImGui::DragFloat(l,&point.Y,0.1f,0.0f,0.0f,"%.2f");
            }

            ImGui::TableSetColumnIndex(7);
            if (!isMapChange && !isAllCheckpoints)
            {
                ImGui::SetNextItemWidth(-1);
                char l[32]; snprintf(l, sizeof(l), "##z_%d", i);
                ImGui::DragFloat(l,&point.Z,0.1f,0.0f,0.0f,"%.2f");
            }

            // --- R/W (Radius or Plane Width) ---
            // For Plane triggers this column shows RadiusWidth (the half-width of the
            // trigger plane perpendicular to its facing angle).
            // For all other spatial triggers it shows the circle Radius.
            ImGui::TableSetColumnIndex(8);
            if (!isMapChange && !isAllCheckpoints)
            {
                ImGui::SetNextItemWidth(-1);
                char l[32]; snprintf(l, sizeof(l), "##r_%d", i);
                ImGui::DragFloat(l, &point.RadiusWidth, 0.1f,0.0f,1000.0f,"%.1f");
                point.RadiusWidth = std::clamp(point.RadiusWidth, 0.0f, 1000.0f);
            }

            // --- Dot Density ---
            // Controls the number of dots rendered for sphere and plane zones.
            // Higher values produce denser coverage; lower values are sparser.
            // Hidden for MapChange and AllCheckpoints trigger types.
            ImGui::TableSetColumnIndex(9);
            if (!isMapChange && !isAllCheckpoints)
            {
                char l[32]; snprintf(l, sizeof(l), "##dotDensity_%d", i);
                ImGui::SetNextItemWidth(-1);
                ImGui::DragInt(l, &point.DotDensity, 1, 30, 1000, "%d");
                point.DotDensity = std::clamp(point.DotDensity, 0, 100000);
            }

            // --- Band Center / Up / Down ---
            // Define the visible latitude band of the dot sphere (in degrees):
            //   BandCenter — vertical centre of the band relative to the equator
            //                (-90° = bottom pole, 0° = equator, 90° = top pole).
            //   BandUp     — degrees above centre where dot alpha fades to 0.
            //   BandDown   — degrees below centre where dot alpha fades to 0.
            // Hidden for MapChange and AllCheckpoints trigger types.
            ImGui::TableSetColumnIndex(10);
            if (!isMapChange && !isAllCheckpoints && point.TriggerType != ETriggerType::Plane)
            {
                char l[32]; snprintf(l, sizeof(l), "##bandCenter_%d", i);
                ImGui::SetNextItemWidth(-1);
                ImGui::DragFloat(l, &point.bandCenterInput, 1.0f, -90.0f, 90.0f, "%.0f");
                point.bandCenterInput = std::clamp(point.bandCenterInput, -90.0f, 90.0f);
            }

            ImGui::TableSetColumnIndex(11);
            if (!isMapChange && !isAllCheckpoints)
            {
                char l[32]; snprintf(l, sizeof(l), "##bandUp_%d", i);
                ImGui::SetNextItemWidth(-1);
                ImGui::DragFloat(l, &point.bandUpInput, 1.0f, 0.0f, 90.0f, "%.0f");
                point.bandUpInput = std::clamp(point.bandUpInput, 0.0f, 90.0f);
            }

            ImGui::TableSetColumnIndex(12);
            if (!isMapChange && !isAllCheckpoints)
            {
                char l[32]; snprintf(l, sizeof(l), "##bandDown_%d", i);
                ImGui::SetNextItemWidth(-1);
                ImGui::DragFloat(l, &point.bandDownInput, 1.0f, 0.0f, 90.0f, "%.0f");
                point.bandDownInput = std::clamp(point.bandDownInput, 0.0f, 90.0f);
            }

            // --- Angle / Re-arm / Hyperbola ---
            // For Plane triggers: editable PlaneAngle (degrees, 0° = north).
            // For CombatArena triggers: a "Re-arm" button that clears the finished
            //   state so the same combat zone can trigger again in the same run
            //   (useful for routes that loop back through a combat arena).
            // For MapChange triggers: editable HyperbolaC (1–100) controlling the
            //   strength of the dot-field hyperbola curve in the screen-space overlay.
            // Hidden for all other trigger types.
            ImGui::TableSetColumnIndex(13);
            if (point.TriggerType == ETriggerType::CombatArena)
            {
                if (i < (int)CheckpointStates.size() && CheckpointStates[i].combat.finished)
                {
                    char rearmLabel[32]; snprintf(rearmLabel, sizeof(rearmLabel), "Re-arm##%d", i);
                    if (ImGui::Button(rearmLabel))
                    {
                        CheckpointStates[i].combat    = {};    // Reset the combat state machine
                        CheckpointStates[i].triggered = false; // Allow the split to fire again
                    }
                }
            }
            else if (point.TriggerType == ETriggerType::Plane)
            {
                ImGui::SetNextItemWidth(-1);
                char l[32]; snprintf(l, sizeof(l), "##angle_%d", i);
                ImGui::DragFloat(l, &point.PlaneAngle, 0.1f, -360.0f, 360.0f, "%.1f");
                point.PlaneAngle = std::clamp(point.PlaneAngle, -360.0f, 360.0f);
            }
            else if (point.TriggerType == ETriggerType::MapChange)
            {
                ImGui::SetNextItemWidth(-1);
                char l[32]; snprintf(l, sizeof(l), "##hyperbola_%d", i);
                ImGui::DragInt(l, &point.HyperbolaC, 1.0f, 1.0f, 100.0f, "%d");
                point.HyperbolaC = std::clamp(point.HyperbolaC, 1, 100);
            }

            // --- Capture button ---
            // Snaps the checkpoint's map ID and world coordinates from the player's
            // current position in one click.
            // For Plane triggers it also calculates the facing angle from the camera
            // direction so the plane is perpendicular to the direction the player is
            // looking. Hidden for "All Checkpoints" (no position to capture).
            ImGui::TableSetColumnIndex(14);
            if (!isAllCheckpoints)
            {
                char capLabel[32]; snprintf(capLabel, sizeof(capLabel), "Cap##%d", i);
                if (ImGui::Button(capLabel) && (MumbleLink || GS.RTAPIAvailable))
                {
                    point.MapID = GS.MapID;
                    if (!isMapChange)
                    {
                        point.X = GS.PlayerX;
                        point.Y = GS.PlayerY;
                        point.Z = GS.PlayerZ;
                    }
                    if (point.TriggerType == ETriggerType::Plane)
                    {
                        // Convert the camera's 2D forward vector (X/Z plane) to a
                        // compass angle in degrees, then rotate 90° so the plane
                        // faces the player rather than running parallel to their gaze.
                        // Camera front is sourced from GS (populated from RTAPI or Mumble).
                        float fx = GS.CameraFrontX;
                        float fz = GS.CameraFrontZ;
                        point.PlaneAngle = -(std::atan2(fx, fz) * 180.0f / 3.14159265f) + 90.0f;
                    }
                }
            }
            else
            {
                ImGui::TextDisabled("—"); // No capture for All Checkpoints
            }

            // --- Grip / menu button (col 15) ---
            // Left click-hold + drag → reorder rows.
            // Right click → opens copy/paste/delete context menu.
            ImGui::TableSetColumnIndex(15);
            char gripLabel[32]; snprintf(gripLabel, sizeof(gripLabel), u8": : : :##grip_%d", i);

            ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0,0,0,0));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(1,1,1,0.1f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ImVec4(1,1,1,0.2f));
            ImGui::Button(gripLabel, ImVec2(-1, 0));
            ImGui::PopStyleColor(3);

            // --- Drag source ---
            if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID))
            {
                ImGui::SetDragDropPayload("ROUTE_ROW", &i, sizeof(int));
                ImGui::Text("Row %d: %s", i + 1, cp.Name);
                ImGui::EndDragDropSource();
            }

            // --- Drag target ---
            if (ImGui::BeginDragDropTarget())
            {
                if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("ROUTE_ROW"))
                {
                    dragReorderFrom = *(const int*)payload->Data;
                    dragReorderTo   = i;
                }
                ImGui::EndDragDropTarget();
            }

            // --- Context menu (right-click) ---
            char popupId[32]; snprintf(popupId, sizeof(popupId), "##rowctx_%d", i);
            if (ImGui::IsItemClicked(ImGuiMouseButton_Right))
                ImGui::OpenPopup(popupId);
            if (ImGui::BeginPopup(popupId))
            {
                if (ImGui::MenuItem("Copy"))
                {
                    clipboard          = cp;
                    clipboard.IsStart  = false; // Role flags are not copied — pasting geometry
                    clipboard.IsGoal   = false; // into a new row shouldn't duplicate start/goal.
                    hasClipboard       = true;
                }
                if (ImGui::MenuItem("Paste above", nullptr, false, hasClipboard))
                {
                    CurrentRoute.Checkpoints.insert(
                        CurrentRoute.Checkpoints.begin() + i, clipboard);
                    FullReset();
                    ImGui::CloseCurrentPopup();
                }
                if (ImGui::MenuItem("Paste below", nullptr, false, hasClipboard))
                {
                    CurrentRoute.Checkpoints.insert(
                        CurrentRoute.Checkpoints.begin() + i + 1, clipboard);
                    FullReset();
                    ImGui::CloseCurrentPopup();
                }
                ImGui::Separator();
                if (ImGui::MenuItem("Delete"))
                    removeIndex = i;
                ImGui::EndPopup();
            }
        }

        ImGui::EndTable();

        // Deferred removal — safe to do here because the table loop is done.
        if (removeIndex >= 0)
        {        
            CurrentRoute.Checkpoints.erase(CurrentRoute.Checkpoints.begin() + removeIndex);
            FullReset(); // Resync trigger-state arrays and reset the timer
        }

        // Deferred drag reorder — move row dragReorderFrom to position dragReorderTo.
        if (dragReorderFrom >= 0 && dragReorderTo >= 0 && dragReorderFrom != dragReorderTo)
        {
            auto& cps = CurrentRoute.Checkpoints;
            CheckpointState moved = cps[dragReorderFrom];
            cps.erase(cps.begin() + dragReorderFrom);
            // Adjust target index if source was before it.
            int insertAt = (dragReorderFrom < dragReorderTo) ? dragReorderTo : dragReorderTo;
            cps.insert(cps.begin() + insertAt, moved);
            FullReset();
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
        CheckpointState cp;
        snprintf(cp.Name, sizeof(cp.Name), "Checkpoint %d", (int)CurrentRoute.Checkpoints.size() + 1);
        cp.Point.X     = GS.PlayerX;
        cp.Point.Y     = GS.PlayerY;
        cp.Point.Z     = GS.PlayerZ;
        cp.Point.MapID = GS.MapID;
        CurrentRoute.Checkpoints.push_back(cp);
        FullReset();
    }
    ImGui::SameLine();

    // Calculator toggle button.
    if (ImGui::Button(showCalculator ? "Calculator [-]" : "Calculator [+]"))
        showCalculator = !showCalculator;

    // Clear Route — pushed to the far right of the same line.
    float clearWidth = ImGui::CalcTextSize("Clear Route").x + ImGui::GetStyle().FramePadding.x * 2.0f;
    ImGui::SameLine(ImGui::GetContentRegionAvail().x + ImGui::GetCursorPosX() - clearWidth);
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

    // -------------------------------------------------------------------------
    // Calculator expansion
    // -------------------------------------------------------------------------
    if (showCalculator)
    {
        ImGui::Separator();

        if (ImGui::BeginTable("##calctable", 3, ImGuiTableFlags_SizingStretchProp))
        {
            ImGui::TableSetupColumn("##points",  ImGuiTableColumnFlags_WidthStretch, 3.0f);
            ImGui::TableSetupColumn("##actions", ImGuiTableColumnFlags_WidthFixed,   80.0f);
            ImGui::TableSetupColumn("##results", ImGuiTableColumnFlags_WidthStretch, 3.0f);

            // --- Row 1: P1 | Calculate | Center XYZ + Radius ---
            ImGui::TableNextRow();

            ImGui::TableSetColumnIndex(0);
            ImGui::Text("P1"); ImGui::SameLine();
            ImGui::SetNextItemWidth(55); ImGui::InputInt("##calcmapid", (int*)&calcMapID, 0, 0); ImGui::SameLine();
            ImGui::SetNextItemWidth(60); ImGui::DragFloat("##p1x", &calcP1X, 0.1f, 0, 0, "%.2f"); ImGui::SameLine();
            ImGui::SetNextItemWidth(60); ImGui::DragFloat("##p1y", &calcP1Y, 0.1f, 0, 0, "%.2f"); ImGui::SameLine();
            ImGui::SetNextItemWidth(60); ImGui::DragFloat("##p1z", &calcP1Z, 0.1f, 0, 0, "%.2f"); ImGui::SameLine();
            if (ImGui::Button("Cap##p1") && (MumbleLink || GS.RTAPIAvailable))
            {
                calcP1X   = GS.PlayerX;
                calcP1Y   = GS.PlayerY;
                calcP1Z   = GS.PlayerZ;
                calcMapID = GS.MapID;
            }

            ImGui::TableSetColumnIndex(1);
            if (ImGui::Button("Calculate", ImVec2(-1, 0)))
            {
                calcCX     = (calcP1X + calcP2X) * 0.5f;
                calcCY     = (calcP1Y + calcP2Y) * 0.5f;
                calcCZ     = (calcP1Z + calcP2Z) * 0.5f;
                float dx   = calcP2X - calcP1X;
                float dy   = calcP2Y - calcP1Y;
                float dz   = calcP2Z - calcP1Z;
                calcRadius = std::sqrt(dx*dx + dy*dy + dz*dz) * 0.5f;
                calcHasResult = true;
            }

            ImGui::TableSetColumnIndex(2);
            if (calcHasResult)
            {
                ImGui::Text("C"); ImGui::SameLine();
                ImGui::SetNextItemWidth(60); ImGui::DragFloat("##cx", &calcCX, 0.1f, 0, 0, "%.2f"); ImGui::SameLine();
                ImGui::SetNextItemWidth(60); ImGui::DragFloat("##cy", &calcCY, 0.1f, 0, 0, "%.2f"); ImGui::SameLine();
                ImGui::SetNextItemWidth(60); ImGui::DragFloat("##cz", &calcCZ, 0.1f, 0, 0, "%.2f"); ImGui::SameLine();
                ImGui::Text("R"); ImGui::SameLine();
                ImGui::SetNextItemWidth(60); ImGui::DragFloat("##cr", &calcRadius, 0.1f, 0, 0, "%.2f");
            }

            // --- Row 2: P2 | Clear | Add Checkpoint + Copy ---
            ImGui::TableNextRow();

            ImGui::TableSetColumnIndex(0);
            ImGui::Text("P2"); ImGui::SameLine();
            ImGui::SetNextItemWidth(55); ImGui::TextDisabled("       "); ImGui::SameLine();
            ImGui::SetNextItemWidth(60); ImGui::DragFloat("##p2x", &calcP2X, 0.1f, 0, 0, "%.2f"); ImGui::SameLine();
            ImGui::SetNextItemWidth(60); ImGui::DragFloat("##p2y", &calcP2Y, 0.1f, 0, 0, "%.2f"); ImGui::SameLine();
            ImGui::SetNextItemWidth(60); ImGui::DragFloat("##p2z", &calcP2Z, 0.1f, 0, 0, "%.2f"); ImGui::SameLine();
            if (ImGui::Button("Cap##p2") && (MumbleLink || GS.RTAPIAvailable))
            {
                calcP2X = GS.PlayerX;
                calcP2Y = GS.PlayerY;
                calcP2Z = GS.PlayerZ;
            }

            ImGui::TableSetColumnIndex(1);
            if (ImGui::Button("Clear##calc", ImVec2(-1, 0)))
            {
                calcP1X = calcP1Y = calcP1Z = 0;
                calcP2X = calcP2Y = calcP2Z = 0;
                calcMapID = 0;
                calcHasResult = false;
            }

            ImGui::TableSetColumnIndex(2);
            if (calcHasResult)
            {
                if (ImGui::Button("Add as Checkpoint##calc"))
                {
                    CheckpointState cp;
                    snprintf(cp.Name, sizeof(cp.Name), "Checkpoint %d", (int)CurrentRoute.Checkpoints.size() + 1);
                    cp.Point.X           = calcCX;
                    cp.Point.Y           = calcCY;
                    cp.Point.Z           = calcCZ;
                    cp.Point.MapID       = calcMapID;
                    cp.Point.RadiusWidth = calcRadius;
                    CurrentRoute.Checkpoints.push_back(cp);
                    FullReset();
                }
                ImGui::SameLine();
                if (ImGui::Button("Copy##calc"))
                {
                    char buf[128];
                    snprintf(buf, sizeof(buf), "MapID:%u X:%.2f Y:%.2f Z:%.2f R:%.2f",
                        calcMapID, calcCX, calcCY, calcCZ, calcRadius);
                    ImGui::SetClipboardText(buf);
                }
            }

            ImGui::EndTable();
        }
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

    if (showCombatWarning)
    {
        ImGui::Spacing();
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.3f, 0.3f, 1.0f)); // Red text
        ImGui::TextWrapped("Combat trigger detected without RTAPI — death detection uses movement heuristics. Keep moving for 2+ seconds after combat ends or the segment will be marked tainted.");
        ImGui::PopStyleColor();
    }

    ImGui::End();
}