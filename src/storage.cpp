// storage.cpp
// All disk I/O for Split Wars 2: routes, history, settings, and the route
// folder tree used by the browser window.
//
// File layout under the addon directory (e.g. "…/addons/Split Wars 2/"):
//   settings.json          — UI preferences (window visibility, timer mode, etc.)
//   MyRoute.json           — a saved route (array of checkpoint objects)
//   MyRoute.history        — run history for that route (paired by name)
//   SubFolder/OtherRoute.json
//   SubFolder/OtherRoute.history
//
// All functions swallow exceptions and return false on failure so callers
// don't need try/catch blocks.  Errors that are actionable by the user
// (bad JSON, old format, missing required fields) are logged to the Nexus
// log panel with a friendly message.

#include "shared.h"
#include "nlohmann_json.hpp"
#include <fstream>
#include <windows.h>
#include <filesystem>
#include <cstdio>
#include <ctime>
#include <algorithm>

using json = nlohmann::json;
namespace fs = std::filesystem;

// ---------------------------------------------------------------------------
// GetAddonDir
// ---------------------------------------------------------------------------
// Returns the directory the addon DLL lives in, with "\\Split Wars 2" appended.
// e.g. "C:\Program Files\Guild Wars 2\addons\Split Wars 2"
//
// Uses GetModuleHandleEx with the FROM_ADDRESS flag to locate our own DLL
// by passing the address of this very function as a landmark — a reliable
// trick that doesn't depend on knowing the DLL's name at compile time.
// ---------------------------------------------------------------------------
std::string GetAddonDir()
{
    char path[MAX_PATH];
    HMODULE hm = nullptr;
    if (!GetModuleHandleExA(
        GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
        GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
        (LPCSTR)&GetAddonDir,
        &hm) && APIDefs)
    {
        APIDefs->Log(LOGL_WARNING, "Split Wars 2", "GetModuleHandleExA failed — addon directory may be incorrect.");
    }
    GetModuleFileNameA(hm, path, sizeof(path));

    std::string fullPath(path);
    size_t last = fullPath.find_last_of("\\/");
    std::string dir = fullPath.substr(0, last);
    return dir + "\\Split Wars 2";
}

// ---------------------------------------------------------------------------
// GetCurrentDateTimeString
// ---------------------------------------------------------------------------
// Returns a "YYYY-MM-DD HH:MM" timestamp string for the current local time.
// Used to stamp each HistoricalRun when it is recorded.
// Minutes are the finest granularity — seconds would rarely be useful in a
// history list and would make the column wider for no benefit.
// ---------------------------------------------------------------------------
std::string GetCurrentDateTimeString()
{
    time_t now = time(nullptr);
    struct tm t;
    localtime_s(&t, &now);
    char buf[32];
    snprintf(buf, sizeof(buf), "%04d-%02d-%02d %02d:%02d",
        t.tm_year + 1900, t.tm_mon + 1, t.tm_mday,
        t.tm_hour, t.tm_min);
    return std::string(buf);
}

// ---------------------------------------------------------------------------
// SerializePoint / DeserializePoint  (file-private helpers)
// ---------------------------------------------------------------------------
// Convert a RoutePoint to/from a JSON object.
// ---------------------------------------------------------------------------
static json SerializePoint(const RoutePoint& p)
{
    return {
        {"mapid",            p.MapID},
        {"x",                p.X},
        {"y",                p.Y},
        {"z",                p.Z},
        {"radius_width",     p.RadiusWidth},
        {"trigger_type",     (int)p.TriggerType},
        {"plane_angle",      p.PlaneAngle},
        {"hyperbola_c",      p.HyperbolaC},
        {"dot_density",      p.DotDensity},
        {"dot_center",       p.bandCenterInput},
        {"dot_up",          p.bandUpInput},
        {"dot_down",        p.bandDownInput},
    };
}

static void DeserializePoint(const json& j, RoutePoint& p)
{
    p.MapID            = j["mapid"];
    p.X                = j["x"];
    p.Y                = j["y"];
    p.Z                = j["z"];
    p.RadiusWidth      = j.value("radius_width", 10.0f);
    p.TriggerType      = (ETriggerType)j.value("trigger_type", 0);
    p.PlaneAngle       = j.value("plane_angle", 0.0f);  // Default: facing north
    p.HyperbolaC       = j.value("hyperbola_c", 12);
    p.DotDensity       = j.value("dot_density", 0);
    p.bandCenterInput  = j.value("dot_center", 0.0f);
    p.bandUpInput      = j.value("dot_up", 10.0f);
    p.bandDownInput    = j.value("dot_down", 0.0f);
}

