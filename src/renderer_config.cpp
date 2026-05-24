// renderer_config.cpp
#include "renderer_shared.h"

namespace fs = std::filesystem;

static void RenderRouteRow(const char* label, RoutePoint& point, int id, bool isGoal = false)
{
    ImGui::TableNextRow();

    // Name
    ImGui::TableSetColumnIndex(0);
    ImGui::TextDisabled("%s", label);

    // Trigger type
    ImGui::TableSetColumnIndex(1);
    const char* triggerTypes[]     = { "Circle", "Plane", "Map Change", "Interact", "Combat(Mumble)" };
    const char* goalTriggerTypes[] = { "Circle", "Plane", "Map Change", "Interact", "Combat(Mumble)", "All Checkpoints" };

    auto ToNonGoalIndex = [](ETriggerType t) -> int {
        if (t == ETriggerType::CombatArena) return 4;
        return (int)t;
    };
    auto FromNonGoalIndex = [](int i) -> ETriggerType {
        if (i == 4) return ETriggerType::CombatArena;
        return (ETriggerType)i;
    };

    int currentType = (int)point.TriggerType;
    ImGui::SetNextItemWidth(-1);
    char comboLabel[32]; snprintf(comboLabel, sizeof(comboLabel), "##type_%d", id);
    if (isGoal)
    {
        if (ImGui::Combo(comboLabel, &currentType, goalTriggerTypes, 6))
            point.TriggerType = (ETriggerType)currentType;
    }
    else
    {
        if (currentType == (int)ETriggerType::AllCheckpoints) currentType = 0;
        int displayIndex = ToNonGoalIndex(point.TriggerType);
        if (ImGui::Combo(comboLabel, &displayIndex, triggerTypes, 5))
            point.TriggerType = FromNonGoalIndex(displayIndex);
    }

    bool isAllCheckpoints = (point.TriggerType == ETriggerType::AllCheckpoints);
    bool isMapChange      = (point.TriggerType == ETriggerType::MapChange);

    // MapID
    ImGui::TableSetColumnIndex(2);
    if (!isAllCheckpoints)
    {
        ImGui::SetNextItemWidth(-1);
        char mapIdLabel[32]; snprintf(mapIdLabel, sizeof(mapIdLabel), "##mapid_%d", id);
        int mapId = (int)point.MapID;
        if (ImGui::InputInt(mapIdLabel, &mapId, 0, 0))
            point.MapID = (unsigned int)mapId;
    }

    // X
    ImGui::TableSetColumnIndex(3);
    if (!isMapChange && !isAllCheckpoints)
    {
        ImGui::SetNextItemWidth(-1);
        char l[32]; snprintf(l, sizeof(l), "##x_%d", id);
        ImGui::InputFloat(l, &point.X, 0.0f, 0.0f, "%.2f");
    }

    // Y
    ImGui::TableSetColumnIndex(4);
    if (!isMapChange && !isAllCheckpoints)
    {
        ImGui::SetNextItemWidth(-1);
        char l[32]; snprintf(l, sizeof(l), "##y_%d", id);
        ImGui::InputFloat(l, &point.Y, 0.0f, 0.0f, "%.2f");
    }

    // Z
    ImGui::TableSetColumnIndex(5);
    if (!isMapChange && !isAllCheckpoints)
    {
        ImGui::SetNextItemWidth(-1);
        char l[32]; snprintf(l, sizeof(l), "##z_%d", id);
        ImGui::InputFloat(l, &point.Z, 0.0f, 0.0f, "%.2f");
    }

    // R/W
    ImGui::TableSetColumnIndex(6);
    if (!isMapChange && !isAllCheckpoints)
    {
        ImGui::SetNextItemWidth(-1);
        if (point.TriggerType == ETriggerType::Plane)
        {
            char l[32]; snprintf(l, sizeof(l), "##w_%d", id);
            ImGui::InputFloat(l, &point.PlaneWidth, 0.0f, 0.0f, "%.2f");
        }
        else
        {
            char l[32]; snprintf(l, sizeof(l), "##r_%d", id);
            ImGui::InputFloat(l, &point.Radius, 0.0f, 0.0f, "%.2f");
        }
    }

    // Angle
    ImGui::TableSetColumnIndex(7);
    if (!isMapChange && !isAllCheckpoints && point.TriggerType == ETriggerType::Plane)
    {
        ImGui::SetNextItemWidth(-1);
        char l[32]; snprintf(l, sizeof(l), "##angle_%d", id);
        ImGui::InputFloat(l, &point.PlaneAngle, 0.0f, 0.0f, "%.1f");
    }

    // Capture
    ImGui::TableSetColumnIndex(8);
    if (!isAllCheckpoints)
    {
        char capLabel[32]; snprintf(capLabel, sizeof(capLabel), "Cap##%d", id);
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
        ImGui::TableSetColumnIndex(8);
        ImGui::TextDisabled("—");
    }

    // Empty remove column for Start/Goal
    ImGui::TableSetColumnIndex(9);
}

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
    if (ImGui::InputText("##routename", routeNameBuf, 64))
        CurrentRouteName = routeNameBuf;
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
            SaveRoute(CurrentRouteFilepath, CurrentRoute, CurrentRouteName);
            if (!CurrentHistoryPath.empty())
                SaveHistory(CurrentHistoryPath, BestSplits, HistoryRuns);
        }
        else
        {
            SaveRoute(newFP, CurrentRoute, CurrentRouteName);
            CurrentRouteFilepath = newFP;
            CurrentHistoryPath   = newHP;
            lastSeenFilepath     = newFP;
            BestSplits.clear();
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

    bool hasInteract = CurrentRoute.Start.TriggerType == ETriggerType::CircleInteract ||
                       CurrentRoute.Goal.TriggerType  == ETriggerType::CircleInteract;
    for (const auto& cp : CurrentRoute.Checkpoints)
        if (cp.Point.TriggerType == ETriggerType::CircleInteract)
            { hasInteract = true; break; }

    float footerReserve = ImGui::GetFrameHeightWithSpacing() * 2.0f
                        + ImGui::GetStyle().ItemSpacing.y * 3.0f + 1.0f;
    if (hasInteract)
        footerReserve += ImGui::GetFrameHeightWithSpacing() * 1.0f;

    ImGui::BeginChild("##route_scroll", ImVec2(0, -footerReserve));
    if (ImGui::BeginTable("route_table", 10,
        ImGuiTableFlags_Borders |
        ImGuiTableFlags_Resizable |
        ImGuiTableFlags_SizingStretchProp |
        ImGuiTableFlags_ScrollY))
    {
        ImGui::TableSetupColumn("Name",    ImGuiTableColumnFlags_WidthFixed,   100.0f);
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

        RenderRouteRow("Start", CurrentRoute.Start, 9999, false);
        RenderRouteRow("Goal",  CurrentRoute.Goal,  9998, true);

        int removeIndex = -1;
        for (int i = 0; i < (int)CurrentRoute.Checkpoints.size(); i++)
        {
            ImGui::TableNextRow();
            RoutePoint& cp = CurrentRoute.Checkpoints[i].Point;
            bool isMapChange = cp.TriggerType == ETriggerType::MapChange;

            // Name
            ImGui::TableSetColumnIndex(0);
            ImGui::SetNextItemWidth(-1);
            char nameLabel[32]; snprintf(nameLabel, sizeof(nameLabel), "##cpname_%d", i);
            ImGui::InputText(nameLabel, CurrentRoute.Checkpoints[i].Name,
                             sizeof(CurrentRoute.Checkpoints[i].Name));

            // Trigger
            ImGui::TableSetColumnIndex(1);
            const char* triggerTypes[] = { "Circle", "Plane", "Map Change", "Interact", "Combat(Mumble)" };
            ImGui::SetNextItemWidth(-1);
            char comboLabel[32]; snprintf(comboLabel, sizeof(comboLabel), "##type_%d", i);
            int displayIndex = (cp.TriggerType == ETriggerType::CombatArena) ? 4 : (int)cp.TriggerType;
            if (ImGui::Combo(comboLabel, &displayIndex, triggerTypes, 5))
            {
                cp.TriggerType = (displayIndex == 4)
                    ? ETriggerType::CombatArena
                    : (ETriggerType)displayIndex;
            }

            // MapID
            ImGui::TableSetColumnIndex(2);
            ImGui::SetNextItemWidth(-1);
            char mapIdLabel[32]; snprintf(mapIdLabel, sizeof(mapIdLabel), "##mapid_%d", i);
            int mapId = (int)cp.MapID;
            if (ImGui::InputInt(mapIdLabel, &mapId, 0, 0))
                cp.MapID = (unsigned int)mapId;

            // X
            ImGui::TableSetColumnIndex(3);
            if (!isMapChange)
            {
                ImGui::SetNextItemWidth(-1);
                char l[32]; snprintf(l, sizeof(l), "##x_%d", i);
                ImGui::InputFloat(l, &cp.X, 0.0f, 0.0f, "%.2f");
            }

            // Y
            ImGui::TableSetColumnIndex(4);
            if (!isMapChange)
            {
                ImGui::SetNextItemWidth(-1);
                char l[32]; snprintf(l, sizeof(l), "##y_%d", i);
                ImGui::InputFloat(l, &cp.Y, 0.0f, 0.0f, "%.2f");
            }

            // Z
            ImGui::TableSetColumnIndex(5);
            if (!isMapChange)
            {
                ImGui::SetNextItemWidth(-1);
                char l[32]; snprintf(l, sizeof(l), "##z_%d", i);
                ImGui::InputFloat(l, &cp.Z, 0.0f, 0.0f, "%.2f");
            }

            // R/W
            ImGui::TableSetColumnIndex(6);
            if (!isMapChange)
            {
                ImGui::SetNextItemWidth(-1);
                if (cp.TriggerType == ETriggerType::Plane)
                {
                    char l[32]; snprintf(l, sizeof(l), "##w_%d", i);
                    ImGui::InputFloat(l, &cp.PlaneWidth, 0.0f, 0.0f, "%.2f");
                }
                else
                {
                    char l[32]; snprintf(l, sizeof(l), "##r_%d", i);
                    ImGui::InputFloat(l, &cp.Radius, 0.0f, 0.0f, "%.2f");
                }
            }

            // Angle / Re-arm
            ImGui::TableSetColumnIndex(7);
            if (cp.TriggerType == ETriggerType::CombatArena)
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
            else if (!isMapChange && cp.TriggerType == ETriggerType::Plane)
            {
                ImGui::SetNextItemWidth(-1);
                char l[32]; snprintf(l, sizeof(l), "##angle_%d", i);
                ImGui::InputFloat(l, &cp.PlaneAngle, 0.0f, 0.0f, "%.1f");
            }

            // Capture
            ImGui::TableSetColumnIndex(8);
            char capLabel[32]; snprintf(capLabel, sizeof(capLabel), "Cap##%d", i);
            if (ImGui::Button(capLabel) && MumbleLink)
            {
                cp.MapID = MumbleLink->Context.MapID;
                if (!isMapChange)
                {
                    cp.X = MumbleLink->AvatarPosition.X;
                    cp.Y = MumbleLink->AvatarPosition.Y;
                    cp.Z = MumbleLink->AvatarPosition.Z;
                }
                if (cp.TriggerType == ETriggerType::Plane)
                {
                    float fx = MumbleLink->CameraFront.X;
                    float fz = MumbleLink->CameraFront.Z;
                    cp.PlaneAngle = -(std::atan2(fx, fz) * 180.0f / 3.14159265f) + 90.0f;
                }
            }

            // Remove
            ImGui::TableSetColumnIndex(9);
            char removeLabel[32]; snprintf(removeLabel, sizeof(removeLabel), "X##rm_%d", i);
            if (ImGui::Button(removeLabel))
                removeIndex = i;
        }

        ImGui::EndTable();

        if (removeIndex >= 0)
            CurrentRoute.Checkpoints.erase(CurrentRoute.Checkpoints.begin() + removeIndex);
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
    }
    ImGui::SameLine();

    if (ImGui::Button("Clear Route"))
    {
        CurrentRoute.Checkpoints.clear();
        CurrentRoute.Start   = RoutePoint{};
        CurrentRoute.Goal    = RoutePoint{};
        CurrentRoute.IsValid = false;
        CurrentRouteName     = "New Route";
        CurrentRouteFilepath.clear();
        CurrentHistoryPath.clear();
        BestSplits.clear();
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
