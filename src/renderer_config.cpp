// renderer_config.cpp
#include "renderer_shared.h"

namespace fs = std::filesystem;

void RenderConfigWindow()
{
    if (!ShowConfig) return;

    ImGui::Begin("Speedrun Config", &ShowConfig);

    // Route name field
    ImGui::Text("Route:");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(200.0f);
    static char routeNameBuf[64] = "New Route";
    if (CurrentRouteName.size() >= 64)
        CurrentRouteName.resize(63);
    strncpy(routeNameBuf, CurrentRouteName.c_str(), 63);
    routeNameBuf[63] = '\0';
    auto routeNameFilter = [](ImGuiInputTextCallbackData* data) -> int
    {
        char c = (char)data->EventChar;
        // Windows: \ / : * ? " < > |
        // Linux/Mac: / and null byte
        // Also block . to prevent . and .. path traversal
        if (c == '\\' || c == '/' || c == ':' || c == '*' ||
            c == '?' || c == '"' || c == '<' || c == '>' || c == '|' ||
            c == '.' || c == '\0')
            return 1; // reject
        return 0; // accept everything else
    };
    if (ImGui::InputText("##routename", routeNameBuf, 64,
        ImGuiInputTextFlags_CallbackCharFilter, routeNameFilter))
        CurrentRouteName = routeNameBuf;
    Tooltip("Tip: \\ / : * ? \" < > | . are not allowed in route names.");
    ImGui::SameLine();

    // Save directory field
    static char saveDirBuf[512]         = "";
    static std::string lastSeenFilepath = "";
    if (CurrentRouteFilepath != lastSeenFilepath)
    {
        lastSeenFilepath = CurrentRouteFilepath;
        std::string dir = CurrentRouteFilepath.empty()
            ? AddonDir
            : fs::path(CurrentRouteFilepath).parent_path().string();
        strncpy(saveDirBuf, dir.c_str(), sizeof(saveDirBuf) - 1);
        saveDirBuf[sizeof(saveDirBuf) - 1] = '\0';
    }
    if (saveDirBuf[0] == '\0' && !AddonDir.empty())
    {
        strncpy(saveDirBuf, AddonDir.c_str(), sizeof(saveDirBuf) - 1);
        saveDirBuf[sizeof(saveDirBuf) - 1] = '\0';
    }

    if (ImGui::Button("Save Route"))
    {
        std::string dir   = saveDirBuf;
        std::string newFP = dir + "\\" + CurrentRouteName + ".json";
        std::string newHP = dir + "\\" + CurrentRouteName + ".history";

        if (!CurrentRouteFilepath.empty() && newFP == CurrentRouteFilepath)
        {
            SaveRoute(CurrentRouteFilepath, CurrentRoute);
            if (!CurrentHistoryPath.empty())
                SaveHistory(CurrentHistoryPath, BestRun, HistoryRuns);
        }
        else
        {
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

    ImGui::Text("Dir:"); ImGui::SameLine();
    ImGui::SetNextItemWidth(-1.0f);
    ImGui::InputText("##savedir", saveDirBuf, sizeof(saveDirBuf));

    ImGui::Separator();

    bool hasInteract = false;
    for (const auto& cp : CurrentRoute.Checkpoints)
        if (cp.Point.TriggerType == ETriggerType::CircleInteract)
            { hasInteract = true; break; }

    float footerReserve = ImGui::GetFrameHeightWithSpacing() * 2.0f
                        + ImGui::GetStyle().ItemSpacing.y * 3.0f + 1.0f;
    if (hasInteract)
        footerReserve += ImGui::GetFrameHeightWithSpacing() * 1.0f;

    ImGui::BeginChild("##route_scroll", ImVec2(0, -footerReserve));
    if (ImGui::BeginTable("route_table", 12,
        ImGuiTableFlags_Borders |
        ImGuiTableFlags_Resizable |
        ImGuiTableFlags_SizingStretchProp |
        ImGuiTableFlags_ScrollY))
    {
        ImGui::TableSetupColumn("Name",    ImGuiTableColumnFlags_WidthFixed,   100.0f);
        ImGui::TableSetupColumn("S",       ImGuiTableColumnFlags_WidthFixed,    20.0f);
        ImGui::TableSetupColumn("G",       ImGuiTableColumnFlags_WidthFixed,    20.0f);
        ImGui::TableSetupColumn("Trigger", ImGuiTableColumnFlags_WidthFixed,    90.0f);
        ImGui::TableSetupColumn("MapID",   ImGuiTableColumnFlags_WidthFixed,    60.0f);
        ImGui::TableSetupColumn("X",       ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Y",       ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Z",       ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("R/W",     ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Angle",   ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Capture", ImGuiTableColumnFlags_WidthFixed,    50.0f);
        ImGui::TableSetupColumn("Remove",  ImGuiTableColumnFlags_WidthFixed,    50.0f);
        ImGui::TableHeadersRow();

        int removeIndex = -1;
        for (int i = 0; i < (int)CurrentRoute.Checkpoints.size(); i++)
        {
            ImGui::TableNextRow();
            Checkpoint& cp       = CurrentRoute.Checkpoints[i];
            RoutePoint& point    = cp.Point;
            bool isMapChange     = point.TriggerType == ETriggerType::MapChange;
            bool isAllCheckpoints = point.TriggerType == ETriggerType::AllCheckpoints;
            bool isGoal          = cp.IsGoal;

            // Name
            ImGui::TableSetColumnIndex(0);
            ImGui::SetNextItemWidth(-1);
            char nameLabel[32]; snprintf(nameLabel, sizeof(nameLabel), "##cpname_%d", i);
            ImGui::InputText(nameLabel, cp.Name, sizeof(cp.Name));

            // Start checkbox — only one allowed
            ImGui::TableSetColumnIndex(1);
            bool isStart = cp.IsStart;
            char startLabel[32]; snprintf(startLabel, sizeof(startLabel), "##s_%d", i);
            if (ImGui::Checkbox(startLabel, &isStart))
            {
                if (isStart)
                {
                    // Clear IsStart on all other checkpoints
                    for (auto& other : CurrentRoute.Checkpoints)
                        other.IsStart = false;
                }
                cp.IsStart = isStart;
            }

            // Goal checkbox — only one allowed
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
            }

            // Trigger type
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
                int displayIndex = (point.TriggerType == ETriggerType::CombatArena) ? 4 : (int)point.TriggerType;
                if (point.TriggerType == ETriggerType::AllCheckpoints) displayIndex = 0;
                if (ImGui::Combo(comboLabel, &displayIndex, triggerTypes, 5))
                    point.TriggerType = (displayIndex == 4) ? ETriggerType::CombatArena : (ETriggerType)displayIndex;
            }

            // MapID
            ImGui::TableSetColumnIndex(4);
            if (!isAllCheckpoints)
            {
                ImGui::SetNextItemWidth(-1);
                char mapIdLabel[32]; snprintf(mapIdLabel, sizeof(mapIdLabel), "##mapid_%d", i);
                int mapId = (int)point.MapID;
                if (ImGui::InputInt(mapIdLabel, &mapId, 0, 0))
                    point.MapID = (unsigned int)mapId;
            }

            // X
            ImGui::TableSetColumnIndex(5);
            if (!isMapChange && !isAllCheckpoints)
            {
                ImGui::SetNextItemWidth(-1);
                char l[32]; snprintf(l, sizeof(l), "##x_%d", i);
                ImGui::InputFloat(l, &point.X, 0.0f, 0.0f, "%.2f");
            }

            // Y
            ImGui::TableSetColumnIndex(6);
            if (!isMapChange && !isAllCheckpoints)
            {
                ImGui::SetNextItemWidth(-1);
                char l[32]; snprintf(l, sizeof(l), "##y_%d", i);
                ImGui::InputFloat(l, &point.Y, 0.0f, 0.0f, "%.2f");
            }

            // Z
            ImGui::TableSetColumnIndex(7);
            if (!isMapChange && !isAllCheckpoints)
            {
                ImGui::SetNextItemWidth(-1);
                char l[32]; snprintf(l, sizeof(l), "##z_%d", i);
                ImGui::InputFloat(l, &point.Z, 0.0f, 0.0f, "%.2f");
            }

            // R/W
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

            // Angle / Re-arm
            ImGui::TableSetColumnIndex(9);
            if (point.TriggerType == ETriggerType::CombatArena)
            {
                if (i < (int)CombatCheckpoints.size() && CombatCheckpoints[i].finished)
                {
                    char rearmLabel[32]; snprintf(rearmLabel, sizeof(rearmLabel), "Re-arm##%d", i);
                    if (ImGui::Button(rearmLabel))
                    {
                        CombatCheckpoints[i] = {};
                        if (i < (int)checkpointTriggered.size())
                            checkpointTriggered[i] = false;
                    }
                }
            }
            else if (!isMapChange && !isAllCheckpoints && point.TriggerType == ETriggerType::Plane)
            {
                ImGui::SetNextItemWidth(-1);
                char l[32]; snprintf(l, sizeof(l), "##angle_%d", i);
                ImGui::InputFloat(l, &point.PlaneAngle, 0.0f, 0.0f, "%.1f");
            }

            // Capture
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
                        float fx = MumbleLink->CameraFront.X;
                        float fz = MumbleLink->CameraFront.Z;
                        point.PlaneAngle = -(std::atan2(fx, fz) * 180.0f / 3.14159265f) + 90.0f;
                    }
                }
            }
            else
            {
                ImGui::TextDisabled("—");
            }

            // Remove
            ImGui::TableSetColumnIndex(11);
            char removeLabel[32]; snprintf(removeLabel, sizeof(removeLabel), "X##rm_%d", i);
            if (ImGui::Button(removeLabel))
                removeIndex = i;
        }

        ImGui::EndTable();

        if (removeIndex >= 0)
            CurrentRoute.Checkpoints.erase(CurrentRoute.Checkpoints.begin() + removeIndex);
        FullReset();
    }

    ImGui::EndChild();

    ImGui::Spacing();
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

    if (ImGui::Button("Activate Route"))
    {
        CurrentRoute.IsValid = true;
        FullReset();
    }
    ImGui::SameLine();
    if (ImGui::Button("Reset Timer"))
        FullReset();

    if (hasInteract)
    {
        ImGui::Spacing();
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.6f, 0.0f, 1.0f));
        ImGui::TextWrapped("Interact trigger detected - ensure Interact button is set and passthrough is enabled in Nexus -> Keybinds.");
        ImGui::PopStyleColor();
    }

    ImGui::End();
}
