// render_history.cpp
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

#include "imgui.h"
#include "render_shared.h"

namespace fs = std::filesystem;

// -------------------------------------------------------------------------
// Backs up CurrentHistoryPath to "<path>.bak" before an in-place migration
// (Normalize Times / Add Start Checkpoint below) overwrites it. Best-effort:
// failure isn't fatal, it just means no safety copy was made this time.
// -------------------------------------------------------------------------
static bool BackupHistoryFile()
{
    if (CurrentHistoryPath.empty()) return false;
    std::error_code ec;
    fs::copy_file(CurrentHistoryPath, CurrentHistoryPath + ".bak",
                  fs::copy_options::overwrite_existing, ec);
    return !ec;
}

// -------------------------------------------------------------------------
// History Maintenance — shared state between RenderHistoryWindow() (which
// only has the two trigger buttons + the post-run result message) and
// RenderHistoryMaintenanceWindow() (the actual explanation/preview/confirm
// window, defined at the bottom of this file).
// -------------------------------------------------------------------------
enum class MaintenanceAction { None, Normalize, AddStart };

static MaintenanceAction s_PendingAction       = MaintenanceAction::None;
static std::string       s_MaintenanceMsg;
static bool              s_ShowMaintenanceMsg  = false;

// One run that a pending action would actually change, plus its before/after
// total time for the summary table.
struct AffectedRun
{
    int    runIndex;
    double beforeTotal;
    double afterTotal;
    double offset; // only meaningful for Normalize
};

// Scans HistoryRuns and returns every run the given action would touch.
// Read-only — makes no changes, so it's safe to call every frame for preview.
static std::vector<AffectedRun> ComputeAffectedRuns(MaintenanceAction action)
{
    std::vector<AffectedRun> out;
    for (int i = 0; i < (int)HistoryRuns.size(); i++)
    {
        const auto& run = HistoryRuns[i];
        bool hasStart = !run.Splits.empty() && run.Splits[0].Timestamp <= 0.0005;

        if (action == MaintenanceAction::Normalize)
        {
            if (run.Splits.empty() || hasStart) continue; // already starts at zero
            AffectedRun a;
            a.runIndex    = i;
            a.offset      = run.Splits[0].Timestamp;
            a.beforeTotal = run.TotalTime;
            a.afterTotal  = std::max(0.0, run.TotalTime - a.offset);
            out.push_back(a);
        }
        else if (action == MaintenanceAction::AddStart)
        {
            if (hasStart) continue; // already has a start split
            AffectedRun a;
            a.runIndex    = i;
            a.offset      = 0.0;
            a.beforeTotal = run.TotalTime;
            a.afterTotal  = run.TotalTime; // insertion doesn't change the total
            out.push_back(a);
        }
    }
    return out;
}

