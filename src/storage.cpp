// storage.cpp
#include "storage.h"
#include "nlohmann_json.hpp"
#include <fstream>
#include <windows.h>
#include <filesystem>
#include <cstdio>
#include <ctime>

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
    return dir + "\\gw2-speedrun";
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

static std::string SanitizeFilename(const std::string& name)
{
    std::string result = name;
    for (char& c : result)
    {
        if (c == '/' || c == '\\' || c == ':' || c == '*' ||
            c == '?' || c == '"' || c == '<' || c == '>' || c == '|')
            c = '_';
    }
    return result;
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

bool SaveRoute(const std::string& addonDir, const Route& route, const std::string& routeName)
{
    try
    {
        fs::create_directories(addonDir);

        json j;
        j["name"]  = routeName;
        j["start"] = SerializePoint(route.Start);
        j["goal"]  = SerializePoint(route.Goal);

        json cps = json::array();
        for (const auto& cp : route.Checkpoints)
        {
            json cpj = SerializePoint(cp.Point);
            cpj["name"] = cp.Name;
            cps.push_back(cpj);
        }
        j["checkpoints"] = cps;

        std::string filepath = addonDir + "\\" + SanitizeFilename(routeName) + ".json";
        std::ofstream file(filepath);
        if (!file.is_open()) return false;
        file << j.dump(4);
        return true;
    }
    catch (...) { return false; }
}

bool LoadRoute(const std::string& filename, Route& route, std::string& routeName)
{
    try
    {
        std::ifstream file(filename);
        if (!file.is_open()) return false;

        json j = json::parse(file);
        routeName = j["name"].get<std::string>();

        DeserializePoint(j["start"], route.Start);
        DeserializePoint(j["goal"],  route.Goal);

        route.Checkpoints.clear();
        for (const auto& cp : j["checkpoints"])
        {
            Checkpoint c;
            std::string name = cp["name"].get<std::string>();
            strncpy(c.Name, name.c_str(), sizeof(c.Name) - 1);
            DeserializePoint(cp, c.Point);
            route.Checkpoints.push_back(c);
        }

        route.IsValid = true;
        return true;
    }
    catch (...) { return false; }
}

bool SaveHistory(const std::string& addonDir, const std::string& routeName, const std::vector<Split>& bestSplits, const std::vector<HistoricalRun>& runs)
{
    try
    {
        fs::create_directories(addonDir);

        json j;

        json bs = json::array();
        for (const auto& s : bestSplits)
            bs.push_back({ {"name", s.Name}, {"timestamp", s.Timestamp} });
        j["best_splits"] = bs;

        json history = json::array();
        for (const auto& run : runs)
        {
            json rj;
            rj["date"]       = run.Date;
            rj["total_time"] = run.TotalTime;

            json splits = json::array();
            for (const auto& s : run.Splits)
                splits.push_back({ {"name", s.Name}, {"timestamp", s.Timestamp} });
            rj["splits"] = splits;
            history.push_back(rj);
        }
        j["history"] = history;

        std::string filepath = addonDir + "\\" + SanitizeFilename(routeName) + ".history";
        std::ofstream file(filepath);
        if (!file.is_open()) return false;
        file << j.dump(4);
        return true;
    }
    catch (...) { return false; }
}

bool LoadHistory(const std::string& addonDir, const std::string& routeName, std::vector<Split>& bestSplits, std::vector<HistoricalRun>& runs)
{
    try
    {
        std::string filepath = addonDir + "\\" + SanitizeFilename(routeName) + ".history";
        std::ifstream file(filepath);
        if (!file.is_open()) return false;

        json j = json::parse(file);

        bestSplits.clear();
        for (const auto& s : j["best_splits"])
        {
            Split split;
            std::string name = s["name"].get<std::string>();
            strncpy(split.Name, name.c_str(), sizeof(split.Name) - 1);
            split.Timestamp = s["timestamp"];
            bestSplits.push_back(split);
        }

        runs.clear();
        for (const auto& rj : j["history"])
        {
            HistoricalRun run;
            run.Date      = rj["date"].get<std::string>();
            run.TotalTime = rj["total_time"];

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

        return true;
    }
    catch (...) { return false; }
}

bool SaveSettings(const std::string& addonDir, const Settings& settings)
{
    try
    {
        fs::create_directories(addonDir);

        json j = {
            {"show_timer",       settings.ShowTimer},
            {"show_config",      settings.ShowConfig},
            {"show_zones",       settings.ShowZones},
            {"show_debug",       settings.ShowDebug},
            {"split_mode",       settings.SplitMode},
            {"compact_mode",     settings.CompactMode},
            {"show_history",     settings.ShowHistory},
            {"max_history_runs", settings.MaxHistoryRuns}
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
        settings.ShowTimer      = j.value("show_timer",       true);
        settings.ShowConfig     = j.value("show_config",      true);
        settings.ShowZones      = j.value("show_zones",       true);
        settings.ShowDebug      = j.value("show_debug",       false);
        settings.SplitMode      = j.value("split_mode",       true);
        settings.CompactMode    = j.value("compact_mode",     false);
        settings.ShowHistory    = j.value("show_history",     false);
        settings.MaxHistoryRuns = j.value("max_history_runs", 10);
        return true;
    }
    catch (...) { return false; }
}

std::vector<RouteFile> ListRoutes(const std::string& addonDir)
{
    std::vector<RouteFile> result;
    try
    {
        if (!fs::exists(addonDir)) return result;
        for (const auto& entry : fs::directory_iterator(addonDir))
        {
            if (entry.path().extension() == ".json" &&
                entry.path().filename() != "settings.json")
            {
                RouteFile rf;
                rf.Filename = entry.path().string();

                std::ifstream file(rf.Filename);
                if (file.is_open())
                {
                    json j = json::parse(file);
                    rf.Name = j["name"].get<std::string>();
                    result.push_back(rf);
                }
            }
        }
    }
    catch (...) {}
    return result;
}