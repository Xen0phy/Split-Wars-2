// stream_fonts.cpp
// Streamer-mode / speedo font manager.
//
// Scans <AddonDir>/fonts/ on startup and remembers which .ttf/.otf files
// are available (up to STREAM_FONT_MAX_FILES), without registering
// anything with Nexus yet. Each (stem, role) pair is registered with
// Nexus lazily, the first time GetStreamFont() is asked for it, at
// whatever size is requested at that moment. If the same (stem, role)
// is later asked for again at a different size, the existing Nexus
// registration is resized in place via Fonts_Resize rather than a new
// one being created — this is the same mechanism Nexus's own UI uses
// to keep resized fonts crisp at arbitrary pixel sizes.
//
// "Role" exists because a few callers (the streamer timer) need several
// independently-sized fonts from the same stem, all at once, every
// frame (main digits, millis, header). See EStreamFontRole in the header.
//
// Font identifiers registered with Nexus follow the pattern:
//   "SW2_STREAM_<STEM>_<ROLE>"
// e.g. "SW2_STREAM_Roboto-Regular_StreamerMain"
// The identifier no longer encodes size, since a slot's size now changes
// in place over its lifetime instead of being baked permanently.
//
// GetStreamerFont() returns the font matching the user's current
// StreamerFontName / StreamerFontSize settings, role StreamerMain.

#include "stream_fonts.h"
#include "shared.h"

#include <filesystem>
#include <algorithm>
#include <cstdio>

namespace fs = std::filesystem;

// ---------------------------------------------------------------------------
// Internal state
// ---------------------------------------------------------------------------

// A discovered font file, found during the startup scan.
struct FontFile
{
    std::string stem; // e.g. "Roboto-Regular"
    std::string path; // full path on disk
};

// A single (stem, role) registration with Nexus. Created lazily on first
// request; resized in place (same nexusId, new size) on subsequent
// requests with a different size.
struct FontSlot
{
    std::string     nexusId;     // identifier registered with Nexus
    std::string     stem;        // font file stem, e.g. "Roboto-Regular"
    EStreamFontRole role;        // which independently-sized slot this is
    float           currentSize; // size most recently sent to Nexus (Add or Resize)
    ImFont*         font;        // filled by Nexus callback; nullptr until ready
};

static std::vector<FontFile>     s_Files;
static std::vector<FontSlot>     s_Slots;
static std::vector<std::string>  s_Names;   // unique stems, sorted
static bool                      s_Initialised = false;

// Role names used only to build stable, human-readable Nexus identifiers.
// Order must match EStreamFontRole.
static const char* RoleName(EStreamFontRole role)
{
    switch (role)
    {
        case EStreamFontRole::StreamerMain:       return "StreamerMain";
        case EStreamFontRole::StreamerMainMillis: return "StreamerMainMillis";
        case EStreamFontRole::StreamerCompMillis: return "StreamerCompMillis";
        case EStreamFontRole::StreamerHeader:     return "StreamerHeader";
        case EStreamFontRole::SpeedoLabel:        return "SpeedoLabel";
    }
    return "Unknown";
}

// ---------------------------------------------------------------------------
// Nexus callback
// Called by Nexus once per registered font when the atlas is ready (or when
// a re-atlas happens, e.g. when the user changes Nexus UI scale, or after
// a resize we requested).
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
// FindFile
// Looks up a previously-discovered font file by stem.
// ---------------------------------------------------------------------------
static const FontFile* FindFile(const std::string& stem)
{
    for (auto& f : s_Files)
        if (f.stem == stem)
            return &f;
    return nullptr;
}

// ---------------------------------------------------------------------------
// FindSlot
// Looks up an existing (stem, role) registration, if any.
// ---------------------------------------------------------------------------
static FontSlot* FindSlot(const std::string& stem, EStreamFontRole role)
{
    for (auto& slot : s_Slots)
        if (slot.stem == stem && slot.role == role)
            return &slot;
    return nullptr;
}