// ===========================================================================
// Route  —  SaveRoute / LoadRoute
// ===========================================================================

// ---------------------------------------------------------------------------
// SaveRoute
// ---------------------------------------------------------------------------
// Serialises the route's checkpoint list to a JSON array and writes it to
// filepath, creating any missing parent directories first.
// Each array element contains the checkpoint's spatial data (via
// SerializePoint) plus its name and start/goal flags.
// ---------------------------------------------------------------------------
bool SaveRoute(const std::string& filepath, const Route& route)
{
    try
    {
        fs::create_directories(fs::path(filepath).parent_path());

        json j = json::array();
        for (const auto& cp : route.Checkpoints)
        {
            json cpj = SerializePoint(cp.Point);
            cpj["name"]     = cp.Name;
            cpj["is_start"] = cp.IsStart;
            cpj["is_goal"]  = cp.IsGoal;
            j.push_back(cpj);
        }

        std::ofstream file(filepath);
        if (!file.is_open()) return false;
        file << j.dump(4); // Pretty-print with 4-space indent for human readability
        if (ShowDebug)
            APIDefs->Log(LOGL_DEBUG, "Split Wars 2", ("Route saved: " + fs::path(filepath).stem().string()).c_str());
        return true;
    }
    catch (...) { return false; }
}

// ---------------------------------------------------------------------------
// LoadRoute
// ---------------------------------------------------------------------------
// Reads and validates a route .json file, populating route and routeName.
//
// Validation pipeline:
//   1. Structural check — must be a JSON array (not an object, which would
//      indicate the old SW2 format that used "start"/"goal"/"checkpoints" keys).
//   2. Per-entry field check — every entry must have mapid, x, y, z, radius.
//      Missing fields likely indicate a hand-edited or truncated file.
//   3. Duplicate start/goal correction — if a manually edited file has more
//      than one start or goal flag, only the first is kept and a warning is
//      logged so the user knows the file was auto-corrected.
//
// Parse errors extract the line number from nlohmann's error message and
// produce a plain-English log entry pointing the user at the right line.
// ---------------------------------------------------------------------------
bool LoadRoute(const std::string& filepath, Route& route, std::string& routeName)
{
    try
    {
        std::ifstream file(filepath);
        if (!file.is_open()) return false;

        json j = json::parse(file);
        std::string filename = fs::path(filepath).filename().string();
        routeName = fs::path(filepath).stem().string(); // File name without extension = route display name

        // --- Structural validation ---

        // The old SW2 format was an object with "start", "goal", "checkpoints" keys.
        // We can't migrate it automatically so we reject it with a clear message.
        if (j.is_object())
        {
            if (APIDefs)
            {
                std::string msg = filename + " uses an older route format and cannot be loaded. Please recreate the route.";
                APIDefs->Log(LOGL_WARNING, "Split Wars 2", msg.c_str());
            }
            return false;
        }

        // Completely foreign file — not a JSON array at all.
        if (!j.is_array())
        {
            if (APIDefs)
            {
                std::string msg = "JSON structure of file \"" + filename + "\" is unsupported to load as route.";
                APIDefs->Log(LOGL_WARNING, "Split Wars 2", msg.c_str());
            }
            return false;
        }

        // --- Per-entry field validation ---
        // These five fields are always required; plane_width/angle and
        // trigger_type are optional (they have safe defaults in DeserializePoint).
        const char* requiredFields[] = { "mapid", "x", "y", "z", "radius_width" };
        for (int i = 0; i < (int)j.size(); i++)
        {
            const auto& cp = j[i];
            for (const char* field : requiredFields)
            {
                if (!cp.contains(field))
                {
                    if (APIDefs)
                    {
                        std::string msg = filename + ": entry " + std::to_string(i) +
                            " is missing required field \"" + field + "\".";
                        APIDefs->Log(LOGL_WARNING, "Split Wars 2", msg.c_str());
                    }
                    return false;
                }
            }
        }

        // --- Load entries ---
        route.Checkpoints.clear();
        for (const auto& cp : j)
        {
            Checkpoint c;
            std::string name = cp.value("name", "Unnamed");
            strncpy(c.Name, name.c_str(), sizeof(c.Name) - 1);
            c.IsStart = cp.value("is_start", false);
            c.IsGoal  = cp.value("is_goal",  false);
            DeserializePoint(cp, c.Point);
            route.Checkpoints.push_back(c);
        }

        // --- Duplicate start correction ---
        // Only one start is meaningful — keep the first and clear any extras,
        // logging a single warning if any correction was needed.
        // Goal deduplication is intentionally omitted: multiple goals are
        // allowed so routes can diverge (e.g. two valid finish points).
        // Uncomment the goal block below to restore singular-goal enforcement.
        bool foundStart      = false;
        bool correctedStart  = false;
        for (auto& cp : route.Checkpoints)
        {
            if (cp.IsStart)
            {
                if (foundStart)
                {
                    cp.IsStart      = false;
                    correctedStart  = true;
                }
                else
                {
                    foundStart = true;
                }
            }
            // Uncomment to restore singular-goal enforcement:
            //if (cp.IsGoal)
            //{
            //    if (foundGoal)
            //    {
            //        cp.IsGoal = false;
            //        correctedGoal = true;
            //    }
            //    else { foundGoal = true; }
            //}
        }
        if (correctedStart && APIDefs)
            APIDefs->Log(LOGL_WARNING, "Split Wars 2",
                (filename + ": multiple start checkpoints found, keeping the first.").c_str());

        route.IsValid = true;

        if (APIDefs && ShowDebug)
            APIDefs->Log(LOGL_DEBUG, "Split Wars 2", ("Route loaded: " + routeName).c_str());

        return true;
    }
    catch (const nlohmann::json::parse_error& e)
    {
        if (APIDefs)
        {
            // Extract the line number from nlohmann's error message so we can
            // point the user at the exact location of the syntax error.
            std::string what(e.what());
            std::string lineInfo = "unknown line";
            size_t linePos = what.find("line ");
            if (linePos != std::string::npos)
            {
                size_t numStart = linePos + 5;
                size_t numEnd   = what.find(',', numStart);
                if (numEnd != std::string::npos)
                    lineInfo = "line " + what.substr(numStart, numEnd - numStart);
            }

            std::string filename = fs::path(filepath).filename().string();
            std::string friendly = "Something is wrong in " + filename + " around " + lineInfo +
                ". Please check for missing quotes, colons or brackets.";
            APIDefs->Log(LOGL_WARNING, "Split Wars 2", friendly.c_str());

            if (ShowDebug)
                APIDefs->Log(LOGL_DEBUG, "Split Wars 2", e.what()); // Full technical error in debug mode
        }
        return false;
    }
    catch (...) { return false; }
}

