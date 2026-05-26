// storage.cpp
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

std::string GetAddonDir()
{
    char path[MAX_PATH];
    HMODULE hm = nullptr;
    GetModuleHandleExA(
        GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
        GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
        (LPCSTR)&GetAddonDir,
        &hm
    );
    GetModuleFileNameA(hm, path, sizeof(path));

    std::string fullPath(path);
    size_t last = fullPath.find_last_of("\\/");
    std::string dir = fullPath.substr(0, last);
    return dir + "\\Split Wars 2";
}

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

static json SerializePoint(const RoutePoint& p)
{
    return {
        {"mapid",        p.MapID},
        {"x",            p.X},
        {"y",            p.Y},
        {"z",            p.Z},
        {"radius",       p.Radius},
        {"trigger_type", (int)p.TriggerType},
        {"plane_width",  p.PlaneWidth},
        {"plane_angle",  p.PlaneAngle}
    };
}

static void DeserializePoint(const json& j, RoutePoint& p)
{
    p.MapID       = j["mapid"];
    p.X           = j["x"];
    p.Y           = j["y"];
    p.Z           = j["z"];
    p.Radius      = j["radius"];
    p.TriggerType = (ETriggerType)j.value("trigger_type", 0);
    p.PlaneWidth  = j.value("plane_width", 10.0f);
    p.PlaneAngle  = j.value("plane_angle", 0.0f);
}

// --- Route ---

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
        file << j.dump(4);
        if (ShowDebug)
            APIDefs->Log(LOGL_DEBUG, "Split Wars 2", ("Route saved: " + fs::path(filepath).stem().string()).c_str());
        return true;
    }
    catch (...) { return false; }
}

bool LoadRoute(const std::string& filepath, Route& route, std::string& routeName)
{
    try
    {
        std::ifstream file(filepath);
        if (!file.is_open()) return false;

        json j = json::parse(file);
        std::string filename = fs::path(filepath).filename().string();
        routeName = fs::path(filepath).stem().string();

        // --- Structural validation ---

        // Old SW2 format used an object with "start", "goal", "checkpoints" keys
        if (j.is_object())
        {
            if (APIDefs)
            {
                std::string msg = filename + " uses an older route format and cannot be loaded. Please recreate the route.";
                APIDefs->Log(LOGL_WARNING, "Split Wars 2", msg.c_str());
            }
            return false;
        }

        // Completely foreign file — not an array at all
        if (!j.is_array())
        {
            if (APIDefs)
            {
                std::string msg = "JSON structure of file \"" + filename + "\" is unsupported to load as route.";
                APIDefs->Log(LOGL_WARNING, "Split Wars 2", msg.c_str());
            }
            return false;
        }

        // --- Per-entry validation ---
        const char* requiredFields[] = { "mapid", "x", "y", "z", "radius" };
        for (int i = 0; i < (int)j.size(); i++)
        {
            const auto& cp = j[i];

            // Check all required fields are present
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

        // --- Sanity check: only one start and one goal allowed ---
        // If multiple are found (e.g. manual JSON edit), keep the first and auto-correct the rest
        bool foundStart = false;
        bool foundGoal  = false;
        for (auto& cp : route.Checkpoints)
        {
            if (cp.IsStart)
            {
                if (foundStart)
                {
                    cp.IsStart = false;
                    if (APIDefs)
                        APIDefs->Log(LOGL_WARNING, "Split Wars 2",
                            (filename + ": multiple start checkpoints found, keeping the first.").c_str());
                }
                foundStart = true;
            }
            if (cp.IsGoal)
            {
                if (foundGoal)
                {
                    cp.IsGoal = false;
                    if (APIDefs)
                        APIDefs->Log(LOGL_WARNING, "Split Wars 2",
                            (filename + ": multiple goal checkpoints found, keeping the first.").c_str());
                }
                foundGoal = true;
            }
        }

        route.IsValid = true;

        if (APIDefs)
        {
            if (ShowDebug)
                APIDefs->Log(LOGL_DEBUG, "Split Wars 2", ("Route loaded: " + routeName).c_str());
        }

        return true;
    }
    catch (const nlohmann::json::parse_error& e)
    {
        if (APIDefs)
        {
            // Extract line number from e.what() for a user-friendly message
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
            std::string friendly = "Something is wrong in " + filename + " around " + lineInfo + ". Please check for missing quotes, colons or brackets.";
            APIDefs->Log(LOGL_WARNING, "Split Wars 2", friendly.c_str());

            if (ShowDebug)
                APIDefs->Log(LOGL_DEBUG, "Split Wars 2", e.what());
        }
        return false;
    }
    catch (...) { return false; }
}

// --- History ---
// History is always stored as a sibling .history file next to the .json,
// so the historyPath is the full absolute path (e.g. "…/Fractals/Nightmare.history").
// This means two routes with identical display names in different folders
// will never collide.
bool SaveHistory(const std::string& historyPath, const std::vector<Split>& bestRun, const std::vector<HistoricalRun>& runs, int bestRunIndex)
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

        std::ofstream file(historyPath);
        if (!file.is_open()) return false;
        file << j.dump(4);
        return true;
    }
    catch (...) { return false; }
}

