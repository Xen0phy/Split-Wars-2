// settings_table.inl
// Note: renamed as .h because of .clangd
// Single source of truth for every persisted setting.
//
// Include this file with SETTING / SETTING_ARRAY / SETTING_ENUM defined to
// generate globals, INI I/O, or anything else that needs to touch every setting.
// After each include site, #undef all three macros.
//
// Macro signatures:
//   SETTING      (Section, Key, Type, Default)
//   SETTING_ARRAY(Section, Key, Size, Defaults)   // Defaults = braced list, e.g. {1.0f, 0.0f, 0.0f}
//   SETTING_ENUM (Section, Key, EnumType, StoredType, Default)
//
// Section maps to an INI [Section] heading.
// Key     is both the C++ variable name and the INI key name.

#ifndef SETTING
#define SETTING(S, Key, Type, Default)
#define SETTING_ARRAY(S, Key, Size, Defaults)
#define SETTING_ENUM(S, Key, EnumType, ST, Default)
#define SETTING_STRING(S, Key, Default)
#endif

#ifndef ARR
#define ARR(...) __VA_ARGS__
#endif

// ---------------------------------------------------------------------------
// [Version]
// ---------------------------------------------------------------------------
SETTING       (Version,     LastKnownVersion,       int,   0)

// ---------------------------------------------------------------------------
// [DataSource]
// ---------------------------------------------------------------------------
SETTING_ENUM  (DataSource, PreferredSource, EDataSource, int, EDataSource::Default)

// ---------------------------------------------------------------------------
// [UI]
// ---------------------------------------------------------------------------
SETTING       (UI,          ShowTimer,              bool,  true)
SETTING       (UI,          ShowConfig,             bool,  true)
SETTING       (UI,          ShowZones,              bool,  true)
SETTING       (UI,          ShowHistory,            bool,  false)
SETTING       (UI,          ShowGrandTotal,         bool,  false)
SETTING       (UI,          ShowRouteBrowser,       bool,  false)

// ---------------------------------------------------------------------------
// [Zones]
// ---------------------------------------------------------------------------
SETTING       (Zones,       ZoneFadeStart,          float, 50.0f)
SETTING       (Zones,       ZoneFadeEnd,            float, 150.0f)

// ---------------------------------------------------------------------------
// [Timer]
// ---------------------------------------------------------------------------
SETTING_ENUM  (Timer, TimerDisplayMode, TimerMode, int,   TimerMode::Split)
SETTING       (Timer,       CompactMode,            bool,  false)
SETTING       (Timer,       MaxHistoryRuns,         int,   10)
SETTING       (Timer,       FractalRota,            bool,  false) 

// ---------------------------------------------------------------------------
// [Colors]
// ---------------------------------------------------------------------------
SETTING_ARRAY (Colors,      ColorStart,             3,     ARR( 0.2f,  1.0f,  0.2f))
SETTING_ARRAY (Colors,      ColorGoal,              3,     ARR( 0.2f,  0.5f,  1.0f))
SETTING_ARRAY (Colors,      ColorCheckpoint,        3,     ARR( 1.0f,  1.0f,  1.0f))
SETTING_ARRAY (Colors,      ColorNull,              3,     ARR( 1.0f,  0.6f,  0.0f))
SETTING_ARRAY (Colors,      ColorAhead,             3,     ARR( 0.2f,  1.0f,  0.2f))
SETTING_ARRAY (Colors,      ColorBehind,            3,     ARR( 1.0f,  0.3f,  0.3f))
SETTING_ARRAY (Colors,      ColorBestRow,           3,     ARR( 0.2f,  0.5f,  0.2f))

// ---------------------------------------------------------------------------
// [Windows]
// ---------------------------------------------------------------------------
SETTING       (Windows,     ConfigWindowW,          float, 800.0f)
SETTING       (Windows,     ConfigWindowH,          float, 400.0f)
SETTING       (Windows,     HistoryWindowW,         float, 400.0f)
SETTING       (Windows,     HistoryWindowH,         float, 400.0f)
SETTING       (Windows,     BrowserWindowW,         float, 400.0f)
SETTING       (Windows,     BrowserWindowH,         float, 400.0f)

// ---------------------------------------------------------------------------
// [Streamer]
// ---------------------------------------------------------------------------
SETTING       (Streamer,    StreamerMode,           bool,  false)
SETTING       (Streamer,    StreamerFontSize,       int,   32)
SETTING       (Streamer,    StreamerHeaderFontSize, int,   20)
SETTING       (Streamer,    ShowRunningMillis,      bool,  false)
SETTING       (Streamer,    ShowCMFill,             bool,  true)
SETTING       (Streamer,    ShowCMShadow,           bool,  true)
SETTING_ARRAY (Streamer,    StreamerAnchor,         2,     ARR(10.0f, 10.0f))
SETTING_STRING(Streamer,    StreamerFontName,       "")

// ---------------------------------------------------------------------------
// [CrashMode]
// ---------------------------------------------------------------------------
SETTING       (CrashMode,   CrashMode,              bool,  false)
SETTING_ARRAY (CrashMode,   CMDigitShadowColor,     3,     ARR( 0.0f,  0.0f,  0.0f))
SETTING_ARRAY (CrashMode,   CMDigitShadowOffset,    2,     ARR( 0.0f,  1.0f))
SETTING_ARRAY (CrashMode,   CMDigitFillColor,       3,     ARR( 0.0f,  0.0f,  0.0f))
SETTING_ARRAY (CrashMode,   CMDigitBaseColor,       3,     ARR( 1.0f,  0.45f, 0.0f))
SETTING_ARRAY (CrashMode,   CMDigitOverlay,         3,     ARR( 0.9f,  0.0f,  0.0f))

// ---------------------------------------------------------------------------
// [Speedometer]
// ---------------------------------------------------------------------------
SETTING       (Speedometer,      ShowSpeedo,             bool,  true)
SETTING       (Speedometer,      SpeedUnitMph,           bool,  false)   // false = km/h, true = mph
SETTING       (Speedometer,      SpeedoTachometer,       bool,  false)
SETTING       (Speedometer,      SpeedoRadius,           float, 80.0f)