void RecalcSegments(const std::vector<HistoricalRun>& runs,
    std::vector<SegmentRecord>& segments)
{
    segments.clear();
    for (const HistoricalRun& run : runs)
        UpdateSegments(run, segments);
}

void UpdateSegments(const HistoricalRun& run,
    std::vector<SegmentRecord>& segments)
{
    static const std::string START_SUFFIX  = " Start";
    static const std::string END_SUFFIX    = " End";
    static const std::string TAINTED_NAME  = "__TAINTED__";

    for (int i = 0; i < (int)run.Splits.size(); i++)
    {
        const std::string& name = run.Splits[i].Name;

        // Check for " Start" suffix.
        if (name.size() <= START_SUFFIX.size()) continue;
        if (name.compare(name.size() - START_SUFFIX.size(),
                START_SUFFIX.size(), START_SUFFIX) != 0) continue;

        std::string prefix = name.substr(0, name.size() - START_SUFFIX.size());
        std::string endName = prefix + END_SUFFIX;

        // Find the nearest matching " End" after this split.
        for (int j = i + 1; j < (int)run.Splits.size(); j++)
        {
            // A tainted split between Start and End invalidates this segment.
            if (run.Splits[j].Name == TAINTED_NAME) break;

            if (run.Splits[j].Name != endName) continue;

            double delta = run.Splits[j].Timestamp - run.Splits[i].Timestamp;

            // Find or create the record for this prefix.
            SegmentRecord* rec = nullptr;
            for (SegmentRecord& r : segments)
            if (r.name == prefix) { rec = &r; break; }

            if (!rec)
            {
                segments.push_back({ prefix, delta, run.Date });
            }
            else if (delta < rec->bestTime)
            {
                rec->bestTime = delta;
                rec->bestDate = run.Date;
            }
            break; // Only pair with the first matching End
        }
    }
}

