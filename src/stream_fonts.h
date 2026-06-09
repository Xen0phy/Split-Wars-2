#pragma once
// stream_fonts.h
// Manages the streamer-mode font atlas.
//
// On startup, scans <AddonDir>/fonts/ for .ttf/.otf files and registers
// every combination of (file x size) with Nexus, where sizes run from
// STREAM_FONT_SIZE_MIN to STREAM_FONT_SIZE_MAX in STREAM_FONT_SIZE_STEP steps.
// Up to STREAM_FONT_MAX_FILES fonts are loaded.
//
// Usage:
//   InitStreamFonts();              // call once from AddonLoad after APIDefs ready
//   GetStreamFont(name, size)       // returns ImFont* or nullptr if not ready
//   GetStreamFontNames()            // sorted list of discovered font name stems
//   ReleaseStreamFonts();           // call from AddonUnload

#include "imgui.h"
#include <string>
#include <vector>

static constexpr float STREAM_FONT_SIZE_MIN  = 24.0f;
static constexpr float STREAM_FONT_SIZE_MAX  = 48.0f;
static constexpr float STREAM_FONT_SIZE_STEP =  2.0f;
static constexpr int   STREAM_FONT_MAX_FILES =  5;

// Scan fonts folder and register all sizes with Nexus.
// Safe to call multiple times; subsequent calls are no-ops.
void InitStreamFonts();

// Release all registered fonts from Nexus.
void ReleaseStreamFonts();

// Returns the ImFont* for the given font stem name and pixel size.
// Returns nullptr if not yet received from Nexus or not found.
ImFont* GetStreamFont(const std::string& name, float size);

// Returns the list of discovered font name stems (e.g. "Roboto-Regular").
// Empty until InitStreamFonts() has been called.
const std::vector<std::string>& GetStreamFontNames();

// Convenience wrapper used by the renderers -- returns the currently
// selected font (StreamerFontName / StreamerFontSize from settings).
ImFont* GetStreamerFont();