// Actually performs the mutation for `action`, backs up the file first, saves
// afterward, and sets the post-run result message shown back in the History
// window. Called only once the user hits Confirm in the preview window.
static void ApplyMaintenanceAction(MaintenanceAction action, CheckpointState* startCp)
{
    BackupHistoryFile();

    int changedRuns = 0;

    if (action == MaintenanceAction::Normalize)
    {
        for (auto& run : HistoryRuns)
        {
            if (run.Splits.empty()) continue;
            double offset = run.Splits[0].Timestamp;
            if (offset <= 0.0005) continue;

            for (auto& s : run.Splits)
                s.Timestamp -= offset;

            run.TotalTime  = std::max(0.0, run.TotalTime  - offset);
            run.GrandTotal = std::max(0.0, run.GrandTotal - offset); // GrandTimer starts at the same instant as SpeedrunTimer
            changedRuns++;
        }

        if (changedRuns > 0)
        {
            // A run's total time may have shrunk enough to become the new
            // best -- re-derive BestRunIndex/BestRun rather than leave them stale.
            int    newBest     = -1;
            double newBestTime = -1.0;
            for (int i = 0; i < (int)HistoryRuns.size(); i++)
            {
                if (newBest < 0 || HistoryRuns[i].TotalTime < newBestTime)
                {
                    newBest     = i;
                    newBestTime = HistoryRuns[i].TotalTime;
                }
            }
            BestRunIndex = newBest;
            BestRun      = (newBest >= 0) ? HistoryRuns[newBest].Splits : std::vector<Split>{};
        }

        s_MaintenanceMsg = (changedRuns > 0)
            ? ("Normalized " + std::to_string(changedRuns) + " run(s).\n\n"
               "If you'd added a checkpoint to your route just to work around the "
               "missing start split, you don't need it anymore -- feel free to remove "
               "it from the route.")
            : "Nothing to normalize -- every run already starts at 0:00:00.000.";
    }
    else if (action == MaintenanceAction::AddStart && startCp)
    {
        for (auto& run : HistoryRuns)
        {
            bool hasStart = !run.Splits.empty() && run.Splits[0].Timestamp <= 0.0005;
            if (hasStart) continue;

            Split startSplit;
            startSplit.Timestamp = 0.0;
            snprintf(startSplit.Name, sizeof(startSplit.Name), "%s", startCp->Name);
            run.Splits.insert(run.Splits.begin(), startSplit);
            changedRuns++;
        }

        if (changedRuns > 0 && BestRunIndex >= 0 && BestRunIndex < (int)HistoryRuns.size())
            BestRun = HistoryRuns[BestRunIndex].Splits; // re-sync in case the best run was one we just touched

        s_MaintenanceMsg = (changedRuns > 0)
            ? ("Added a \"" + std::string(startCp->Name) + "\" start split to " +
               std::to_string(changedRuns) + " run(s).")
            : "Nothing to add -- every run already has a split at 0:00:00.000.";
    }

    if (changedRuns > 0)
        SaveHistory(CurrentHistoryPath, HistoryRuns, SegmentRecords, BestRunIndex, MaxHistoryRuns);

    s_ShowMaintenanceMsg = true;
}

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

    // "Evaluation" opens the stacked run-chart window (render_evaluation.cpp)
    // for whatever history file is currently loaded. Only shown once a file
    // is actually loaded, since there's nothing to evaluate otherwise.
    if (!CurrentHistoryPath.empty())
    {
        ImGui::SameLine();
        if (ImGui::Button("Evaluation"))
            ShowEvaluation = true;
    }

    ImGui::Separator();

    // -------------------------------------------------------------------------
    // Max History Runs — per-route cap on stored runs, persisted in this
    // route's own .history file (0 = unlimited). Replaces the old global
    // settings.ini value so different routes can keep different amounts of
    // history (e.g. a daily fractal route vs. a rarely-run boss kill).
    // -------------------------------------------------------------------------
    if (!CurrentHistoryPath.empty())
    {
        if (ImGui::BeginTable("##maxhistoryrunstable", 2, ImGuiTableFlags_None))
        {
            ImGui::TableSetupColumn("##left",  ImGuiTableColumnFlags_WidthFixed, 80);
            ImGui::TableSetupColumn("##right", ImGuiTableColumnFlags_WidthFixed);

            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::Text("Max Runs kept");
            ImGui::SetNextItemWidth(40.0f);
            if (ImGui::InputInt("##maxhistoryruns", &MaxHistoryRuns, 0, 0))
            {
                if (MaxHistoryRuns < 0) MaxHistoryRuns = 0;
                SaveHistory(CurrentHistoryPath, HistoryRuns, SegmentRecords, BestRunIndex, MaxHistoryRuns);
            }
            Tooltip("0 = unlimited. Caps how many runs this route keeps; oldest runs\n"
                    "are trimmed first (the best run and fastest run are never removed).");

            ImGui::TableSetColumnIndex(1);
            ImGui::TextDisabled("(large history files can slow down loading — keeping\n"
                                 "it under a few hundred runs is recommended)");

            ImGui::EndTable();
        }
        ImGui::Separator();
    }


    // -------------------------------------------------------------------------
    // Footer height reservation
    // History Maintenance is drawn after the tab bar closes, below. We need to
    // know its height up front so the Runs/Segments tables (each scrollable,
    // further down) can reserve exactly enough room for it — otherwise the
    // maintenance buttons get pushed toward/behind the bottom of the window.
    // Mirrors the same approach used in render_config.cpp.
    // -------------------------------------------------------------------------
    bool showMaintenance = !CurrentHistoryPath.empty() && !HistoryRuns.empty();

    float footerReserve = 0.0f;
    if (showMaintenance)
    {
        footerReserve = ImGui::GetFrameHeightWithSpacing()   // "History Maintenance" label
                      + ImGui::GetFrameHeightWithSpacing()   // buttons row
                      + ImGui::GetStyle().ItemSpacing.y + 10.0f; // separator above it

        // The Dismiss message adds its own (variable-height) block: a spacer,
        // however many lines the wrapped message needs at the current window
        // width, and the Dismiss button row.
        if (s_ShowMaintenanceMsg)
        {
            float wrapWidth = ImGui::GetContentRegionAvail().x;
            ImVec2 msgSize  = ImGui::CalcTextSize(s_MaintenanceMsg.c_str(), nullptr, false, wrapWidth);
            footerReserve  += msgSize.y + ImGui::GetStyle().ItemSpacing.y
                            + ImGui::GetFrameHeightWithSpacing();
        }
    }

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
                            SaveHistory(CurrentHistoryPath, HistoryRuns, SegmentRecords, BestRunIndex, MaxHistoryRuns);
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
                    ImGuiTableFlags_ScrollY |
                    ImGuiTableFlags_Sortable,
                    ImVec2(0.0f, -footerReserve)))
                {
                    ImGui::TableSetupColumn("#",    ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_NoSort, 30.0f);
                    ImGui::TableSetupColumn("Date", ImGuiTableColumnFlags_WidthFixed,  140.0f);
                    ImGui::TableSetupColumn("Time", ImGuiTableColumnFlags_WidthStretch);
                    ImGui::TableHeadersRow();

                    // -------------------------------------------------------------------------
                    // Display order — HistoryRuns itself is never reordered (BestRunIndex,
                    // removeIndex, and every popup/selectable ID below are real indices into
                    // HistoryRuns and must stay valid regardless of what the player sorts by).
                    // Instead we sort a separate index array and iterate through that; "i"
                    // below always means "real index into HistoryRuns", "n" means "display
                    // position", matching the existing variable meaning everywhere else in
                    // this file.
                    // -------------------------------------------------------------------------
                    std::vector<int> order(HistoryRuns.size());
                    for (int n = 0; n < (int)order.size(); n++) order[n] = n;

                    if (ImGuiTableSortSpecs* sortSpecs = ImGui::TableGetSortSpecs())
                    {
                        if (sortSpecs->SpecsCount > 0)
                        {
                            const ImGuiTableColumnSortSpecs& spec = sortSpecs->Specs[0];
                            bool ascending = (spec.SortDirection == ImGuiSortDirection_Ascending);
                            if (spec.ColumnIndex == 1) // Date column
                            {
                                std::sort(order.begin(), order.end(), [&](int a, int b) {
                                    return ascending ? HistoryRuns[a].Date < HistoryRuns[b].Date
                                                      : HistoryRuns[a].Date > HistoryRuns[b].Date;
                                });
                            }
                            else if (spec.ColumnIndex == 2) // Time column
                            {
                                std::sort(order.begin(), order.end(), [&](int a, int b) {
                                    return ascending ? HistoryRuns[a].TotalTime < HistoryRuns[b].TotalTime
                                                      : HistoryRuns[a].TotalTime > HistoryRuns[b].TotalTime;
                                });
                            }
                            sortSpecs->SpecsDirty = false;
                        }
                    }

                    char buf[32];
                    int removeIndex = -1; // Set when the player chooses "Delete Run" in the context menu
                    int hoveredRow  = -1; // Set to the row index the cursor is over; -1 = none

                    for (int n = 0; n < (int)order.size(); n++)
                    {
                        int i = order[n];
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
                        ImGui::Text("%d", n + 1);

                        ImGui::TableSetColumnIndex(1);
                        ImGui::Text("%s", run.Date.c_str());
                        if (RunIsTainted(run))
                        {
                            ImGui::SameLine();
                            ImGui::TextColored(
                                ImVec4(ColorBehind[0], ColorBehind[1], ColorBehind[2], 1.0f),
                                "[!]");
                        }

                        ImGui::TableSetColumnIndex(2);
                        FormatTime(buf, sizeof(buf), run.TotalTime);
                        ImGui::TextColored(
                            isFastest
                                ? ImVec4(ColorAhead[0], ColorAhead[1], ColorAhead[2], 1.0f)
                                : ImGui::GetStyle().Colors[ImGuiCol_Text],
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
                                    SaveHistory(CurrentHistoryPath, HistoryRuns, SegmentRecords, BestRunIndex, MaxHistoryRuns);
                            }

                            // ---------------------------------------------------------
                            // Copy to clipboard — tab-separated splits in tooltip order,
                            // using H:MM:SS.mmm format so spreadsheets parse it as time.
                            // ---------------------------------------------------------
                            if (ImGui::MenuItem("Copy to clipboard"))
                            {
                                // Suppress the AllCheckpoints synthetic Goal split,
                                // same logic as the hover tooltip. Looked up by type
                                // (not GetGoal()) since a route can have multiple goals.
                                const CheckpointState* goalCp = GetGoalOfType(CurrentRoute, ETriggerType::AllCheckpoints);
                                bool goalIsAllCheckpoints = goalCp != nullptr;
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
                    // LiveSplit mode falls back to Segment display in the tooltip.
                    // The final "Goal" split added by the AllCheckpoints goal type is
                    // hidden because it carries no meaningful time of its own.
                    //
                    // When a best run is set, a "Diff" column shows how each split in
                    // the hovered run compares to BestRun — same comparison the live
                    // timer already does (see render_timer.cpp), so a historical run's
                    // tooltip and the live splits view always agree on what "ahead" or
                    // "behind" means. Like the live timer, this compares by split index,
                    // not name — if a route's splits were renamed/reordered after
                    // BestRun was captured, the diff can silently compare mismatched
                    // splits. Pre-existing limitation, not introduced by this change.
                    // -----------------------------------------------------------------
                    if (hoveredRow >= 0)
                    {
                        const HistoricalRun& run = HistoryRuns[hoveredRow];
                        bool hasBest = !BestRun.empty();
                        ImGui::BeginTooltip();
                        int tooltipCols = hasBest ? 3 : 2;
                        if (ImGui::BeginTable("tooltip_splits", tooltipCols, ImGuiTableFlags_None))
                        {
                            if (hasBest)
                                ImGui::TableSetupColumn("Diff", ImGuiTableColumnFlags_WidthFixed, 70.0f);
                            ImGui::TableSetupColumn("Time",  ImGuiTableColumnFlags_WidthFixed, 100.0f);
                            ImGui::TableSetupColumn("Split", ImGuiTableColumnFlags_WidthStretch);

                            // Suppress the synthetic "Goal" split that AllCheckpoints
                            // goals append — it's redundant with the Total line below.
                            // Looked up by type (not GetGoal()) since a route can have
                            // multiple goals.
                            const CheckpointState* tooltipGoalCp = GetGoalOfType(CurrentRoute, ETriggerType::AllCheckpoints);
                            bool tooltipGoalIsAllCheckpoints = tooltipGoalCp != nullptr;
                            int splitsToShow = (int)run.Splits.size();
                            if (tooltipGoalIsAllCheckpoints && splitsToShow > 0 && tooltipGoalCp &&
                                strcmp(run.Splits.back().Name, tooltipGoalCp->Name) == 0)
                                splitsToShow--;

                            for (int s = 0; s < splitsToShow; s++)
                            {
                                // Split 0 is the run-start marker (always 0:00:00.000).
                                // Runs recorded before this existed simply don't have
                                // one, so this only ever hides anything on newer runs.
                                // Mirrors the same setting on the live timer.
                                if (s == 0 && !ShowStartSplit) continue;

                                double splitTime = (TimerDisplayMode == TimerMode::Split)
                                    ? run.Splits[s].Timestamp
                                    : (s == 0 ? run.Splits[s].Timestamp
                                        : run.Splits[s].Timestamp - run.Splits[s-1].Timestamp);

                                // Resolve the best-run reference values for this split index —
                                // identical logic to the live timer's splits table.
                                double bestSplitTime = 0.0;
                                if (hasBest && s < (int)BestRun.size())
                                {
                                    bestSplitTime = (TimerDisplayMode == TimerMode::Segment)
                                        ? (s == 0 ? BestRun[s].Timestamp : BestRun[s].Timestamp - BestRun[s-1].Timestamp)
                                        : BestRun[s].Timestamp;
                                }

                                // LiveSplit mode: segment times displayed, but diffs are
                                // cumulative — matches the live timer's behavior.
                                double diffCurrent = (TimerDisplayMode == TimerMode::LiveSplit)
                                    ? run.Splits[s].Timestamp
                                    : splitTime;
                                double diffBest = (TimerDisplayMode == TimerMode::LiveSplit)
                                    ? (hasBest && s < (int)BestRun.size() ? BestRun[s].Timestamp : 0.0)
                                    : bestSplitTime;
                                double diff = (hasBest && s < (int)BestRun.size())
                                    ? diffCurrent - diffBest : 0.0;

                                ImGui::TableNextRow();

                                if (hasBest)
                                {
                                    ImGui::TableSetColumnIndex(0);
                                    if (s < (int)BestRun.size() && std::abs(diff) > 0.0005)
                                    {
                                        char diffBuf[32];
                                        if (FormatDiff(diffBuf, sizeof(diffBuf), diff, true, true))
                                        {
                                            float diffWidth = ImGui::CalcTextSize(diffBuf).x;
                                            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - diffWidth);
                                            ImGui::TextColored(diff < 0
                                                ? ImVec4(ColorAhead[0],  ColorAhead[1],  ColorAhead[2],  1.0f)
                                                : ImVec4(ColorBehind[0], ColorBehind[1], ColorBehind[2], 1.0f), "%s", diffBuf);
                                        }
                                    }
                                }

                                ImGui::TableSetColumnIndex(hasBest ? 1 : 0);
                                FormatTime(buf, sizeof(buf), splitTime);
                                float textWidth = ImGui::CalcTextSize(buf).x;
                                ImGui::SetCursorPosX(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - textWidth);
                                ImGui::Text("%s", buf);
                                ImGui::TableSetColumnIndex(hasBest ? 2 : 1);
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
                            SaveHistory(CurrentHistoryPath, HistoryRuns, SegmentRecords, BestRunIndex, MaxHistoryRuns);
                    }
                }

                // -------------------------------------------------------------------------
                // Average run time — across all runs currently in history (no fixed
                // window; respects whatever the configured history limit has trimmed
                // down to). Gives a rough "how long does a session of this route cost
                // me" sense, distinct from the PB highlighted above. Lives in the strip
                // reserved below the table for future controls.
                // -------------------------------------------------------------------------
                if (!HistoryRuns.empty())
                {
                    double sumTime = 0.0;
                    for (const auto& r : HistoryRuns)
                        sumTime += r.TotalTime;
                    double avgTime = sumTime / (double)HistoryRuns.size();

                    char avgBuf[32];
                    FormatTime(avgBuf, sizeof(avgBuf), avgTime);
                    ImGui::Text("Average: %s across %d runs", avgBuf, (int)HistoryRuns.size());
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
                            SaveHistory(CurrentHistoryPath, HistoryRuns, SegmentRecords, BestRunIndex, MaxHistoryRuns);
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
                    ImGuiTableFlags_ScrollY |
                    ImGuiTableFlags_Sortable,
                    ImVec2(0.0f, -footerReserve)))
                {
                    ImGui::TableSetupColumn("Segment", ImGuiTableColumnFlags_WidthStretch);
                    ImGui::TableSetupColumn("Best",    ImGuiTableColumnFlags_WidthFixed, 80.0f);
                    ImGui::TableSetupColumn("Date",    ImGuiTableColumnFlags_WidthFixed, 140.0f);
                    ImGui::TableHeadersRow();

                    // Display order — SegmentRecords itself is never reordered, since
                    // removeSegIndex below is a real index into it. Same approach as the
                    // Runs table above: sort a separate index array, not the data itself.
                    std::vector<int> segOrder(SegmentRecords.size());
                    for (int n = 0; n < (int)segOrder.size(); n++) segOrder[n] = n;

                    if (ImGuiTableSortSpecs* segSortSpecs = ImGui::TableGetSortSpecs())
                    {
                        if (segSortSpecs->SpecsCount > 0)
                        {
                            const ImGuiTableColumnSortSpecs& spec = segSortSpecs->Specs[0];
                            bool ascending = (spec.SortDirection == ImGuiSortDirection_Ascending);
                            if (spec.ColumnIndex == 0) // Segment name
                            {
                                std::sort(segOrder.begin(), segOrder.end(), [&](int a, int b) {
                                    return ascending ? SegmentRecords[a].name < SegmentRecords[b].name
                                                      : SegmentRecords[a].name > SegmentRecords[b].name;
                                });
                            }
                            else if (spec.ColumnIndex == 1) // Best time
                            {
                                std::sort(segOrder.begin(), segOrder.end(), [&](int a, int b) {
                                    return ascending ? SegmentRecords[a].bestTime < SegmentRecords[b].bestTime
                                                      : SegmentRecords[a].bestTime > SegmentRecords[b].bestTime;
                                });
                            }
                            else if (spec.ColumnIndex == 2) // Date
                            {
                                std::sort(segOrder.begin(), segOrder.end(), [&](int a, int b) {
                                    return ascending ? SegmentRecords[a].bestDate < SegmentRecords[b].bestDate
                                                      : SegmentRecords[a].bestDate > SegmentRecords[b].bestDate;
                                });
                            }
                            segSortSpecs->SpecsDirty = false;
                        }
                    }

                    char buf[32];
                    int removeSegIndex = -1; // Set when "Delete Segment" is chosen
                    int hoveredSegIdx  = -1; // Set to the row index the cursor is over; -1 = none

                    for (int n = 0; n < (int)segOrder.size(); n++)
                    {
                        int i = segOrder[n];
                        const SegmentRecord& seg = SegmentRecords[i];

                        ImGui::TableNextRow();

                        // Row-wide selectable for right-click detection.
                        ImGui::TableSetColumnIndex(0);
                        char segSelectableId[32]; snprintf(segSelectableId, sizeof(segSelectableId), "##segrow_%d", i);
                        ImGui::Selectable(segSelectableId, false,
                            ImGuiSelectableFlags_SpanAllColumns,
                            ImVec2(0.0f, ImGui::GetTextLineHeight()));
                        bool segRightClicked = ImGui::IsItemClicked(ImGuiMouseButton_Right);
                        if (ImGui::IsItemHovered() && hoveredSegIdx == -1) hoveredSegIdx = i;

                        ImGui::SameLine();
                        ImGui::Text("%s", seg.name.c_str());

                        ImGui::TableSetColumnIndex(1);
                        FormatTime(buf, sizeof(buf), seg.bestTime);
                        ImGui::TextColored(ImVec4(ColorAhead[0],  ColorAhead[1],  ColorAhead[2],  1.0f), "%s", buf);

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

                    // Runner-up hover — drawn after the table closes (same reason as
                    // the Runs tab above: doing this inline per-row could show two
                    // tooltips at once near a row boundary).
                    if (hoveredSegIdx >= 0)
                    {
                        const SegmentRecord& hoveredSeg = SegmentRecords[hoveredSegIdx];
                        if (!hoveredSeg.secondDate.empty())
                        {
                            ImGui::BeginTooltip();
                            char rankBuf[32];
                            FormatTime(rankBuf, sizeof(rankBuf), hoveredSeg.secondTime);
                            ImGui::Text("2nd: %s (%s)", rankBuf, hoveredSeg.secondDate.c_str());
                            if (!hoveredSeg.thirdDate.empty())
                            {
                                FormatTime(rankBuf, sizeof(rankBuf), hoveredSeg.thirdTime);
                                ImGui::Text("3rd: %s (%s)", rankBuf, hoveredSeg.thirdDate.c_str());
                            }
                            ImGui::EndTooltip();
                        }
                    }

                    // Deferred segment deletion — safe to do after the table loop ends.
                    if (removeSegIndex >= 0)
                    {
                        SegmentRecords.erase(SegmentRecords.begin() + removeSegIndex);
                        if (!CurrentHistoryPath.empty())
                            SaveHistory(CurrentHistoryPath, HistoryRuns, SegmentRecords, BestRunIndex, MaxHistoryRuns);
                    }
                }
            }
            ImGui::EndTabItem();
        } // end BeginTabItem("Segments")

    ImGui::EndTabBar();
    } // end BeginTabBar

    // =========================================================================
    // History Maintenance — one-off migration tools for runs recorded before
    // start checkpoints were properly registered as splits. Both back up the
    // .history file first and only touch runs that actually need it.
    // =========================================================================
    if (showMaintenance)
    {
        ImGui::Separator();
        ImGui::TextDisabled("History Maintenance");

        // -------------------------------------------------------------------
        // Normalize Times — for runs where the first split's timestamp isn't
        // 0.000 (e.g. a real gap before the first recorded checkpoint, or a
        // manual workaround entry that isn't at exactly zero), subtract that
        // offset from every split (and the run's totals) so the run is
        // re-based to start at zero. Doesn't touch or remove any entry.
        // Opens RenderHistoryMaintenanceWindow() for explanation + preview.
        // -------------------------------------------------------------------
        if (ImGui::Button("Normalize Times"))
            s_PendingAction = MaintenanceAction::Normalize;
        Tooltip("For any run whose first split isn't at 0:00:00.000, subtracts that\n"
                "split's time from every split in the run (and from the run's total/\n"
                "grand total) so the run is re-based to start at zero. Nothing is\n"
                "deleted -- the first split just becomes 0:00:00.000 like the rest.\n"
                "Shows a preview before changing anything.");

        ImGui::SameLine();

        // -------------------------------------------------------------------
        // Add Start Checkpoint — for runs that never recorded any entry near
        // 0.000 at all (recorded before start splits existed, no workaround
        // in place), inserts this route's designated start checkpoint as a
        // new split at 0:00:00.000. Doesn't touch any existing split's time.
        // -------------------------------------------------------------------
        CheckpointState* startCp = GetStart(CurrentRoute);

        if (!startCp)
        {
            ImGui::PushItemFlag(ImGuiItemFlags_Disabled, true); ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * 0.5f);
            ImGui::Button("Add Start Checkpoint");
            ImGui::PopItemFlag(); ImGui::PopStyleVar();
            Tooltip("This route has no checkpoint flagged as Start, so there's nothing to insert.");
        }
        else
        {
            if (ImGui::Button("Add Start Checkpoint"))
                s_PendingAction = MaintenanceAction::AddStart;

            char tipBuf[256];
            snprintf(tipBuf, sizeof(tipBuf),
                "For any run that has no split at 0:00:00.000, inserts a new split\n"
                "named \"%s\" (this route's start checkpoint) at 0:00:00.000.\n"
                "No existing split times change -- only the missing entry at the\n"
                "very beginning is added. Shows a preview before changing anything.",
                startCp->Name);
            Tooltip(tipBuf);
        }

        // -------------------------------------------------------------------
        // Result message for whichever tool above last ran. Shown inline
        // (not as a popup) once the preview window's Confirm button applies
        // the change.
        // -------------------------------------------------------------------
        if (s_ShowMaintenanceMsg)
        {
            ImGui::Spacing();
            ImGui::TextWrapped("%s", s_MaintenanceMsg.c_str());
            if (ImGui::SmallButton("Dismiss"))
                s_ShowMaintenanceMsg = false;
        }
    }

    ImGui::End();
}