// ===========================================================================
// History  —  SaveHistory / LoadHistory
// ===========================================================================
// History is stored in a .history file that lives alongside its .json route.
// Using a full absolute path (rather than just a name) means two routes with
// the same display name in different sub-folders never collide.

// ---------------------------------------------------------------------------
// SaveHistory
// ---------------------------------------------------------------------------
// Writes the full run history and the index of the designated best run to
// the .history file.  bestRunIndex = -1 means no best run is set.
//
// Format:
//   {
//     "best_run_index": 0,
//     "history": [
//       { "date": "…", "total_time": 123.4, "grand_total": 130.0,
//         "splits": [ { "name": "…", "timestamp": 45.6 }, … ] },
//       …
//     ]
//   }
// ---------------------------------------------------------------------------
bool SaveHistory(const std::string& historyPath, const std::vector<HistoricalRun>& runs,
    const std::vector<SegmentRecord>& segments, int bestRunIndex)
{
    try
    {
        fs::create_directories(fs::path(historyPath).parent_path());

        json j;
        j["best_run_index"] = bestRunIndex;

        json history = json::array();
        for (const auto& run : runs)
        {
            json rj;
            rj["date"]        = run.Date;
            rj["total_time"]  = run.TotalTime;
            rj["grand_total"] = run.GrandTotal;

            json splits = json::array();
            for (const auto& s : run.Splits)
                splits.push_back({ {"name", s.Name}, {"timestamp", s.Timestamp} });
            rj["splits"] = splits;
            history.push_back(rj);
        }
        j["history"] = history;

        json segArr = json::array();
        for (const SegmentRecord& s : segments)
            segArr.push_back({
                {"name",      s.name},
                {"best_time", s.bestTime},
                {"best_date", s.bestDate}
            });
        j["segments"] = segArr;

        std::ofstream file(historyPath);
        if (!file.is_open()) return false;
        file << j.dump(4);
        return true;
    }
    catch (...) { return false; }
}

// ---------------------------------------------------------------------------
// LoadHistory
// ---------------------------------------------------------------------------
// Reads the .history file and populates runs and bestRun.
// grand_total uses j.value() with a 0.0 default so history files saved
// before the grand total feature was added still load without errors.
// The best run is resolved by index: bestRun is set to the splits of the
// run at best_run_index, or left empty if the index is -1 or out of range.
// ---------------------------------------------------------------------------
bool LoadHistory(const std::string& historyPath, std::vector<Split>& bestRun,
    std::vector<HistoricalRun>& runs, std::vector<SegmentRecord>& segments, int& outBestIndex)
{
    try
    {
        std::ifstream file(historyPath);
        if (!file.is_open()) return false;

        json j = json::parse(file);

        runs.clear();
        for (const auto& rj : j["history"])
        {
            HistoricalRun run;
            run.Date       = rj["date"].get<std::string>();
            run.TotalTime  = rj["total_time"];
            run.GrandTotal = rj.value("grand_total", 0.0); // Default 0 for older history files

            for (const auto& s : rj["splits"])
            {
                Split split;
                std::string name = s["name"].get<std::string>();
                strncpy(split.Name, name.c_str(), sizeof(split.Name) - 1);
                split.Timestamp = s["timestamp"];
                run.Splits.push_back(split);
            }
            runs.push_back(run);
        }

        // Resolve the best run by index — copy its splits into bestRun.
        bestRun.clear();
        int bestIndex = j.value("best_run_index", -1);
        outBestIndex  = (bestIndex >= 0 && bestIndex < (int)runs.size()) ? bestIndex : -1;
        if (outBestIndex >= 0)
            bestRun = runs[outBestIndex].Splits;

        // Load segment records — absent in older files, recalculated by caller.
        segments.clear();
        if (j.contains("segments"))
        {
            for (const auto& s : j["segments"])
            {
                SegmentRecord rec;
                rec.name     = s.value("name",      "");
                rec.bestTime = s.value("best_time", 0.0);
                rec.bestDate = s.value("best_date", "");
                if (!rec.name.empty()) segments.push_back(rec);
            }
        }
        if (segments.empty() && !runs.empty())
            RecalcSegments(runs, segments);

        return true;
    }
    catch (...) { return false; }
}