bool LoadHistory(const std::string& historyPath, std::vector<Split>& bestRun, std::vector<HistoricalRun>& runs)
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
            run.GrandTotal = rj.value("grand_total", 0.0);

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

        bestRun.clear();
        int bestIndex = j.value("best_run_index", -1);
        if (bestIndex >= 0 && bestIndex < (int)runs.size())
            bestRun = runs[bestIndex].Splits;

        return true;
    }
    catch (...) { return false; }
}

// --- Settings ---

bool SaveSettings(const std::string& addonDir, const Settings& settings)
{
    try
    {
        fs::create_directories(addonDir);

        json j = {
            {"show_timer",          settings.ShowTimer},
            {"show_config",         settings.ShowConfig},
            {"show_zones",          settings.ShowZones},
            {"show_debug",          settings.ShowDebug},
            {"timer_display_mode",  settings.TimerDisplayMode},
            {"compact_mode",        settings.CompactMode},
            {"show_history",        settings.ShowHistory},
            {"show_grand_total",    settings.ShowGrandTotal},
            {"show_route_browser",  settings.ShowRouteBrowser},
            {"max_history_runs",    settings.MaxHistoryRuns}
        };

        std::string filepath = addonDir + "\\settings.json";
        std::ofstream file(filepath);
        if (!file.is_open()) return false;
        file << j.dump(4);
        return true;
    }
    catch (...) { return false; }
}

bool LoadSettings(const std::string& addonDir, Settings& settings)
{
    try
    {
        std::string filepath = addonDir + "\\settings.json";
        std::ifstream file(filepath);
        if (!file.is_open()) return false;

        json j = json::parse(file);
        settings.ShowTimer          = j.value("show_timer",         true);
        settings.ShowConfig         = j.value("show_config",        true);
        settings.ShowZones          = j.value("show_zones",         true);
        settings.ShowDebug          = j.value("show_debug",         false);
        settings.TimerDisplayMode   = j.value("timer_display_mode", 1);
        settings.CompactMode        = j.value("compact_mode",       false);
        settings.ShowHistory        = j.value("show_history",       false);
        settings.ShowGrandTotal     = j.value("show_grand_total",   false);
        settings.ShowRouteBrowser   = j.value("show_route_browser", false);
        settings.MaxHistoryRuns     = j.value("max_history_runs",   10);
        return true;
    }
    catch (...) { return false; }
}

// --- Route tree ---

// Derives the .history path from a .json path: same location, swaps extension.
static std::string HistoryPathFromJsonPath(const std::string& jsonPath)
{
    fs::path p(jsonPath);
    p.replace_extension(".history");
    return p.string();
}

// Recursively builds a RouteFolder for the given directory.
// Subfolders become RouteFolder children; .json files (excluding settings.json)
// become RouteFile entries sorted alphabetically.
static RouteFolder BuildFolderNode(const fs::path& dir, const fs::path& rootDir)
{
    RouteFolder node;
    node.FolderName = dir.filename().string();
    node.FolderPath = dir.string();

    // If this is the root, use a friendlier display name
    if (dir == rootDir)
        node.FolderName = "";

    try
    {
        std::vector<fs::directory_entry> entries;
        for (const auto& entry : fs::directory_iterator(dir))
            entries.push_back(entry);

        // Sort: directories first (alphabetical), then files (alphabetical)
        std::sort(entries.begin(), entries.end(), [](const fs::directory_entry& a, const fs::directory_entry& b)
        {
            bool aDir = a.is_directory();
            bool bDir = b.is_directory();
            if (aDir != bDir) return aDir > bDir;
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
                    entry.path().filename() != "settings.json")
            {
                RouteFile rf;
                rf.Name        = entry.path().stem().string();
                rf.Filepath    = entry.path().string();
                rf.HistoryPath = HistoryPathFromJsonPath(rf.Filepath);
                node.Routes.push_back(rf);
            }
        }
    }
    catch (...) {}

    return node;
}

RouteFolder BuildRouteTree(const std::string& addonDir)
{
    fs::path root(addonDir);
    if (!fs::exists(root))
        return RouteFolder{};
    return BuildFolderNode(root, root);
}
