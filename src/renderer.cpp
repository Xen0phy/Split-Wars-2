// renderer.cpp
#include "renderer.h"
#include "shared.h"
#include "imgui.h"
#include "storage.h"
#include <cstdio>
#include <cmath>
#include <string>

static void FormatTime(char* buf, int bufSize, double elapsed)
{
    int hours   = (int)(elapsed / 3600);
    int minutes = (int)(elapsed / 60) % 60;
    int seconds = (int)(elapsed) % 60;
    int millis  = (int)(elapsed * 1000) % 1000;
    snprintf(buf, bufSize, "%02d:%02d:%02d.%03d", hours, minutes, seconds, millis);
}

static void FormatDiff(char* buf, int bufSize, double diff)
{
    double abs  = std::abs(diff);
    int seconds = (int)(abs) % 60;
    int millis  = (int)(abs * 1000) % 1000;
    snprintf(buf, bufSize, "%s%d.%03d", diff < 0 ? "-" : "+", seconds, millis);
}

static ImVec4 TimeColor(double current, double best, bool running)
{
    if (best <= 0.0)
        return ImVec4(0.2f, 1.0f, 0.2f, 1.0f);
    if (current <= best)
        return ImVec4(0.2f, 1.0f, 0.2f, 1.0f);
    return ImVec4(1.0f, 0.3f, 0.3f, 1.0f);
}

