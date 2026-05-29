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
        //
        // Hover/context detection is row-wide via a SpanAllColumns Selectable.
        // The tooltip is drawn after the table closes to ensure only one tooltip
        // is shown at a time regardless of cursor position between rows.
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
            int hoveredRow  = -1; // Set to the row index the cursor is over; -1 = none

            for (int i = 0; i < (int)HistoryRuns.size(); i++)
            {
                const HistoricalRun& run = HistoryRuns[i];

                bool isFastest = std::abs(run.TotalTime - fastestTime) < 0.001;

                ImGui::TableNextRow();

                // Highlight the active best run row with a subtle green background.
                if (i == BestRunIndex)
                    ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0, IM_COL32(50, 80, 50, 150));

                // Row-wide hover/context detection via an invisible Selectable that
                // spans all columns. Cell contents are drawn on top via SameLine.
                ImGui::TableSetColumnIndex(0);
                char selectableId[32]; snprintf(selectableId, sizeof(selectableId), "##row_%d", i);
                ImGui::Selectable(selectableId, false,
                    ImGuiSelectableFlags_SpanAllColumns,
                    ImVec2(0.0f, ImGui::GetTextLineHeight()));
                if (ImGui::IsItemHovered() && hoveredRow == -1) hoveredRow = i;
                bool rowRightClicked = ImGui::IsItemClicked(ImGuiMouseButton_Right);

                // Draw cell contents on top of the selectable.
                ImGui::SameLine();
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

                // -----------------------------------------------------------------
                // Right-click context menu
                //   "Set as best"  → promotes this run's splits to BestRun so the
                //                    live timer can diff against them.
                //   "Delete Run"   → marks this row for deferred removal.
                // -----------------------------------------------------------------
                char popupId[32]; snprintf(popupId, sizeof(popupId), "##ctx_%d", i);
                if (rowRightClicked)
                    ImGui::OpenPopup(popupId);
                if (ImGui::BeginPopup(popupId))
                {
                    if (ImGui::MenuItem("Set as best"))
                    {
                        BestRun      = run.Splits;
                        BestRunIndex = i;
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
            // Hover tooltip — drawn after the table closes so only one tooltip
            // is ever shown per frame regardless of cursor position between rows.
            // Shows a per-split breakdown for the hovered run.
            // The time shown per split follows the global TimerDisplayMode:
            //   Split mode   → cumulative time from run start
            //   Segment mode → time for this segment only (delta from previous split)
            // The final "Goal" split added by the AllCheckpoints goal type is
            // hidden because it carries no meaningful time of its own.
            // -----------------------------------------------------------------
            if (hoveredRow >= 0)
            {
                const HistoricalRun& run = HistoryRuns[hoveredRow];
                ImGui::BeginTooltip();
                if (ImGui::BeginTable("tooltip_splits", 2, ImGuiTableFlags_None))
                {
                    ImGui::TableSetupColumn("Time",  ImGuiTableColumnFlags_WidthFixed, 100.0f);
                    ImGui::TableSetupColumn("Split", ImGuiTableColumnFlags_WidthStretch);

                    // Suppress the synthetic "Goal" split that AllCheckpoints
                    // goals append — it's redundant with the Total line below.
                    const Checkpoint* tooltipGoalCp = GetGoal(CurrentRoute);
                    bool tooltipGoalIsAllCheckpoints = tooltipGoalCp &&
                        tooltipGoalCp->Point.TriggerType == ETriggerType::AllCheckpoints;
                    int splitsToShow = (int)run.Splits.size();
                    if (tooltipGoalIsAllCheckpoints && splitsToShow > 0 && tooltipGoalCp &&
                        strcmp(run.Splits.back().Name, tooltipGoalCp->Name) == 0)
                        splitsToShow--;

                    for (int s = 0; s < splitsToShow; s++)
                    {
                        double splitTime = (TimerDisplayMode == TimerMode::Split)
                            ? run.Splits[s].Timestamp
                            : (s == 0 ? run.Splits[s].Timestamp
                                : run.Splits[s].Timestamp - run.Splits[s-1].Timestamp);

                        ImGui::TableNextRow();
                        ImGui::TableSetColumnIndex(0);
                        FormatTime(buf, sizeof(buf), splitTime);
                        float textWidth = ImGui::CalcTextSize(buf).x;
                        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - textWidth);
                        ImGui::Text("%s", buf);
                        ImGui::TableSetColumnIndex(1);
                        ImGui::Text("%s", run.Splits[s].Name);
                    }
                    ImGui::EndTable();
                }

                // Footer — always shows the run's total and grand total.
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

            // -----------------------------------------------------------------
            // Deferred run deletion — safe to do after the table loop ends.
            // After erasing, BestRunIndex is adjusted by simple arithmetic
            // rather than timestamp matching since we track the index directly.
            // -----------------------------------------------------------------
            if (removeIndex >= 0)
            {
                HistoryRuns.erase(HistoryRuns.begin() + removeIndex);

                if (BestRunIndex == removeIndex)
                {
                    // The best run itself was deleted — clear it.
                    BestRun.clear();
                    BestRunIndex = -1;
                }
                else if (BestRunIndex > removeIndex)
                {
                    // A run before the best run was deleted — shift the index down.
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