// ===========================================================================
// Settings  —  SaveSettings / LoadSettings
// ===========================================================================

// ---------------------------------------------------------------------------
// SaveSettings
// ---------------------------------------------------------------------------
// Writes all user preferences to settings.json in the addon directory.
// ---------------------------------------------------------------------------
bool SaveSettings(const std::string& addonDir, const Settings& settings)
{
    try
    {
        fs::create_directories(addonDir);

        json j = {
            {"show_timer",             settings.ShowTimer},
            {"show_config",            settings.ShowConfig},
            {"show_zones",             settings.ShowZones},
            {"zone_fade_start",        settings.ZoneFadeStart},
            {"zone_fade_end",          settings.ZoneFadeEnd},
            {"show_debug",             settings.ShowDebug},
            {"timer_display_mode",     settings.TimerDisplayMode},
            {"compact_mode",           settings.CompactMode},
            {"show_history",           settings.ShowHistory},
            {"show_grand_total",       settings.ShowGrandTotal},
            {"show_route_browser",    settings.ShowRouteBrowser},
            {"max_history_runs",      settings.MaxHistoryRuns},
            {"data_source",           settings.DataSource},
            {"color_start",      {settings.ColorStart[0],      settings.ColorStart[1],      settings.ColorStart[2]}},
            {"color_goal",       {settings.ColorGoal[0],       settings.ColorGoal[1],       settings.ColorGoal[2]}},
            {"color_checkpoint", {settings.ColorCheckpoint[0], settings.ColorCheckpoint[1], settings.ColorCheckpoint[2]}},
            {"color_null",       {settings.ColorNull[0],       settings.ColorNull[1],       settings.ColorNull[2]}},
            {"color_ahead",      {settings.ColorAhead[0],      settings.ColorAhead[1],      settings.ColorAhead[2]}},
            {"color_behind",     {settings.ColorBehind[0],     settings.ColorBehind[1],     settings.ColorBehind[2]}},
            {"color_best_row",   {settings.ColorBestRow[0],    settings.ColorBestRow[1],    settings.ColorBestRow[2]}},
            {"timer_font_scale", settings.TimerFontScale},
        };

        std::string filepath = addonDir + "\\settings.json";
        std::ofstream file(filepath);
        if (!file.is_open()) return false;
        file << j.dump(4);
        return true;
    }
    catch (...) { return false; }
}

// ---------------------------------------------------------------------------
// LoadSettings
// ---------------------------------------------------------------------------
// Reads settings.json and fills the Settings struct.
// Every field uses j.value() with a sensible default so a settings file that
// pre-dates a newly added option still loads cleanly.
// ---------------------------------------------------------------------------
bool LoadSettings(const std::string& addonDir, Settings& settings)
{
    try
    {
        std::string filepath = addonDir + "\\settings.json";
        std::ifstream file(filepath);
        if (!file.is_open()) return false;

        json j = json::parse(file);
        settings.ShowTimer        = j.value("show_timer",         true);
        settings.ShowConfig       = j.value("show_config",        true);
        settings.ShowZones        = j.value("show_zones",         true);
        settings.ZoneFadeStart    = j.value("zone_fade_start",    50.0f);
        settings.ZoneFadeEnd      = j.value("zone_fade_end",      150.0f);
        settings.ShowDebug        = j.value("show_debug",         false);
        settings.TimerDisplayMode = j.value("timer_display_mode", 1);
        settings.CompactMode      = j.value("compact_mode",       false);
        settings.ShowHistory      = j.value("show_history",       false);
        settings.ShowGrandTotal   = j.value("show_grand_total",   false);
        settings.ShowRouteBrowser = j.value("show_route_browser", false);
        settings.MaxHistoryRuns   = j.value("max_history_runs",   10);
        settings.DataSource       = j.value("data_source",        0);
        if (j.contains("color_start") && j["color_start"].size() == 3)
            for (int i = 0; i < 3; i++) settings.ColorStart[i] = j["color_start"][i].get<float>();
        if (j.contains("color_goal") && j["color_goal"].size() == 3)
            for (int i = 0; i < 3; i++) settings.ColorGoal[i] = j["color_goal"][i].get<float>();
        if (j.contains("color_checkpoint") && j["color_checkpoint"].size() == 3)
            for (int i = 0; i < 3; i++) settings.ColorCheckpoint[i] = j["color_checkpoint"][i].get<float>();
        if (j.contains("color_null") && j["color_null"].size() == 3)
            for (int i = 0; i < 3; i++) settings.ColorNull[i] = j["color_null"][i].get<float>();
        if (j.contains("color_ahead") && j["color_ahead"].size() == 3)
            for (int i = 0; i < 3; i++) settings.ColorAhead[i] = j["color_ahead"][i].get<float>();
        if (j.contains("color_behind") && j["color_behind"].size() == 3)
            for (int i = 0; i < 3; i++) settings.ColorBehind[i] = j["color_behind"][i].get<float>();
        if (j.contains("color_best_row") && j["color_best_row"].size() == 3)
            for (int i = 0; i < 3; i++) settings.ColorBestRow[i] = j["color_best_row"][i].get<float>();
        settings.TimerFontScale = j.value("timer_font_scale", 1.0);
        return true;
    }
    catch (...) { return false; }
}

