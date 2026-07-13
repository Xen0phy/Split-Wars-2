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
SETTING        (Version,      LastKnownVersion,       int,   0)

// ---------------------------------------------------------------------------
// [DataSource]
// ---------------------------------------------------------------------------
SETTING_ENUM   (DataSource, PreferredSource, EDataSource, int, EDataSource::Default)

// ---------------------------------------------------------------------------
// [UI]
// ---------------------------------------------------------------------------
SETTING        (UI,           ShowTimer,              bool,  true)
SETTING        (UI,           ShowConfig,             bool,  true)
SETTING        (UI,           ShowZones,              bool,  true)
SETTING        (UI,           ShowHistory,            bool,  false)
SETTING        (UI,           ShowGrandTotal,         bool,  false)
SETTING        (UI,           ShowRouteBrowser,       bool,  false)
SETTING        (UI,           ShowEvaluation,         bool,  false)

// ---------------------------------------------------------------------------
// [Zones]
// ---------------------------------------------------------------------------
SETTING        (Zones,        ZoneFadeStart,          float, 50.0f)
SETTING        (Zones,        ZoneFadeEnd,            float, 150.0f)

// ---------------------------------------------------------------------------
// [Timer]
// ---------------------------------------------------------------------------
SETTING_ENUM   (Timer, TimerDisplayMode, TimerMode, int,   TimerMode::Split)
SETTING        (Timer,        CompactMode,            bool,  false)
SETTING        (Timer,        FractalRota,            bool,  false) 
SETTING        (Timer,        ShowStartSplit,         bool,  false)

// ---------------------------------------------------------------------------
// [Colors]
// ---------------------------------------------------------------------------
SETTING_ARRAY  (Colors,       ColorStart,             3,     ARR( 0.2f,  1.0f,  0.2f))
SETTING_ARRAY  (Colors,       ColorGoal,              3,     ARR( 0.2f,  0.5f,  1.0f))
SETTING_ARRAY  (Colors,       ColorCheckpoint,        3,     ARR( 1.0f,  1.0f,  1.0f))
SETTING_ARRAY  (Colors,       ColorNull,              3,     ARR( 1.0f,  0.6f,  0.0f))
SETTING_ARRAY  (Colors,       ColorAhead,             3,     ARR( 0.2f,  1.0f,  0.2f))
SETTING_ARRAY  (Colors,       ColorBehind,            3,     ARR( 1.0f,  0.3f,  0.3f))
SETTING_ARRAY  (Colors,       ColorBestRow,           3,     ARR( 0.2f,  0.5f,  0.2f))

// Evaluation Colors
SETTING        (Colors,       CoreColorHue,           float, 0.573f)
SETTING        (Colors,       RotatingColorHue,       float, 0.093f)
SETTING        (Colors,       ChildColorHue,          float, 0.772f)
SETTING_ARRAY  (Colors,       HoverColor,             3,     ARR(1.000f, 0.310f, 0.690f))

// ---------------------------------------------------------------------------
// [Windows]
// ---------------------------------------------------------------------------
SETTING        (Windows,      ConfigWindowW,          float, 800.0f)
SETTING        (Windows,      ConfigWindowH,          float, 400.0f)
SETTING        (Windows,      HistoryWindowW,         float, 400.0f)
SETTING        (Windows,      HistoryWindowH,         float, 400.0f)
SETTING        (Windows,      BrowserWindowW,         float, 400.0f)
SETTING        (Windows,      BrowserWindowH,         float, 400.0f)
SETTING        (Windows,      EvaluationWindowW,      float, 930.0f)
SETTING        (Windows,      EvaluationWindowH,      float, 640.0f)

// ---------------------------------------------------------------------------
// [Evaluation]
// ---------------------------------------------------------------------------
SETTING        (Windows,      BarWidth,               float, 42.0f)
SETTING        (Windows,      BarGap,                 float, 15.0f)


