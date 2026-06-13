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
        ImGui::Checkbox("Show Speedometer", &ShowSpeedo);
        ImGui::Checkbox("mph", &SpeedUnitMph);
        ImGui::Checkbox("tacho", &SpeedoTachometer);
        ImGui::InputFloat("##speedorad", &SpeedoRadius);
    }
}