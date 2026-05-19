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
        return ImVec4(0.2f, 1.0f, 0.2f, 1.0f); // no best = green
    if (current <= best)
        return ImVec4(0.2f, 1.0f, 0.2f, 1.0f); // ahead = green
    return ImVec4(1.0f, 0.3f, 0.3f, 1.0f);     // behind = red
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

    const auto& splits   = SpeedrunTimer.GetSplits();
    double      elapsed  = SpeedrunTimer.GetElapsedSeconds();
    bool        running  = SpeedrunTimer.IsRunning();
    bool        finished = SpeedrunTimer.IsFinished();
    bool        hasBest  = !BestSplits.empty();
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

            // Completed splits
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

            // Current running segment
            if (running || finished)
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

                ImGui::TableSetColumnIndex(hasBest ? 2 : 1);
                ImGui::TextDisabled("Total");
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

static void RenderStartPoint(RoutePoint& point)
{
    ImGui::Text("Start:");
    ImGui::SameLine();

    ImGui::Text("MapID");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(60.0f);
    int mapId = (int)point.MapID;
    if (ImGui::InputInt("##mapid_start", &mapId, 0, 0))
        point.MapID = (unsigned int)mapId;
    ImGui::SameLine();

    ImGui::Text("X");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(70.0f);
    ImGui::InputFloat("##x_start", &point.X, 0.0f, 0.0f, "%.2f");
    ImGui::SameLine();

    ImGui::Text("Y");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(70.0f);
    ImGui::InputFloat("##y_start", &point.Y, 0.0f, 0.0f, "%.2f");
    ImGui::SameLine();

    ImGui::Text("Z");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(70.0f);
    ImGui::InputFloat("##z_start", &point.Z, 0.0f, 0.0f, "%.2f");
    ImGui::SameLine();

    ImGui::Text("R");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(60.0f);
    ImGui::InputFloat("##r_start", &point.Radius, 0.0f, 0.0f, "%.2f");
    ImGui::SameLine();

    if (ImGui::Button("Capture##start") && MumbleLink)
    {
        point.X     = MumbleLink->AvatarPosition.X;
        point.Y     = MumbleLink->AvatarPosition.Y;
        point.Z     = MumbleLink->AvatarPosition.Z;
        point.MapID = MumbleLink->Context.MapID;
    }
}