void RenderTimerOverlay()
{
    if (!ShowTimer) return;

    ImGui::SetNextWindowPos(ImVec2(10, 10), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowBgAlpha(0.6f);
    ImGui::Begin("Speedrun Timer", nullptr,
        ImGuiWindowFlags_NoDecoration |
        ImGuiWindowFlags_AlwaysAutoResize |
        ImGuiWindowFlags_NoFocusOnAppearing |
        ImGuiWindowFlags_NoNav
    );

    const auto& splits    = SpeedrunTimer.GetSplits();
    double      elapsed   = SpeedrunTimer.GetElapsedSeconds();
    double      grand     = GrandTimer.GetElapsedSeconds();
    bool        running   = SpeedrunTimer.IsRunning();
    bool        finished  = SpeedrunTimer.IsFinished();
    bool        hasBest   = !BestSplits.empty();
    int         numSplits = (int)splits.size();
    char        buf[32];
    char        diffBuf[32];

    if (CompactMode)
    {
        FormatTime(buf, sizeof(buf), elapsed);
        ImVec4 color = (running || finished)
            ? TimeColor(elapsed, hasBest ? BestSplits.back().Timestamp : 0.0, running)
            : ImVec4(0.5f, 0.5f, 0.5f, 1.0f);
        ImGui::TextColored(color, "%s", buf);
    }
    else
    {
        int numCols = hasBest ? 3 : 2;
        if (ImGui::BeginTable("splits", numCols, ImGuiTableFlags_None))
        {
            if (hasBest)
                ImGui::TableSetupColumn("Diff", ImGuiTableColumnFlags_WidthFixed, 70.0f);
            ImGui::TableSetupColumn("Time", ImGuiTableColumnFlags_WidthFixed, 100.0f);
            ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch);

            for (int i = 0; i < numSplits; i++)
            {
                double splitTime = SplitMode
                    ? (i == 0 ? splits[i].Timestamp : splits[i].Timestamp - splits[i-1].Timestamp)
                    : splits[i].Timestamp;

                double bestSplitTime = 0.0;
                double bestTimestamp = 0.0;
                if (hasBest && i < (int)BestSplits.size())
                {
                    bestTimestamp = BestSplits[i].Timestamp;
                    bestSplitTime = SplitMode
                        ? (i == 0 ? BestSplits[i].Timestamp : BestSplits[i].Timestamp - BestSplits[i-1].Timestamp)
                        : BestSplits[i].Timestamp;
                }
                double diff = (hasBest && i < (int)BestSplits.size())
                    ? splitTime - bestSplitTime : 0.0;

                ImGui::TableNextRow();

                if (hasBest)
                {
                    ImGui::TableSetColumnIndex(0);
                    if (i < (int)BestSplits.size() && std::abs(diff) > 0.0005)
                    {
                        FormatDiff(diffBuf, sizeof(diffBuf), diff);
                        ImGui::TextColored(diff < 0
                            ? ImVec4(0.2f, 1.0f, 0.2f, 1.0f)
                            : ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "%s", diffBuf);
                    }
                }

                ImGui::TableSetColumnIndex(hasBest ? 1 : 0);
                FormatTime(buf, sizeof(buf), splitTime);
                ImGui::TextColored(TimeColor(splitTime, bestSplitTime, false), "%s", buf);

                ImGui::TableSetColumnIndex(hasBest ? 2 : 1);
                ImGui::TextDisabled("%s", splits[i].Name);
            }

            bool goalIsAllCheckpoints = CurrentRoute.Goal.TriggerType == ETriggerType::AllCheckpoints;

            if (running || (finished && !goalIsAllCheckpoints))
            {
                double segmentStart = numSplits > 0 ? splits[numSplits-1].Timestamp : 0.0;
                double segmentTime  = SplitMode ? (elapsed - segmentStart) : elapsed;

                double bestSegmentTime = 0.0;
                bool   hasDiff = hasBest && numSplits < (int)BestSplits.size();
                if (hasDiff)
                {
                    bestSegmentTime = SplitMode
                        ? (numSplits == 0
                            ? BestSplits[0].Timestamp
                            : BestSplits[numSplits].Timestamp - BestSplits[numSplits-1].Timestamp)
                        : BestSplits[numSplits].Timestamp;
                }
                double diff = hasDiff ? segmentTime - bestSegmentTime : 0.0;

                ImGui::TableNextRow();

                if (hasBest)
                {
                    ImGui::TableSetColumnIndex(0);
                    if (hasDiff && std::abs(diff) > 0.0005)
                    {
                        FormatDiff(diffBuf, sizeof(diffBuf), diff);
                        ImGui::TextColored(diff < 0
                            ? ImVec4(0.2f, 1.0f, 0.2f, 1.0f)
                            : ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "%s", diffBuf);
                    }
                }

                ImGui::TableSetColumnIndex(hasBest ? 1 : 0);
                FormatTime(buf, sizeof(buf), segmentTime);
                ImGui::TextColored(TimeColor(segmentTime, bestSegmentTime, running), "%s", buf);

                ImGui::TableSetColumnIndex(hasBest ? 2 : 1);
                ImGui::TableSetColumnIndex(hasBest ? 2 : 1);
                if (finished)
                    ImGui::TextDisabled("Goal");
            }

            // Total row
            if ((running || finished) && numSplits > 0)
            {
                ImGui::TableNextRow();

                if (hasBest)
                {
                    ImGui::TableSetColumnIndex(0);
                    double bestTotal = BestSplits.back().Timestamp;
                    double totalDiff = elapsed - bestTotal;
                    if (std::abs(totalDiff) > 0.0005)
                    {
                        FormatDiff(diffBuf, sizeof(diffBuf), totalDiff);
                        ImGui::TextColored(totalDiff < 0
                            ? ImVec4(0.2f, 1.0f, 0.2f, 1.0f)
                            : ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "%s", diffBuf);
                    }
                }

                ImGui::TableSetColumnIndex(hasBest ? 1 : 0);
                FormatTime(buf, sizeof(buf), elapsed);
                double bestTotal = hasBest ? BestSplits.back().Timestamp : 0.0;
                ImGui::TextColored(TimeColor(elapsed, bestTotal, running), "%s", buf);

                // Tooltip on Total showing Grand Total
                if (ImGui::IsItemHovered() && grand > 0.0)
                {
                    ImGui::BeginTooltip();
                    FormatTime(buf, sizeof(buf), grand);
                    ImGui::Text("Grand Total: %s", buf);
                    ImGui::EndTooltip();
                }

                ImGui::TableSetColumnIndex(hasBest ? 2 : 1);
                ImGui::TextDisabled("Total");
            }

            // Grand Total row (always visible toggle)
            if (ShowGrandTotal && (running || finished) && grand > 0.0)
            {
                ImGui::TableNextRow();

                if (hasBest)
                    ImGui::TableSetColumnIndex(0); // empty diff cell

                ImGui::TableSetColumnIndex(hasBest ? 1 : 0);
                FormatTime(buf, sizeof(buf), grand);
                ImGui::TextDisabled("%s", buf);

                ImGui::TableSetColumnIndex(hasBest ? 2 : 1);
                ImGui::TextDisabled("Grand Total");
            }

            ImGui::EndTable();
        }

        if (!running && !finished)
        {
            ImGui::TextDisabled("00:00:00.000");
            if (!CurrentRoute.IsValid)
                ImGui::TextDisabled("No route set");
        }

        if (finished && RunFinished)
        {
            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();

            ImGui::Text("Run finished.");
            ImGui::Spacing();

            if (ImGui::Button("Save as best"))
            {
                BestSplits = splits;
                if (BestSplits.empty() || strcmp(BestSplits.back().Name, "Goal") != 0)
                {
                    Split goalSplit;
                    strncpy(goalSplit.Name, "Goal", sizeof(goalSplit.Name) - 1);
                    goalSplit.Timestamp = elapsed;
                    BestSplits.push_back(goalSplit);
                }
                SaveRoute(AddonDir, CurrentRoute, CurrentRouteName);
                SaveHistory(AddonDir, CurrentRouteName, BestSplits, HistoryRuns);
                RunFinished = false;
            }

            ImGui::SameLine();
            ImGui::Spacing();
            ImGui::SameLine();
            ImGui::Spacing();
            ImGui::SameLine();

            if (ImGui::Button("Reset Timer"))
            {
                SpeedrunTimer.Reset();
                RunFinished = false;
            }
        }
    }

    ImGui::End();
}