// -----------------------------------------------------------------------------
// RenderSplitComparison
// -----------------------------------------------------------------------------
// Draws a two-column Before/After split-by-split table for one run under a
// pending maintenance action.
//   Normalize: every split's name is unchanged; every time shifts by -offset.
//   AddStart:  every existing split is unchanged; a new row is inserted at
//              the top ("(missing)" on the Before side, the new split on
//              the After side).
// -----------------------------------------------------------------------------
static void RenderSplitComparison(const HistoricalRun& run, MaintenanceAction action,
                                   double offset, const char* startName)
{
    int rowCount = (int)run.Splits.size() + (action == MaintenanceAction::AddStart ? 1 : 0);

    if (!ImGui::BeginTable("##splitcompare", 2,
        ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY,
        ImVec2(0.0f, 220.0f)))
        return;

    ImGui::TableSetupColumn("Before");
    ImGui::TableSetupColumn("After");
    ImGui::TableHeadersRow();

    char buf[32];
    for (int i = 0; i < rowCount; i++)
    {
        ImGui::TableNextRow();

        if (action == MaintenanceAction::AddStart)
        {
            // Row 0 is the inserted split; every row after mirrors run.Splits[i-1] unchanged.
            ImGui::TableSetColumnIndex(0);
            if (i == 0)
                ImGui::TextDisabled("(missing)");
            else
            {
                const Split& s = run.Splits[i - 1];
                FormatTime(buf, sizeof(buf), s.Timestamp, false);
                ImGui::Text("%s   %s", buf, s.Name);
            }

            ImGui::TableSetColumnIndex(1);
            if (i == 0)
            {
                FormatTime(buf, sizeof(buf), 0.0, false);
                ImGui::TextColored(ImVec4(ColorAhead[0], ColorAhead[1], ColorAhead[2], 1.0f),
                                    "%s   %s", buf, startName);
            }
            else
            {
                const Split& s = run.Splits[i - 1];
                FormatTime(buf, sizeof(buf), s.Timestamp, false);
                ImGui::Text("%s   %s", buf, s.Name);
            }
        }
        else // Normalize — every row shifts by -offset, name unchanged
        {
            const Split& s = run.Splits[i];

            ImGui::TableSetColumnIndex(0);
            FormatTime(buf, sizeof(buf), s.Timestamp, false);
            ImGui::Text("%s   %s", buf, s.Name);

            ImGui::TableSetColumnIndex(1);
            double after = std::max(0.0, s.Timestamp - offset);
            FormatTime(buf, sizeof(buf), after, false);
            ImGui::TextColored(ImVec4(ColorAhead[0], ColorAhead[1], ColorAhead[2], 1.0f),
                                "%s   %s", buf, s.Name);
        }
    }

    ImGui::EndTable();
}

