// addon_options.cpp
// Implements the Split Wars 2 section inside the Nexus options panel.
//
// This is a Nexus UI callback — it draws into a panel that Nexus owns,
// not a standalone window. All widgets write directly into the global
// variables declared in shared.h. Settings are persisted to disk via
// SaveCurrentSettings() when the user clicks "Save Settings".

#include "render_shared.h"

// ---------------------------------------------------------------------------
// AddonOptions
// ---------------------------------------------------------------------------
// Draws the Split Wars 2 section inside the Nexus options panel.
// All the standard ImGui widgets write directly into the global booleans and
// enums; the "Save Settings" button at the bottom persists them to disk.
// ---------------------------------------------------------------------------
void AddonOptions()
{
    
    //Save Settings
    if (ImGui::Button("Save Settings"))
        SaveCurrentSettings();
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    //Timer related UI
    ImGui::Text("Timer:");
    ImGui::Checkbox("Show Timer",         &ShowTimer);
    Tooltip("Toggles the speedrun timer overlay.");
    ImGui::SameLine();
    // Cycle button — label reflects the current mode so the player always
    // knows what clicking it will do.
    const char* timerModeLabel = (TimerDisplayMode == TimerMode::Segment)  ? "Mode: Segment"
                               : (TimerDisplayMode == TimerMode::LiveSplit) ? "Mode: LiveSplit"
                               :                                               "Mode: Split";
    if (ImGui::Button(timerModeLabel))
        TimerDisplayMode = (TimerMode)(((int)TimerDisplayMode + 1) % 3);
    Tooltip("Controls how split times and differences are displayed.\n\n"
            "Segment   - Each row shows the time for that segment only.\n"
            "            Diffs compare against your best time for that segment.\n\n"
            "Split     - Each row shows the elapsed time since the run started.\n"
            "            Diffs show how far ahead or behind you are overall.\n\n"
            "LiveSplit - Each row shows the time for that segment only.\n"
            "            Diffs still show your overall lead or deficit,\n"
            "            matching the behaviour of LiveSplit.");
    ImGui::Checkbox("Show Grand Total",   &ShowGrandTotal);
    Tooltip("Adds an additional timer to the split timer.\nThis will show the time including the load screens.");
    ImGui::Checkbox("Compact Mode",       &CompactMode);
    Tooltip("Reduces the timer to one line.");
    ImGui::Checkbox("Streamer Mode", &StreamerMode);
    Tooltip("Uses a larger font (FontBig) for better stream visibility. Overrides Timer Scale.");
    ImGui::SliderFloat("Timer Scale", &TimerFontScale, 0.5f, 4.0f, "%.1fx");
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    //Route related UI
    ImGui::Text("Windows:");
    ImGui::Checkbox("Show Route Config", &ShowConfig);
    Tooltip("Toggles the route configuration window.");
    ImGui::Checkbox("Show Route Browser", &ShowRouteBrowser);
    Tooltip("Toggles the route file browser.");
    ImGui::Checkbox("Show History",       &ShowHistory);
    Tooltip("Toggles the history window.");
    ImGui::SameLine();
    ImGui::Dummy(ImVec2(5.0f, ImGui::GetFrameHeight()));
    ImGui::SameLine();
    // Max History Runs — clamped to [1, 100].
    ImGui::Text("Max");
    Tooltip("Set an amount between 1 and 100.");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(80.0f);
    ImGui::DragInt("##maxruns", &MaxHistoryRuns, 1.0f, 1, 100);
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    //Checkpoint/Zone related UI
    ImGui::Text("Checkpoints:");
    ImGui::Checkbox("Show Checkpoints",   &ShowZones);
    Tooltip("Toggles the visibility of checkpoints.");
    ImGui::Text("Distance Fade:");
    if (!ShowZones)
    {
        ImGui::PushItemFlag(ImGuiItemFlags_Disabled, true);
        ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * 0.5f);
    }
    float prevStart = ZoneFadeStart;
    float prevEnd   = ZoneFadeEnd;
    ImGui::SetNextItemWidth(80.0f);
    ImGui::DragFloat("##fadestart", &ZoneFadeStart, 1.0f, 0.0f, 0.0f, "%.0fm");
    Tooltip("Distance at which zones start fading out (metres)");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(80.0f);
    ImGui::DragFloat("##fadeend", &ZoneFadeEnd, 1.0f, 0.0f, 0.0f, "%.0fm");
    Tooltip("Distance at which zones are fully hidden (metres)");
    if (!ShowZones)
    {
        ImGui::PopItemFlag();
        ImGui::PopStyleVar();
    }
    // absolute bounds
    ZoneFadeStart = std::clamp(ZoneFadeStart, 1.0f, 1000.0f);
    ZoneFadeEnd   = std::clamp(ZoneFadeEnd,   1.0f, 1000.0f);
    
    // relationship
    if (ZoneFadeStart != prevStart && ZoneFadeStart >= ZoneFadeEnd)
        ZoneFadeEnd = ZoneFadeStart + 1.0f;
    if (ZoneFadeEnd != prevEnd && ZoneFadeEnd <= ZoneFadeStart)
        ZoneFadeStart = ZoneFadeEnd - 1.0f;
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();
    
    // Data source selector — lets the user choose between RTAPI and Mumble.
    ImGui::Text("Data Source:");
    const char* sourceLabel = (PreferredSource == EDataSource::RTAPI)   ? "RTAPI"
                            : (PreferredSource == EDataSource::Mumble)  ? "Mumble"
                            :                                              "Default";
    ImGui::SetNextItemWidth(80.0f);
    if (ImGui::BeginCombo("##datasource", sourceLabel))
    {
        if (ImGui::Selectable("Default", PreferredSource == EDataSource::Default))
            PreferredSource = EDataSource::Default;
        Tooltip("Use RTAPI if available, otherwise Mumble.");
        if (ImGui::Selectable("Mumble",  PreferredSource == EDataSource::Mumble))
            PreferredSource = EDataSource::Mumble;
        Tooltip("Always use Mumble, even if RTAPI is available.");
        if (ImGui::Selectable("RTAPI",   PreferredSource == EDataSource::RTAPI))
            PreferredSource = EDataSource::RTAPI;
        Tooltip("Always use RTAPI. Falls back to Mumble if RTAPI is unavailable.");
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    ImGui::TextDisabled(GS.RTAPIAvailable ? "(RTAPI connected)" : "(RTAPI not available)");
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // Colors
    ImGui::Text("Zone Colors:");
    ImGui::SetNextItemWidth(200.0f);
    ImGui::ColorEdit3("Start",       ColorStart,      ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_PickerHueWheel);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(200.0f);
    ImGui::ColorEdit3("Goal",        ColorGoal,       ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_PickerHueWheel);
    ImGui::SameLine();
    ImGui::ColorEdit3("Checkpoint",  ColorCheckpoint, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_PickerHueWheel);
    ImGui::SameLine();
    ImGui::ColorEdit3("Null",     ColorNull,    ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_PickerHueWheel);
    ImGui::Text("Time Colors:");
    ImGui::ColorEdit3("Ahead",    ColorAhead,   ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_PickerHueWheel);
    ImGui::SameLine();
    ImGui::ColorEdit3("Behind",   ColorBehind,  ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_PickerHueWheel);
    ImGui::SameLine();
    ImGui::ColorEdit3("Best Row", ColorBestRow, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_PickerHueWheel);
    ImGui::SameLine();
    if (ImGui::Button("Reset Colors"))
    {
        float defStart[3]      = { 0.2f, 1.0f, 0.2f };
        float defGoal[3]       = { 0.2f, 0.5f, 1.0f };
        float defCheckpoint[3] = { 1.0f, 1.0f, 1.0f };
        float defNull[3]       = { 1.0f, 0.6f, 0.0f };
        float defAhead[3]      = { 0.2f, 1.0f, 0.2f };
        float defBehind[3]     = { 1.0f, 0.3f, 0.3f };
        float defBestRow[3]    = { 0.2f, 0.3f, 0.2f };
        std::copy(defStart,      defStart      + 3, ColorStart);
        std::copy(defGoal,       defGoal       + 3, ColorGoal);
        std::copy(defCheckpoint, defCheckpoint + 3, ColorCheckpoint);
        std::copy(defNull,       defNull       + 3, ColorNull);
        std::copy(defAhead,      defAhead      + 3, ColorAhead);
        std::copy(defBehind,     defBehind     + 3, ColorBehind);
        std::copy(defBestRow,    defBestRow    + 3, ColorBestRow);
    }
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // Window sizing
    ImGui::Text("Window Sizes:");
    ImGui::TextDisabled("Tip: You can also resize any window by dragging its edges or bottom-right corner.");

    ImGui::Text("Config  ");  ImGui::SameLine();
    ImGui::SetNextItemWidth(65.0f);
    if (ImGui::InputFloat("W##cw", &ConfigWindowW, 0, 0, "%.0f"))
    {
        ConfigWindowW = std::clamp(ConfigWindowW, 200.0f, 3000.0f);
        ImGui::SetWindowSize("Split Wars 2 - Route Config", ImVec2(ConfigWindowW, ConfigWindowH));
    }
    ImGui::SameLine();
    ImGui::SetNextItemWidth(65.0f);
    if (ImGui::InputFloat("H##ch", &ConfigWindowH, 0, 0, "%.0f"))
    {
        ConfigWindowH = std::clamp(ConfigWindowH, 150.0f, 3000.0f);
        ImGui::SetWindowSize("Split Wars 2 - Route Config", ImVec2(ConfigWindowW, ConfigWindowH));
    }

    ImGui::Text("History ");  ImGui::SameLine();
    ImGui::SetNextItemWidth(65.0f);
    if (ImGui::InputFloat("W##hw", &HistoryWindowW, 0, 0, "%.0f"))
    {
        HistoryWindowW = std::clamp(HistoryWindowW, 200.0f, 3000.0f);
        ImGui::SetWindowSize("Split Wars 2 - Run History", ImVec2(HistoryWindowW, HistoryWindowH));
    }
    ImGui::SameLine();
    ImGui::SetNextItemWidth(65.0f);
    if (ImGui::InputFloat("H##hh", &HistoryWindowH, 0, 0, "%.0f"))
    {
        HistoryWindowH = std::clamp(HistoryWindowH, 150.0f, 3000.0f);
        ImGui::SetWindowSize("Split Wars 2 - Run History", ImVec2(HistoryWindowW, HistoryWindowH));
    }

    ImGui::Text("Browser ");  ImGui::SameLine();
    ImGui::SetNextItemWidth(65.0f);
    if (ImGui::InputFloat("W##bw", &BrowserWindowW, 0, 0, "%.0f"))
    {
        BrowserWindowW = std::clamp(BrowserWindowW, 200.0f, 3000.0f);
        ImGui::SetWindowSize("Split Wars 2 - Route Browser", ImVec2(BrowserWindowW, BrowserWindowH));
    }
    ImGui::SameLine();
    ImGui::SetNextItemWidth(65.0f);
    if (ImGui::InputFloat("H##bh", &BrowserWindowH, 0, 0, "%.0f"))
    {
        BrowserWindowH = std::clamp(BrowserWindowH, 150.0f, 3000.0f);
        ImGui::SetWindowSize("Split Wars 2 - Route Browser", ImVec2(BrowserWindowW, BrowserWindowH));
    }
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // Debug
    ImGui::Text("Debugging:");
    ImGui::Checkbox("Show Debug Window", &ShowDebug);
    Tooltip("Toggles debugging text which is not fully implemented");
}