static void RenderRouteRow(const char* label, RoutePoint& point, int id, bool isGoal = false)
{
    ImGui::TableNextRow();

    // Name
    ImGui::TableSetColumnIndex(0);
    ImGui::TextDisabled("%s", label);

    // Trigger type
    ImGui::TableSetColumnIndex(1);
    const char* triggerTypes[]     = { "Circle", "Plane", "Map Change" };
    const char* goalTriggerTypes[] = { "Circle", "Plane", "Map Change", "All Checkpoints" };
    int currentType = (int)point.TriggerType;
    ImGui::SetNextItemWidth(-1);
    char comboLabel[32]; snprintf(comboLabel, sizeof(comboLabel), "##type_%d", id);
    if (isGoal)
    {
        if (ImGui::Combo(comboLabel, &currentType, goalTriggerTypes, 4))
            point.TriggerType = (ETriggerType)currentType;
    }
    else
    {
        if (currentType > 2) currentType = 0; // safety: Start can't be AllCheckpoints
        if (ImGui::Combo(comboLabel, &currentType, triggerTypes, 3))
            point.TriggerType = (ETriggerType)currentType;
    }

    bool isAllCheckpoints = (point.TriggerType == ETriggerType::AllCheckpoints);
    bool isMapChange      = (point.TriggerType == ETriggerType::MapChange);

    // MapID — hidden for AllCheckpoints
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

    ImGui::Begin("Speedrun Config");

    ImGui::Text("Route:");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(200.0f);
    static char routeNameBuf[128] = "New Route";
    if (CurrentRouteName.size() < sizeof(routeNameBuf))
        strncpy(routeNameBuf, CurrentRouteName.c_str(), sizeof(routeNameBuf) - 1);
    if (ImGui::InputText("##routename", routeNameBuf, sizeof(routeNameBuf)))
        CurrentRouteName = routeNameBuf;
    ImGui::SameLine();

    if (ImGui::Button("Save Route"))
        SaveRoute(AddonDir, CurrentRoute, CurrentRouteName);
    ImGui::SameLine();

    static std::vector<RouteFile> routeFiles;
    static bool needsRefresh = true;
    if (needsRefresh)
    {
        routeFiles   = ListRoutes(AddonDir);
        needsRefresh = false;
    }

    ImGui::SetNextItemWidth(180.0f);
    if (ImGui::BeginCombo("##routeload", "Load Route"))
    {
        needsRefresh = true;
        for (const auto& rf : routeFiles)
        {
            if (ImGui::Selectable(rf.Name.c_str()))
            {
                Route newRoute;
                std::string newName;
                if (LoadRoute(rf.Filename, newRoute, newName))
                {
                    CurrentRoute     = newRoute;
                    CurrentRouteName = newName;
                    strncpy(routeNameBuf, newName.c_str(), sizeof(routeNameBuf) - 1);
                    BestSplits.clear();
                    HistoryRuns.clear();
                    LoadHistory(AddonDir, newName, BestSplits, HistoryRuns);
                    SpeedrunTimer.Reset();
                    RunFinished = false;
                }
            }
        }
        ImGui::EndCombo();
    }

    ImGui::SameLine();
    if (ImGui::Button("History"))
        ShowHistory = !ShowHistory;

    ImGui::Separator();

    // Route table
    if (ImGui::BeginTable("route_table", 10,
        ImGuiTableFlags_Borders |
        ImGuiTableFlags_Resizable |
        ImGuiTableFlags_SizingStretchProp))
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
            const char* triggerTypes[] = { "Circle", "Plane", "Map Change" };
            int currentType = (int)cp.TriggerType;
            ImGui::SetNextItemWidth(-1);
            char comboLabel[32]; snprintf(comboLabel, sizeof(comboLabel), "##type_%d", i);
            if (ImGui::Combo(comboLabel, &currentType, triggerTypes, 3))
                cp.TriggerType = (ETriggerType)currentType;

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

            // Angle
            ImGui::TableSetColumnIndex(7);
            if (!isMapChange && cp.TriggerType == ETriggerType::Plane)
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

    ImGui::Spacing();
    ImGui::Separator();

    if (ImGui::Button("Activate Route"))
    {
        CurrentRoute.IsValid = true;
        SpeedrunTimer.Reset();
        RunFinished  = false;
        PendingStart = false;
    }
    ImGui::SameLine();
    if (ImGui::Button("Reset Timer"))
    {
        SpeedrunTimer.Reset();
        RunFinished  = false;
        PendingStart = false;
    }

    ImGui::End();
}

