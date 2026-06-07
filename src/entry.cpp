// entry.cpp
// Addon lifecycle entry point — the first and last code Nexus calls.
//
// Responsibilities:
//   - GetAddonDef(): exports the addon descriptor to Nexus
//   - AddonLoad(): one-time setup (ImGui context, data sources, settings,
//                  callbacks, hotbar icon, keybinds)
//   - AddonUnload(): teardown (save settings, deregister everything, clear state)
//   - ApplySettings() / GatherSettings(): translate between the flat Settings
//                  struct on disk and the global variables used at runtime
//   - SaveCurrentSettings(): public wrapper called by addon_options.cpp
//   - AddonQuickAccessMenu(): right-click context menu on the hotbar icon
//   - RTAPI hot-load/unload event handlers

#include "hotbar_icon.h"
#include "imgui.h"
#include "shared.h"

// The global addon descriptor filled in by GetAddonDef() and handed to Nexus.
AddonDefinition_t AddonDef{};

// Forward declarations — these functions are defined later in this file.
void AddonLoad(AddonAPI_t* aApi);
void AddonUnload();

// ---------------------------------------------------------------------------
// GetAddonDef
// ---------------------------------------------------------------------------
// Nexus calls this function when it first discovers the DLL.
// We fill out every field of the AddonDefinition_t struct here so Nexus
// knows who we are, what version we are, and which callbacks to call.
// The function is exported as a plain C symbol so Nexus can find it
// regardless of C++ name mangling.
// ---------------------------------------------------------------------------
extern "C" __declspec(dllexport) AddonDefinition_t* GetAddonDef()
{
    AddonDef.Signature    = 0x53573200;           // Unique numeric ID for this addon
    AddonDef.APIVersion   = NEXUS_API_VERSION;    // Nexus API version this addon was built against
    AddonDef.Name         = "Split Wars 2";
    AddonDef.Version      = {1, 0,2,0};
    AddonDef.Author       = "Xenophy.2716";
    AddonDef.Description  = "A speedrun timer with coordinate-based triggers.";
    AddonDef.Load         = AddonLoad;            // Called once when the addon is loaded
    AddonDef.Unload       = AddonUnload;          // Called once when the addon is unloaded
    AddonDef.Flags        = AF_None;
    AddonDef.Provider     = UP_GitHub;            // Where Nexus should look for updates
    AddonDef.UpdateLink   = "https://github.com/Xen0phy/Split-Wars-2";

    return &AddonDef;
}

// ---------------------------------------------------------------------------
// OnAddonLoaded / OnAddonUnloaded
// ---------------------------------------------------------------------------
// Nexus broadcasts EV_ADDON_LOADED and EV_ADDON_UNLOADED whenever any addon
// is loaded or unloaded at runtime (including after the initial load pass).
// aEventArgs points to the integer signature of the addon that changed.
//
// We listen for RTAPI_SIG specifically so RTAPIData stays valid across
// hot-load/unload cycles — e.g. if the user enables RTAPI after Split Wars 2
// is already running, or disables it mid-session.  Without these handlers,
// RTAPIData would remain a stale pointer after RTAPI unloads.
// ---------------------------------------------------------------------------
void OnAddonLoaded(void* aEventArgs)
{
    int* sig = (int*)aEventArgs;
    if (!sig) return;
    if (*sig == RTAPI_SIG)
        RTAPIData = (RTAPI::RealTimeData*)APIDefs->DataLink_Get(DL_RTAPI);
}

void OnAddonUnloaded(void* aEventArgs)
{
    int* sig = (int*)aEventArgs;
    if (!sig) return;
    if (*sig == RTAPI_SIG)
        RTAPIData = nullptr;
}

// ---------------------------------------------------------------------------
// Settings helpers
// These two functions translate between the flat Settings struct (which is
// what gets serialised to disk) and the individual global booleans / enums
// used throughout the rest of the code.
// ---------------------------------------------------------------------------

