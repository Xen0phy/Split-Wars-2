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
// [Speedo] — replace existing [Speedo] block in settings_table.h with this
SETTING(Speedo, ShowSpeedo,          bool,  false)
SETTING(Speedo, SpeedUnitMph,        bool,  false)
SETTING(Speedo, SpeedoTachometer,    bool,  false)
SETTING(Speedo, SpeedoEditMode,      bool,  false)
SETTING(Speedo, SpeedoArcAngle,      float, 120.0f)  // sweep in degrees; small=flat, 360=circle
SETTING(Speedo, SpeedoArcLength,     float, 200.0f)  // arc length in px
SETTING(Speedo, SpeedoAngle,         float, 270.0f)  // rotation; 270=arc opens upward
SETTING(Speedo, SpeedoPDistance,     float, 0.0f)    // needle origin offset from C (0=center)
SETTING(Speedo, SpeedoNeedleVisible, bool,  true)
SETTING(Speedo, SpeedoNeedleWidth,   float, 1.5f)
SETTING(Speedo, SpeedoArcWidth,      float, 4.0f)
SETTING(Speedo, SpeedoArcBgWidth,    float, 2.0f)
SETTING(Speedo, SpeedoLabelVisible,  bool,  true)
SETTING(Speedo, SpeedoSpringK,       float, 12.0f)
SETTING(Speedo, SpeedoDamping,       float, 6.0f)
SETTING(Speedo, SpeedoWindowX, float, -1.0f)  // -1 = not yet set
SETTING(Speedo, SpeedoWindowY, float, -1.0f)