// ---------------------------------------------------------------------------
// [Streamer]
// ---------------------------------------------------------------------------
SETTING        (Streamer,     StreamerMode,           bool,  false)
SETTING        (Streamer,     StreamerFontSize,       int,   32)
SETTING        (Streamer,     StreamerHeaderFontSize, int,   20)
SETTING        (Streamer,     ShowRunningMillis,      bool,  false)
SETTING        (Streamer,     ShowCMFill,             bool,  true)
SETTING        (Streamer,     ShowCMShadow,           bool,  true)
SETTING_ARRAY  (Streamer,     StreamerAnchor,         2,     ARR(10.0f, 10.0f))
SETTING_STRING (Streamer,     StreamerFontName,       "")

// ---------------------------------------------------------------------------
// [CrashMode]
// ---------------------------------------------------------------------------
SETTING        (CrashMode,    CrashMode,              bool,  false)
SETTING_ARRAY  (CrashMode,    CMDigitShadowColor,     3,     ARR( 0.0f,  0.0f,  0.0f))
SETTING_ARRAY  (CrashMode,    CMDigitShadowOffset,    2,     ARR( 0.0f,  1.0f))
SETTING_ARRAY  (CrashMode,    CMDigitFillColor,       3,     ARR( 0.0f,  0.0f,  0.0f))
SETTING_ARRAY  (CrashMode,    CMDigitBaseColor,       3,     ARR( 1.0f,  0.45f, 0.0f))
SETTING_ARRAY  (CrashMode,    CMDigitOverlay,         3,     ARR( 0.9f,  0.0f,  0.0f))

// ---------------------------------------------------------------------------
// [Speedo] — replace existing [Speedo] block in settings_table.h with this
// ---------------------------------------------------------------------------

// General
SETTING        (Speedo,       ShowSpeedo,                 bool,   false)
// Mount visibility mask — bit N = show speedo when on mount index N
// Bit 0 = unmounted, bits 1-10 = mount order from EMountIndex
// -1 (all bits set) means "show on all", 0 means "controlled only by ShowSpeedo"
SETTING        (Speedo,       SpeedoMountMask,            int,   -1)
SETTING        (Speedo,       SpeedoTachometer,           bool,   false)
SETTING        (Speedo,       SpeedoEditMode,             bool,   false)

// Label
SETTING        (Speedo,       SpeedoLabelVisible,         bool,   true)
SETTING_STRING (Speedo,       SpeedoFontName,             "")
SETTING        (Speedo,       SpeedoFontSize,             float,  24.0f)

SETTING        (Speedo,       SpeedoShowUnit,             bool,   true)
SETTING        (Speedo,       SpeedUnit,                  int,    0)  // 0=km/h, 1=mph, 2=u/s

// Window position
SETTING        (Speedo,       SpeedoWindowX,              float, -1.0f)
SETTING        (Speedo,       SpeedoWindowY,              float, -1.0f)
SETTING        (Speedo,       SpeedoLabelX,               float,  100.0f)
SETTING        (Speedo,       SpeedoLabelY,               float,  300.0f)

// Geometry
SETTING        (SpeedoArc,    SpeedoArcRotation,          float,  270.0f)
SETTING        (SpeedoArc,    SpeedoArcAngle,             float,  60.0f)
SETTING        (SpeedoArc,    SpeedoArcLength,            float,  400.0f)

// Color + thickness stops (stop 1 always enabled at pos 0)
SETTING_ARRAY  (SpeedoArc,    SpeedoStop1Color,           4,      ARR(0.0f, 0.78f, 1.0f, 1.0f))
SETTING        (SpeedoArc,    SpeedoStop1Thickness,       float,  2.0f)

SETTING        (SpeedoArc,    SpeedoStop2Enabled,         bool,   false)
SETTING        (SpeedoArc,    SpeedoStop2Pos,             float,  0.5f)
SETTING_ARRAY  (SpeedoArc,    SpeedoStop2Color,           4,      ARR(0.0f, 1.0f, 0.4f, 1.0f))
SETTING        (SpeedoArc,    SpeedoStop2Thickness,       float,  6.0f)

