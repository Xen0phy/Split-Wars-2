// renderer_history.cpp
// Implements the "Run History" window — a table of every completed run for
// the active route, with split details available as a hover tooltip.
//
// Features:
//   • Fastest run time is highlighted in ColorAhead.
//   • The active "best run" (used for split comparisons) is highlighted with
//     a ColorBestRow background.
//   • Hovering a run's time shows a per-split breakdown tooltip.
//   • Right-clicking a run opens a context menu to set it as the best run
//     or permanently delete it.
//   • A "Clear History" button (with a confirmation popup) wipes all runs.
//   • All changes are persisted to the .history file on disk immediately.

#include "renderer_shared.h"

void RenderHistoryWindow()
{
    if (!ShowHistory) return;

    // Window size
    static bool firstFrame = true;
    if (firstFrame) {
        ImGui::SetNextWindowSize(ImVec2(HistoryWindowW, HistoryWindowH), ImGuiCond_Always);
        firstFrame = false;
    }
    ImGui::Begin("Split Wars 2 - Run History", &ShowHistory);
    ImVec2 sz = ImGui::GetWindowSize();
    HistoryWindowW = sz.x;
    HistoryWindowH = sz.y;

    ImGui::Text("Route: %s", CurrentRouteName.c_str());
    ImGui::Separator();

    if (ImGui::BeginTabBar("##historytabs"))
    {
    // =========================================================================
    // Runs tab — completed run list
    // =========================================================================
    if (ImGui::BeginTabItem("Runs"))
    {
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
                    SaveHistory(CurrentHistoryPath, HistoryRuns, SegmentRecords, BestRunIndex);
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel"))
                ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }

        ImGui::Separator();

        // -------------------------------------------------------------------------
        // Find the fastest total time across all runs so we can highlight it with ColorAhead.
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

                // Highlight the active best run row with ColorBestRow.
                if (i == BestRunIndex)
                    ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0,
                        IM_COL32((int)(ColorBestRow[0] * 255),
                                (int)(ColorBestRow[1] * 255),
                                (int)(ColorBestRow[2] * 255),
                                150));

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
                            SaveHistory(CurrentHistoryPath, HistoryRuns, SegmentRecords, BestRunIndex);
                    }

                    // ---------------------------------------------------------
                    // Copy to clipboard — tab-separated splits in tooltip order,
                    // using H:MM:SS.mmm format so spreadsheets parse it as time.
                    // ---------------------------------------------------------
                    if (ImGui::MenuItem("Copy to clipboard"))
                    {
                        // Suppress the AllCheckpoints synthetic Goal split,
                        // same logic as the hover tooltip.
                        const Checkpoint* goalCp = GetGoal(CurrentRoute);
                        bool goalIsAllCheckpoints = goalCp &&
                            goalCp->Point.TriggerType == ETriggerType::AllCheckpoints;
                        int splitsToShow = (int)run.Splits.size();
                        if (goalIsAllCheckpoints && splitsToShow > 0 && goalCp &&
                            strcmp(run.Splits.back().Name, goalCp->Name) == 0)
                            splitsToShow--;

                        std::string clip;
                        char buf[32];
                        for (int s = 0; s < splitsToShow; s++)
                        {
                            double splitTime = (TimerDisplayMode == TimerMode::Split)
                                ? run.Splits[s].Timestamp
                                : (s == 0 ? run.Splits[s].Timestamp
                                    : run.Splits[s].Timestamp - run.Splits[s-1].Timestamp);

                            FormatTimeExport(buf, sizeof(buf), splitTime);
                            clip += run.Splits[s].Name;
                            clip += '\t';
                            clip += buf;
                            clip += '\n';
                        }

                        // Append total on a final line.
                        FormatTimeExport(buf, sizeof(buf), run.TotalTime);
                        clip += "Total\t";
                        clip += buf;

                        FormatTimeExport(buf, sizeof(buf), run.GrandTotal);
                        clip += "\nGrand Total\t";
                        clip += buf;

                        ImGui::SetClipboardText(clip.c_str());
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
                    SaveHistory(CurrentHistoryPath, HistoryRuns, SegmentRecords, BestRunIndex);
            }
        }
    } // end else (HistoryRuns not empty)
    ImGui::EndTabItem();
    } // end BeginTabItem("Runs")

    // =========================================================================
    // Segments tab — best times for named Start/End split pairs
    // =========================================================================
    if (ImGui::BeginTabItem("Segments"))
    {
        if (SegmentRecords.empty())
        {
            ImGui::TextDisabled("No segments recorded yet.");
            ImGui::TextDisabled("Name splits 'X Start' and 'X End' to track them.");
        }
        else
        {
            // -------------------------------------------------------------------------
            // Clear Segments button — confirmation popup before wiping all records.
            // -------------------------------------------------------------------------
            if (ImGui::Button("Clear Segments"))
                ImGui::OpenPopup("##confirmclearseg");

            if (ImGui::BeginPopup("##confirmclearseg"))
            {
                ImGui::Text("Clear all segment records?");
                ImGui::Spacing();
                ImGui::Separator();
                ImGui::Spacing();
                if (ImGui::Button("Yes, clear"))
                {
                    SegmentRecords.clear();
                    if (!CurrentHistoryPath.empty())
                        SaveHistory(CurrentHistoryPath, HistoryRuns, SegmentRecords, BestRunIndex);
                    ImGui::CloseCurrentPopup();
                }
                ImGui::SameLine();
                if (ImGui::Button("Cancel"))
                    ImGui::CloseCurrentPopup();
                ImGui::EndPopup();
            }

            ImGui::Separator();

            if (ImGui::BeginTable("segments", 3,
                ImGuiTableFlags_Borders |
                ImGuiTableFlags_RowBg   |
                ImGuiTableFlags_ScrollY,
                ImVec2(0.0f, -40.0f)))
            {
                ImGui::TableSetupColumn("Segment", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableSetupColumn("Best",    ImGuiTableColumnFlags_WidthFixed, 80.0f);
                ImGui::TableSetupColumn("Date",    ImGuiTableColumnFlags_WidthFixed, 140.0f);
                ImGui::TableHeadersRow();

                char buf[32];
                int removeSegIndex = -1; // Set when "Delete Segment" is chosen

                for (int i = 0; i < (int)SegmentRecords.size(); i++)
                {
                    const SegmentRecord& seg = SegmentRecords[i];

                    ImGui::TableNextRow();

                    // Row-wide selectable for right-click detection.
                    ImGui::TableSetColumnIndex(0);
                    char segSelectableId[32]; snprintf(segSelectableId, sizeof(segSelectableId), "##segrow_%d", i);
                    ImGui::Selectable(segSelectableId, false,
                        ImGuiSelectableFlags_SpanAllColumns,
                        ImVec2(0.0f, ImGui::GetTextLineHeight()));
                    bool segRightClicked = ImGui::IsItemClicked(ImGuiMouseButton_Right);

                    ImGui::SameLine();
                    ImGui::Text("%s", seg.name.c_str());

                    ImGui::TableSetColumnIndex(1);
                    FormatTime(buf, sizeof(buf), seg.bestTime);
                    ImGui::TextColored(ImVec4(0.2f, 1.0f, 0.2f, 1.0f), "%s", buf);

                    ImGui::TableSetColumnIndex(2);
                    ImGui::Text("%s", seg.bestDate.c_str());

                    // -----------------------------------------------------------------
                    // Right-click context menu — delete a single segment record.
                    // -----------------------------------------------------------------
                    char segPopupId[32]; snprintf(segPopupId, sizeof(segPopupId), "##segctx_%d", i);
                    if (segRightClicked)
                        ImGui::OpenPopup(segPopupId);
                    if (ImGui::BeginPopup(segPopupId))
                    {
                        if (ImGui::MenuItem("Delete Segment"))
                            removeSegIndex = i;
                        ImGui::EndPopup();
                    }
                }

                ImGui::EndTable();

                // Deferred segment deletion — safe to do after the table loop ends.
                if (removeSegIndex >= 0)
                {
                    SegmentRecords.erase(SegmentRecords.begin() + removeSegIndex);
                    if (!CurrentHistoryPath.empty())
                        SaveHistory(CurrentHistoryPath, HistoryRuns, SegmentRecords, BestRunIndex);
                }
            }
        }
        ImGui::EndTabItem();
    } // end BeginTabItem("Segments")

    ImGui::EndTabBar();
    } // end BeginTabBar

    ImGui::End();
}