// ===========================================================================
// Route tree  —  BuildRouteTree
// ===========================================================================

// ---------------------------------------------------------------------------
// HistoryPathFromJsonPath  (file-private helper)
// ---------------------------------------------------------------------------
// Derives the .history file path from a .json path by swapping the extension.
// e.g. "…/Routes/Fractals/Nightmare.json" → "…/Routes/Fractals/Nightmare.history"
// ---------------------------------------------------------------------------
static std::string HistoryPathFromJsonPath(const std::string& jsonPath)
{
    fs::path p(jsonPath);
    p.replace_extension(".history");
    return p.string();
}

// ---------------------------------------------------------------------------
// BuildFolderNode  (file-private recursive helper)
// ---------------------------------------------------------------------------
// Recursively scans a directory and returns a RouteFolder node containing:
//   SubFolders — one child RouteFolder per sub-directory (recursive)
//   Routes     — one RouteFile per .json file (excluding settings.json)
//
// Entries are sorted: directories first (alphabetical), then files
// (alphabetical) — matching the convention most file browsers use.
//
// settings.json is excluded because it lives in the root addon directory and
// is not a route file; including it would show it as a selectable route.
// ---------------------------------------------------------------------------
static RouteFolder BuildFolderNode(const fs::path& dir, const fs::path& rootDir)
{
    RouteFolder node;
    node.FolderName = dir.filename().string();
    node.FolderPath = dir.string();

    // The root node gets an empty name; the browser renders it as "[ root ]".
    if (dir == rootDir)
        node.FolderName = "";

    try
    {
        // Collect all entries first so we can sort them before processing.
        std::vector<fs::directory_entry> entries;
        for (const auto& entry : fs::directory_iterator(dir))
            entries.push_back(entry);

        // Sort: directories before files; alphabetical within each group.
        std::sort(entries.begin(), entries.end(), [](const fs::directory_entry& a, const fs::directory_entry& b)
        {
            bool aDir = a.is_directory();
            bool bDir = b.is_directory();
            if (aDir != bDir) return aDir > bDir; // Directories sort before files
            return a.path().filename().string() < b.path().filename().string();
        });

        for (const auto& entry : entries)
        {
            if (entry.is_directory())
            {
                node.SubFolders.push_back(BuildFolderNode(entry.path(), rootDir));
            }
            else if (entry.is_regular_file() &&
                     entry.path().extension() == ".json" &&
                     entry.path().filename() != "settings.json") // Exclude the settings file
            {
                RouteFile rf;
                rf.Name        = entry.path().stem().string(); // Display name = filename without extension
                rf.Filepath    = entry.path().string();
                rf.HistoryPath = HistoryPathFromJsonPath(rf.Filepath);
                node.Routes.push_back(rf);
            }
        }
    }
    catch (...) {}

    return node;
}

// ---------------------------------------------------------------------------
// BuildRouteTree
// ---------------------------------------------------------------------------
// Public entry point: scans the entire addon directory and returns the root
// RouteFolder.  Returns an empty RouteFolder if the directory doesn't exist
// yet (e.g. first launch before any routes have been saved).
// ---------------------------------------------------------------------------
RouteFolder BuildRouteTree(const std::string& addonDir)
{
    fs::path root(addonDir);
    if (!fs::exists(root))
        return RouteFolder{};
    return BuildFolderNode(root, root);
}
