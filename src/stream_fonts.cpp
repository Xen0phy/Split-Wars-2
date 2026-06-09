// stream_fonts.cpp
// Streamer-mode font manager.
//
// Scans <AddonDir>/fonts/ on startup, registers every .ttf/.otf file at
// every size from 20px (STREAM_FONT_ATLAS_MIN) to 48px in 2px steps with
// Nexus, and caches the resulting ImFont* pointers as Nexus delivers them
// via callbacks.  The atlas starts at 20px so that derived sizes used by
// the streamer timer (main − 2, main − 4) are always available even at the
// minimum user-selectable font size of 24px.
//
// Font identifiers registered with Nexus follow the pattern:
//   "SW2_STREAM_<STEM>_<SIZE>"
// e.g. "SW2_STREAM_Roboto-Regular_32"
//
// GetStreamerFont() returns the font matching the user's current
// StreamerFontName / StreamerFontSize settings.

#include "stream_fonts.h"
#include "shared.h"

#include <filesystem>
#include <algorithm>
#include <cstdio>

namespace fs = std::filesystem;

// ---------------------------------------------------------------------------
// Internal state
// ---------------------------------------------------------------------------
struct FontSlot
{
    std::string nexusId;  // identifier registered with Nexus
    std::string stem;     // font file stem, e.g. "Roboto-Regular"
    float       size;     // pixel size
    ImFont*     font;     // filled by Nexus callback; nullptr until ready
};

static std::vector<FontSlot>     s_Slots;
static std::vector<std::string>  s_Names;   // unique stems, sorted
static bool                      s_Initialised = false;

// ---------------------------------------------------------------------------
// Nexus callback
// Called by Nexus once per registered font when the atlas is ready (or when
// a re-atlas happens, e.g. when the user changes Nexus UI scale).
// The identifier lets us find the right slot to update.
// ---------------------------------------------------------------------------
static void OnFontReceived(const char* aIdentifier, void* aFont)
{
    for (auto& slot : s_Slots)
    {
        if (slot.nexusId == aIdentifier)
        {
            slot.font = (ImFont*)aFont;
            return;
        }
    }
}

// ---------------------------------------------------------------------------
// InitStreamFonts
// ---------------------------------------------------------------------------
void InitStreamFonts()
{
    if (s_Initialised) return;
    if (!APIDefs)      return;

    std::string fontsDir = GetAddonDir() + "\\fonts";

    // Collect font files (up to STREAM_FONT_MAX_FILES)
    std::vector<fs::path> files;
    std::error_code ec;
    if (fs::exists(fontsDir, ec))
    {
        for (auto& entry : fs::directory_iterator(fontsDir, ec))
        {
            if (!entry.is_regular_file()) continue;
            auto ext = entry.path().extension().string();
            // case-insensitive extension check
            std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
            if (ext == ".ttf" || ext == ".otf")
            {
                files.push_back(entry.path());
                if ((int)files.size() >= STREAM_FONT_MAX_FILES) break;
            }
        }
    }

    if (files.empty())
    {
        if (APIDefs)
            APIDefs->Log(LOGL_WARNING, "Split Wars 2",
                "StreamFonts: no fonts found in fonts/ folder.");
        return;
    }

    // Sort for deterministic dropdown order
    std::sort(files.begin(), files.end());

    // Register every file x size combination.
    // The atlas covers STREAM_FONT_ATLAS_MIN (20px) through STREAM_FONT_SIZE_MAX (48px)
    // so that derived sizes (main - 2, main - 4) are baked even when the user
    // selects the minimum user-facing size of 24px.
    int numSizes = (int)((STREAM_FONT_SIZE_MAX - STREAM_FONT_ATLAS_MIN) / STREAM_FONT_SIZE_STEP) + 1;
    s_Slots.reserve(files.size() * numSizes);

    for (auto& path : files)
    {
        std::string stem = path.stem().string();

        // Track unique stems for the dropdown
        if (std::find(s_Names.begin(), s_Names.end(), stem) == s_Names.end())
            s_Names.push_back(stem);

        for (float sz = STREAM_FONT_ATLAS_MIN; sz <= STREAM_FONT_SIZE_MAX + 0.01f; sz += STREAM_FONT_SIZE_STEP)
        {
            char id[128];
            snprintf(id, sizeof(id), "SW2_STREAM_%s_%.0f", stem.c_str(), sz);

            FontSlot slot;
            slot.nexusId = id;
            slot.stem    = stem;
            slot.size    = sz;
            slot.font    = nullptr;
            s_Slots.push_back(std::move(slot));

            APIDefs->Fonts_AddFromFile(id, sz, path.string().c_str(), OnFontReceived, nullptr);
        }
    }

    std::sort(s_Names.begin(), s_Names.end());
    s_Initialised = true;

    char msg[128];
    snprintf(msg, sizeof(msg), "StreamFonts: registered %d fonts x %d sizes.",
             (int)files.size(), numSizes);
    APIDefs->Log(LOGL_INFO, "Split Wars 2", msg);
}

// ---------------------------------------------------------------------------
// ReleaseStreamFonts
// ---------------------------------------------------------------------------
void ReleaseStreamFonts()
{
    if (!APIDefs) return;
    for (auto& slot : s_Slots)
        APIDefs->Fonts_Release(slot.nexusId.c_str(), OnFontReceived);
    s_Slots.clear();
    s_Names.clear();
    s_Initialised = false;
}

// ---------------------------------------------------------------------------
// GetStreamFont
// ---------------------------------------------------------------------------
ImFont* GetStreamFont(const std::string& name, float size)
{
    for (auto& slot : s_Slots)
    {
        if (slot.stem == name && std::abs(slot.size - size) < 0.5f)
            return slot.font; // may still be nullptr if atlas not yet ready
    }
    return nullptr;
}

// ---------------------------------------------------------------------------
// GetStreamFontNames
// ---------------------------------------------------------------------------
const std::vector<std::string>& GetStreamFontNames()
{
    return s_Names;
}

// ---------------------------------------------------------------------------
// GetStreamerFont
// Convenience wrapper — returns the font matching the user's current settings,
// falling back to FontBig from Nexus if nothing is available yet.
// ---------------------------------------------------------------------------
ImFont* GetStreamerFont()
{
    // Try the user's selected font first
    if (!StreamerFontName.empty())
    {
        ImFont* f = GetStreamFont(StreamerFontName, (float)StreamerFontSize);
        if (f) return f;
    }

    // Fallback: first available slot at the requested size
    for (auto& slot : s_Slots)
    {
        if (std::abs(slot.size - (float)StreamerFontSize) < 0.5f && slot.font)
            return slot.font;
    }

    // Last resort: FontBig from Nexus (same as before)
    NexusLinkData_t* nl = (NexusLinkData_t*)APIDefs->DataLink_Get(DL_NEXUS_LINK);
    if (nl) return (ImFont*)nl->FontBig;

    return nullptr;
}