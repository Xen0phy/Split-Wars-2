// stream_fonts.h
// Manages dynamically-resizable streamer-mode / speedo fonts.
//
// On startup, scans <AddonDir>/fonts/ for .ttf/.otf files (up to
// STREAM_FONT_MAX_FILES) and remembers their stems + paths. Fonts are NOT
// pre-baked at a fixed grid of sizes. Instead, each (stem, role) pair gets
// exactly one Nexus font registration, created lazily on first request and
// then resized in place via Nexus's Fonts_Resize whenever the caller asks
// for a different size — so any arbitrary pixel size is supported, the
// same way Nexus's own UI fonts can be freely resized while staying crisp.
//
// "Role" exists because a few call sites (the streamer timer) need several
// independently-sized fonts from the SAME stem at the SAME time every
// frame (main digits, millis, header, etc.). Keying purely by size doesn't
// work once size becomes a mutable property instead of a lookup key, since
// derived sizes (main-4, main-8) shift together as the user changes the
// main size — a role gives each of those a stable identity to resize.
//
// Usage:
//   InitStreamFonts();                       // call once from AddonLoad after APIDefs ready
//   GetStreamFont(name, role, size)          // returns ImFont* or nullptr if not ready
//   GetStreamFontNames()                     // sorted list of discovered font name stems
//   ReleaseStreamFonts();                    // call from AddonUnload

#pragma once

#include "imgui.h"
#include <string>
#include <vector>

static constexpr int STREAM_FONT_MAX_FILES = 5;

// Identifies which independently-sized font a caller needs from a given
// stem. Add new roles here as new callers need their own concurrently-sized
// slot; each role gets exactly one Nexus registration per stem, resized in
// place rather than re-registered when its size changes.
enum class EStreamFontRole
{
    StreamerMain,        // streamer timer: running h:m:s
    StreamerMainMillis,  // streamer timer: running .xxx / comparison h:m:s (shared)
    StreamerCompMillis,  // streamer timer: comparison .xxx
    StreamerHeader,      // streamer timer: title bar + buttons
    SpeedoLabel,         // speedo: numeric speed label
};

// Scan fonts folder and remember discovered files. Does not register any
// fonts with Nexus yet — registration happens lazily per (stem, role) the
// first time GetStreamFont requests it. Safe to call multiple times;
// subsequent calls are no-ops.
void InitStreamFonts();

// Release all registered fonts from Nexus.
void ReleaseStreamFonts();

// Returns the ImFont* for the given font stem name, role, and pixel size.
// If this (stem, role) hasn't been requested before, registers it with
// Nexus at `size` and returns nullptr until the callback delivers it.
// If it has been requested before at a different size, resizes it in
// place via Nexus and keeps returning the previous pointer/size's font
// until the resize callback delivers the updated one.
// Returns nullptr if `name` doesn't match any discovered font stem.
ImFont* GetStreamFont(const std::string& name, EStreamFontRole role, float size);

// Returns the list of discovered font name stems (e.g. "Roboto-Regular").
// Empty until InitStreamFonts() has been called.
const std::vector<std::string>& GetStreamFontNames();

// Convenience wrapper used by the renderers -- returns the currently
// selected font (StreamerFontName / StreamerFontSize from settings) for
// the StreamerMain role.
ImFont* GetStreamerFont();