SETTING        (SpeedoArc,    SpeedoStop3Enabled,         bool,   false)
SETTING        (SpeedoArc,    SpeedoStop3Pos,             float,  0.75f)
SETTING_ARRAY  (SpeedoArc,    SpeedoStop3Color,           4,      ARR(1.0f, 0.5f, 0.0f, 1.0f))
SETTING        (SpeedoArc,    SpeedoStop3Thickness,       float,  10.0f)

SETTING        (SpeedoArc,    SpeedoStop4Enabled,         bool,   false)
SETTING        (SpeedoArc,    SpeedoStop4Pos,             float,  1.0f)
SETTING_ARRAY  (SpeedoArc,    SpeedoStop4Color,           4,      ARR(1.0f, 0.0f, 0.0f, 1.0f))
SETTING        (SpeedoArc,    SpeedoStop4Thickness,       float,  20.0f)

SETTING        (SpeedoArc,    SpeedoGradientSmooth,       bool,   true)
// When true, the whole arc/line is drawn as a single color sampled at the
// current fill fraction (t), fading/stepping all at once as speed changes,
// instead of each point along the arc showing the color for its own
// position. SpeedoGradientSmooth still controls fade-vs-step in either mode.
SETTING        (SpeedoArc,    SpeedoGradientWholeArc,     bool,   false)

// Arc rendering (background arc)
SETTING        (SpeedoArc,    SpeedoArcBgOpacity,         float,  1.0f) // Opacity of the background arc/line only
SETTING        (SpeedoArc,    SpeedoArcBgWidth,           float,  2.0f)

// Needle
SETTING        (SpeedoNeedle, SpeedoNeedleVisible,        bool,   false)
SETTING        (SpeedoNeedle, SpeedoNeedleWidth,          float,  1.5f)

// Needle texture
SETTING        (SpeedoNeedle, SpeedoNeedleTexEnabled,     bool,   false)
SETTING        (SpeedoNeedle, SpeedoPDistance,            float,  0.0f)
SETTING_STRING (SpeedoNeedle, SpeedoNeedleTexPath,        "")
SETTING        (SpeedoNeedle, SpeedoNeedleTexScale,       float,  1.0f)
SETTING        (SpeedoNeedle, SpeedoNeedleTexAngleOffset, float,  180.0f)
// Pivot point inside the texture, in unscaled source-image pixels, measured
// from the image's top-left corner. This is the point that gets placed on
// the drawn needle's pivot P and rotated around. -1, -1 is a sentinel
// meaning "use w/2, h/2" (the image centre), since most needle textures
// are already drawn pivoting around their own centre.
SETTING        (SpeedoNeedle, SpeedoNeedleTexPivotX,      float, -1.0f)
SETTING        (SpeedoNeedle, SpeedoNeedleTexPivotY,      float, -1.0f)

// Physics
SETTING        (SpeedoNeedle, SpeedoSpringK,              float,  12.0f)
SETTING        (SpeedoNeedle, SpeedoDamping,              float,  6.0f)

// Peak hold
SETTING        (SpeedoNeedle, SpeedoPeakHoldEnabled,      bool,   false)
SETTING        (SpeedoNeedle, SpeedoPeakHoldTime,         float,  2.0f)
SETTING        (SpeedoNeedle, SpeedoPeakHoldSize,         float,  10.0f)

// Face texture
SETTING        (SpeedoFace,   SpeedoFaceEnabled,          bool,   false)
SETTING_STRING (SpeedoFace,   SpeedoFacePath,             "")
SETTING        (SpeedoFace,   SpeedoFaceScale,            float,  1.0f)
SETTING        (SpeedoFace,   SpeedoFaceX,                float,  100.0f)
SETTING        (SpeedoFace,   SpeedoFaceY,                float,  100.0f)