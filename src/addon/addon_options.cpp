// addon_options.cpp
// Implements the Split Wars 2 section inside the Nexus options panel.
//
// This is a Nexus UI callback — it draws into a panel that Nexus owns,
// not a standalone window. All widgets write directly into the global
// variables declared in shared.h. Settings are persisted to disk via
// Settings are persisted to settings.ini via SaveCurrentSettings().

#include "build_info.h"
#include "render_shared.h"
#include "stream_fonts.h"

// Scoped "disable + dim" helper for ImGui 1.80 (no native BeginDisabled/EndDisabled in this version).
// Usage:  DisabledBlock(cond) { ...widgets... }
// While `cond` is true, widgets inside the block are non-interactive and drawn at half alpha.
// The pop happens automatically when the block ends (braces, return, break — anything),
// so there's no EndDisabled() call to forget.
struct ImGuiScopedDisabled
{
    bool active;
    ImGuiScopedDisabled(bool cond) : active(cond)
    {
        if (active) { ImGui::PushItemFlag(ImGuiItemFlags_Disabled, true); ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * 0.5f); }
    }
    ~ImGuiScopedDisabled()
    {
        if (active) { ImGui::PopItemFlag(); ImGui::PopStyleVar(); }
    }
    explicit operator bool() const { return true; }
};

#define DISABLED_BLOCK_CONCAT_(a, b) a##b
#define DISABLED_BLOCK_CONCAT(a, b)  DISABLED_BLOCK_CONCAT_(a, b)
#define DisabledBlock(cond) if (ImGuiScopedDisabled DISABLED_BLOCK_CONCAT(_disabled_scope_, __LINE__){cond})