// -----------------------------------------------------------------------------
// RenderHistoryMaintenanceWindow
// -----------------------------------------------------------------------------
// Standalone window opened by the "Normalize Times" / "Add Start Checkpoint"
// buttons in RenderHistoryWindow(). Explains what the action does, lists every
// run it would actually change with a before/after total-time summary, lets
// the user click through runs for a full split-by-split preview, and only
// touches the .history file once Confirm is pressed (backing it up first).
// -----------------------------------------------------------------------------
void RenderHistoryMaintenanceWindow()
{
    if (s_PendingAction == MaintenanceAction::None) return;

    CheckpointState* startCp = (s_PendingAction == MaintenanceAction::AddStart)
        ? GetStart(CurrentRoute) : nullptr;

    const char* title = (s_PendingAction == MaintenanceAction::Normalize)
        ? "Split Wars 2 - Normalize Times"
        : "Split Wars 2 - Add Start Checkpoint";

    bool open = true;
    ImGui::SetNextWindowSize(ImVec2(640, 520), ImGuiCond_FirstUseEver);
    ImGui::Begin(title, &open);

    // Bail out cleanly if the window was closed, the route/history changed
    // out from under us, or (AddStart only) the route no longer has a start
    // checkpoint to insert.
    if (!open || CurrentHistoryPath.empty() ||
        (s_PendingAction == MaintenanceAction::AddStart && !startCp))
    {
        if (s_PendingAction == MaintenanceAction::AddStart && !startCp && open)
            ImGui::TextWrapped("This route no longer has a checkpoint flagged as Start.");
        s_PendingAction = MaintenanceAction::None;
        ImGui::End();
        return;
    }

    // --- Explanation ---
    if (s_PendingAction == MaintenanceAction::Normalize)
    {
        ImGui::TextWrapped(
            "For every run below whose first split isn't at 0:00:00.000, this "
            "subtracts that split's time from every split in the run (and from "
            "the run's total/grand total), so the run is re-based to start at "
            "zero. Nothing is deleted -- the first split becomes 0:00:00.000 "
            "like the rest. A backup (.history.bak) is written before anything "
            "changes.");
    }
    else
    {
        ImGui::TextWrapped(
            "For every run below that has no split at 0:00:00.000, this inserts "
            "a new split named \"%s\" (this route's start checkpoint) at "
            "0:00:00.000. No existing split times change -- only the missing "
            "entry at the very beginning is added. A backup (.history.bak) is "
            "written before anything changes.",
            startCp->Name);
    }

    ImGui::Separator();

    std::vector<AffectedRun> affected = ComputeAffectedRuns(s_PendingAction);

    if (affected.empty())
    {
        ImGui::TextDisabled("Nothing to do -- every run already looks correct.");
        ImGui::Spacing();
        if (ImGui::Button("Close"))
            s_PendingAction = MaintenanceAction::None;
        ImGui::End();
        return;
    }

    ImGui::Text("%d of %d run(s) will be changed. Example:",
                (int)affected.size(), (int)HistoryRuns.size());

    ImGui::Separator();

    const AffectedRun&   preview    = affected[0];
    const HistoricalRun& previewRun = HistoryRuns[preview.runIndex];
    ImGui::Text("Split-by-split preview: %s", previewRun.Date.c_str());
    RenderSplitComparison(previewRun, s_PendingAction, preview.offset,
                          startCp ? startCp->Name : "");

    ImGui::Separator();

    if (ImGui::Button("Confirm"))
    {
        ApplyMaintenanceAction(s_PendingAction, startCp);
        s_PendingAction = MaintenanceAction::None;
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel"))
        s_PendingAction = MaintenanceAction::None;

    ImGui::End();
}