// ---------------------------------------------------------------------------
// InitStreamFonts
// ---------------------------------------------------------------------------
void InitStreamFonts()
{
    if (s_Initialised) return;
    if (!APIDefs)      return;

    std::string fontsDir = GetAddonDir() + "\\fonts";

    std::error_code ec;
    fs::create_directories(fontsDir, ec);
    if (fs::exists(fontsDir, ec))
    {
        std::vector<fs::path> paths;
        for (auto& entry : fs::directory_iterator(fontsDir, ec))
        {
            if (!entry.is_regular_file()) continue;
            auto ext = entry.path().extension().string();
            // case-insensitive extension check
            std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
            if (ext == ".ttf" || ext == ".otf")
            {
                paths.push_back(entry.path());
                if ((int)paths.size() >= STREAM_FONT_MAX_FILES) break;
            }
        }

        // Sort for deterministic dropdown order
        std::sort(paths.begin(), paths.end());

        for (auto& path : paths)
        {
            FontFile file;
            file.stem = path.stem().string();
            file.path = path.string();
            s_Files.push_back(file);
            s_Names.push_back(file.stem);
        }
    }

    std::sort(s_Names.begin(), s_Names.end());
    s_Initialised = true;

    if (s_Files.empty())
    {
        APIDefs->Log(LOGL_WARNING, "Split Wars 2",
            "StreamFonts: no fonts found in fonts/ folder.");
        return;
    }

    char msg[128];
    snprintf(msg, sizeof(msg), "StreamFonts: discovered %d font file(s).",
             (int)s_Files.size());
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
    s_Files.clear();
    s_Names.clear();
    s_Initialised = false;
}

// ---------------------------------------------------------------------------
// GetStreamFont
// ---------------------------------------------------------------------------
ImFont* GetStreamFont(const std::string& name, EStreamFontRole role, float size)
{
    if (!APIDefs) return nullptr;

    const FontFile* file = FindFile(name);
    if (!file) return nullptr; // unknown stem -- nothing to register

    FontSlot* slot = FindSlot(name, role);
    if (!slot)
    {
        // First time this (stem, role) has been requested: register fresh.
        char id[160];
        snprintf(id, sizeof(id), "SW2_STREAM_%s_%s", name.c_str(), RoleName(role));

        FontSlot newSlot;
        newSlot.nexusId     = id;
        newSlot.stem        = name;
        newSlot.role        = role;
        newSlot.currentSize = size;
        newSlot.font        = nullptr;
        s_Slots.push_back(std::move(newSlot));
        slot = &s_Slots.back();

        APIDefs->Fonts_AddFromFile(id, size, file->path.c_str(), OnFontReceived, nullptr);
        return nullptr; // not ready yet -- callback will deliver it
    }

    // Already registered. If the size changed, resize in place rather than
    // re-registering, so we keep one stable identifier per (stem, role)
    // for its whole lifetime and Nexus can resize the existing atlas entry.
    if (std::abs(slot->currentSize - size) >= 0.5f)
    {
        slot->currentSize = size;
        APIDefs->Fonts_Resize(slot->nexusId.c_str(), size);
        // Keep returning the previous font this frame; OnFontReceived will
        // update slot->font once Nexus delivers the resized version.
    }

    return slot->font; // may be nullptr if nothing has been delivered yet
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
        ImFont* f = GetStreamFont(StreamerFontName, EStreamFontRole::StreamerMain, (float)StreamerFontSize);
        if (f) return f;
    }

    // Fallback: any already-delivered slot for this stem, regardless of role,
    // so the overlay isn't blank while the requested role's font loads.
    for (auto& slot : s_Slots)
    {
        if (slot.stem == StreamerFontName && slot.font)
            return slot.font;
    }

    // Last resort: FontBig from Nexus (same as before)
    NexusLinkData_t* nl = (NexusLinkData_t*)APIDefs->DataLink_Get(DL_NEXUS_LINK);
    if (nl) return (ImFont*)nl->FontBig;

    return nullptr;
}