// Push a loaded Settings object into the global variables.
static void ApplySettings(const Settings& s)
{
    ShowTimer        = s.ShowTimer;
    ShowConfig       = s.ShowConfig;
    ShowZones        = s.ShowZones;
    ZoneFadeEnd      = s.ZoneFadeEnd;
    ZoneFadeStart    = s.ZoneFadeStart;
    ShowDebug        = s.ShowDebug;
    TimerDisplayMode = (TimerMode)s.TimerDisplayMode;
    CompactMode      = s.CompactMode;
    ShowHistory      = s.ShowHistory;
    ShowGrandTotal   = s.ShowGrandTotal;
    ShowRouteBrowser = s.ShowRouteBrowser;
    MaxHistoryRuns   = s.MaxHistoryRuns;
    PreferredSource  = (EDataSource)s.DataSource;
    std::copy(s.ColorStart,      s.ColorStart      + 3, ColorStart);
    std::copy(s.ColorGoal,       s.ColorGoal       + 3, ColorGoal);
    std::copy(s.ColorCheckpoint, s.ColorCheckpoint + 3, ColorCheckpoint);
    std::copy(s.ColorNull,       s.ColorNull       + 3, ColorNull);
    std::copy(s.ColorAhead,      s.ColorAhead      + 3, ColorAhead);
    std::copy(s.ColorBehind,     s.ColorBehind     + 3, ColorBehind);
    std::copy(s.ColorBestRow,    s.ColorBestRow    + 3, ColorBestRow);
    ConfigWindowW  = s.ConfigWindowW;
    ConfigWindowH  = s.ConfigWindowH;
    HistoryWindowW = s.HistoryWindowW;
    HistoryWindowH = s.HistoryWindowH;
    BrowserWindowW = s.BrowserWindowW;
    BrowserWindowH = s.BrowserWindowH;
}

// Snapshot the current global variables into a Settings struct ready for saving.
static Settings GatherSettings()
{
    Settings s;
    s.ShowTimer        = ShowTimer;
    s.ShowConfig       = ShowConfig;
    s.ShowZones        = ShowZones;
    s.ZoneFadeStart    = ZoneFadeStart;
    s.ZoneFadeEnd      = ZoneFadeEnd;
    s.ShowDebug        = ShowDebug;
    s.TimerDisplayMode = (int)TimerDisplayMode;
    s.CompactMode      = CompactMode;
    s.ShowHistory      = ShowHistory;
    s.ShowGrandTotal   = ShowGrandTotal;
    s.ShowRouteBrowser = ShowRouteBrowser;
    s.MaxHistoryRuns   = MaxHistoryRuns;
    s.DataSource       = (int)PreferredSource;
    std::copy(ColorStart,      ColorStart      + 3, s.ColorStart);
    std::copy(ColorGoal,       ColorGoal       + 3, s.ColorGoal);
    std::copy(ColorCheckpoint, ColorCheckpoint + 3, s.ColorCheckpoint);
    std::copy(ColorNull,       ColorNull       + 3, s.ColorNull);
    std::copy(ColorAhead,      ColorAhead      + 3, s.ColorAhead);
    std::copy(ColorBehind,     ColorBehind     + 3, s.ColorBehind);
    std::copy(ColorBestRow,    ColorBestRow    + 3, s.ColorBestRow);
    s.ConfigWindowW  = ConfigWindowW;
    s.ConfigWindowH  = ConfigWindowH;
    s.HistoryWindowW = HistoryWindowW;
    s.HistoryWindowH = HistoryWindowH;
    s.BrowserWindowW = BrowserWindowW;
    s.BrowserWindowH = BrowserWindowH;
    return s;
}

// ---------------------------------------------------------------------------
// SaveCurrentSettings
// ---------------------------------------------------------------------------
// Public wrapper called by addon_options.cpp to persist the current globals
// to disk without exposing GatherSettings() or the Settings struct outside
// this file.
// ---------------------------------------------------------------------------
void SaveCurrentSettings()
{
    SaveSettings(AddonDir, GatherSettings());
}

// ---------------------------------------------------------------------------
// AddonQuickAccessMenu
// ---------------------------------------------------------------------------
// Draws the right-click context menu that appears when the player right-clicks
// the Split Wars 2 icon in the Nexus Quick Access bar.  Each item is a
// checkbox-style menu entry that toggles the matching window directly.
// ---------------------------------------------------------------------------
static void AddonQuickAccessMenu()
{
    if (ImGui::MenuItem("Timer",         nullptr, &ShowTimer))        {}
    ImGui::Separator();
    if (ImGui::MenuItem("Route Config",  nullptr, &ShowConfig))       {}
    if (ImGui::MenuItem("History",       nullptr, &ShowHistory))      {}
    if (ImGui::MenuItem("Route Browser", nullptr, &ShowRouteBrowser)) {}
    ImGui::Separator();
    if (ImGui::MenuItem("Checkpoints",   nullptr, &ShowZones))        {}
}

// ---------------------------------------------------------------------------
// HandleIdentityUpdate
// ---------------------------------------------------------------------------
// Nexus fires this event whenever the player's identity changes (character
// swap, FOV slider, etc.).  We only care about FOV so we can pass it on to
// the world-space overlay renderer (worldrender.cpp).
// ---------------------------------------------------------------------------
void HandleIdentityUpdate(void* aEventArgs)
{
    if (!aEventArgs) return;
    Mumble::Identity* identity = (Mumble::Identity*)aEventArgs;
    SetMumbleFOV(identity->FOV);
}

