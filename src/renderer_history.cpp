// renderer_history.cpp
#include "renderer_shared.h"

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
        if (ImGui::Button("Clear History"))
            ImGui::OpenPopup("##confirmclear");

        if (ImGui::BeginPopup("##confirmclear"))
        {
            ImGui::Text("Clear all history?");
            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();
            if (ImGui::Button("Yes, clear"))
            {
                HistoryRuns.clear();
                if (!CurrentHistoryPath.empty())
                    SaveHistory(CurrentHistoryPath, BestSplits, HistoryRuns);
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel"))
                ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }

        ImGui::Separator();

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
            int removeIndex = -1;

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
                            double splitTime = (TimerDisplayMode == TimerMode::Split)
                                ? run.Splits[s].Timestamp
                                : (s == 0 ? run.Splits[s].Timestamp
                                    : run.Splits[s].Timestamp - run.Splits[s-1].Timestamp);

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

                char popupId[32]; snprintf(popupId, sizeof(popupId), "##ctx_%d", i);
                if (ImGui::BeginPopupContextItem(popupId))
                {
                    if (ImGui::MenuItem("Set as best"))
                    {
                        BestSplits = run.Splits;
                        if (!CurrentHistoryPath.empty())
                            SaveHistory(CurrentHistoryPath, BestSplits, HistoryRuns);
                    }
                    ImGui::Spacing();
                    ImGui::Separator();
                    ImGui::Spacing();
                    if (ImGui::MenuItem("Delete Run"))
                        removeIndex = i;
                    ImGui::EndPopup();
                }
            }

            ImGui::EndTable();

            if (removeIndex >= 0)
            {
                HistoryRuns.erase(HistoryRuns.begin() + removeIndex);
                if (!CurrentHistoryPath.empty())
                    SaveHistory(CurrentHistoryPath, BestSplits, HistoryRuns);
            }
        }
    }

    ImGui::End();
}