bool HueOnlyColorEdit(const char* label, float& hue)
{
    float colorBuffer[4];
    ImGui::ColorConvertHSVtoRGB(hue, 172.0f / 255.0f, 172.0f / 255.0f, colorBuffer[0], colorBuffer[1], colorBuffer[2]);
    colorBuffer[3] = 1.0f; // alpha, irrelevant since NoAlpha is set

    bool changed = ImGui::ColorEdit4(label, colorBuffer,
        ImGuiColorEditFlags_NoAlpha |
        ImGuiColorEditFlags_NoInputs |
        ImGuiColorEditFlags_PickerHueWheel);

    float h, s, v;
    ImGui::ColorConvertRGBtoHSV(colorBuffer[0], colorBuffer[1], colorBuffer[2], h, s, v);
    hue = h;

    return changed;
}

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
    // Build Info
    // ---------------------------------------------------------------------------
    ImGui::TextDisabled("Release: %s", DateAndTime.c_str());
    if (ShowDebug)
    {
        ImGui::SameLine();
        ImGui::TextDisabled("Render time (avg/1s): %.3f ms", AddonRenderAvgMs);
    }

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
    ImGui::SetNextItemWidth(90.0f);
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

        if (ImGui::BeginTable("##timersettings", 2, ImGuiTableFlags_None))
        {
            ImGui::TableSetupColumn("##left",  ImGuiTableColumnFlags_WidthFixed, 200);
            ImGui::TableSetupColumn("##right", ImGuiTableColumnFlags_WidthFixed);
    
            // Row 1
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::Checkbox("Show Timer", &ShowTimer);
            Tooltip("Toggles the speedrun timer overlay.");
            ImGui::SameLine();
            const char* timerModeLabel = (TimerDisplayMode == TimerMode::Segment)  ? "Mode: Segment"
                                       : (TimerDisplayMode == TimerMode::LiveSplit) ? "Mode: LiveSplit"
                                       :                                               "Mode: Split";
            DisabledBlock(timerDisabled)
            {
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
                if (ImGui::SmallButton("Reset Color##tc"))
                {
                    float defAhead[3]   = { 0.2f, 1.0f, 0.2f };
                    float defBehind[3]  = { 1.0f, 0.3f, 0.3f };
                    std::copy(defAhead,   defAhead   + 3, ColorAhead);
                    std::copy(defBehind,  defBehind  + 3, ColorBehind);
                    SaveCurrentSettings();
                }

                // Row 5
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::Checkbox("Show Start Split", &ShowStartSplit);
                Tooltip("Shows the split fired by whatever checkpoint started the run, at 0:00:00.000.\n"
                        "This split is always recorded and saved to the run's history/JSON regardless of\n"
                        "this setting -- it only controls whether the 0:00:00.000 row is drawn on screen.");
                        
                ImGui::TableSetColumnIndex(1);
                // empty

                //Row 6
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::Separator();
                ImGui::TableSetColumnIndex(1);
                ImGui::Separator();
            
                // --- Streamer section ---
                // Row 7
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::Checkbox("Streamer Mode", &StreamerMode);
                Tooltip("Uses a larger font for better stream visibility.");
            
                const auto& fontNames = GetStreamFontNames();
    
                ImGui::TableSetColumnIndex(1);
                DisabledBlock(!StreamerMode)
                {
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
    
                    // Row 8
                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0);
                    // empty
    
                    ImGui::TableSetColumnIndex(1);
                    {
                        DisabledBlock(fontNames.empty())
                        {
                            ImGui::SetNextItemWidth(90.0f);
                            if (ImGui::InputInt("Size##streamer", &StreamerFontSize, 0, 0))
                            {
                                // Floor of 16, not lower: mainMillis/compMillis derive
                                // as Size-4 / Size-8, so anything smaller would push
                                // compMillis to <=8px or non-positive.
                                StreamerFontSize = std::clamp(StreamerFontSize, 16, 48);
                            }
                            if (ImGui::IsItemDeactivatedAfterEdit()) SaveCurrentSettings();
                            Tooltip("Pixel size of the main time digits.");

                            ImGui::SameLine();

                            ImGui::SetNextItemWidth(90.0f);
                            if (ImGui::InputInt("Header Size##streamer", &StreamerHeaderFontSize, 0, 0))
                            {
                                StreamerHeaderFontSize = std::clamp(StreamerHeaderFontSize, 8, 32);
                            }
                            if (ImGui::IsItemDeactivatedAfterEdit()) SaveCurrentSettings();
                            Tooltip("Pixel size of the section title bar labels.");
                        }
                    }
    
                }

                //Row 9
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::Separator();
                ImGui::TableSetColumnIndex(1);
                ImGui::Separator();
    
                // --- Crash Mode section ---
                bool crashDisabled = !StreamerMode || !CrashMode  || fontNames.empty();
    
                // Row 8 — checkbox + shadow color
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                // Crash checkbox only needs streamer enabled
                DisabledBlock(!StreamerMode || fontNames.empty())
                {
                    if (fontNames.empty() || !StreamerMode) CrashMode = false;
                    if (ImGui::Checkbox("Crash Mode##cm", &CrashMode))
                        SaveCurrentSettings();
                    Tooltip("Enables the layered digit style with shadow, fill, base and gradient overlay.");
                }
            
                ImGui::TableSetColumnIndex(1);
                DisabledBlock(crashDisabled)
                {
                    ImGui::Text("Crash Mode Colors:");
    
                    // Row 9 — offset box + fill/base/overlay colors
                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0);
                    {
                        ImGui::Text("Shadow Offset");
                        ImVec2 canvasPos = ImGui::GetCursorScreenPos();
                        float canvasSize = 90.0f;
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
                        if (ImGui::SmallButton("Reset Offset"))
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
                    DisabledBlock(!ShowCMFill)
                    {
                        ImGui::ColorEdit3("Fill##cm",    CMDigitFillColor,   ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_PickerHueWheel);
                        if (ImGui::IsItemDeactivatedAfterEdit()) SaveCurrentSettings();
                    }
                    ImGui::Checkbox("##hide_cm_2", &ShowCMShadow);
                    ImGui::SameLine();
                    DisabledBlock(!ShowCMShadow)
                    {
                        ImGui::ColorEdit3("Shadow##cm",  CMDigitShadowColor, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_PickerHueWheel);
                        if (ImGui::IsItemDeactivatedAfterEdit()) SaveCurrentSettings();
                    }
                    ImGui::Dummy(ImVec2(ImGui::GetFrameHeight(), ImGui::GetFrameHeight()));
                    ImGui::SameLine();
                    ImGui::ColorEdit3("Base##cm",    CMDigitBaseColor,   ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_PickerHueWheel);
                    if (ImGui::IsItemDeactivatedAfterEdit()) SaveCurrentSettings();
                    ImGui::Dummy(ImVec2(ImGui::GetFrameHeight(), ImGui::GetFrameHeight()));
                    ImGui::SameLine();
                    ImGui::ColorEdit3("Overlay##cm", CMDigitOverlay,     ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_PickerHueWheel);
                    if (ImGui::IsItemDeactivatedAfterEdit()) SaveCurrentSettings();
                    ImGui::TableSetColumnIndex(1);
                    if (ImGui::SmallButton("Reset Color##cm"))
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
    
                }
            
                ImGui::EndTable();
            }
        }
    
    
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();
    }

    // ---------------------------------------------------------------------------
    // Window Settings
    // ---------------------------------------------------------------------------
    if (ImGui::CollapsingHeader("Window Settings"))
    {
        ImGui::TextDisabled("Tip: You can also resize any window by dragging its edges or bottom-right corner.");
        
        if (ImGui::BeginTable("##windowsettings", 2, ImGuiTableFlags_None))
        {
            ImGui::TableSetupColumn("##wleft",  ImGuiTableColumnFlags_WidthFixed, 200);
            ImGui::TableSetupColumn("##wright", ImGuiTableColumnFlags_WidthFixed);

            // Row 1 - Route Config
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::Checkbox("Show Route Config", &ShowConfig);
            Tooltip("Toggles the route configuration window.");
            DisabledBlock(!ShowConfig)
            {
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
            }
            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();
            
            ImGui::TableSetColumnIndex(1);
            // empty

            // Row 2 - Route Browser
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::Checkbox("Show Route Browser", &ShowRouteBrowser);
            Tooltip("Toggles the route file browser.");
            DisabledBlock(!ShowRouteBrowser)
            {
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
            }
            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();
            
            ImGui::TableSetColumnIndex(1);
            // empty
            
            // Row 3 - Route History
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::Checkbox("Show History",       &ShowHistory);
            Tooltip("Toggles the history window.");
            DisabledBlock(!ShowHistory)
            {
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

                ImGui::TableSetColumnIndex(1);
                ImGui::ColorEdit3("Best Row##hw", ColorBestRow, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_PickerHueWheel);
                ImGui::SameLine();
                if (ImGui::SmallButton("Reset Color##hw"))
                {
                    float defBestRow[3]    = { 0.2f, 0.3f, 0.2f };
                    std::copy(defBestRow,    defBestRow    + 3, ColorBestRow);
                }
            }
            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();
            
            // Row 4 - Evaluation Tool
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::Checkbox("Show Evaluation",       &ShowEvaluation);
            Tooltip("Toggles the evaluation window.");
            DisabledBlock(!ShowEvaluation)
            {
                ImGui::SetNextItemWidth(65.0f);
                if (ImGui::InputFloat("W##ew", &EvaluationWindowW, 0, 0, "%.0f"))
                {
                    EvaluationWindowW = std::clamp(EvaluationWindowW, 200.0f, 3000.0f);
                    ImGui::SetWindowSize("Split Wars 2 - Evaluation Tool", ImVec2(EvaluationWindowW, EvaluationWindowH));
                }
                ImGui::SameLine();
                ImGui::SetNextItemWidth(65.0f);
                if (ImGui::InputFloat("H##eh", &EvaluationWindowH, 0, 0, "%.0f"))
                {
                    EvaluationWindowH = std::clamp(EvaluationWindowH, 150.0f, 3000.0f);
                    ImGui::SetWindowSize("Split Wars 2 - Evaluation Tool", ImVec2(EvaluationWindowW, EvaluationWindowH));
                }
                
                ImGui::TableSetColumnIndex(1);
                HueOnlyColorEdit("Core##ew", CoreColorHue);
                ImGui::SameLine();
                HueOnlyColorEdit("Rotating##ew", RotatingColorHue);
                HueOnlyColorEdit("Child##ew", ChildColorHue);
                ImGui::SameLine();
                ImGui::ColorEdit3("Hover##eW", HoverColor, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_PickerHueWheel);
                ImGui::SameLine();
                if (ImGui::SmallButton("Reset Color##ew"))
                {
                    float defHover[3] = { 1.000f, 0.310f, 0.690f };
                    CoreColorHue = 0.573f;
                    RotatingColorHue = 0.093f;
                    ChildColorHue = 0.772f;
                    std::copy(defHover,    defHover    + 3, HoverColor);
                }
                ImGui::SetNextItemWidth(65.0f);
                ImGui::InputFloat("Bar Width##ew", &BarWidth,0.0f,0.0f,"%.0f");
                if (BarWidth < 1 ) BarWidth = 1;
                ImGui::SameLine();
                ImGui::SetNextItemWidth(65.0f);
                ImGui::InputFloat("Bar Gap##ew", &BarGap,0.0f,0.0f,"%.0f");
                if (BarGap < 1 ) BarGap = 1;
            }
            
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
            DisabledBlock(!ShowZones)
            {
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
                ImGui::SetNextItemWidth(90.0f);
                ImGui::DragFloat("##fadestart", &ZoneFadeStart, 1.0f, 0.0f, 0.0f, "%.0fm");
                Tooltip("Distance at which zones start fading out (metres)");
                ImGui::SameLine();
                ImGui::SetNextItemWidth(90.0f);
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
                if (ImGui::SmallButton("Reset Color##zone"))
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

            }

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
        static int selectedStop = 0;
    
        static const char* mountNames[] = {
            "Foot", "Jackal", "Griffon", "Springer", "Skimmer", "Raptor",
            "Beetle", "Warclaw", "Skyscale", "Skiff", "Turtle"
        };
    
        const auto& fontNames = GetStreamFontNames();

        if (ImGui::BeginTable("##speedosettings", 2, ImGuiTableFlags_None))
        {
            ImGui::TableSetupColumn("##speedoleft",  ImGuiTableColumnFlags_WidthFixed, 200);
            ImGui::TableSetupColumn("##speedoright", ImGuiTableColumnFlags_WidthFixed);

            // ── Row 1: Show Speedometer | Speed Unit ────────────────────────
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::Checkbox("Show Speedometer", &ShowSpeedo);
    
            DisabledBlock(!ShowSpeedo)
            {

                ImGui::TableSetColumnIndex(1);
                ImGui::Checkbox("Show Label", &SpeedoLabelVisible);
                Tooltip("Show the numeric speed readout.");
                ImGui::SameLine();
                DisabledBlock(!SpeedoLabelVisible || fontNames.empty())
                {
                    if (ImGui::SmallButton("Reset Font"))
                    {
                        SpeedoFontName = "";
                        SpeedoFontSize = 24.0f;
                        SaveCurrentSettings();
                    }
                    Tooltip("Restore the default font and size for the speed label.");
                }

                // ── Row 2: Mount combo | Show Label + Reset Font ─────────────────
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                {
                    // Build preview string
                    char mountPreview[64] = "None";
                    if ((SpeedoMountMask & 0x7FF) == 0x7FF)
                    {
                        strcpy(mountPreview, "All");
                    }
                    else if (SpeedoMountMask != 0)
                    {
                        mountPreview[0] = '\0';
                        for (int i = 0; i <= 10; i++)
                        {
                            if (SpeedoMountMask & (1 << i))
                            {
                                if (mountPreview[0]) strncat(mountPreview, ", ", sizeof(mountPreview) - strlen(mountPreview) - 1);
                                strncat(mountPreview, mountNames[i], sizeof(mountPreview) - strlen(mountPreview) - 1);
                            }
                        }
                    }
                    ImGui::SetNextItemWidth(190.0f);
                    if (ImGui::BeginCombo("##mountfilter", mountPreview))
                    {
                        if (ImGui::SmallButton("All"))  { SpeedoMountMask = -1; SaveCurrentSettings(); }
                        ImGui::SameLine();
                        if (ImGui::SmallButton("None")) { SpeedoMountMask =  0; SaveCurrentSettings(); }
                        for (int i = 0; i <= 10; i++)
                        {
                            bool checked = (SpeedoMountMask & (1 << i)) != 0;
                            if (ImGui::Checkbox(mountNames[i], &checked))
                            {
                                if (checked) SpeedoMountMask |=  (1 << i);
                                else         SpeedoMountMask &= ~(1 << i);
                                SaveCurrentSettings();
                            }
                        }
                        ImGui::EndCombo();
                    }
                    Tooltip("Show speedometer only when on selected mounts.");
                }
    
                ImGui::TableSetColumnIndex(1);
                DisabledBlock(!SpeedoLabelVisible)
                {
                    const char* unitItems[] = { "km/h", "mph", "u/s" };
                    ImGui::SetNextItemWidth(90.0f);
                    if (ImGui::BeginCombo("##speedunit", unitItems[SpeedUnit < 3 ? SpeedUnit : 0]))
                    {
                        for (int i = 0; i < 3; i++)
                        {
                            if (ImGui::Selectable(unitItems[i], SpeedUnit == i))
                            {
                                SpeedUnit = i;
                                SaveCurrentSettings();
                            }
                            if (SpeedUnit == i) ImGui::SetItemDefaultFocus();
                        }
                        ImGui::EndCombo();
                    }
                    Tooltip("u/s = game units per second (1 unit ≈ 1 inch).");
                    ImGui::SameLine();
                    ImGui::Checkbox("Unit##speedunit", &SpeedoShowUnit);
                    Tooltip("Show/Hide units.");
                }

                // ── Row 3: Tachometer | Font combo ──────────────────────────────
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::Checkbox("Tachometer", &SpeedoTachometer);
                Tooltip("Switch between numeric display and tachometer arc.");
    
                ImGui::TableSetColumnIndex(1);
                DisabledBlock(!SpeedoLabelVisible)
                {
                    if (fontNames.empty())
                    {
                        ImGui::TextDisabled("No fonts — drop .ttf/.otf into fonts/ and restart.");
                    }
                    else
                    {
                        const char* fontPreview = SpeedoFontName.empty() ? "Default" : SpeedoFontName.c_str();
                        ImGui::SetNextItemWidth(120.0f);
                        if (ImGui::BeginCombo("##speedofont", fontPreview))
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
                        Tooltip("Font used for the speed label.");
                        ImGui::SameLine();
                        ImGui::SetNextItemWidth(70.0f);
                        if (ImGui::InputFloat("##speedofontsize", &SpeedoFontSize, 0.0f, 0.0f,"%.0f"))
                        {
                            SpeedoFontSize = std::clamp(SpeedoFontSize, 6.0f, 200.0f);
                        }
                        if (ImGui::IsItemDeactivatedAfterEdit()) SaveCurrentSettings();
                        Tooltip("Font size for the speed label.");
                    }
                }

                // ── Row 4: Edit Mode | Label X / Y ──────────────────────────────
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                DisabledBlock(!SpeedoTachometer)
                {
                    ImGui::Checkbox("Edit Mode", &SpeedoEditMode);
                    Tooltip("Makes the speedometer windows draggable.");
                }
    
                ImGui::TableSetColumnIndex(1);
                if (SpeedoLabelVisible)
                    ImGui::TextDisabled("Drag label in Edit Mode");
    
                // ── Separator ────────────────────────────────────────────────────
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::Separator();
                ImGui::TableSetColumnIndex(1);
                ImGui::Separator();

                // ── Row 5 (geometry): canvas left | sliders right ────────────────
                // The canvas is 200px tall so we emit it first in col 0,
                // then use SameLine-style via the table to fill col 1 with rows.
                ImGui::TableNextRow();
                DisabledBlock(!SpeedoEditMode || !SpeedoTachometer)
                {
                    ImGui::TableSetColumnIndex(0);
                    {
                        static constexpr float canvasSize = 190.0f;
                        static constexpr float canvasR    = canvasSize * 0.5f;
    
                        ImVec2 canvasPos = ImGui::GetCursorScreenPos();
                        ImGui::InvisibleButton("##speedogeo", ImVec2(canvasSize, canvasSize));
                        Tooltip("Drag to set the arc's rotation and sweep angle.");
    
                        float cx = canvasPos.x + canvasR;
                        float cy = canvasPos.y + canvasR;
    
                        if (ImGui::IsItemActive())
                        {
                            ImVec2 mouse = ImGui::GetMousePos();
                            float  dx    = mouse.x - cx;
                            float  dy    = mouse.y - cy;
                            float  dist  = std::sqrt(dx*dx + dy*dy);
                            float  tVal  = std::fmin(dist / canvasR, 1.0f);
    
                            float angleDeg = std::atan2(dy, dx) * 180.0f / IM_PI;
                            if (angleDeg < 0.0f) angleDeg += 360.0f;
                            SpeedoArcRotation    = angleDeg;
                            SpeedoArcAngle = tVal * 359.0f;
    
                            float radius = SpeedoArcLength / (SpeedoArcAngle * IM_PI / 180.0f);
                            SpeedoPDistance = std::fmin(SpeedoPDistance, std::fmin(500.0f, radius));
                            SaveCurrentSettings();
                        }
    
                        ImDrawList* dl = ImGui::GetWindowDrawList();
                        dl->AddCircleFilled(ImVec2(cx, cy), canvasR, IM_COL32(40, 40, 40, 255));
                        dl->AddCircle(ImVec2(cx, cy),       canvasR, IM_COL32(120, 120, 120, 255));
                        dl->AddLine(ImVec2(cx, canvasPos.y), ImVec2(cx, canvasPos.y + canvasSize), IM_COL32(80, 80, 80, 255));
                        dl->AddLine(ImVec2(canvasPos.x, cy), ImVec2(canvasPos.x + canvasSize, cy), IM_COL32(80, 80, 80, 255));
    
                        float  tVal       = SpeedoArcAngle / 359.0f;
                        float  handleAngle = SpeedoArcRotation * IM_PI / 180.0f;
                        ImVec2 handle(cx + std::cos(handleAngle) * tVal * canvasR,
                                      cy + std::sin(handleAngle) * tVal * canvasR);
                        if (SpeedoEditMode)
                            dl->AddCircleFilled(handle, 5.0f, IM_COL32(255, 255, 255, 255));
                        else
                            dl->AddCircleFilled(handle, 5.0f, IM_COL32(128, 128, 128, 128));
                        dl->AddCircle(handle,       5.0f, IM_COL32(0, 0, 0, 255));
                    }

                    ImGui::TableSetColumnIndex(1);
                    if (ImGui::BeginTable("##speedosettingssub1", 2, ImGuiTableFlags_None))
        			{
            			ImGui::TableSetupColumn("##sub1left",  ImGuiTableColumnFlags_WidthFixed, 100);
            			ImGui::TableSetupColumn("##sub1right", ImGuiTableColumnFlags_WidthFixed);
                    
                        ImGui::TableNextRow();
						ImGui::TableSetColumnIndex(0);
						ImGui::Text("Arc:");
                        ImGui::SameLine();
                    	if (ImGui::SmallButton("Reset"))
                    	{
                        	SpeedoArcRotation     = 270.0f;
	                        SpeedoArcAngle  = 60.0f;
                        	SpeedoArcLength = 400.0f;
                        	SaveCurrentSettings();
                    	}
                    	Tooltip("Restore the default arc rotation, sweep, and length.");
						ImGui::TableSetColumnIndex(1);
                    	ImGui::SetNextItemWidth(90.0f);
                    	ImGui::DragFloat("##georot",  &SpeedoArcRotation,    1.0f, 0.0f,   360.0f, "Rot: %.0f°");
                    	Tooltip("Rotation of the whole speedometer.");
                        
                        ImGui::TableNextRow();
						ImGui::TableSetColumnIndex(0);
                    	ImGui::SetNextItemWidth(90.0f);
                    	ImGui::DragFloat("##geoangle", &SpeedoArcAngle, 1.0f, 0.0f,   359.0f, "Angle: %.0f°");
                    	Tooltip("Total sweep of the arc. Below 1° the speedo draws as a straight line instead.");

						ImGui::TableSetColumnIndex(1);
                    	ImGui::SetNextItemWidth(90.0f);
                    	ImGui::DragFloat("##geolength", &SpeedoArcLength, 1.0f, 10.0f, 2000.0f, "Size: %.0f px");
                    	Tooltip("Length of the arc (or line, in straight-line mode) in pixels.");
                        
                        ImGui::TableNextRow();
                        ImGui::TableSetColumnIndex(0);
						ImGui::Spacing();
                        ImVec2 p = ImGui::GetCursorScreenPos();
                        
                        float row_w = 0.0f;
                        for (int i = 0; i < 2; i++)
                            row_w += ImGui::GetColumnWidth(i);
                        
                        ImGui::GetWindowDrawList()->AddLine( ImVec2(p.x, p.y), ImVec2(p.x + row_w, p.y), ImGui::GetColorU32(ImGuiCol_Separator), 1.0f);
						ImGui::Spacing();
                        
                        ImGui::TableSetColumnIndex(1);
                        // empty

                        
                    	// ── Needle ───────────────────────────────────────────────────
                        ImGui::TableNextRow();
						ImGui::TableSetColumnIndex(0);
						ImGui::Text("Needle:");

						ImGui::TableSetColumnIndex(1);
                        // empty
                        
                        ImGui::TableNextRow();
						ImGui::TableSetColumnIndex(0);
                    	ImGui::Checkbox("Draw", &SpeedoNeedleVisible);
                    	Tooltip("Draw the plain needle line.");

						ImGui::TableSetColumnIndex(1);
                    	DisabledBlock(!SpeedoNeedleVisible)
                    	{
                        	ImGui::SetNextItemWidth(90.0f);
                        	ImGui::DragFloat("##needlewidth",  &SpeedoNeedleWidth,  0.1f, 0.1f, 5.0f,     "Width: %.1f px");
                        	Tooltip("Needle width");
                    	}
                        
                        ImGui::TableNextRow();
						ImGui::TableSetColumnIndex(0);
                    	ImGui::Checkbox("Texture##needletex", &SpeedoNeedleTexEnabled);
                    	Tooltip("Load a needle image. It rotates around the drawn needle's pivot point P.");
                    	if (ImGui::IsItemDeactivatedAfterEdit()) SaveCurrentSettings();

						ImGui::TableSetColumnIndex(1);
                    	DisabledBlock(!SpeedoNeedleVisible && !SpeedoNeedleTexEnabled)
                    	{
                        	float radius   = SpeedoArcLength / (SpeedoArcAngle * IM_PI / 180.0f);
                        	float maxPDist = std::fmin(500.0f, radius);
                        	SpeedoPDistance = std::fmin(SpeedoPDistance, maxPDist);
                        	ImGui::SetNextItemWidth(90.0f);
                        	ImGui::DragFloat("##needleorigin", &SpeedoPDistance, 0.5f, 0.0f, maxPDist, "Origin: %.0f px");
                        	Tooltip("Needle origin");
                    	}
                        
                        ImGui::TableNextRow();
						ImGui::TableSetColumnIndex(0);
                    	DisabledBlock(!SpeedoNeedleTexEnabled)
                    	{
                        	{
                            	const auto& texNames = GetSpeedoTextureNames();
                            	const char* needlePreview = SpeedoNeedleTexPath.empty() ? "None" : SpeedoNeedleTexPath.c_str();
								ImGui::SetNextItemWidth(90.0f);
                            	if (ImGui::BeginCombo("##needletexcombo", needlePreview))
                            	{
                                	if (ImGui::Selectable("None", SpeedoNeedleTexPath.empty()))
                                	{
                                    	SpeedoNeedleTexPath.clear();
                                    	SaveCurrentSettings();
                                	}
                                	for (const auto& name : texNames)
                                	{
                                    	bool sel = (SpeedoNeedleTexPath == name);
                                    	if (ImGui::Selectable(name.c_str(), sel))
                                    	{
                                        	SpeedoNeedleTexPath = name;
                                        	SaveCurrentSettings();
                                    	}
                                    	if (sel) ImGui::SetItemDefaultFocus();
                                	}
                                	ImGui::EndCombo();
                            	}
                            	Tooltip("Select the needle image to use.");

								ImGui::TableSetColumnIndex(1);
                            	if (ImGui::SmallButton("Refresh##needletex"))
                                	ScanTextureFiles();
                            	Tooltip("PNG/JPG files from addons/Split Wars 2/textures/. Hit Refresh after adding new files.");
                        	}
                            
                            ImGui::TableNextRow();
			    			ImGui::TableSetColumnIndex(0);
                            float scalePercent = SpeedoNeedleTexScale * 100.0f;
				    		ImGui::SetNextItemWidth(90.0f);
                            if (ImGui::DragFloat("##needlescale", &scalePercent, 1.0f, 1.0f, 1000.0f, "Scale: %.0f%%"))
                            {
                                SpeedoNeedleTexScale = scalePercent / 100.0f;
                            }
                            if (ImGui::IsItemDeactivatedAfterEdit()) SaveCurrentSettings();
                            Tooltip("Scale the needle texture uniformly.");

						    ImGui::TableSetColumnIndex(1);
						    ImGui::SetNextItemWidth(90.0f);
                            ImGui::DragFloat("##needleangle", &SpeedoNeedleTexAngleOffset, 1.0f, -180.0f, 180.0f, "Angle: %.0f°");
                            if (ImGui::IsItemDeactivatedAfterEdit()) SaveCurrentSettings();
                            Tooltip("Rotates the texture relative to the needle angle. Use this to align the tip of your needle image with the drawn needle direction.");
                            
                            ImGui::TableNextRow();
						    ImGui::TableSetColumnIndex(0);
						    ImGui::SetNextItemWidth(90.0f);
                            {
                                float texW = 0.0f, texH = 0.0f;
                                bool hasTex = GetSpeedoNeedleTexSize(texW, texH);
                                float pivotXDisplay = (SpeedoNeedleTexPivotX < 0.0f && hasTex)
                                    ? texW * 0.5f : SpeedoNeedleTexPivotX;
                                if (ImGui::DragFloat("##needlepivotx", &pivotXDisplay, 0.5f, 0.0f, 4096.0f, "Pivot X: %.0f px"))
                                    SpeedoNeedleTexPivotX = pivotXDisplay;
                            }
                            if (ImGui::IsItemDeactivatedAfterEdit()) SaveCurrentSettings();
                            Tooltip("X position of the needle's pivot point inside the texture, in pixels from the left edge.\n"
                                    "You can read this off your image in any image editor (e.g. GIMP, paint.net).\n"
                                    "This point gets placed on the drawn needle's pivot P and rotated about.");

						    ImGui::TableSetColumnIndex(1);
						    ImGui::SetNextItemWidth(90.0f);
                            {
                                float texW = 0.0f, texH = 0.0f;
                                bool hasTex = GetSpeedoNeedleTexSize(texW, texH);
                                float pivotYDisplay = (SpeedoNeedleTexPivotY < 0.0f && hasTex)
                                    ? texH * 0.5f : SpeedoNeedleTexPivotY;
                                if (ImGui::DragFloat("##needlepivoty", &pivotYDisplay, 0.5f, 0.0f, 4096.0f, "Pivot Y: %.0f px"))
                                    SpeedoNeedleTexPivotY = pivotYDisplay;
                            }
                            if (ImGui::IsItemDeactivatedAfterEdit()) SaveCurrentSettings();
                            Tooltip("Y position of the needle's pivot point inside the texture, in pixels from the top edge.");
                        }

                        DisabledBlock(!SpeedoNeedleVisible && !SpeedoNeedleTexEnabled)
                        {
                            ImGui::TableNextRow();
                            ImGui::TableSetColumnIndex(0);
                            ImGui::SetNextItemWidth(90.0f);
                            ImGui::DragFloat("##springphys",  &SpeedoSpringK, 0.1f, 0.1f, 50.0f, "Spring: %.1f");
                            Tooltip("Spring stiffness for the needle's physics-based motion. Higher values snap to the target speed faster.");

                            ImGui::TableSetColumnIndex(1);
                            ImGui::SetNextItemWidth(90.0f);
                            ImGui::DragFloat("##dampphys", &SpeedoDamping, 0.1f, 0.1f, 50.0f, "Damp: %.1f");
                            Tooltip("Damping for the needle's physics-based motion. Higher values reduce overshoot and settle the needle faster.");
                        }
                        ImGui::EndTable();
                    }
            		
                    // ── Separator ────────────────────────────────────────────────────
                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0);
                    ImGui::Separator();
                    ImGui::TableSetColumnIndex(1);
                    ImGui::Separator();

                    // ── Row: Arc style left | Needle right ───────────────────────────
                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0);
                    ImGui::Text("Arc Gradient:");
                    {    
                        // ── Gradient bar ─────────────────────────────────────────────
                        struct UIStop { float* pos; float* color; float* thickness; bool* enabled; };
                        UIStop uiStops[4] = {
                            { nullptr,          SpeedoStop1Color, &SpeedoStop1Thickness, nullptr             },
                            { &SpeedoStop2Pos,  SpeedoStop2Color, &SpeedoStop2Thickness, &SpeedoStop2Enabled },
                            { &SpeedoStop3Pos,  SpeedoStop3Color, &SpeedoStop3Thickness, &SpeedoStop3Enabled },
                            { &SpeedoStop4Pos,  SpeedoStop4Color, &SpeedoStop4Thickness, &SpeedoStop4Enabled },
                        };
                        float stopPos[4] = { 0.0f, SpeedoStop2Pos, SpeedoStop3Pos, SpeedoStop4Pos };
                        bool  stopOn[4]  = { true, SpeedoStop2Enabled, SpeedoStop3Enabled, SpeedoStop4Enabled };
    
                        static constexpr float barW = 190.0f;
                        static constexpr float barH = 16.0f;
                        static constexpr float dotR = 6.0f;
    
                        ImVec2 barPos = ImGui::GetCursorScreenPos();
                        barPos.y     += dotR + 2.0f;
                        ImGui::Dummy(ImVec2(barW, barH + (dotR + 2.0f) * 2.0f));
                        ImDrawList* dl = ImGui::GetWindowDrawList();
    
                        float maxThick = SpeedoStop1Thickness;
                        if (SpeedoStop2Enabled) maxThick = std::fmax(maxThick, SpeedoStop2Thickness);
                        if (SpeedoStop3Enabled) maxThick = std::fmax(maxThick, SpeedoStop3Thickness);
                        if (SpeedoStop4Enabled) maxThick = std::fmax(maxThick, SpeedoStop4Thickness);
    
                        for (int px = 0; px < (int)barW; px++)
                        {
                            float p = (float)px / barW;
                            float prevPos    = 0.0f;
                            float prevCol[4] = { SpeedoStop1Color[0], SpeedoStop1Color[1], SpeedoStop1Color[2], SpeedoStop1Color[3] };
                            float prevThick  = SpeedoStop1Thickness;
                            float col[4];
                            float thick = prevThick;
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
                                        thick = prevThick + (*uiStops[s].thickness - prevThick) * tt;
                                    }
                                    else
                                    {
                                        for (int c = 0; c < 4; c++) col[c] = prevCol[c];
                                        thick = prevThick;
                                    }
                                    goto drawnColor;
                                }
                                prevPos   = stopPos[s];
                                prevThick = *uiStops[s].thickness;
                                for (int c = 0; c < 4; c++) prevCol[c] = uiStops[s].color[c];
                            }
                            for (int c = 0; c < 4; c++) col[c] = prevCol[c];
                            thick = prevThick;
    
                            drawnColor:
                            {
                                // In whole-arc mode the live speedo shows one uniform
                                // color for the entire fill, matching the color this
                                // pixel's own position would sample — so painting
                                // each pixel with its own col[] here already previews
                                // "if current speed were p, the whole arc would look
                                // like this column." Thickness still follows position
                                // either way, per the live render.
                                float halfH = (thick / maxThick) * (barH * 0.5f);
                                float midY  = barPos.y + barH * 0.5f;
                                float top   = midY - halfH;
                                float bot   = midY + halfH;
    
                                int topFull = (int)std::ceil(top);
                                int botFull = (int)std::floor(bot);
                                if (botFull > topFull)
                                    dl->AddRectFilled(
                                        ImVec2(barPos.x + px,     (float)topFull),
                                        ImVec2(barPos.x + px + 1, (float)botFull),
                                        IM_COL32((int)(col[0]*255),(int)(col[1]*255),(int)(col[2]*255),(int)(col[3]*255)));
    
                                float topAlpha = (float)topFull - top;
                                if (topAlpha > 0.0f)
                                    dl->AddRectFilled(
                                        ImVec2(barPos.x + px,     top),
                                        ImVec2(barPos.x + px + 1, (float)topFull),
                                        IM_COL32((int)(col[0]*255),(int)(col[1]*255),(int)(col[2]*255),(int)(col[3]*topAlpha*255)));
    
                                float botAlpha = bot - (float)botFull;
                                if (botAlpha > 0.0f)
                                    dl->AddRectFilled(
                                        ImVec2(barPos.x + px,     (float)botFull),
                                        ImVec2(barPos.x + px + 1, bot),
                                        IM_COL32((int)(col[0]*255),(int)(col[1]*255),(int)(col[2]*255),(int)(col[3]*botAlpha*255)));
                            }
                        }
                        dl->AddRect(barPos, ImVec2(barPos.x + barW, barPos.y + barH), IM_COL32(120, 120, 120, 255));
    
                        // Stop dots
                        if (SpeedoEditMode)
                        {
                            for (int s = 0; s < 4; s++)
                            {
                                if (!stopOn[s]) continue;
                                float  dotX  = barPos.x + stopPos[s] * barW;
                                float  dotY  = barPos.y + barH * 0.5f;
                                bool   isSel = (selectedStop == s);
                                ImU32  dotCol = IM_COL32(
                                    (int)(uiStops[s].color[0]*255),
                                    (int)(uiStops[s].color[1]*255),
                                    (int)(uiStops[s].color[2]*255), 255);
                                dl->AddCircleFilled(ImVec2(dotX, dotY), dotR, dotCol);
                                dl->AddCircle(ImVec2(dotX, dotY), dotR,
                                    isSel ? IM_COL32(255,255,255,255) : IM_COL32(0,0,0,200),
                                    12, isSel ? 2.0f : 1.0f);
        
                                ImVec2 mouse  = ImGui::GetMousePos();
                                float  mdx    = mouse.x - dotX;
                                float  mdy    = mouse.y - dotY;
                                bool   hovered = (mdx*mdx + mdy*mdy) <= (dotR*dotR * 4.0f);
        
                                if (hovered && ImGui::IsMouseClicked(0))
                                    selectedStop = s;
        
                                if (hovered && ImGui::IsMouseDown(0) && s > 0)
                                {
                                    selectedStop = s;
                                    float newPos = std::fmin(std::fmax(
                                        (ImGui::GetMousePos().x - barPos.x) / barW, 0.01f), 1.0f);
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
                        }
    
                        // +/- buttons and smooth checkbox on one line
                        ImGui::SetCursorScreenPos(ImVec2(barPos.x, barPos.y + barH + dotR + 6.0f));
                        int  activeCount = 0;
                        for (int s = 0; s < 4; s++) if (stopOn[s]) activeCount++;
    
                        bool canAdd = activeCount < 4;
                        DisabledBlock(!canAdd)
                        {
                            if (ImGui::SmallButton("+##addstop"))
                            {
                                for (int s = 1; s < 4; s++)
                                {
                                    if (!stopOn[s])
                                    {
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
                            Tooltip("Add up to four gradient stops.");
                        }
                
                        ImGui::SameLine();
    
                        bool canRemove = selectedStop > 0 && stopOn[selectedStop];
                        DisabledBlock(!canRemove)
                        {
                            if (ImGui::SmallButton("-##removestop"))
                            {
                                *uiStops[selectedStop].enabled = false;
                                for (int s = selectedStop + 1; s < 4; s++)
                                    if (uiStops[s].enabled) *uiStops[s].enabled = false;
                                selectedStop = std::max(0, selectedStop - 1);
                                SaveCurrentSettings();
                            }
                            Tooltip("Removes all gradient stops from the selected one to the right.");
                        }
                
                        ImGui::SameLine();
                        ImGui::Checkbox("Smooth##gradient", &SpeedoGradientSmooth);
                        Tooltip("Blend smoothly between stops instead of stepping abruptly at each one.");

                        ImGui::SameLine();
                        ImGui::Checkbox("Whole Arc##gradient", &SpeedoGradientWholeArc);
                        Tooltip("Color the entire arc as one solid color matching the current speed,\ninstead of showing each stop's color at its own position along the arc.");
    
                        // Stop rows
                        for (int s = 0; s < 4; s++)
                        {
                            bool active = stopOn[s];
                            DisabledBlock(!active)
                            {
                                ImGui::SetNextItemWidth(90.0f);
                                char thickId[16];
                                snprintf(thickId, sizeof(thickId), "##thick%d", s);
                                ImGui::DragFloat(thickId, uiStops[s].thickness, 0.1f, 0.1f, 20.0f, "%.1f px");
                                Tooltip("Arc thickness at this stop.");
                                if (ImGui::IsItemDeactivatedAfterEdit()) SaveCurrentSettings();
                                ImGui::SameLine();
                                char label[32];
                                snprintf(label, sizeof(label), "Stop %d", s + 1);
                                ImGui::ColorEdit4(label, uiStops[s].color,
                                    ImGuiColorEditFlags_AlphaBar |
                                    ImGuiColorEditFlags_NoInputs |
                                    ImGuiColorEditFlags_PickerHueWheel);
                                if (ImGui::IsItemDeactivatedAfterEdit()) SaveCurrentSettings();
                            }
                        }
                    }
    
                    ImGui::TableSetColumnIndex(1);
                    if (ImGui::BeginTable("##speedosettingssub2", 2, ImGuiTableFlags_None))
        			{
            			ImGui::TableSetupColumn("##sub2left",  ImGuiTableColumnFlags_WidthFixed, 100);
            			ImGui::TableSetupColumn("##sub2right", ImGuiTableColumnFlags_WidthFixed);
                        
                        ImGui::TableNextRow();
                        ImGui::TableSetColumnIndex(0);
                        ImGui::Text("Background Arc:");

                        ImGui::TableSetColumnIndex(1);
                        // empty

                        {
                            ImGui::TableNextRow();
                            ImGui::TableSetColumnIndex(0);
                            ImGui::SetNextItemWidth(90.0f);
                            ImGui::DragFloat("##arcopacity",  &SpeedoArcBgOpacity,   0.01f, 0.0f, 1.0f,  "Alpha: %.2f");
                            Tooltip("Opacity of the background (unfilled) arc track.");
                            
                            ImGui::TableSetColumnIndex(1);
                            ImGui::SetNextItemWidth(90.0f);
                            ImGui::DragFloat("##arcwidth", &SpeedoArcBgWidth, 0.1f, 0.1f, 20.0f, "Width: %.1f px");
                            Tooltip("Thickness of the background arc track.");

                            // ── Face Texture ─────────────────────────────────────────────
                            ImGui::TableNextRow();
                            ImGui::TableSetColumnIndex(0);
                            ImGui::Checkbox("Texture##facetex", &SpeedoFaceEnabled);
                            Tooltip("Load a gauge-face image. Position and scale it freely, then overlay the drawn arc on top for alignment.");
                            if (ImGui::IsItemDeactivatedAfterEdit()) SaveCurrentSettings();
                            
                            ImGui::TableSetColumnIndex(1);
                            // empty

                            DisabledBlock(!SpeedoFaceEnabled)
                            {

                                {
                                    const auto& texNames = GetSpeedoTextureNames();
                                    const char* facePreview = SpeedoFacePath.empty() ? "None" : SpeedoFacePath.c_str();
                                    ImGui::TableNextRow();
                                    ImGui::TableSetColumnIndex(0);
                                    ImGui::SetNextItemWidth(90.0f);
                                    if (ImGui::BeginCombo("##facetexcombo", facePreview))
                                    {
                                        if (ImGui::Selectable("None", SpeedoFacePath.empty()))
                                        {
                                            SpeedoFacePath.clear();
                                            SaveCurrentSettings();
                                        }
                                        for (const auto& name : texNames)
                                        {
                                            bool sel = (SpeedoFacePath == name);
                                            if (ImGui::Selectable(name.c_str(), sel))
                                            {
                                                SpeedoFacePath = name;
                                                SaveCurrentSettings();
                                            }
                                            if (sel) ImGui::SetItemDefaultFocus();
                                        }
                                        ImGui::EndCombo();
                                    }
                                    Tooltip("Select the gauge-face image to use.");
                                    ImGui::TableSetColumnIndex(1);
                                    if (ImGui::SmallButton("Refresh##facetex"))
                                        ScanTextureFiles();
                                    Tooltip("PNG/JPG files from addons/Split Wars 2/textures/. Hit Refresh after adding new files.");
                                }

                                float scalePercent = SpeedoFaceScale * 100.0f;
                                ImGui::TableNextRow();
                                ImGui::TableSetColumnIndex(0);
                                ImGui::SetNextItemWidth(90.0f);
                                if (ImGui::DragFloat("##needlescale", &scalePercent, 1.0f, 1.0f, 1000.0f, "Scale: %.0f%%"))
                                {
                                    SpeedoFaceScale = scalePercent / 100.0f;
                                }
                                if (ImGui::IsItemDeactivatedAfterEdit()) SaveCurrentSettings();
                                Tooltip("Scale the face texture uniformly.");
                                
                                ImGui::TableSetColumnIndex(1);
                                // empty
                                
                                ImGui::TableNextRow();
                                ImGui::TableSetColumnIndex(0);
                                ImGui::SetNextItemWidth(90.0f);
                                ImGui::DragFloat("##facex", &SpeedoFaceX, 1.0f, 0.0f, 4096.0f, "X: %.0f px");
                                if (ImGui::IsItemDeactivatedAfterEdit()) SaveCurrentSettings();
                                
                                ImGui::TableSetColumnIndex(1);
                                ImGui::SetNextItemWidth(90.0f);
                                ImGui::DragFloat("##facey", &SpeedoFaceY, 1.0f, 0.0f, 4096.0f, "Y: %.0f px");
                                if (ImGui::IsItemDeactivatedAfterEdit()) SaveCurrentSettings();
                                Tooltip("Position of the top-left corner of the face texture. In Edit Mode you can also drag it directly on screen.");
                                
                                ImGui::TableNextRow();
                                ImGui::TableSetColumnIndex(0);
                                ImGui::Spacing();
                                ImVec2 p = ImGui::GetCursorScreenPos();
                                
                                float row_w = 0.0f;
                                for (int i = 0; i < 2; i++)
                                    row_w += ImGui::GetColumnWidth(i);
                                
                                ImGui::GetWindowDrawList()->AddLine( ImVec2(p.x, p.y), ImVec2(p.x + row_w, p.y), ImGui::GetColorU32(ImGuiCol_Separator), 1.0f);
                                ImGui::Spacing();
                                
                                ImGui::TableSetColumnIndex(1);
                                // empty
                            }
                                
                            ImGui::TableNextRow();
                            ImGui::TableSetColumnIndex(0);
                            ImGui::Checkbox("Peak Hold", &SpeedoPeakHoldEnabled);
                            Tooltip("Briefly hold and display the highest speed reached before it decays back down.");
                            ImGui::TableSetColumnIndex(1);
                            // empty

                            DisabledBlock(!SpeedoPeakHoldEnabled)
                            {
                                ImGui::TableNextRow();
                                ImGui::TableSetColumnIndex(0);
                                ImGui::SetNextItemWidth(90.0f);
                                ImGui::DragFloat("##peaktime", &SpeedoPeakHoldTime, 0.1f, 0.1f, 10.0f, "Time: %.1f s");
                                Tooltip("How long the peak marker stays before decaying back to the current speed.");
                                ImGui::TableSetColumnIndex(1);
                                ImGui::SetNextItemWidth(90.0f);
                                ImGui::DragFloat("##peaksize", &SpeedoPeakHoldSize, 0.1f, 0.1f, 20.0f, "Size: %.1f px");
                                Tooltip("Size of the peak-hold marker.");
                            }
                        }
                        ImGui::EndTable();
                    }
                }
            }
            ImGui::EndTable();
        }
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();
    }
}