// ---------------------------------------------------------------------------
// OnCombatEvent
// ---------------------------------------------------------------------------
// Called by Nexus for every ArcDPS combat event relayed via the integration bridge.
// aEventArgs is a pointer to an EvCombatData struct (squad and local use the same layout).
// ---------------------------------------------------------------------------
struct EvCombatData
{
    ArcDPS::CombatEvent* ev;
    ArcDPS::AgentShort*  src;
    ArcDPS::AgentShort*  dst;
    const char*          skillname;
    uint64_t             id;
    uint64_t             revision;
};

static void OnCombatEvent(void* aEventArgs)
{
    if (!aEventArgs) return;
    EvCombatData* data = (EvCombatData*)aEventArgs;

    // Target changed: ev is null, src->Specialization == 1
    if (!data->ev)
    {
        if (data->src && data->src->Specialization == 1)
        {
            LastTargetID = data->src->ID;
            HasTarget    = true;
        }
        return;
    }

    // Only direct strike events
    if (data->ev->IsStatechange != ArcDPS::CBTS_NONE) return;
    if ((ArcDPS::ECombatResult)data->ev->Result != ArcDPS::CBTR_KILLINGBLOW) return;

    HasKillingBlow              = true;
    LastKillingBlow.Time        = data->ev->Time;
    LastKillingBlow.SourceAgent = data->ev->SourceAgent;
    LastKillingBlow.DestAgent   = data->ev->DestinationAgent;
    LastKillingBlow.IFF         = (ArcDPS::EIsFriendFoe)data->ev->IFF;
    if (data->dst && data->dst->Name)
        strncpy(LastKillingBlow.DestName, data->dst->Name, sizeof(LastKillingBlow.DestName) - 1);
    else
        LastKillingBlow.DestName[0] = '\0';
}

// ---------------------------------------------------------------------------
// AddonLoad
// ---------------------------------------------------------------------------
// Called once by Nexus after the DLL is loaded.  This is where we do all
// one-time setup:
//   1. Store the API pointer and hook into ImGui's allocator so our ImGui
//      calls share the same heap as the host process.
//   2. Grab the MumbleLink shared-memory pointer (always-present fallback
//      for position, map, FOV, and IsMapOpen).
//   3. Attempt to grab the RTAPI data pointer (optional higher-accuracy
//      source; null if RTAPI is not installed).
//   4. Subscribe to EV_ADDON_LOADED/UNLOADED to keep RTAPIData valid if
//      RTAPI is hot-loaded or unloaded mid-session.
//   5. Load persisted settings from disk if a settings file exists.
//   6. Register the render and options callbacks with Nexus.
//   7. Subscribe to EV_MUMBLE_IDENTITY_UPDATED (for camera FOV changes).
//   8. Upload the hotbar icon textures and add the Quick Access button.
//   9. Register all keybinds (defined in addon.cpp).
// ---------------------------------------------------------------------------
void AddonLoad(AddonAPI_t* aApi)
{
    APIDefs = aApi;

    // Point ImGui at the host's context and memory allocator so all ImGui
    // state is shared — without this, font atlases and style settings would
    // be duplicated and potentially crash.
    ImGui::SetCurrentContext((ImGuiContext*)APIDefs->ImguiContext);
    ImGui::SetAllocatorFunctions(
        (void* (*)(size_t, void*))APIDefs->ImguiMalloc,
        (void(*)(void*, void*))APIDefs->ImguiFree
    );

    // MumbleLink is a shared-memory block that GW2 writes every frame with
    // player position, map ID, combat state, and more.  It is always the
    // fallback data source and the sole source for IsMapOpen and FOV.
    MumbleLink = (Mumble::Data*)APIDefs->DataLink_Get("DL_MUMBLE_LINK");

    // RTAPIData is an optional higher-accuracy data source provided by the
    // RTAPI addon.  Null if RTAPI is not installed or not yet loaded.
    // UpdateGameState() selects the active source each frame based on
    // PreferredSource and whether RTAPIData is non-null.
    RTAPIData = (RTAPI::RealTimeData*)APIDefs->DataLink_Get(DL_RTAPI);

    // Subscribe to addon lifecycle events so RTAPIData is kept in sync if
    // RTAPI is loaded or unloaded after Split Wars 2 is already running.
    APIDefs->Events_Subscribe("EV_ADDON_LOADED",   OnAddonLoaded);
    APIDefs->Events_Subscribe("EV_ADDON_UNLOADED", OnAddonUnloaded);
    AddonDir   = GetAddonDir();

    // Apply persisted settings if a settings file is found; otherwise the
    // compiled-in defaults from shared.h remain active.
    Settings s;
    if (LoadSettings(AddonDir, s))
        ApplySettings(s);

    // Register the per-frame render callback and the Nexus options panel callback.
    APIDefs->GUI_Register(RT_Render, AddonRender);
    APIDefs->GUI_Register(RT_OptionsRender, AddonOptions);
 
    // Subscribe to identity updates so we can keep CameraFOV in sync.
    APIDefs->Events_Subscribe("EV_MUMBLE_IDENTITY_UPDATED", HandleIdentityUpdate);

    APIDefs->Events_Subscribe("EV_ARCDPS_COMBATEVENT_LOCAL_RAW",  OnCombatEvent);
    APIDefs->Events_Subscribe("EV_ARCDPS_COMBATEVENT_SQUAD_RAW",  OnCombatEvent);

    // Load hotbar icon textures from embedded memory and register QuickAccess shortcut.
    // The icon images are baked into hotbar_icon.h as byte arrays at compile time.
    APIDefs->Textures_GetOrCreateFromMemory(
        "TEX_SW2_HOTBAR",
        (void*)g_HotbarIconData,
        g_HotbarIconData_size
    );
    APIDefs->Textures_GetOrCreateFromMemory(
        "TEX_SW2_HOTBAR_HOVER",
        (void*)g_HotbarIconHoverData,
        g_HotbarIconHoverData_size
    );
    // The Quick Access button uses the hotbar toggle keybind action so a left-click
    // on the icon triggers the same show/hide behaviour as the keybind.
    APIDefs->QuickAccess_Add(
        "QA_SW2_HIDE_TOGGLE",
        "TEX_SW2_HOTBAR",
        "TEX_SW2_HOTBAR_HOVER",
        "SW2 Toggle Hide All Windows",
        "Split Wars 2: Hide/Restore Windows"
    );
    APIDefs->QuickAccess_AddContextMenu("QA_SW2_CTXMENU", "QA_SW2_HIDE_TOGGLE", AddonQuickAccessMenu);

    // Register all keybinds defined in the table above.
    RegisterKeybinds();

    APIDefs->Log(LOGL_INFO, "Split Wars 2", "Split Wars 2 loaded.");
}