static void RenderRoutePoint(const char* label, RoutePoint& point)
{
    ImGui::Text("%s", label);
    ImGui::SameLine();

    const char* triggerTypes[] = { "Circle", "Plane", "Map Change" };
    int currentType = (int)point.TriggerType;
    ImGui::SetNextItemWidth(90.0f);
    char comboLabel[32]; snprintf(comboLabel, sizeof(comboLabel), "##type_%s", label);
    if (ImGui::Combo(comboLabel, &currentType, triggerTypes, 3))
        point.TriggerType = (ETriggerType)currentType;
    ImGui::SameLine();

    ImGui::Text("MapID");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(60.0f);
    char mapIdLabel[32];
    snprintf(mapIdLabel, sizeof(mapIdLabel), "##mapid_%s", label);
    int mapId = (int)point.MapID;
    if (ImGui::InputInt(mapIdLabel, &mapId, 0, 0))
        point.MapID = (unsigned int)mapId;
    ImGui::SameLine();

    if (point.TriggerType != ETriggerType::MapChange)
    {
        ImGui::Text("X");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(70.0f);
        char xLabel[32]; snprintf(xLabel, sizeof(xLabel), "##x_%s", label);
        ImGui::InputFloat(xLabel, &point.X, 0.0f, 0.0f, "%.2f");
        ImGui::SameLine();

        ImGui::Text("Y");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(70.0f);
        char yLabel[32]; snprintf(yLabel, sizeof(yLabel), "##y_%s", label);
        ImGui::InputFloat(yLabel, &point.Y, 0.0f, 0.0f, "%.2f");
        ImGui::SameLine();

        ImGui::Text("Z");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(70.0f);
        char zLabel[32]; snprintf(zLabel, sizeof(zLabel), "##z_%s", label);
        ImGui::InputFloat(zLabel, &point.Z, 0.0f, 0.0f, "%.2f");
        ImGui::SameLine();

        if (point.TriggerType == ETriggerType::Plane)
        {
            ImGui::Text("W");
            ImGui::SameLine();
            ImGui::SetNextItemWidth(60.0f);
            char wLabel[32]; snprintf(wLabel, sizeof(wLabel), "##w_%s", label);
            ImGui::InputFloat(wLabel, &point.PlaneWidth, 0.0f, 0.0f, "%.2f");
            ImGui::SameLine();

            ImGui::Text("Angle");
            ImGui::SameLine();
            ImGui::SetNextItemWidth(60.0f);
            char aLabel[32]; snprintf(aLabel, sizeof(aLabel), "##angle_%s", label);
            ImGui::InputFloat(aLabel, &point.PlaneAngle, 0.0f, 0.0f, "%.1f");
            ImGui::SameLine();
        }
        else
        {
            ImGui::Text("R");
            ImGui::SameLine();
            ImGui::SetNextItemWidth(60.0f);
            char rLabel[32]; snprintf(rLabel, sizeof(rLabel), "##r_%s", label);
            ImGui::InputFloat(rLabel, &point.Radius, 0.0f, 0.0f, "%.2f");
            ImGui::SameLine();
        }
    }

    char capLabel[32]; snprintf(capLabel, sizeof(capLabel), "Capture##%s", label);
    if (ImGui::Button(capLabel) && MumbleLink)
    {
        point.MapID = MumbleLink->Context.MapID;
        if (point.TriggerType != ETriggerType::MapChange)
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

void RenderConfigWindow()
{
    if (!ShowConfig) return;

    ImGui::SetNextWindowSize(ImVec2(900.0f, 0.0f), ImGuiCond_Always);
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

    RenderStartPoint(CurrentRoute.Start);
    RenderRoutePoint("Goal: ", CurrentRoute.Goal);

    ImGui::Separator();

    int removeIndex = -1;
    for (int i = 0; i < (int)CurrentRoute.Checkpoints.size(); i++)
    {
        Checkpoint& cp = CurrentRoute.Checkpoints[i];

        ImGui::SetNextItemWidth(120.0f);
        char nameLabel[32]; snprintf(nameLabel, sizeof(nameLabel), "##cpname_%d", i);
        ImGui::InputText(nameLabel, cp.Name, sizeof(cp.Name));
        ImGui::SameLine();

        RenderRoutePoint(cp.Name, cp.Point);
        ImGui::SameLine();

        char removeLabel[32]; snprintf(removeLabel, sizeof(removeLabel), "X##rm_%d", i);
        if (ImGui::Button(removeLabel))
            removeIndex = i;
    }

    if (removeIndex >= 0)
        CurrentRoute.Checkpoints.erase(CurrentRoute.Checkpoints.begin() + removeIndex);

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
        RunFinished = false;
    }
    ImGui::SameLine();
    if (ImGui::Button("Reset Timer"))
    {
        SpeedrunTimer.Reset();
        RunFinished = false;
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
        // Find fastest time in history
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

                // Check if this is the active best (used for comparison)
                bool isActiveBest = !BestSplits.empty() &&
                    !run.Splits.empty() &&
                    std::abs(run.TotalTime - BestSplits.back().Timestamp) < 0.001;

                bool isFastest = std::abs(run.TotalTime - fastestTime) < 0.001;

                ImGui::TableNextRow();

                // Highlight background for active best
                if (isActiveBest)
                    ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0,
                        IM_COL32(50, 80, 50, 150));

                ImGui::TableSetColumnIndex(0);
                ImGui::Text("%d", i + 1);

                ImGui::TableSetColumnIndex(1);
                ImGui::Text("%s", run.Date.c_str());

                ImGui::TableSetColumnIndex(2);
                FormatTime(buf, sizeof(buf), run.TotalTime);

                // Green for fastest, white for others
                ImGui::TextColored(
                    isFastest
                        ? ImVec4(0.2f, 1.0f, 0.2f, 1.0f)
                        : ImVec4(1.0f, 1.0f, 1.0f, 1.0f),
                    "%s", buf);

                // Hover tooltip
                if (ImGui::IsItemHovered())
                {
                    ImGui::BeginTooltip();
                    if (ImGui::BeginTable("tooltip_splits", 2, ImGuiTableFlags_None))
                    {
                        ImGui::TableSetupColumn("Split", ImGuiTableColumnFlags_WidthStretch);
                        ImGui::TableSetupColumn("Time",  ImGuiTableColumnFlags_WidthFixed, 100.0f);

                        for (int s = 0; s < (int)run.Splits.size(); s++)
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
                    ImGui::EndTooltip();
                }

                // Set as best button
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