void RenderHistoryWindow()
{
    if (!ShowHistory) return;

    ImGui::SetNextWindowSize(ImVec2(500.0f, 400.0f), ImGuiCond_FirstUseEver);
    ImGui::Begin("Run History", &ShowHistory);

    ImGui::Text("Route: %s", CurrentRouteName.c_str());
    ImGui::Separator();

    if (HistoryRuns.empty())
    {
        ImGui::TextDisabled("No runs recorded yet.");
    }
    else
    {
        double fastestTime = -1.0;
        for (const auto& r : HistoryRuns)
            if (fastestTime < 0.0 || r.TotalTime < fastestTime)
                fastestTime = r.TotalTime;

        if (ImGui::BeginTable("history", 3,
            ImGuiTableFlags_Borders |
            ImGuiTableFlags_RowBg |
            ImGuiTableFlags_ScrollY,
            ImVec2(0.0f, -40.0f)))
        {
            ImGui::TableSetupColumn("#",    ImGuiTableColumnFlags_WidthFixed,   30.0f);
            ImGui::TableSetupColumn("Date", ImGuiTableColumnFlags_WidthFixed,  140.0f);
            ImGui::TableSetupColumn("Time", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableHeadersRow();

            char buf[32];
            for (int i = 0; i < (int)HistoryRuns.size(); i++)
            {
                const HistoricalRun& run = HistoryRuns[i];

                bool isActiveBest = !BestSplits.empty() &&
                    !run.Splits.empty() &&
                    std::abs(run.TotalTime - BestSplits.back().Timestamp) < 0.001;

                bool isFastest = std::abs(run.TotalTime - fastestTime) < 0.001;

                ImGui::TableNextRow();

                if (isActiveBest)
                    ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0,
                        IM_COL32(50, 80, 50, 150));

                ImGui::TableSetColumnIndex(0);
                ImGui::Text("%d", i + 1);

                ImGui::TableSetColumnIndex(1);
                ImGui::Text("%s", run.Date.c_str());

                ImGui::TableSetColumnIndex(2);
                FormatTime(buf, sizeof(buf), run.TotalTime);
                ImGui::TextColored(
                    isFastest
                        ? ImVec4(0.2f, 1.0f, 0.2f, 1.0f)
                        : ImVec4(1.0f, 1.0f, 1.0f, 1.0f),
                    "%s", buf);

                if (ImGui::IsItemHovered())
                {
                    ImGui::BeginTooltip();
                    if (ImGui::BeginTable("tooltip_splits", 2, ImGuiTableFlags_None))
                    {
                        ImGui::TableSetupColumn("Split", ImGuiTableColumnFlags_WidthStretch);
                        ImGui::TableSetupColumn("Time",  ImGuiTableColumnFlags_WidthFixed, 100.0f);

                        bool tooltipGoalIsAllCheckpoints = CurrentRoute.Goal.TriggerType == ETriggerType::AllCheckpoints;
                        int  splitsToShow = (int)run.Splits.size();
                        if (tooltipGoalIsAllCheckpoints && splitsToShow > 0 &&
                            strcmp(run.Splits.back().Name, "Goal") == 0)
                            splitsToShow--;

                        for (int s = 0; s < splitsToShow; s++)
                        {
                            double splitTime = SplitMode
                                ? (s == 0 ? run.Splits[s].Timestamp
                                    : run.Splits[s].Timestamp - run.Splits[s-1].Timestamp)
                                : run.Splits[s].Timestamp;

                            ImGui::TableNextRow();
                            ImGui::TableSetColumnIndex(0);
                            ImGui::Text("%s", run.Splits[s].Name);
                            ImGui::TableSetColumnIndex(1);
                            FormatTime(buf, sizeof(buf), splitTime);
                            ImGui::Text("%s", buf);
                        }
                        ImGui::EndTable();
                    }
                    ImGui::Separator();
                    FormatTime(buf, sizeof(buf), run.TotalTime);
                    ImGui::Text("Total: %s", buf);
                    if (run.GrandTotal > 0.0)
                    {
                        FormatTime(buf, sizeof(buf), run.GrandTotal);
                        ImGui::Text("Grand Total: %s", buf);
                    }
                    ImGui::EndTooltip();
                }

                ImGui::SameLine();
                char setLabel[32]; snprintf(setLabel, sizeof(setLabel), "Set as best##%d", i);
                if (ImGui::SmallButton(setLabel))
                {
                    BestSplits = run.Splits;
                    SaveHistory(AddonDir, CurrentRouteName, BestSplits, HistoryRuns);
                }
            }

            ImGui::EndTable();
        }
    }

    ImGui::End();
}