// ---------------------------------------------------------------------------
// AddonUnload
// ---------------------------------------------------------------------------
// Called by Nexus just before the DLL is unloaded (e.g. on game exit or when
// the user disables the addon).  We mirror everything done in AddonLoad:
// save settings, deregister all keybinds, remove the Quick Access button,
// and unsubscribe from all events (including EV_ADDON_LOADED/UNLOADED for
// RTAPI lifecycle tracking) and render callbacks so no dangling function
// pointers remain inside Nexus after our DLL is gone.
// ---------------------------------------------------------------------------
void AddonUnload()
{
    SaveSettings(AddonDir, GatherSettings());
    
    // Deregister all keybinds.
    DeregisterKeybinds();

    APIDefs->QuickAccess_Remove("QA_SW2_HIDE_TOGGLE");
    APIDefs->QuickAccess_RemoveContextMenu("QA_SW2_CTXMENU");
    APIDefs->GUI_Deregister(AddonRender);
    APIDefs->GUI_Deregister(AddonOptions);

    APIDefs->Events_Unsubscribe("EV_ARCDPS_COMBATEVENT_LOCAL_RAW",  OnCombatEvent);
    APIDefs->Events_Unsubscribe("EV_ARCDPS_COMBATEVENT_SQUAD_RAW",  OnCombatEvent);

    // Unsubscribe all event listeners registered in AddonLoad.
    APIDefs->Events_Unsubscribe("EV_MUMBLE_IDENTITY_UPDATED", HandleIdentityUpdate);
    APIDefs->Events_Unsubscribe("EV_ADDON_LOADED",            OnAddonLoaded);
    APIDefs->Events_Unsubscribe("EV_ADDON_UNLOADED",          OnAddonUnloaded);

    APIDefs->Log(LOGL_INFO, "Split Wars 2", "Split Wars 2 unloaded.");

    // Clear route state so a subsequent reload starts clean.
    CurrentRoute         = Route{};
    CurrentRouteName     = "";
    CurrentRouteFilepath = "";
    CurrentHistoryPath   = "";
    HistoryRuns.clear();
    SegmentRecords.clear();
    BestRun.clear();
    BestRunIndex = -1;
    FullReset();
}