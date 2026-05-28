// renderer_timer.cpp
// Implements the main "Speedrun Timer" overlay window — the split table the
// player watches during a run.
//
// The window has two modes:
//
//   Compact mode — a single coloured time value, no table.  Useful when the
//                  player wants minimal screen real estate.
//
//   Full mode    — a scrollable table with up to four kinds of rows:
//     1. Completed split rows  — one per split already recorded this run.
//     2. Current segment row   — the live/in-progress segment at the bottom
//                                of the split list (shown while running or
//                                when the run finished without a goal split).
//     3. Total row             — the cumulative run time; hovering shows the
//                                grand total (with load screens) as a tooltip.
//     4. Grand Total row       — load-inclusive time, shown when enabled in
//                                settings.
//
// Each time cell is coloured green (ahead/equal) or red (behind) relative to
// the active best run.  The Diff column shows the signed delta.  Both use the
// three display modes: Split (cumulative), Segment (per-leg), or LiveSplit
// (segment times, cumulative diffs — matching LiveSplit software behaviour).
//
// After a run finishes, "Save as best" and "Reset Timer" buttons are shown.

#include "renderer_shared.h"

void RenderTimerOverlay()
{
    if (!ShowTimer) return;

    ImGui::SetNextWindowPos(ImVec2(10, 10), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowBgAlpha(0.6f); // Semi-transparent so the game world shows through
    ImGui::Begin("Speedrun Timer", nullptr,
        ImGuiWindowFlags_NoDecoration      |  // No title bar or borders
        ImGuiWindowFlags_AlwaysAutoResize  |  // Shrink/grow to fit content
        ImGuiWindowFlags_NoFocusOnAppearing|  // Don't steal keyboard focus on show
        ImGuiWindowFlags_NoNav               // Not keyboard-navigable
    );

    // Snapshot all the state we'll need so we're not calling getters repeatedly.
    const auto& splits    = SpeedrunTimer.GetSplits();
    double      elapsed   = SpeedrunTimer.GetElapsedSeconds();
    double      grand     = DisplayedGrandTotal;  // Load-inclusive time (updated in AddonRender)
    bool        running   = SpeedrunTimer.IsRunning();
    bool        finished  = SpeedrunTimer.IsFinished();
    bool        hasBest   = !BestRun.empty();
    int         numSplits = (int)splits.size();
    char        buf[32];
    char        diffBuf[32];

    // -------------------------------------------------------------------------
    // Compact mode — single line, no split table
    // Shows milliseconds only while the run is in progress (too noisy when idle).
    // Colour: green/red vs best total time while running/finished; grey when idle.
    // -------------------------------------------------------------------------
    if (CompactMode)
    {
        FormatTime(buf, sizeof(buf), elapsed, !running); // showMillis = false when not running
        ImVec4 color = (running || finished)
            ? TimeColor(elapsed, hasBest ? BestRun.back().Timestamp : 0.0, running)
            : ImVec4(0.5f, 0.5f, 0.5f, 1.0f); // Grey when idle
        ImGui::TextColored(color, "%s", buf);
    }
    else
    {
        // -------------------------------------------------------------------------
        // Full mode — split table
        // The Diff column is only added when a best run exists to compare against.
        // Column order: [Diff] | Time | Name
        // -------------------------------------------------------------------------
        int numCols = hasBest ? 3 : 2;
        if (ImGui::BeginTable("splits", numCols, ImGuiTableFlags_None))
        {
            if (hasBest)
                ImGui::TableSetupColumn("Diff", ImGuiTableColumnFlags_WidthFixed, 70.0f);
            ImGui::TableSetupColumn("Time", ImGuiTableColumnFlags_WidthFixed, 100.0f);
            ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch);

            // --- Completed split rows ---
            for (int i = 0; i < numSplits; i++)
            {
                // The time shown per split depends on the display mode:
                //   Split   → cumulative time from run start
                //   Segment / LiveSplit → time for this segment only (delta from previous)
                double splitTime = (TimerDisplayMode == TimerMode::Split)
                    ? splits[i].Timestamp
                    : (i == 0 ? splits[i].Timestamp : splits[i].Timestamp - splits[i-1].Timestamp);

                // Resolve the best-run reference values for this split index.
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

                // LiveSplit mode: segment times displayed, but diffs are cumulative
                // (matching how LiveSplit software shows it — you see how far ahead/
                // behind you are in total, not just for this individual segment).
                double diffCurrent = (TimerDisplayMode == TimerMode::LiveSplit)
                    ? splits[i].Timestamp
                    : splitTime;
                double diffBest = (TimerDisplayMode == TimerMode::LiveSplit)
                    ? (hasBest && i < (int)BestRun.size() ? BestRun[i].Timestamp : 0.0)
                    : bestSplitTime;
                double diff = (hasBest && i < (int)BestRun.size())
                    ? diffCurrent - diffBest : 0.0;

                ImGui::TableNextRow();

                // Diff cell — only drawn when a best run exists and the delta is
                // large enough to be meaningful (> 0.5 ms to avoid floating-point noise).
                if (hasBest)
                {
                    ImGui::TableSetColumnIndex(0);
                    if (i < (int)BestRun.size() && std::abs(diff) > 0.0005)
                    {
                        // isSplit = true: always show full precision for completed splits
                        if (FormatDiff(diffBuf, sizeof(diffBuf), diff, true))
                            ImGui::TextColored(diff < 0
                                ? ImVec4(0.2f, 1.0f, 0.2f, 1.0f)  // Green = ahead
                                : ImVec4(1.0f, 0.3f, 0.3f, 1.0f), // Red   = behind
                                "%s", diffBuf);
                    }
                }

                // Time cell
                ImGui::TableSetColumnIndex(hasBest ? 1 : 0);
                FormatTime(buf, sizeof(buf), splitTime);
                ImGui::TextColored(TimeColor(diffCurrent, diffBest, false), "%s", buf);

                // Name cell — dimmed so it doesn't compete visually with the time
                ImGui::TableSetColumnIndex(hasBest ? 2 : 1);
                ImGui::TextDisabled("%s", splits[i].Name);
            }

            const Checkpoint* goalCp = GetGoal(CurrentRoute);
            bool goalIsAllCheckpoints = goalCp &&
                goalCp->Point.TriggerType == ETriggerType::AllCheckpoints;

            // Detect a manually stopped run — in that case "Manual Stop" is the
            // final split and we don't need a separate current-segment row.
            bool manualStop = finished && numSplits > 0 &&
                              strcmp(splits[numSplits - 1].Name, "Manual Stop") == 0;

            // -------------------------------------------------------------------------
            // Current segment row (live or finished-but-no-goal-split)
            // Shown while the timer is running, and also when the run has finished
            // without an AllCheckpoints goal (which generates its own final split)
            // and wasn't manually stopped.
            // -------------------------------------------------------------------------
            if (running || (finished && !goalIsAllCheckpoints && !manualStop))
            {
                // Segment time = elapsed since the last recorded split (or from 0 if
                // no splits yet). In Split mode we just show the total elapsed time.
                double segmentStart = numSplits > 0 ? splits[numSplits-1].Timestamp : 0.0;
                double segmentTime = (TimerDisplayMode == TimerMode::Split)
                    ? elapsed
                    : (elapsed - segmentStart);

                // Look up the corresponding best-run segment for comparison.
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

                // LiveSplit mode: diff uses cumulative elapsed time, not segment time.
                double diffCurSeg  = (TimerDisplayMode == TimerMode::LiveSplit) ? elapsed        : segmentTime;
                double diffBestSeg = (TimerDisplayMode == TimerMode::LiveSplit) ? bestSegmentTime : bestSegmentTime;
                double diff = hasDiff ? diffCurSeg - diffBestSeg : 0.0;

                ImGui::TableNextRow();

                // Diff cell for the live segment — uses live comparison mode
                // (FormatDiff with isSplit = false) which hides the diff when
                // far enough ahead to keep the display clean mid-run.
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

                // Time cell — always shows milliseconds for the live segment
                ImGui::TableSetColumnIndex(hasBest ? 1 : 0);
                FormatTime(buf, sizeof(buf), segmentTime, true);
                ImGui::TextColored(TimeColor(diffCurSeg, diffBestSeg, running), "%s", buf);

                // Name cell — blank while running; "Goal" label when the run is done
                ImGui::TableSetColumnIndex(hasBest ? 2 : 1);
                if (finished)
                    ImGui::TextDisabled("Goal");
            }

            // -------------------------------------------------------------------------
            // Total row — cumulative run time
            // Only shown once at least one split has been recorded.
            // Hovering the time shows the grand total (load-inclusive) as a tooltip.
            // -------------------------------------------------------------------------
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
                // Show milliseconds only while running; whole seconds once finished
                FormatTime(buf, sizeof(buf), elapsed, !running);
                double bestTotal = hasBest ? BestRun.back().Timestamp : 0.0;
                ImGui::TextColored(TimeColor(elapsed, bestTotal, running), "%s", buf);

                // Grand total tooltip on hover — only shown when a grand total exists
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

            // -------------------------------------------------------------------------
            // Grand Total row — load-inclusive time, only when enabled in settings
            // Shown as a dimmed row beneath Total so it doesn't distract mid-run.
            // -------------------------------------------------------------------------
            if (ShowGrandTotal && (running || finished) && grand > 0.0)
            {
                ImGui::TableNextRow();

                if (hasBest)
                    ImGui::TableSetColumnIndex(0); // Leave diff cell empty

                ImGui::TableSetColumnIndex(hasBest ? 1 : 0);
                FormatTime(buf, sizeof(buf), grand);
                ImGui::TextDisabled("%s", buf); // Dimmed — secondary info

                ImGui::TableSetColumnIndex(hasBest ? 2 : 1);
                ImGui::TextDisabled("Grand Total");
            }

            ImGui::EndTable();
        }

        // Idle placeholder — shown when the timer has never started and no route
        // is active yet, so the window isn't just a blank floating box.
        if (!running && !finished)
        {
            ImGui::TextDisabled("00:00:00.000");
            if (!CurrentRoute.IsValid)
                ImGui::TextDisabled("No route set");
        }

        // -------------------------------------------------------------------------
        // Post-run panel — shown after a run finishes via a goal trigger.
        // "Save as best" promotes this run's splits to BestRun and persists it.
        // "Reset Timer"  clears all state ready for the next attempt.
        // -------------------------------------------------------------------------
        if (finished && RunFinished)
        {
            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();

            ImGui::Text("Run finished.");
            ImGui::Spacing();

            if (ImGui::Button("Save as best"))
            {
                BestRun      = splits;
                BestRunIndex = 0; // Newest run is always inserted at index 0
                if (!CurrentHistoryPath.empty())
                    SaveHistory(CurrentHistoryPath, BestRun, HistoryRuns, BestRunIndex);
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
