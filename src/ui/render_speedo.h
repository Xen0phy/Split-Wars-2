// render_speedo.h
// Speedometer overlay for Split Wars 2.
//
// Settings (all persisted via settings_table.h):
//
//   General
//   ShowSpeedo          — toggles the window
//   SpeedUnit           — 0=km/h, 1=mph, 2=u/s (game units/sec)
//   SpeedoShowUnit      — show/hide the unit suffix on the label
//   SpeedoTachometer    — false = numeric display, true = tachometer
//   SpeedoMountMask     — bitmask of mounts to show on (-1 = all, 0 = none)
//
//   Window / label position (set by dragging, not by a slider)
//   SpeedoEditMode      — false = click-through, true = moveable with border
//   SpeedoWindowX/Y     — tachometer window position (-1 = auto-center)
//   SpeedoLabelX/Y      — speed label position (right-edge anchor)
//
//   Geometry
//   SpeedoArcRotation   — rotation of the whole speedo (degrees, 0-360)
//   SpeedoArcAngle      — arc sweep in degrees (1-359); below 1 = straight line
//   SpeedoArcLength     — total length of the arc/line in px
//                         (radius is derived: SpeedoArcLength / SpeedoArcAngle)
//   SpeedoPDistance     — distance of the needle origin P from the arc
//
//   Needle
//   SpeedoNeedleVisible — show/hide the plain needle line
//   SpeedoNeedleWidth   — needle line width in px
//   SpeedoNeedleTexEnabled        — use a needle image instead of/alongside the line
//   SpeedoNeedleTexPath           — filename of the needle texture
//   SpeedoNeedleTexScale          — uniform scale of the needle texture
//   SpeedoNeedleTexAngleOffset    — rotation offset to align the texture's tip
//   SpeedoNeedleTexPivotX/Y       — pivot point inside the texture (-1 = image centre)
//   SpeedoSpringK       — needle spring stiffness
//   SpeedoDamping       — needle spring damping
//
//   Arc rendering
//   SpeedoOpacity        — opacity of the background (unfilled) arc/line only
//   SpeedoArcBgWidth     — background arc/line width in px
//   SpeedoGradientSmooth — blend between gradient stops vs hard step
//   SpeedoStop1Color/Thickness                  — stop 1, always enabled, fixed at pos 0
//   SpeedoStop2/3/4 Enabled/Pos/Color/Thickness — up to 3 additional gradient stops
//
//   Face texture
//   SpeedoFaceEnabled   — show a gauge-face image behind the arc
//   SpeedoFacePath      — filename of the face texture
//   SpeedoFaceScale     — uniform scale of the face texture
//   SpeedoFaceX/Y       — top-left position of the face texture
//
//   Peak hold
//   SpeedoPeakHoldEnabled — briefly hold the highest speed reached
//   SpeedoPeakHoldTime    — how long the peak is held before decaying
//   SpeedoPeakHoldSize    — size of the peak-hold marker
//
//   Label
//   SpeedoLabelVisible  — show/hide the speed number label
//   SpeedoFontName      — font used for the label ("" = default)
//   SpeedoFontSize      — font size for the label

#pragma once

#include <string>
#include <vector>

void RenderSpeedoWindow();

// Returns the sorted list of PNG/JPG filenames in the textures folder.
// Call ScanTextureFiles() to refresh after adding new files.
const std::vector<std::string>& GetSpeedoTextureNames();
void ScanTextureFiles();

// Returns the currently loaded needle texture's native pixel size via
// outW/outH, or sets both to 0 and returns false if no needle texture is
// loaded. Used by the options panel to display the image-centre default
// when SpeedoNeedleTexPivotX/Y == -1.
bool GetSpeedoNeedleTexSize(float& outW, float& outH);