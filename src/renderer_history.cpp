// renderer_history.cpp
// Implements the "Run History" window — a table of every completed run for
// the active route, with split details available as a hover tooltip.
//
// Features:
//   • Fastest run time is highlighted in green.
//   • The active "best run" (used for split comparisons) is highlighted with
//     a green row background.
//   • Hovering a run's time shows a per-split breakdown tooltip.
//   • Right-clicking a run opens a context menu to set it as the best run
//     or permanently delete it.
//   • A "Clear History" button (with a confirmation popup) wipes all runs.
//   • All changes are persisted to the .history file on disk immediately.

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
        // -------------------------------------------------------------------------
        // Clear History button — opens a confirmation popup before wiping data
        // so an accidental click can't destroy the history irreversibly.
        // -------------------------------------------------------------------------
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
                BestRun.clear();
                BestRunIndex = -1;
                if (!CurrentHistoryPath.empty())
                    SaveHistory(CurrentHistoryPath, BestRun, HistoryRuns, BestRunIndex);
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel"))
                ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }

        ImGui::Separator();

        // -------------------------------------------------------------------------
        // Find the fastest total time across all runs so we can colour it green.
        // We scan every time on every draw; the list is small enough that this is
        // negligible compared to ImGui draw costs.
        // -------------------------------------------------------------------------
        double fastestTime = -1.0;
        for (const auto& r : HistoryRuns)
            if (fastestTime < 0.0 || r.TotalTime < fastestTime)
                fastestTime = r.TotalTime;

        // -------------------------------------------------------------------------
        // Run history table
        // Columns: # (row number) | Date | Time
        // The table is scrollable; 40 px at the bottom is reserved for any
        // controls we might add below it in future.
        // -------------------------------------------------------------------------
        if (ImGui::BeginTable("history", 3,
            ImGuiTableFlags_Borders |
            ImGuiTableFlags_RowBg   |
            ImGuiTableFlags_ScrollY,
            ImVec2(0.0f, -40.0f)))
        {
            ImGui::TableSetupColumn("#",    ImGuiTableColumnFlags_WidthFixed,   30.0f);
            ImGui::TableSetupColumn("Date", ImGuiTableColumnFlags_WidthFixed,  140.0f);
            ImGui::TableSetupColumn("Time", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableHeadersRow();

            char buf[32];
            int removeIndex = -1; // Set when the player chooses "Delete Run" in the context menu

            for (int i = 0; i < (int)HistoryRuns.size(); i++)
            {
                const HistoricalRun& run = HistoryRuns[i];

                bool isFastest = std::abs(run.TotalTime - fastestTime) < 0.001;

                ImGui::TableNextRow();
                
                // Check whether this run is the currently active "best run".
                // Highlight the active best run row with a subtle green background.
                if (i == BestRunIndex)
                    ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0,
                        IM_COL32(50, 80, 50, 150));

                ImGui::TableSetColumnIndex(0);
                ImGui::Text("%d", i + 1);

                ImGui::TableSetColumnIndex(1);
                ImGui::Text("%s", run.Date.c_str());

                ImGui::TableSetColumnIndex(2);
                FormatTime(buf, sizeof(buf), run.TotalTime);
                // Fastest run is drawn in bright green; all others are plain white.
                ImGui::TextColored(
                    isFastest
                        ? ImVec4(0.2f, 1.0f, 0.2f, 1.0f)
                        : ImVec4(1.0f, 1.0f, 1.0f, 1.0f),
                    "%s", buf);

                // -----------------------------------------------------------------
                // Hover tooltip — shows a per-split breakdown for this run.
                // The time shown per split follows the global TimerDisplayMode:
                //   Split mode    → cumulative time from run start
                //   Segment mode  → time for this segment only (delta from previous split)
                // The final "Goal" split added by the AllCheckpoints goal type is
                // hidden here because it carries no meaningful time of its own.
                // -----------------------------------------------------------------
                if (ImGui::IsItemHovered())
                {
                    ImGui::BeginTooltip();
                    if (ImGui::BeginTable("tooltip_splits", 2, ImGuiTableFlags_None))
                    {
                        ImGui::TableSetupColumn("Split", ImGuiTableColumnFlags_WidthStretch);
                        ImGui::TableSetupColumn("Time",  ImGuiTableColumnFlags_WidthFixed, 100.0f);

                        // Suppress the synthetic "Goal" split that AllCheckpoints
                        // goals append — it's redundant with the Total line below.
                        const Checkpoint* tooltipGoalCp = GetGoal(CurrentRoute);
                        bool tooltipGoalIsAllCheckpoints = tooltipGoalCp &&
                            tooltipGoalCp->Point.TriggerType == ETriggerType::AllCheckpoints;
                        int splitsToShow = (int)run.Splits.size();
                        if (tooltipGoalIsAllCheckpoints && splitsToShow > 0 &&
                            strcmp(run.Splits.back().Name, "Goal") == 0)
                            splitsToShow--;

                        for (int s = 0; s < splitsToShow; s++)
                        {
                            // In Split mode show cumulative time; in all other modes
                            // show the segment delta (time since the previous split).
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
                    // Footer of the tooltip: always show the run's total and grand total.
                    ImGui::Separator();
                    FormatTime(buf, sizeof(buf), run.TotalTime);
                    ImGui::Text("Total: %s", buf);
                    if (run.GrandTotal > 0.0)
                    {
                        // Grand total includes load screen time; only shown when it
                        // differs meaningfully from TotalTime (i.e. loads were detected).
                        FormatTime(buf, sizeof(buf), run.GrandTotal);
                        ImGui::Text("Grand Total: %s", buf);
                    }
                    ImGui::EndTooltip();
                }

                // -----------------------------------------------------------------
                // Right-click context menu
                //   "Set as best"  → promotes this run's splits to BestRun so the
                //                    live timer can diff against them.
                //   "Delete Run"   → marks this row for deferred removal.
                // -----------------------------------------------------------------
                char popupId[32]; snprintf(popupId, sizeof(popupId), "##ctx_%d", i);
                if (ImGui::BeginPopupContextItem(popupId))
                {
                    if (ImGui::MenuItem("Set as best"))
                    {
                        BestRun      = run.Splits;
                        BestRunIndex = i; // Store the index directly — no timestamp matching needed
                        if (!CurrentHistoryPath.empty())
                            SaveHistory(CurrentHistoryPath, BestRun, HistoryRuns, BestRunIndex);
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

            // -----------------------------------------------------------------
            // Deferred run deletion — safe to do after the table loop ends.
            // After erasing, we re-scan to find the new index of the best run
            // (deletion may have shifted it) so the history file is written with
            // the correct best-run pointer.
            // -----------------------------------------------------------------
            if (removeIndex >= 0)
            {
                HistoryRuns.erase(HistoryRuns.begin() + removeIndex);

                // Adjust BestRunIndex using simple arithmetic — no timestamp
                // matching needed now that we track the index directly.
                if (BestRunIndex == removeIndex)
                {
                    // The best run itself was deleted — clear it.
                    BestRun.clear();
                    BestRunIndex = -1;
                }
                else if (BestRunIndex > removeIndex)
                {
                    // A run before the best run was deleted — shift the index down by one.
                    BestRunIndex--;
                }
                // If BestRunIndex < removeIndex the best run is unaffected.

                if (!CurrentHistoryPath.empty())
                    SaveHistory(CurrentHistoryPath, BestRun, HistoryRuns, BestRunIndex);
            }
        }
    }
    
    ImGui::End();
}
