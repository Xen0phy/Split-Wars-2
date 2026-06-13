// render_speedo.h
// Speedometer overlay for Split Wars 2.
//
// Settings (all persisted via settings_table.h):
//
//   ShowSpeedo          — toggles the window
//   SpeedUnitMph        — false = km/h, true = mph
//   SpeedoTachometer    — false = numeric display, true = tachometer
//   SpeedoEditMode      — false = click-through, true = moveable with border
//
//   SpeedoRadius        — distance from C to the arc in px
//                         below 5px = straight line mode
//   SpeedoAngle         — orientation of the arc midpoint / straight line (degrees, 0-360)
//   SpeedoArcLength     — total length of the arc in px
//
//   SpeedoNeedleVisible — show/hide the needle
//   SpeedoNeedleWidth   — needle line width in px
//   SpeedoArcWidth      — filled sweep arc width in px
//   SpeedoArcBgWidth    — background arc width in px
//
//   SpeedoLabelVisible  — show/hide the speed number label
//   SpeedoSpringK       — needle spring stiffness
//   SpeedoDamping       — needle spring damping

#pragma once

void RenderSpeedoWindow();