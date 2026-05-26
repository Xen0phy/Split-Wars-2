// renderer_timer.cpp
#include "renderer_shared.h"

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
    double      grand     = DisplayedGrandTotal;
    bool        running   = SpeedrunTimer.IsRunning();
    bool        finished  = SpeedrunTimer.IsFinished();
    bool        hasBest   = !BestRun.empty();
    int         numSplits = (int)splits.size();
    char        buf[32];
    char        diffBuf[32];

    if (CompactMode)
    {
        FormatTime(buf, sizeof(buf), elapsed, !running);
        ImVec4 color = (running || finished)
            ? TimeColor(elapsed, hasBest ? BestRun.back().Timestamp : 0.0, running)
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
                double splitTime = (TimerDisplayMode == TimerMode::Split)
                    ? splits[i].Timestamp
                    : (i == 0 ? splits[i].Timestamp : splits[i].Timestamp - splits[i-1].Timestamp);

                double bestSplitTime = 0.0;
                double bestTimestamp = 0.0;
                if (hasBest && i < (int)BestRun.size())
                {
                    bestTimestamp = BestRun[i].Timestamp;
                    if (TimerDisplayMode == TimerMode::Segment)
                        bestSplitTime = (i == 0 ? BestRun[i].Timestamp : BestRun[i].Timestamp - BestRun[i-1].Timestamp);
                    else
                        bestSplitTime = BestRun[i].Timestamp;
                }
                double diffCurrent = (TimerDisplayMode == TimerMode::LiveSplit)
                    ? splits[i].Timestamp
                    : splitTime;
                double diffBest = (TimerDisplayMode == TimerMode::LiveSplit)
                    ? (hasBest && i < (int)BestRun.size() ? BestRun[i].Timestamp : 0.0)
                    : bestSplitTime;
                double diff = (hasBest && i < (int)BestRun.size())
                    ? diffCurrent - diffBest : 0.0;

                ImGui::TableNextRow();

                if (hasBest)
                {
                    ImGui::TableSetColumnIndex(0);
                    if (i < (int)BestRun.size() && std::abs(diff) > 0.0005)
                    {
                        if (FormatDiff(diffBuf, sizeof(diffBuf), diff, true))
                            ImGui::TextColored(diff < 0
                                ? ImVec4(0.2f, 1.0f, 0.2f, 1.0f)
                                : ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "%s", diffBuf);
                    }
                }

                ImGui::TableSetColumnIndex(hasBest ? 1 : 0);
                FormatTime(buf, sizeof(buf), splitTime);
                ImGui::TextColored(TimeColor(diffCurrent, diffBest, false), "%s", buf);

                ImGui::TableSetColumnIndex(hasBest ? 2 : 1);
                ImGui::TextDisabled("%s", splits[i].Name);
            }

            const Checkpoint* goalCp = GetGoal(CurrentRoute);
            bool goalIsAllCheckpoints = goalCp && goalCp->Point.TriggerType == ETriggerType::AllCheckpoints;

            bool manualStop = finished && numSplits > 0 &&
                              strcmp(splits[numSplits - 1].Name, "Manual Stop") == 0;

            if (running || (finished && !goalIsAllCheckpoints && !manualStop))
            {
                double segmentStart = numSplits > 0 ? splits[numSplits-1].Timestamp : 0.0;
                double segmentTime = (TimerDisplayMode == TimerMode::Split)
                    ? elapsed
                    : (elapsed - segmentStart);

                double bestSegmentTime = 0.0;
                bool   hasDiff = hasBest && numSplits < (int)BestRun.size();
                if (hasDiff)
                {
                    if (TimerDisplayMode == TimerMode::Segment)
                        bestSegmentTime = (numSplits == 0
                            ? BestRun[0].Timestamp
                            : BestRun[numSplits].Timestamp - BestRun[numSplits-1].Timestamp);
                    else
                        bestSegmentTime = BestRun[numSplits].Timestamp;
                }
                double diffCurSeg  = (TimerDisplayMode == TimerMode::LiveSplit) ? elapsed          : segmentTime;
                double diffBestSeg = (TimerDisplayMode == TimerMode::LiveSplit) ? bestSegmentTime   : bestSegmentTime;
                double diff = hasDiff ? diffCurSeg - diffBestSeg : 0.0;

                ImGui::TableNextRow();

                if (hasBest)
                {
                    ImGui::TableSetColumnIndex(0);
                    if (hasDiff && std::abs(diff) > 0.0005)
                    {
                        if (FormatDiff(diffBuf, sizeof(diffBuf), diff, finished))
                            ImGui::TextColored(diff < 0
                                ? ImVec4(0.2f, 1.0f, 0.2f, 1.0f)
                                : ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "%s", diffBuf);
                    }
                }

                ImGui::TableSetColumnIndex(hasBest ? 1 : 0);
                FormatTime(buf, sizeof(buf), segmentTime, true);
                ImGui::TextColored(TimeColor(diffCurSeg, diffBestSeg, running), "%s", buf);

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
                    double bestTotal = BestRun.back().Timestamp;
                    double totalDiff = elapsed - bestTotal;
                    if (std::abs(totalDiff) > 0.0005)
                    {
                        if (FormatDiff(diffBuf, sizeof(diffBuf), totalDiff, finished))
                            ImGui::TextColored(totalDiff < 0
                                ? ImVec4(0.2f, 1.0f, 0.2f, 1.0f)
                                : ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "%s", diffBuf);
                    }
                }

                ImGui::TableSetColumnIndex(hasBest ? 1 : 0);
                FormatTime(buf, sizeof(buf), elapsed, !running);
                double bestTotal = hasBest ? BestRun.back().Timestamp : 0.0;
                ImGui::TextColored(TimeColor(elapsed, bestTotal, running), "%s", buf);

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

            // Grand Total row
            if (ShowGrandTotal && (running || finished) && grand > 0.0)
            {
                ImGui::TableNextRow();

                if (hasBest)
                    ImGui::TableSetColumnIndex(0);

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
                BestRun = splits;
                if (BestRun.empty() || strcmp(BestRun.back().Name, "Goal") != 0)
                {
                    Split goalSplit;
                    strncpy(goalSplit.Name, "Goal", sizeof(goalSplit.Name) - 1);
                    goalSplit.Timestamp = elapsed;
                    BestRun.push_back(goalSplit);
                }
                if (!CurrentHistoryPath.empty())
                    SaveHistory(CurrentHistoryPath, BestRun, HistoryRuns, 0);
                RunFinished = false;
            }

            ImGui::SameLine();
            ImGui::Spacing();
            ImGui::SameLine();
            ImGui::Spacing();
            ImGui::SameLine();

            if (ImGui::Button("Reset Timer"))
            {
                FullReset();
                RunFinished = false;
            }
        }
    }

    ImGui::End();
}
