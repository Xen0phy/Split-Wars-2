// addon_options.cpp
// Implements the Split Wars 2 section inside the Nexus options panel.
//
// This is a Nexus UI callback — it draws into a panel that Nexus owns,
// not a standalone window. All widgets write directly into the global
// variables declared in shared.h. Settings are persisted to disk via
// Settings are persisted to settings.ini via SaveCurrentSettings().

#include "imgui.h"
#include "render_shared.h"
#include "shared.h"
#include "stream_fonts.h"

// ---------------------------------------------------------------------------
// AddonOptions
// ---------------------------------------------------------------------------
// Draws the Split Wars 2 section inside the Nexus options panel.
// All the standard ImGui widgets write directly into the global booleans and
// enums; the "Save Settings" button at the bottom persists them to disk.
// ---------------------------------------------------------------------------
void AddonOptions()
{
    
    // ---------------------------------------------------------------------------
    // Save Settings
    // ---------------------------------------------------------------------------
    if (ImGui::Button("Save Settings"))
        SaveCurrentSettings();
    ImGui::SameLine();
    ImGui::Dummy(ImVec2(ImGui::GetFrameHeight(), ImGui::GetFrameHeight()));
    ImGui::SameLine();
        
    // Data source selector — lets the user choose between RTAPI and Mumble.
    ImGui::Text("Data Source:");
    const char* sourceLabel = (PreferredSource == EDataSource::RTAPI)   ? "RTAPI"
                            : (PreferredSource == EDataSource::Mumble)  ? "Mumble"
                            :                                              "Default";
    ImGui::SameLine();
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

    // ---------------------------------------------------------------------------
    // Data Source
    // ---------------------------------------------------------------------------
    ImGui::TextDisabled(GS.RTAPIAvailable ? "(RTAPI connected)" : "(RTAPI not available)");
    ImGui::SameLine();
    ImGui::Dummy(ImVec2(ImGui::GetFrameHeight(), ImGui::GetFrameHeight()));
    ImGui::SameLine();

    // ---------------------------------------------------------------------------
    // Debug
    // ---------------------------------------------------------------------------
    ImGui::Checkbox("Show Debug Window", &ShowDebug);
    Tooltip("Shows the debug information window.");
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // ---------------------------------------------------------------------------
    // Timer Settings
    // ---------------------------------------------------------------------------
    if (ImGui::CollapsingHeader("Timer Settings"))
    {
        bool timerDisabled = !ShowTimer;
        if (timerDisabled) { ImGui::PushItemFlag(ImGuiItemFlags_Disabled, true); ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * 0.5f); }
    
        if (ImGui::BeginTable("##timersettings", 2, ImGuiTableFlags_None))
        {
            ImGui::TableSetupColumn("##left",  ImGuiTableColumnFlags_WidthFixed, 200);
            ImGui::TableSetupColumn("##right", ImGuiTableColumnFlags_WidthFixed);
    
            // Row 1
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            if (timerDisabled) { ImGui::PopItemFlag(); ImGui::PopStyleVar(); }
            ImGui::Checkbox("Show Timer", &ShowTimer);
            Tooltip("Toggles the speedrun timer overlay.");
            ImGui::SameLine();
            const char* timerModeLabel = (TimerDisplayMode == TimerMode::Segment)  ? "Mode: Segment"
                                       : (TimerDisplayMode == TimerMode::LiveSplit) ? "Mode: LiveSplit"
                                       :                                               "Mode: Split";
            if (timerDisabled) { ImGui::PushItemFlag(ImGuiItemFlags_Disabled, true); ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * 0.5f); }
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
    
            ImGui::TableSetColumnIndex(1);
            ImGui::Text("Time Colors:");
    
            // Row 2
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::Checkbox("Show Grand Total", &ShowGrandTotal);
            Tooltip("Adds an additional timer to the split timer.\nThis will show the time including the load screens.");
    
            ImGui::TableSetColumnIndex(1);
            ImGui::ColorEdit3("Ahead##tc",    ColorAhead,   ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_PickerHueWheel);
    
            // Row 3
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::Checkbox("Compact Mode", &CompactMode);
            Tooltip("Reduces the timer to one line.");
    
            ImGui::TableSetColumnIndex(1);
            ImGui::ColorEdit3("Behind##tc",   ColorBehind,  ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_PickerHueWheel);
    
            // Row 4
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::Checkbox("Show Milliseconds##streamer", &ShowRunningMillis);
            Tooltip("When enabled, the live segment and total rows show milliseconds while the timer is running.\nDisabled by default: milliseconds only appear once the segment is stopped.");
    
            ImGui::TableSetColumnIndex(1);
            if (ImGui::Button("Reset Time Colors##tc"))
            {
                float defAhead[3]   = { 0.2f, 1.0f, 0.2f };
                float defBehind[3]  = { 1.0f, 0.3f, 0.3f };
                std::copy(defAhead,   defAhead   + 3, ColorAhead);
                std::copy(defBehind,  defBehind  + 3, ColorBehind);
                SaveCurrentSettings();
            }
            
            // Row 5 — Fractal Rota
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            if (ImGui::Checkbox("Fractal Rota", &FractalRota))
                SaveCurrentSettings();
            Tooltip("When enabled, sets the comparison run to the run from exactly\n"
                    "15 days ago on route load, matching GW2's fractal daily rotation.\n"
                    "If the history has no run from 15 days ago, no comparison is made.\n"
                    "Requires at least one run older than 15 days in history.\n");
            ImGui::Separator();

            ImGui::TableSetColumnIndex(1);
            ImGui::Dummy(ImVec2(ImGui::GetFrameHeight(), ImGui::GetFrameHeight()));
            ImGui::Separator();
    
            // --- Streamer section ---
            // Row 6
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::Checkbox("Streamer Mode", &StreamerMode);
            Tooltip("Uses a larger font for better stream visibility.");
            
            const auto& fontNames = GetStreamFontNames();
    
            ImGui::TableSetColumnIndex(1);
            if (!StreamerMode) { ImGui::PushItemFlag(ImGuiItemFlags_Disabled, true); ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * 0.5f); }
            {
                if (!fontNames.empty())
                {
                    const char* preview = StreamerFontName.empty() ? fontNames[0].c_str() : StreamerFontName.c_str();
                    ImGui::SetNextItemWidth(200.0f);
                    if (ImGui::BeginCombo("##streamerfont", preview))
                    {
                        for (auto& name : fontNames)
                        {
                            bool selected = (StreamerFontName == name);
                            if (ImGui::Selectable(name.c_str(), selected))
                            {
                                StreamerFontName = name;
                                SaveCurrentSettings();
                            }
                            if (selected) ImGui::SetItemDefaultFocus();
                        }
                        ImGui::EndCombo();
                    }
                    Tooltip("Drop .ttf/.otf files into the Split Wars 2/fonts/ folder and restart.");
                }
                else
                {
                    ImGui::TextDisabled("No fonts found — drop .ttf/.otf into fonts/ and restart.");
                }
            }
    
            // Row 7
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            // empty
    
            ImGui::TableSetColumnIndex(1);
            {
                if (fontNames.empty()) { ImGui::PushItemFlag(ImGuiItemFlags_Disabled, true); ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * 0.5f); }
                static const int fontSizes[] = { 24, 28, 32, 36, 40, 44, 48 };
                char preview[8];
                snprintf(preview, sizeof(preview), "%d", StreamerFontSize);
                ImGui::SetNextItemWidth(80.0f);
                if (ImGui::BeginCombo("Size##streamer", preview))
                {
                    for (int s : fontSizes)
                    {
                        char label[8];
                        snprintf(label, sizeof(label), "%d", s);
                        if (ImGui::Selectable(label, StreamerFontSize == s))
                        {
                            StreamerFontSize = s;
                            SaveCurrentSettings();
                        }
                        if (StreamerFontSize == s) ImGui::SetItemDefaultFocus();
                    }
                    ImGui::EndCombo();
                }
                Tooltip("Pixel size of the main time digits.");

                ImGui::SameLine();

                static const int headerSizes[] = { 16, 20, 24, 28, 32 };
                char headerPreview[8];
                snprintf(headerPreview, sizeof(headerPreview), "%d", StreamerHeaderFontSize);
                ImGui::SetNextItemWidth(80.0f);
                if (ImGui::BeginCombo("Header Size##streamer", headerPreview))
                {
                    for (int s : headerSizes)
                    {
                        char label[8];
                        snprintf(label, sizeof(label), "%d", s);
                        if (ImGui::Selectable(label, StreamerHeaderFontSize == s))
                        {
                            StreamerHeaderFontSize = s;
                            SaveCurrentSettings();
                        }
                        if (StreamerHeaderFontSize == s) ImGui::SetItemDefaultFocus();
                    }
                    ImGui::EndCombo();
                }
                Tooltip("Pixel size of the section title bar labels.");
                if (fontNames.empty()) { ImGui::PopItemFlag(); ImGui::PopStyleVar(); }
            }
    
            if (!StreamerMode) { ImGui::PopItemFlag(); ImGui::PopStyleVar(); }

            //Row 8
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::Separator();
            ImGui::TableSetColumnIndex(1);
            ImGui::Separator();
    
            // --- Crash Mode section ---
            bool crashDisabled = !StreamerMode || !CrashMode  || fontNames.empty();
    
            // Row 9 — checkbox + shadow color
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            // Crash checkbox only needs streamer enabled
            if (!StreamerMode || fontNames.empty()) { ImGui::PushItemFlag(ImGuiItemFlags_Disabled, true); ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * 0.5f); }
            if (fontNames.empty() || !StreamerMode) CrashMode = false;
            if (ImGui::Checkbox("Crash Mode##cm", &CrashMode))
                SaveCurrentSettings();
            Tooltip("Enables the layered digit style with shadow, fill, base and gradient overlay.");
            if (!StreamerMode || fontNames.empty()) { ImGui::PopItemFlag(); ImGui::PopStyleVar(); }
            
            ImGui::TableSetColumnIndex(1);
            if (crashDisabled) { ImGui::PushItemFlag(ImGuiItemFlags_Disabled, true); ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * 0.5f); }
            ImGui::Text("Crash Mode Colors:");
    
            // Row 10 — offset box + fill/base/overlay colors
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            {
                ImGui::Text("Shadow Offset");
                ImVec2 canvasPos = ImGui::GetCursorScreenPos();
                float canvasSize = 80.0f;
                ImGui::InvisibleButton("##shadowoffset", ImVec2(canvasSize, canvasSize));
                if (ImGui::IsItemActive())
                {
                    ImVec2 mouse = ImGui::GetMousePos();
                    CMDigitShadowOffset[0] = ImClamp((mouse.x - canvasPos.x) / canvasSize * 20.0f - 10.0f, -10.0f, 10.0f);
                    CMDigitShadowOffset[1] = ImClamp((mouse.y - canvasPos.y) / canvasSize * 20.0f - 10.0f, -10.0f, 10.0f);
                    SaveCurrentSettings();
                }
                ImDrawList* dl = ImGui::GetWindowDrawList();
                dl->AddRectFilled(canvasPos, ImVec2(canvasPos.x + canvasSize, canvasPos.y + canvasSize), IM_COL32(40, 40, 40, 255));
                dl->AddRect(canvasPos,       ImVec2(canvasPos.x + canvasSize, canvasPos.y + canvasSize), IM_COL32(120, 120, 120, 255));
                float cx = canvasPos.x + canvasSize * 0.5f;
                float cy = canvasPos.y + canvasSize * 0.5f;
                dl->AddLine(ImVec2(cx, canvasPos.y), ImVec2(cx, canvasPos.y + canvasSize), IM_COL32(80, 80, 80, 255));
                dl->AddLine(ImVec2(canvasPos.x, cy), ImVec2(canvasPos.x + canvasSize, cy), IM_COL32(80, 80, 80, 255));
                float nx = (CMDigitShadowOffset[0] + 10.0f) / 20.0f;
                float ny = (CMDigitShadowOffset[1] + 10.0f) / 20.0f;
                ImVec2 handle = ImVec2(canvasPos.x + nx * canvasSize, canvasPos.y + ny * canvasSize);
                if (CrashMode)
                    dl->AddCircleFilled(handle, 5.0f, IM_COL32(255, 255, 255, 255));
                else
                    dl->AddCircleFilled(handle, 5.0f, IM_COL32(128, 128, 128, 128));
                dl->AddCircle(handle, 5.0f, IM_COL32(0, 0, 0, 255));
                if (ImGui::Button("Reset Offset"))
                {
                    float defOffset[2]  = { 0.0f, 1.0f };
                    std::copy(defOffset,  defOffset  + 2, CMDigitShadowOffset);
                    SaveCurrentSettings();
                }
                ImGui::SameLine();
                ImGui::Text("(%.1f, %.1f)", CMDigitShadowOffset[0], CMDigitShadowOffset[1]);
            }
            
            ImGui::TableSetColumnIndex(1);
            ImGui::Checkbox("##hide_cm_1", &ShowCMFill);
            ImGui::SameLine();
            if (!ShowCMFill) { ImGui::PushItemFlag(ImGuiItemFlags_Disabled, true); ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * 0.5f); }
            ImGui::ColorEdit3("Fill##cm",    CMDigitFillColor,   ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_PickerHueWheel);
            if (ImGui::IsItemDeactivatedAfterEdit()) SaveCurrentSettings();
            if (!ShowCMFill) { ImGui::PopItemFlag(); ImGui::PopStyleVar(); }
            ImGui::Checkbox("##hide_cm_2", &ShowCMShadow);
            ImGui::SameLine();
            if (!ShowCMShadow) { ImGui::PushItemFlag(ImGuiItemFlags_Disabled, true); ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * 0.5f); }
            ImGui::ColorEdit3("Shadow##cm",  CMDigitShadowColor, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_PickerHueWheel);
            if (ImGui::IsItemDeactivatedAfterEdit()) SaveCurrentSettings();
            if (!ShowCMShadow) { ImGui::PopItemFlag(); ImGui::PopStyleVar(); }
            ImGui::Dummy(ImVec2(ImGui::GetFrameHeight(), ImGui::GetFrameHeight()));
            ImGui::SameLine();
            ImGui::ColorEdit3("Base##cm",    CMDigitBaseColor,   ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_PickerHueWheel);
            if (ImGui::IsItemDeactivatedAfterEdit()) SaveCurrentSettings();
            ImGui::Dummy(ImVec2(ImGui::GetFrameHeight(), ImGui::GetFrameHeight()));
            ImGui::SameLine();
            ImGui::ColorEdit3("Overlay##cm", CMDigitOverlay,     ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_PickerHueWheel);
            if (ImGui::IsItemDeactivatedAfterEdit()) SaveCurrentSettings();
            ImGui::TableSetColumnIndex(1);
            if (ImGui::Button("Reset Crash Mode Colors##cm"))
            {
                float defShadow[3]  = { 0.0f, 0.0f, 0.0f };
                float defFill[3]    = { 0.0f, 0.0f, 0.0f };
                float defBase[3]    = { 1.0f, 1.0f, 0.0f };
                float defOverlay[3] = { 0.9f, 0.0f,  0.0f };
                std::copy(defShadow,  defShadow  + 3, CMDigitShadowColor);
                std::copy(defFill,    defFill    + 3, CMDigitFillColor);
                std::copy(defBase,    defBase    + 3, CMDigitBaseColor);
                std::copy(defOverlay, defOverlay + 3, CMDigitOverlay);
                SaveCurrentSettings();
            }
    
            if (crashDisabled) { ImGui::PopItemFlag(); ImGui::PopStyleVar(); }
    
            ImGui::EndTable();
        }
    
        if (timerDisabled) { ImGui::PopItemFlag(); ImGui::PopStyleVar(); }
    
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();
    }

    // ---------------------------------------------------------------------------
    // Window Settings
    // ---------------------------------------------------------------------------
    if (ImGui::CollapsingHeader("Window Settings"))
    {
        if (ImGui::BeginTable("##windowsettings", 2, ImGuiTableFlags_None))
        {
            ImGui::TableSetupColumn("##wleft",  ImGuiTableColumnFlags_WidthFixed, 200);
            ImGui::TableSetupColumn("##wright", ImGuiTableColumnFlags_WidthFixed);

            // Row 1 - Route Config
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::Checkbox("Show Route Config", &ShowConfig);
            Tooltip("Toggles the route configuration window.");
            
            ImGui::TableSetColumnIndex(1);
            ImGui::SetNextItemWidth(65.0f);
            if (!ShowConfig) { ImGui::PushItemFlag(ImGuiItemFlags_Disabled, true); ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * 0.5f); }
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
            if (!ShowConfig) { ImGui::PopItemFlag(); ImGui::PopStyleVar(); }

            // Row 2 - Route Browser
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::Checkbox("Show Route Browser", &ShowRouteBrowser);
            Tooltip("Toggles the route file browser.");
            
            ImGui::TableSetColumnIndex(1);
            ImGui::SetNextItemWidth(65.0f);
            if (!ShowRouteBrowser) { ImGui::PushItemFlag(ImGuiItemFlags_Disabled, true); ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * 0.5f); }
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
            if (!ShowRouteBrowser) { ImGui::PopItemFlag(); ImGui::PopStyleVar(); }
            
            // Row 3 - Route History
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::Checkbox("Show History",       &ShowHistory);
            Tooltip("Toggles the history window.");
            ImGui::SameLine();
            if (!ShowHistory) { ImGui::PushItemFlag(ImGuiItemFlags_Disabled, true); ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * 0.5f); }
            ImGui::ColorEdit3("Best Row##tc", ColorBestRow, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_PickerHueWheel);

            ImGui::TableSetColumnIndex(1);
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
            if (!ShowHistory) { ImGui::PopItemFlag(); ImGui::PopStyleVar(); }

            // Row 4 - Max History, Best Row Color
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            // Max History Runs — clamped to [1, 100].
            ImGui::Text("Max");
            Tooltip("Set an amount between 1 and 100.");
            ImGui::SameLine();
            ImGui::SetNextItemWidth(65.0f);
            ImGui::DragInt("##maxruns", &MaxHistoryRuns, 1.0f, 1, 100);
            if (!ShowHistory) { ImGui::PushItemFlag(ImGuiItemFlags_Disabled, true); ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * 0.5f); }
            ImGui::SameLine();
            if (ImGui::Button("Reset Color"))
            {
                float defBestRow[3]    = { 0.2f, 0.3f, 0.2f };
                std::copy(defBestRow,    defBestRow    + 3, ColorBestRow);
            }
            if (!ShowHistory) { ImGui::PopItemFlag(); ImGui::PopStyleVar(); }
            
            ImGui::TableSetColumnIndex(1);
            ImGui::TextDisabled("Tip: You can also resize any window by dragging its edges or bottom-right corner.");
            
            ImGui::EndTable();
        }

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();
    }

    // ---------------------------------------------------------------------------
    // Checkpoint Settings
    // ---------------------------------------------------------------------------
    if (ImGui::CollapsingHeader("Checkpoint Settings"))
    {
        if (ImGui::BeginTable("##checkpointsettings", 2, ImGuiTableFlags_None))
        {
            ImGui::TableSetupColumn("##cpleft",  ImGuiTableColumnFlags_WidthFixed, 200);
            ImGui::TableSetupColumn("##cpright", ImGuiTableColumnFlags_WidthFixed);

            //Row 1
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::Checkbox("Show Checkpoints",   &ShowZones);
            Tooltip("Toggles the visibility of checkpoints.");
            
            ImGui::TableSetColumnIndex(1);
            if (!ShowZones) {ImGui::PushItemFlag(ImGuiItemFlags_Disabled, true); ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * 0.5f);}
            ImGui::ColorEdit3("Start",       ColorStart,      ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_PickerHueWheel);
            
            // Row 2
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::Text("Distance Fade:");
            
            ImGui::TableSetColumnIndex(1);
            ImGui::ColorEdit3("Goal",        ColorGoal,       ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_PickerHueWheel);
            
            // Row 3
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            float prevStart = ZoneFadeStart;
            float prevEnd   = ZoneFadeEnd;
            ImGui::SetNextItemWidth(80.0f);
            ImGui::DragFloat("##fadestart", &ZoneFadeStart, 1.0f, 0.0f, 0.0f, "%.0fm");
            Tooltip("Distance at which zones start fading out (metres)");
            ImGui::SameLine();
            ImGui::SetNextItemWidth(80.0f);
            ImGui::DragFloat("##fadeend", &ZoneFadeEnd, 1.0f, 0.0f, 0.0f, "%.0fm");
            Tooltip("Distance at which zones are fully hidden (metres)");
            // absolute bounds
            ZoneFadeStart = std::clamp(ZoneFadeStart, 1.0f, 1000.0f);
            ZoneFadeEnd   = std::clamp(ZoneFadeEnd,   1.0f, 1000.0f);
            
            // relationship
            if (ZoneFadeStart != prevStart && ZoneFadeStart >= ZoneFadeEnd)
                ZoneFadeEnd = ZoneFadeStart + 1.0f;
            if (ZoneFadeEnd != prevEnd && ZoneFadeEnd <= ZoneFadeStart)
                ZoneFadeStart = ZoneFadeEnd - 1.0f;
                
            ImGui::TableSetColumnIndex(1);
            ImGui::ColorEdit3("Checkpoint",  ColorCheckpoint, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_PickerHueWheel);

            // Row 4
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            // empty
            
            ImGui::TableSetColumnIndex(1);
            ImGui::ColorEdit3("Null",     ColorNull,    ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_PickerHueWheel);
            ImGui::SameLine();
            if (ImGui::Button("Reset Colors"))
            {
                float defStart[3]      = { 0.2f, 1.0f, 0.2f };
                float defGoal[3]       = { 0.2f, 0.5f, 1.0f };
                float defCheckpoint[3] = { 1.0f, 1.0f, 1.0f };
                float defNull[3]       = { 1.0f, 0.6f, 0.0f };
                std::copy(defStart,      defStart      + 3, ColorStart);
                std::copy(defGoal,       defGoal       + 3, ColorGoal);
                std::copy(defCheckpoint, defCheckpoint + 3, ColorCheckpoint);
                std::copy(defNull,       defNull       + 3, ColorNull);
            }

            if (!ShowZones) { ImGui::PopItemFlag(); ImGui::PopStyleVar(); }

            ImGui::EndTable();
        }
        
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();    
    }

    // ---------------------------------------------------------------------------
    // Speedometer Settings
    // ---------------------------------------------------------------------------
    if (ImGui::CollapsingHeader("Speedometer Settings"))
    {
        static constexpr float PI = 3.14159265f;
        static int selectedStop = 0; 

        ImGui::Checkbox("Show Speedometer", &ShowSpeedo);
        ImGui::Checkbox("Speed Unit",       &SpeedUnitMph);
        ImGui::Checkbox("Tachometer mode",  &SpeedoTachometer);
        ImGui::Checkbox("Edit mode",        &SpeedoEditMode);
        ImGui::DragFloat("Opacity",         &SpeedoOpacity, 0.01f, 0.0f, 1.0f, "%.2f");

        // ── Geometry ────────────────────────────────────────────────────────
        ImGui::Separator();
        {
            static constexpr float canvasSize = 200.0f;
            static constexpr float canvasR    = canvasSize * 0.5f;

            ImVec2 canvasPos = ImGui::GetCursorScreenPos();
            ImGui::InvisibleButton("##speedogeo", ImVec2(canvasSize, canvasSize));

            float cx = canvasPos.x + canvasR;
            float cy = canvasPos.y + canvasR;

            if (ImGui::IsItemActive())
            {
                ImVec2 mouse = ImGui::GetMousePos();
                float  dx    = mouse.x - cx;
                float  dy    = mouse.y - cy;
                float  dist  = std::sqrt(dx*dx + dy*dy);
                float  tVal  = std::fmin(dist / canvasR, 1.0f);

                float angleDeg = std::atan2(dy, dx) * 180.0f / PI;
                if (angleDeg < 0.0f) angleDeg += 360.0f;
                SpeedoAngle    = angleDeg;
                SpeedoArcAngle = tVal * 359.0f;

                float radius   = SpeedoArcLength / (SpeedoArcAngle * PI / 180.0f);
                SpeedoPDistance = std::fmin(SpeedoPDistance, std::fmin(200.0f, radius));
                SaveCurrentSettings();
            }

            ImDrawList* dl = ImGui::GetWindowDrawList();
            dl->AddCircleFilled(ImVec2(cx, cy), canvasR, IM_COL32(40, 40, 40, 255));
            dl->AddCircle(ImVec2(cx, cy),       canvasR, IM_COL32(120, 120, 120, 255));
            dl->AddLine(ImVec2(cx, canvasPos.y), ImVec2(cx, canvasPos.y + canvasSize), IM_COL32(80, 80, 80, 255));
            dl->AddLine(ImVec2(canvasPos.x, cy), ImVec2(canvasPos.x + canvasSize, cy), IM_COL32(80, 80, 80, 255));

            float  tVal       = SpeedoArcAngle / 359.0f;
            float  handleAngle = SpeedoAngle * PI / 180.0f;
            ImVec2 handle(cx + std::cos(handleAngle) * tVal * canvasR,
                          cy + std::sin(handleAngle) * tVal * canvasR);
            dl->AddCircleFilled(handle, 5.0f, IM_COL32(255, 255, 255, 255));
            dl->AddCircle(handle,       5.0f, IM_COL32(0, 0, 0, 255));

            ImGui::SameLine();
            ImGui::BeginGroup();
            ImGui::SetNextItemWidth(200);
            ImGui::DragFloat("Rotation:", &SpeedoAngle, 1.0f, 0.0f, 360.0f, "%.0f°");
            ImGui::SetNextItemWidth(200);
            ImGui::DragFloat("Arc Angle:", &SpeedoArcAngle, 1.0f, 0.0f, 359.0f, "%.0f°");
            ImGui::EndGroup();

            if (ImGui::Button("Reset Geometry"))
            {
                SpeedoAngle    = 270.0f;
                SpeedoArcAngle = 60.0f;
                SaveCurrentSettings();
            }
        }

        ImGui::DragFloat("Arc Length", &SpeedoArcLength, 1.0f, 10.0f, 2000.0f, "%.0f px");
        {
            float radius   = SpeedoArcLength / (SpeedoArcAngle * PI / 180.0f);
            float maxPDist = std::fmin(200.0f, radius);
            SpeedoPDistance = std::fmin(SpeedoPDistance, maxPDist);
            ImGui::DragFloat("Needle Origin", &SpeedoPDistance, 0.5f, 0.0f, maxPDist, "%.0f px");
        }

        // ── Arc Style ───────────────────────────────────────────────────────
        ImGui::Separator();
        ImGui::Text("Arc Style");
        ImGui::Checkbox("Smooth gradient", &SpeedoGradientSmooth);
        ImGui::DragFloat("Bg width",       &SpeedoArcBgWidth, 0.1f, 0.1f, 20.0f, "%.1f px");

        // Gradient bar
        {
            // Mirror current stop state for the UI
            struct UIStop { float* pos; float* color; float* thickness; bool* enabled; };
            UIStop uiStops[4] = {
                { nullptr,        SpeedoStop1Color, &SpeedoStop1Thickness, nullptr           },
                { &SpeedoStop2Pos, SpeedoStop2Color, &SpeedoStop2Thickness, &SpeedoStop2Enabled },
                { &SpeedoStop3Pos, SpeedoStop3Color, &SpeedoStop3Thickness, &SpeedoStop3Enabled },
                { &SpeedoStop4Pos, SpeedoStop4Color, &SpeedoStop4Thickness, &SpeedoStop4Enabled },
            };
            // Current positions as floats for bar drawing (stop 1 always 0)
            float stopPos[4] = { 0.0f, SpeedoStop2Pos, SpeedoStop3Pos, SpeedoStop4Pos };
            bool  stopOn[4]  = { true, SpeedoStop2Enabled, SpeedoStop3Enabled, SpeedoStop4Enabled };

            static constexpr float barW = 240.0f;
            static constexpr float barH = 16.0f;
            static constexpr float dotR = 6.0f;

            ImVec2      barPos = ImGui::GetCursorScreenPos();
            barPos.y          += dotR + 2.0f;
            ImGui::Dummy(ImVec2(barW, barH + (dotR + 2.0f) * 2.0f));
            ImDrawList* dl = ImGui::GetWindowDrawList();

            // Draw gradient bar
            for (int px = 0; px < (int)barW; px++)
            {
                float p = (float)px / barW;

                // Gather active stops for sampling
                float prevPos  = 0.0f;
                float prevCol[4] = { SpeedoStop1Color[0], SpeedoStop1Color[1], SpeedoStop1Color[2], SpeedoStop1Color[3] };
                float col[4];
                for (int c = 0; c < 4; c++) col[c] = prevCol[c];

                for (int s = 1; s < 4; s++)
                {
                    if (!stopOn[s]) continue;
                    if (p <= stopPos[s])
                    {
                        if (SpeedoGradientSmooth)
                        {
                            float seg = stopPos[s] - prevPos;
                            float tt  = seg > 0.0f ? (p - prevPos) / seg : 0.0f;
                            for (int c = 0; c < 4; c++)
                                col[c] = prevCol[c] + (uiStops[s].color[c] - prevCol[c]) * tt;
                        }
                        else
                        {
                            for (int c = 0; c < 4; c++) col[c] = prevCol[c];
                        }
                        goto drawnColor;
                    }
                    prevPos = stopPos[s];
                    for (int c = 0; c < 4; c++) prevCol[c] = uiStops[s].color[c];
                }
                for (int c = 0; c < 4; c++) col[c] = prevCol[c];

                drawnColor:
                dl->AddRectFilled(
                    ImVec2(barPos.x + px,     barPos.y),
                    ImVec2(barPos.x + px + 1, barPos.y + barH),
                    IM_COL32((int)(col[0]*255),(int)(col[1]*255),(int)(col[2]*255),(int)(col[3]*255)));
            }
            dl->AddRect(barPos, ImVec2(barPos.x + barW, barPos.y + barH), IM_COL32(120, 120, 120, 255));

            // Draw stop dots and handle interaction
            for (int s = 0; s < 4; s++)
            {
                if (!stopOn[s]) continue;

                float  dotX = barPos.x + stopPos[s] * barW;
                float  dotY = barPos.y + barH * 0.5f;
                bool   isSel = (selectedStop == s);
                ImU32  dotCol = IM_COL32(
                    (int)(uiStops[s].color[0]*255),
                    (int)(uiStops[s].color[1]*255),
                    (int)(uiStops[s].color[2]*255), 255);

                dl->AddCircleFilled(ImVec2(dotX, dotY), dotR, dotCol);
                dl->AddCircle(ImVec2(dotX, dotY), dotR,
                    isSel ? IM_COL32(255,255,255,255) : IM_COL32(0,0,0,200),
                    12, isSel ? 2.0f : 1.0f);

                ImVec2 mouse = ImGui::GetMousePos();
                float  mdx   = mouse.x - dotX;
                float  mdy   = mouse.y - dotY;
                bool   hovered = (mdx*mdx + mdy*mdy) <= (dotR*dotR * 4.0f);
                
                if (hovered && ImGui::IsMouseClicked(0))
                    selectedStop = s;
                
                if (hovered && ImGui::IsMouseDown(0) && s > 0)
                {
                    selectedStop = s;
                    float newPos = std::fmin(std::fmax(
                        (ImGui::GetMousePos().x - barPos.x) / barW, 0.01f), 1.0f);

                    // Clamp between neighbours
                    float lo = 0.01f, hi = 1.0f;
                    for (int prev = s-1; prev >= 0; prev--)
                        if (stopOn[prev]) { lo = stopPos[prev] + 0.01f; break; }
                    for (int next = s+1; next < 4; next++)
                        if (stopOn[next]) { hi = stopPos[next] - 0.01f; break; }
                    newPos = std::fmin(std::fmax(newPos, lo), hi);

                    *uiStops[s].pos = newPos;
                    SaveCurrentSettings();
                }
            }

            // Add / Remove buttons
            ImGui::SetCursorScreenPos(ImVec2(barPos.x, barPos.y + barH + dotR + 6.0f));

            // Count active stops
            int activeCount = 0;
            for (int s = 0; s < 4; s++) if (stopOn[s]) activeCount++;

            // Add: enable the first disabled stop after the last active one
            bool canAdd = activeCount < 4;
            if (!canAdd) { ImGui::PushItemFlag(ImGuiItemFlags_Disabled, true); ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * 0.5f); }
            if (ImGui::SmallButton("+##addstop"))
            {
                for (int s = 1; s < 4; s++)
                {
                    if (!stopOn[s])
                    {
                        // Place new stop halfway between last active and 1.0
                        float lastPos = 0.0f;
                        for (int prev = s-1; prev >= 0; prev--)
                            if (stopOn[prev]) { lastPos = stopPos[prev]; break; }
                        *uiStops[s].pos     = lastPos + (1.0f - lastPos) * 0.5f;
                        *uiStops[s].enabled = true;
                        selectedStop        = s;
                        SaveCurrentSettings();
                        break;
                    }
                }
            }
            if (!canAdd) { ImGui::PopItemFlag(); ImGui::PopStyleVar(); }

            ImGui::SameLine();

            // Remove: disable selected stop (not stop 1)
            bool canRemove = selectedStop > 0 && stopOn[selectedStop];
            if (!canRemove) { ImGui::PushItemFlag(ImGuiItemFlags_Disabled, true); ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * 0.5f); }
            if (ImGui::SmallButton("-##removestop"))
            {
                *uiStops[selectedStop].enabled = false;
                // Cascade: disable all stops after this one too
                for (int s = selectedStop + 1; s < 4; s++)
                    if (uiStops[s].enabled) *uiStops[s].enabled = false;
                selectedStop = std::max(0, selectedStop - 1);
                SaveCurrentSettings();
            }
            if (!canRemove) { ImGui::PopItemFlag(); ImGui::PopStyleVar(); }
        }

        // Selected stop editor
        {
            float* col   = nullptr;
            float* thick = nullptr;
            if      (selectedStop == 0) { col = SpeedoStop1Color; thick = &SpeedoStop1Thickness; }
            else if (selectedStop == 1) { col = SpeedoStop2Color; thick = &SpeedoStop2Thickness; }
            else if (selectedStop == 2) { col = SpeedoStop3Color; thick = &SpeedoStop3Thickness; }
            else if (selectedStop == 3) { col = SpeedoStop4Color; thick = &SpeedoStop4Thickness; }

            ImGui::Text("Stop %d", selectedStop + 1);
            if (col)   ImGui::ColorEdit4("Color##stop", col,
                                         ImGuiColorEditFlags_AlphaBar |
                                         ImGuiColorEditFlags_PickerHueWheel);
            if (thick) ImGui::DragFloat("Thickness##stop", thick, 0.1f, 1.0f, 20.0f, "%.1f px");
        }

        // ── Decorative line ─────────────────────────────────────────────────
        ImGui::Separator();
        ImGui::Text("Decorative Line");
        ImGui::Checkbox("Enable",     &SpeedoDecoLineEnabled);
        if (SpeedoDecoLineEnabled)
        {
            ImGui::DragFloat("Offset",    &SpeedoDecoLineOffset,   0.5f, -50.0f, 50.0f, "%.0f px");
            ImGui::ColorEdit4("Color##deco", SpeedoDecoLineColor,
                              ImGuiColorEditFlags_AlphaBar |
                              ImGuiColorEditFlags_PickerHueWheel);
        }

        // ── Needle ──────────────────────────────────────────────────────────
        ImGui::Separator();
        ImGui::Text("Needle");
        ImGui::Checkbox("Show needle",   &SpeedoNeedleVisible);
        if (SpeedoNeedleVisible)
            ImGui::DragFloat("Needle width", &SpeedoNeedleWidth, 0.1f, 0.1f, 20.0f, "%.1f px");

        // ── Peak hold ───────────────────────────────────────────────────────
        ImGui::Separator();
        ImGui::Text("Peak Hold");
        ImGui::Checkbox("Enable##peak",  &SpeedoPeakHoldEnabled);
        if (SpeedoPeakHoldEnabled)
            ImGui::DragFloat("Hold time",    &SpeedoPeakHoldTime, 0.1f, 0.1f, 10.0f, "%.1f s");

        // ── Tick marks ──────────────────────────────────────────────────────
        ImGui::Separator();
        ImGui::Text("Tick Marks");
        ImGui::Checkbox("Enable##ticks",    &SpeedoTicksEnabled);
        if (SpeedoTicksEnabled)
        {
            ImGui::DragFloat("Minor interval", &SpeedoTickInterval,      1.0f, 1.0f,  100.0f, "%.0f");
            ImGui::DragFloat("Major interval", &SpeedoTickMajorInterval, 1.0f, 1.0f,  200.0f, "%.0f");
            ImGui::DragFloat("Height",         &SpeedoTickHeight,        0.5f, 2.0f,  30.0f,  "%.0f px");
        }

        // ── Label ───────────────────────────────────────────────────────────
        ImGui::Separator();
        ImGui::Text("Label");
        ImGui::Checkbox("Show label", &SpeedoLabelVisible);
        if (SpeedoLabelVisible)
        {
            const auto& fontNames = GetStreamFontNames();
            if (!fontNames.empty())
            {
                const char* preview = SpeedoFontName.empty() ? "Default" : SpeedoFontName.c_str();
                ImGui::SetNextItemWidth(200.0f);
                if (ImGui::BeginCombo("Font##speedo", preview))
                {
                    if (ImGui::Selectable("Default", SpeedoFontName.empty()))
                    {
                        SpeedoFontName = "";
                        SaveCurrentSettings();
                    }
                    for (const auto& name : fontNames)
                    {
                        bool sel = (SpeedoFontName == name);
                        if (ImGui::Selectable(name.c_str(), sel))
                        {
                            SpeedoFontName = name;
                            SaveCurrentSettings();
                        }
                        if (sel) ImGui::SetItemDefaultFocus();
                    }
                    ImGui::EndCombo();
                }

                static const float fontSizes[] = { 16, 20, 24, 28, 32, 36, 40, 44, 48 };
                char sizePreview[8];
                snprintf(sizePreview, sizeof(sizePreview), "%.0f", SpeedoFontSize);
                ImGui::SetNextItemWidth(80.0f);
                if (ImGui::BeginCombo("Size##speedo", sizePreview))
                {
                    for (float s : fontSizes)
                    {
                        char label[8];
                        snprintf(label, sizeof(label), "%.0f", s);
                        if (ImGui::Selectable(label, SpeedoFontSize == s))
                        {
                            SpeedoFontSize = s;
                            SaveCurrentSettings();
                        }
                        if (SpeedoFontSize == s) ImGui::SetItemDefaultFocus();
                    }
                    ImGui::EndCombo();
                }
                ImGui::SameLine();
                if (ImGui::Button("Reset font"))
                {
                    SpeedoFontName = "";
                    SpeedoFontSize = 24.0f;
                    SaveCurrentSettings();
                }
            }
            else
            {
                ImGui::TextDisabled("No fonts found — drop .ttf/.otf into fonts/ and restart.");
            }
        }

        // ── Physics ─────────────────────────────────────────────────────────
        ImGui::Separator();
        ImGui::Text("Physics");
        ImGui::DragFloat("Spring stiffness", &SpeedoSpringK,  0.1f, 0.1f, 50.0f, "%.1f");
        ImGui::DragFloat("Damping",          &SpeedoDamping,  0.1f, 0.1f, 50.0f, "%.1f");
    }
}