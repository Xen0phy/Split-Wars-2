// render_evaluation.cpp
//
// ImGui (1.80) port of gw2-evaluation-tool-final.html -- a stacked, per-run
// "fractal history" bar chart. Every visual state change in the original
// (paging, pinning, hovering, click-to-stretch, jump-to-fastest) is driven
// there by CSS transitions; here the same transitions are reproduced by
// hand with small per-element lerp/ease animations updated every frame from
// ImGui::GetIO().DeltaTime, driving toward freshly computed target values
// each frame exactly like the JS recomputes target attributes on every
// render()/onHover() call and lets CSS ease toward them.
//
// Only GetAddonDir() is assumed to already exist elsewhere in the codebase.
// Everything else needed to load and interpret a .history file (JSON
// parsing, the Start/End span parser, all-time stats) is self-contained
// below so this file compiles on its own.
//
// NOTE: adjust this include to wherever ShowEvaluation / RenderEvaluationWindow
// are actually declared in the rest of the addon (per the prompt, alongside
// ShowDebug) -- named "Shared.h" here as a placeholder.
#include "render_shared.h"

#include "imgui.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <set>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

extern std::string GetAddonDir();

// Definition for the flag declared extern in Shared.h.
bool ShowEvaluation = false;

namespace EvalTool
{

// ---------------------------------------------------------------------
// Minimal, self-contained JSON reader. Just enough to walk a .history file
// (objects, arrays, strings, numbers, bool/null) -- not a general-purpose
// validator.
// ---------------------------------------------------------------------
namespace Json
{
    struct Value
    {
        enum class Type { Null, Bool, Number, String, Array, Object } type = Type::Null;
        bool b = false;
        double num = 0.0;
        std::string str;
        std::vector<Value> arr;
        std::vector<std::pair<std::string, Value>> obj;

        const Value* Find(const std::string& key) const
        {
            for (auto& kv : obj)
                if (kv.first == key)
                    return &kv.second;
            return nullptr;
        }
    };

    class Parser
    {
    public:
        explicit Parser(const std::string& s) : s_(s), i_(0) {}

        bool Parse(Value& out)
        {
            SkipWs();
            return ParseValue(out);
        }

    private:
        const std::string& s_;
        size_t i_;

        void SkipWs()
        {
            while (i_ < s_.size() && (unsigned char)s_[i_] <= ' ')
                i_++;
        }

        bool ParseValue(Value& v)
        {
            SkipWs();
            if (i_ >= s_.size())
                return false;
            char c = s_[i_];
            if (c == '{') return ParseObject(v);
            if (c == '[') return ParseArray(v);
            if (c == '"') return ParseString(v);
            if (c == 't' || c == 'f') return ParseBool(v);
            if (c == 'n') { i_ += 4; v.type = Value::Type::Null; return true; }
            return ParseNumber(v);
        }

        bool ParseObject(Value& v)
        {
            v.type = Value::Type::Object;
            i_++; // {
            SkipWs();
            if (i_ < s_.size() && s_[i_] == '}') { i_++; return true; }
            while (true)
            {
                SkipWs();
                Value key;
                if (i_ >= s_.size() || s_[i_] != '"' || !ParseString(key))
                    return false;
                SkipWs();
                if (i_ >= s_.size() || s_[i_] != ':')
                    return false;
                i_++;
                Value val;
                if (!ParseValue(val))
                    return false;
                v.obj.emplace_back(std::move(key.str), std::move(val));
                SkipWs();
                if (i_ < s_.size() && s_[i_] == ',') { i_++; continue; }
                if (i_ < s_.size() && s_[i_] == '}') { i_++; break; }
                return false;
            }
            return true;
        }

        bool ParseArray(Value& v)
        {
            v.type = Value::Type::Array;
            i_++; // [
            SkipWs();
            if (i_ < s_.size() && s_[i_] == ']') { i_++; return true; }
            while (true)
            {
                Value val;
                if (!ParseValue(val))
                    return false;
                v.arr.push_back(std::move(val));
                SkipWs();
                if (i_ < s_.size() && s_[i_] == ',') { i_++; continue; }
                if (i_ < s_.size() && s_[i_] == ']') { i_++; break; }
                return false;
            }
            return true;
        }

        bool ParseString(Value& v)
        {
            v.type = Value::Type::String;
            if (i_ >= s_.size() || s_[i_] != '"')
                return false;
            i_++;
            std::string out;
            while (i_ < s_.size() && s_[i_] != '"')
            {
                char c = s_[i_];
                if (c == '\\')
                {
                    i_++;
                    if (i_ >= s_.size()) return false;
                    char e = s_[i_];
                    switch (e)
                    {
                        case 'n': out += '\n'; break;
                        case 't': out += '\t'; break;
                        case 'r': out += '\r'; break;
                        case 'b': out += '\b'; break;
                        case 'f': out += '\f'; break;
                        case '"': out += '"'; break;
                        case '\\': out += '\\'; break;
                        case '/': out += '/'; break;
                        case 'u':
                        {
                            if (i_ + 4 < s_.size())
                            {
                                std::string hex = s_.substr(i_ + 1, 4);
                                unsigned int cp = (unsigned int)strtoul(hex.c_str(), nullptr, 16);
                                i_ += 4;
                                if (cp < 0x80) out += (char)cp;
                                else if (cp < 0x800)
                                {
                                    out += (char)(0xC0 | (cp >> 6));
                                    out += (char)(0x80 | (cp & 0x3F));
                                }
                                else
                                {
                                    out += (char)(0xE0 | (cp >> 12));
                                    out += (char)(0x80 | ((cp >> 6) & 0x3F));
                                    out += (char)(0x80 | (cp & 0x3F));
                                }
                            }
                            break;
                        }
                        default: out += e; break;
                    }
                    i_++;
                }
                else
                {
                    out += c;
                    i_++;
                }
            }
            if (i_ < s_.size()) i_++; // closing quote
            v.str = std::move(out);
            return true;
        }

        bool ParseBool(Value& v)
        {
            v.type = Value::Type::Bool;
            if (s_.compare(i_, 4, "true") == 0) { v.b = true; i_ += 4; return true; }
            if (s_.compare(i_, 5, "false") == 0) { v.b = false; i_ += 5; return true; }
            return false;
        }

        bool ParseNumber(Value& v)
        {
            size_t start = i_;
            if (i_ < s_.size() && (s_[i_] == '-' || s_[i_] == '+')) i_++;
            while (i_ < s_.size() &&
                   (isdigit((unsigned char)s_[i_]) || s_[i_] == '.' || s_[i_] == 'e' ||
                    s_[i_] == 'E' || s_[i_] == '+' || s_[i_] == '-'))
                i_++;
            if (i_ == start) return false;
            v.type = Value::Type::Number;
            v.num = strtod(s_.substr(start, i_ - start).c_str(), nullptr);
            return true;
        }
    };
}

// ---------------------------------------------------------------------
// Data model (mirrors the flat shape parseHistoryFile() produces in the
// HTML: per-run list of top-level blocks in real chronological/play order).
// Nested "children" (the stretch-then-split-open sub-view) are collapsed
// away here -- only the top-level block durations they contribute to are
// kept, matching what the normal (non-split) chart view ever shows.
// ---------------------------------------------------------------------
struct EvalBlock
{
    std::string name;
    double dur = 0.0;
};

struct EvalRun
{
    std::string date;
    double total_time = 0.0;
    std::vector<EvalBlock> blocks; // bottom -> top stacking order (play order)
};

struct EvalStat
{
    int count = 0;
    double totalDur = 0.0;
    double bestDur = 0.0;
    std::string bestDate;
};

static double Round2(double n) { return std::round(n * 100.0) / 100.0; }

struct BoundaryPt
{
    std::string name;
    double timestamp = 0.0;
    bool valid = false;
};

struct HistorySplitPoint
{
    std::string name;
    double timestamp = 0.0;
};

// Recursive span parser -- see the big comment above parseSpan() in the
// original HTML for the full reasoning. This C++ version keeps exactly the
// same top-level block list; nested children are parsed (to correctly
// consume the split stream) but discarded rather than stored, since this
// port doesn't implement the stretch-then-split-open reveal.
static void ParseSpan(const std::vector<HistorySplitPoint>& splits, int startIdx, int endIdx,
                       const BoundaryPt& opening, const BoundaryPt* closing,
                       std::vector<EvalBlock>& outBlocks)
{
    BoundaryPt prev = opening;
    int i = startIdx;
    while (i < endIdx)
    {
        const HistorySplitPoint& sp = splits[i];
        bool isStart = sp.name.size() > 6 && sp.name.compare(sp.name.size() - 6, 6, " Start") == 0;

        if (isStart)
        {
            std::string base = sp.name.substr(0, sp.name.size() - 6);
            int endFound = -1;
            for (int j = i + 1; j < endIdx; j++)
            {
                if (splits[j].name == base + " End") { endFound = j; break; }
            }

            if (endFound == -1)
            {
                if (prev.valid)
                    outBlocks.push_back({ prev.name + " \xE2\x86\x92 " + sp.name, Round2(sp.timestamp - prev.timestamp) });
                prev = { sp.name, sp.timestamp, true };
                i += 1;
                continue;
            }

            if (prev.valid)
                outBlocks.push_back({ prev.name + " \xE2\x86\x92 " + sp.name, Round2(sp.timestamp - prev.timestamp) });

            double nestedStart = sp.timestamp;
            double nestedEnd = splits[endFound].timestamp;
            std::vector<EvalBlock> discardedChildren;
            BoundaryPt innerOpen{ sp.name, nestedStart, true };
            BoundaryPt innerClose{ splits[endFound].name, nestedEnd, true };
            ParseSpan(splits, i + 1, endFound, innerOpen, &innerClose, discardedChildren);

            outBlocks.push_back({ base, Round2(nestedEnd - nestedStart) });
            prev = { splits[endFound].name, nestedEnd, true };
            i = endFound + 1;
        }
        else
        {
            if (prev.valid)
                outBlocks.push_back({ prev.name + " \xE2\x86\x92 " + sp.name, Round2(sp.timestamp - prev.timestamp) });
            prev = { sp.name, sp.timestamp, true };
            i += 1;
        }
    }

    if (closing && prev.valid)
        outBlocks.push_back({ prev.name + " \xE2\x86\x92 " + closing->name, Round2(closing->timestamp - prev.timestamp) });
}

static bool LoadHistoryFile(const std::string& path, std::vector<EvalRun>& outRuns, std::string& err)
{
    std::ifstream f(path, std::ios::binary);
    if (!f)
    {
        err = "Could not open history file: " + path;
        return false;
    }
    std::stringstream ss;
    ss << f.rdbuf();
    std::string content = ss.str();

    Json::Value root;
    Json::Parser parser(content);
    if (!parser.Parse(root) || root.type != Json::Value::Type::Object)
    {
        err = "That file isn't valid JSON -- is it really a .history file?";
        return false;
    }

    const Json::Value* history = root.Find("history");
    if (!history || history->type != Json::Value::Type::Array)
    {
        err = "Not a recognized .history file (missing \"history\" array).";
        return false;
    }

    outRuns.clear();
    outRuns.reserve(history->arr.size());
    for (auto& runVal : history->arr)
    {
        EvalRun run;
        if (auto* d = runVal.Find("date")) run.date = d->str;
        double totalTime = 0.0;
        if (auto* t = runVal.Find("total_time")) totalTime = t->num;
        run.total_time = Round2(totalTime);

        std::vector<HistorySplitPoint> splits;
        if (auto* sp = runVal.Find("splits"))
        {
            splits.reserve(sp->arr.size());
            for (auto& s : sp->arr)
            {
                HistorySplitPoint pt;
                if (auto* n = s.Find("name")) pt.name = n->str;
                if (auto* ts = s.Find("timestamp")) pt.timestamp = ts->num;
                splits.push_back(std::move(pt));
            }
        }

        BoundaryPt none;
        ParseSpan(splits, 0, (int)splits.size(), none, nullptr, run.blocks);
        outRuns.push_back(std::move(run));
    }

    if (outRuns.empty())
    {
        err = "That file parsed fine but contains no runs.";
        return false;
    }
    return true;
}

static std::unordered_map<std::string, EvalStat> ComputeAllTimeStats(const std::vector<EvalRun>& runs)
{
    std::unordered_map<std::string, EvalStat> stats;
    for (auto& run : runs)
    {
        for (auto& b : run.blocks)
        {
            auto& s = stats[b.name];
            s.count += 1;
            s.totalDur += b.dur;
            if (s.count == 1 || b.dur < s.bestDur)
            {
                s.bestDur = b.dur;
                s.bestDate = run.date;
            }
        }
    }
    return stats;
}

// ---------------------------------------------------------------------
// Layout / style constants -- kept 1:1 with the HTML's pixel values.
// ---------------------------------------------------------------------
static const float BAR_W = 46.0f;
static const float BAR_GAP = 18.0f;
static const float TOTAL_BAND_H = 22.0f;
static const float TOP_PAD = 18.0f + TOTAL_BAND_H;
static const float PIN_ROW_H = 18.0f;
static const float PIN_ROW_GAP = 4.0f;
static const float BOTTOM_PAD = 34.0f + PIN_ROW_GAP + PIN_ROW_H;
static const float CHART_H = 460.0f + PIN_ROW_GAP + PIN_ROW_H;
static const float PLOT_H = CHART_H - TOP_PAD - BOTTOM_PAD;
static const int WINDOW_SIZE = 15;
static const float ANIM_DUR = 0.32f;   // matches .bar-block's transition duration
static const float FADE_DUR = 0.25f;   // matches opacity transition duration
static const float FILL_DUR = 0.15f;   // matches fill-color transition duration
static const float STRETCH_HEADROOM = 0.92f;
static const float FLASH_IN = 0.30f;
static const float FLASH_OUT_END = 0.65f;

static ImU32 HexColor(unsigned int hex, float a = 1.0f)
{
    float r = ((hex >> 16) & 0xFF) / 255.0f;
    float g = ((hex >> 8) & 0xFF) / 255.0f;
    float b = (hex & 0xFF) / 255.0f;
    return ImGui::ColorConvertFloat4ToU32(ImVec4(r, g, b, a));
}

static void HexToRGB(unsigned int hex, float& r, float& g, float& b)
{
    r = ((hex >> 16) & 0xFF) / 255.0f;
    g = ((hex >> 8) & 0xFF) / 255.0f;
    b = (hex & 0xFF) / 255.0f;
}

static const unsigned int CORE_DARK = 0x14365e;
static const unsigned int CORE_LIGHT = 0x6fb3e8;
static const unsigned int ROT_DARK = 0x6e3d0a;
static const unsigned int ROT_LIGHT = 0xf0a84e;
static const unsigned int FOCUS = 0xff4fb0;
static const unsigned int BAND_BG = 0x0c0d10;
static const unsigned int BAND_STROKE = 0x2a2e35;
static const unsigned int TEXT_DIM = 0x8a8f98;
static const unsigned int TEXT_COL = 0xd8dadf;
static const unsigned int ACCENT = 0x6fb3e8;

// ---------------------------------------------------------------------
// Time formatting -- mirrors formatSegmentTime / formatRunTime / formatDiff.
// ---------------------------------------------------------------------
static std::string FormatSegmentTime(double seconds)
{
    int m = (int)std::floor(seconds / 60.0);
    double s = seconds - m * 60.0;
    char buf[32];
    if (m > 0) snprintf(buf, sizeof(buf), "%d:%06.3f", m, s);
    else snprintf(buf, sizeof(buf), "%.3f", s);
    return buf;
}

static std::string FormatRunTime(double seconds, bool withMs)
{
    int h = (int)(seconds / 3600.0);
    int m = (int)(std::fmod(seconds, 3600.0) / 60.0);
    double s = std::fmod(seconds, 60.0);
    char buf[64];
    if (h > 0)
    {
        if (withMs) snprintf(buf, sizeof(buf), "%d:%02d:%06.3f", h, m, s);
        else snprintf(buf, sizeof(buf), "%d:%02d:%02d", h, m, (int)s);
    }
    else
    {
        if (withMs) snprintf(buf, sizeof(buf), "%d:%06.3f", m, s);
        else snprintf(buf, sizeof(buf), "%d:%02d", m, (int)s);
    }
    return buf;
}

static bool FormatDiff(double thisTime, double bestTime, std::string& out)
{
    double diff = thisTime - bestTime;
    if (diff <= 0.0005) return false;
    out = "+" + FormatSegmentTime(diff);
    return true;
}

// ---------------------------------------------------------------------
// Tiny animation primitive: eases from `from` to `to` over `dur` seconds,
// using a smoothstep easing curve as a stand-in for the HTML's
// cubic-bezier(.2,.7,.3,1) -- same idea (slow-in, fast-middle, settle at
// the end), driven every frame instead of by the browser's compositor.
// ---------------------------------------------------------------------
struct AnimF
{
    float from = 0.0f, to = 0.0f, cur = 0.0f;
    double start = 0.0;
    float dur = ANIM_DUR;
    bool init = false;

    void SetTarget(float target, double now)
    {
        if (!init) { from = to = cur = target; init = true; return; }
        if (std::fabs(target - to) > 0.001f) { from = cur; to = target; start = now; }
    }

    void Update(double now)
    {
        if (!init) return;
        float t = dur > 0.0f ? (float)((now - start) / dur) : 1.0f;
        if (t >= 1.0f) { cur = to; return; }
        if (t < 0.0f) t = 0.0f;
        float e = t * t * (3.0f - 2.0f * t); // smoothstep
        cur = from + (to - from) * e;
    }
};

struct BlockAnim
{
    AnimF x, y, h, opacity;
    AnimF colR, colG, colB;
    bool colorInit = false;
};

struct GroupAnim
{
    AnimF x; // shared x for date label / total label / pin box
};

// ---------------------------------------------------------------------
// Per-frame layout structs (rebuilt every render, animation state persists
// separately in EvalState::anim / groupAnim keyed by date/name).
// ---------------------------------------------------------------------
struct BarBlockVis
{
    std::string name;
    double dur = 0.0;
    float baseX = 0, baseY = 0, baseH = 0;
    bool isCore = false;
    float fillT = 0.0f;
};

struct BarGroupVis
{
    const EvalRun* run = nullptr;
    std::vector<BarBlockVis> blocks; // bottom -> top play order
    float baseX = 0;
    int origIndex = 0;
};

struct EvalState
{
    std::vector<EvalRun> allRunsChronological;
    std::unordered_map<std::string, EvalStat> allTimeStats;
    int windowStart = 0;
    bool loaded = false;
    bool loadError = false;
    std::string errorMessage;

    std::set<std::string> pinnedDates;
    std::string stretchedName;

    std::string hoverName;
    int hoverAnchorOrigIdx = -1;

    std::string pendingFlashDate;
    double flashStartTime = -1.0;
    float flashX = 0, flashTop = 0, flashBottom = 0;
    bool flashPending = false;

    std::unordered_map<std::string, BlockAnim> anim;      // key: date + "\x1f" + blockName
    std::unordered_map<std::string, GroupAnim> groupAnim;  // key: date

    float footerWidth = 700.0f;
    bool haveMousePrev = false;
};

static EvalState& GetState()
{
    static EvalState s;
    return s;
}

static std::string AnimKey(const std::string& date, const std::string& name)
{
    std::string k;
    k.reserve(date.size() + 1 + name.size());
    k += date;
    k += '\x1f';
    k += name;
    return k;
}

// ---------------------------------------------------------------------
// Hover-cluster / stretch-cluster reorder -- mirrors computeClusterOrder():
// matching bars are pulled immediately adjacent to the anchor bar, keeping
// their original left/right side and relative order; everything else keeps
// its relative order around that cluster.
// ---------------------------------------------------------------------
static std::vector<int> ComputeClusterOrder(const std::vector<int>& matchIdxs, int anchorIdx, int n)
{
    std::set<int> isMatch(matchIdxs.begin(), matchIdxs.end());
    std::vector<int> leftMatches, rightMatches;
    for (int i : matchIdxs)
    {
        if (i < anchorIdx) leftMatches.push_back(i);
        else if (i > anchorIdx) rightMatches.push_back(i);
    }
    std::sort(leftMatches.begin(), leftMatches.end());
    std::sort(rightMatches.begin(), rightMatches.end());

    std::vector<int> before, after;
    for (int i = 0; i < n; i++)
    {
        if (isMatch.count(i)) continue;
        if (i < anchorIdx) before.push_back(i);
        else if (i > anchorIdx) after.push_back(i);
    }

    std::vector<int> seq;
    seq.insert(seq.end(), before.begin(), before.end());
    seq.insert(seq.end(), leftMatches.begin(), leftMatches.end());
    seq.push_back(anchorIdx);
    seq.insert(seq.end(), rightMatches.begin(), rightMatches.end());
    seq.insert(seq.end(), after.begin(), after.end());
    return seq;
}

static void PageBy(EvalState& cs, int delta)
{
    int nonPinnedCount = (int)cs.allRunsChronological.size() - (int)cs.pinnedDates.size();
    int availableSlots = std::max(0, WINDOW_SIZE - (int)cs.pinnedDates.size());
    int maxStart = std::max(0, nonPinnedCount - availableSlots);
    cs.windowStart = std::max(0, std::min(maxStart, cs.windowStart + delta));
}

static void DrawFooterNote(EvalState& cs)
{
    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + cs.footerWidth);
    ImGui::PushStyleColor(ImGuiCol_Text, HexColor(TEXT_DIM));
    ImGui::TextWrapped(
        "Each bar is one run, with segments stacked bottom-to-top in play order -- whichever "
        "segments are present in every run currently on screen are core, everything else is "
        "rotating; this is recomputed live from whatever's in view. Hovering a block snaps every "
        "same-named block onto a shared bottom line and pulls those bars horizontally adjacent to "
        "the one you're pointing at. The window always shows up to 15 runs; Prev/Next page by 1, "
        "shift+click pages by a full window. Check the box under a bar to pin it to the far right, "
        "locked in place across every range so you can compare runs that are far apart in your "
        "history (up to 15 pinned at once). Click a block to stretch every occurrence of that "
        "fractal on screen for comparison; click it again (or elsewhere) to undo.");
    ImGui::PopStyleColor();
    ImGui::PopTextWrapPos();
}

// Draws the interactive chart. Returns nothing; all interaction/state
// mutation happens on cs directly.
static void DrawChart(EvalState& cs)
{
    double now = ImGui::GetTime();
    float dt = ImGui::GetIO().DeltaTime;
    (void)dt;

    // ---- windowed + pinned run selection (mirrors render()'s dataset math) ----
    std::vector<const EvalRun*> nonPinned, pinnedRuns;
    for (auto& r : cs.allRunsChronological)
    {
        if (cs.pinnedDates.count(r.date)) pinnedRuns.push_back(&r);
        else nonPinned.push_back(&r);
    }
    int availableSlots = std::max(0, WINDOW_SIZE - (int)pinnedRuns.size());
    int maxStart = std::max(0, (int)nonPinned.size() - availableSlots);
    cs.windowStart = std::max(0, std::min(maxStart, cs.windowStart));

    std::vector<const EvalRun*> runs;
    for (int i = cs.windowStart; i < std::min((int)nonPinned.size(), cs.windowStart + availableSlots); i++)
        runs.push_back(nonPinned[i]);
    for (auto* p : pinnedRuns) runs.push_back(p);

    // ---- core = intersection of block-name sets across the visible runs ----
    std::set<std::string> core;
    if (!runs.empty())
    {
        core.clear();
        for (auto& b : runs[0]->blocks) core.insert(b.name);
        for (size_t i = 1; i < runs.size(); i++)
        {
            std::set<std::string> s;
            for (auto& b : runs[i]->blocks) s.insert(b.name);
            std::set<std::string> inter;
            std::set_intersection(core.begin(), core.end(), s.begin(), s.end(), std::inserter(inter, inter.begin()));
            core = inter;
        }
    }
    std::vector<std::string> coreOrder(core.begin(), core.end()); // std::set of strings is already sorted

    double maxTotal = 0.0;
    for (auto* r : runs) maxTotal = std::max(maxTotal, r->total_time);
    if (maxTotal <= 0.0) maxTotal = 1.0;
    double maxMinutes = (maxTotal / 60.0) * 1.08;

    auto yForSeconds = [&](double sec) -> float {
        double minutes = sec / 60.0;
        return (float)(TOP_PAD + PLOT_H - (minutes / maxMinutes) * PLOT_H);
    };
    auto hForSeconds = [&](double sec) -> float {
        return (float)((sec / 60.0 / maxMinutes) * PLOT_H);
    };

    // ---- base (pre-hover/pre-stretch) layout ----
    std::vector<BarGroupVis> groups(runs.size());
    for (size_t ri = 0; ri < runs.size(); ri++)
    {
        const EvalRun* run = runs[ri];
        float x = BAR_GAP + ri * (BAR_W + BAR_GAP);
        groups[ri].run = run;
        groups[ri].baseX = x;
        groups[ri].origIndex = (int)ri;

        std::vector<const EvalBlock*> coreBlocks, rotBlocks;
        for (auto& b : run->blocks)
        {
            if (core.count(b.name)) coreBlocks.push_back(&b);
            else rotBlocks.push_back(&b);
        }
        std::sort(coreBlocks.begin(), coreBlocks.end(), [&](const EvalBlock* a, const EvalBlock* b) {
            int ia = (int)(std::find(coreOrder.begin(), coreOrder.end(), a->name) - coreOrder.begin());
            int ib = (int)(std::find(coreOrder.begin(), coreOrder.end(), b->name) - coreOrder.begin());
            return ia < ib;
        });

        double cum = 0.0;
        for (auto& b : run->blocks)
        {
            bool isCore = core.count(b.name) != 0;
            auto& grp = isCore ? coreBlocks : rotBlocks;
            int pos = (int)(std::find(grp.begin(), grp.end(), &b) - grp.begin());
            int len = (int)grp.size();
            float t = len > 1 ? (float)pos / (float)(len - 1) : 0.0f;

            float yTop = yForSeconds(cum + b.dur);
            float h = hForSeconds(b.dur);
            cum += b.dur;

            BarBlockVis bv;
            bv.name = b.name;
            bv.dur = b.dur;
            bv.baseX = x;
            bv.baseY = yTop;
            bv.baseH = h;
            bv.isCore = isCore;
            bv.fillT = t;
            groups[ri].blocks.push_back(bv);
        }
    }

    // ---- canvas setup ----
    float chartW = runs.empty() ? BAR_GAP : (float)runs.size() * (BAR_W + BAR_GAP) + BAR_GAP;
    ImGui::BeginChild("##fractal_chart_canvas", ImVec2(std::min(chartW, ImGui::GetContentRegionAvail().x), CHART_H + 8.0f),
                       false, ImGuiWindowFlags_HorizontalScrollbar);

    ImVec2 origin = ImGui::GetCursorScreenPos();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 mouseScreen = ImGui::GetIO().MousePos;
    ImVec2 mouseLocal(mouseScreen.x - origin.x, mouseScreen.y - origin.y);
    bool canvasHovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows | ImGuiHoveredFlags_AllowWhenBlockedByPopup);

    // ---- hit-test against the PREVIOUS frame's animated geometry, exactly
    // like the browser hit-tests whatever's actually painted right now, even
    // mid-transition. ----
    std::string hitName;
    const BarGroupVis* hitGroup = nullptr;
    const BarBlockVis* hitBlock = nullptr;
    int hitGroupIdx = -1;

    if (canvasHovered)
    {
        for (int gi = (int)groups.size() - 1; gi >= 0; gi--)
        {
            auto& g = groups[gi];
            for (int bi = (int)g.blocks.size() - 1; bi >= 0; bi--)
            {
                auto& b = g.blocks[bi];
                std::string key = AnimKey(g.run->date, b.name);
                auto it = cs.anim.find(key);
                float rx = it != cs.anim.end() && it->second.x.init ? it->second.x.cur : b.baseX;
                float ry = it != cs.anim.end() && it->second.y.init ? it->second.y.cur : b.baseY;
                float rh = it != cs.anim.end() && it->second.h.init ? it->second.h.cur : b.baseH;
                if (mouseLocal.x >= rx && mouseLocal.x <= rx + BAR_W && mouseLocal.y >= ry && mouseLocal.y <= ry + rh)
                {
                    hitName = b.name;
                    hitGroup = &g;
                    hitBlock = &b;
                    hitGroupIdx = gi;
                    goto hitDone;
                }
            }
        }
    }
hitDone:

    bool leftClicked = canvasHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left);
    bool rightClicked = canvasHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right);

    // ---- click-to-stretch state transitions ----
    if (rightClicked && !cs.stretchedName.empty())
    {
        cs.stretchedName.clear();
    }
    else if (leftClicked)
    {
        if (!hitName.empty())
        {
            if (!cs.stretchedName.empty())
            {
                if (cs.stretchedName == hitName) cs.stretchedName.clear();
                else cs.stretchedName = hitName;
            }
            // (when nothing is stretched yet, a plain click also starts one --
            // matches "click a block to persistently enlarge every occurrence")
            else
            {
                cs.stretchedName = hitName;
            }
        }
        else if (!cs.stretchedName.empty())
        {
            cs.stretchedName.clear();
        }
    }

    bool stretched = !cs.stretchedName.empty();

    // hover is suppressed entirely while stretched, except for showing a
    // tooltip on the stretched fractal's own blocks -- matches onHover()'s
    // early-return behavior.
    std::string effectiveHoverName;
    if (!stretched && !hitName.empty())
    {
        effectiveHoverName = hitName;
        if (cs.hoverName != hitName)
        {
            cs.hoverName = hitName;
            cs.hoverAnchorOrigIdx = hitGroupIdx;
        }
    }
    else if (!stretched)
    {
        cs.hoverName.clear();
        cs.hoverAnchorOrigIdx = -1;
    }

    // ---- compute per-block targets ----
    // slot index per group after any clustering reorder (defaults to identity)
    std::vector<int> slotOfOrig((int)groups.size());
    for (int i = 0; i < (int)groups.size(); i++) slotOfOrig[i] = i;

    std::string clusterName = stretched ? cs.stretchedName : effectiveHoverName;
    int clusterAnchor = stretched ? -1 : cs.hoverAnchorOrigIdx;

    if (!clusterName.empty())
    {
        std::vector<int> matchIdxs;
        for (int gi = 0; gi < (int)groups.size(); gi++)
            for (auto& b : groups[gi].blocks)
                if (b.name == clusterName) { matchIdxs.push_back(gi); break; }

        if (stretched && !matchIdxs.empty())
            clusterAnchor = hitGroupIdx != -1 ? hitGroupIdx : matchIdxs[0];

        if (matchIdxs.size() > 1 && clusterAnchor != -1)
        {
            auto order = ComputeClusterOrder(matchIdxs, clusterAnchor, (int)groups.size());
            for (int slot = 0; slot < (int)order.size(); slot++)
                slotOfOrig[order[slot]] = slot;
        }
    }

    for (int gi = 0; gi < (int)groups.size(); gi++)
    {
        auto& g = groups[gi];
        float targetX = BAR_GAP + slotOfOrig[gi] * (BAR_W + BAR_GAP);

        std::string gkey = g.run->date;
        auto& ganim = cs.groupAnim[gkey];
        ganim.x.SetTarget(targetX, now);
        ganim.x.Update(now);

        bool groupHasHoverMatch = false;
        if (!effectiveHoverName.empty())
            for (auto& b : g.blocks) if (b.name == effectiveHoverName) { groupHasHoverMatch = true; break; }

        // ---- vertical cascade for hover (snap matched block to a shared
        // bottom line, push the rest of the stack out of the way) ----
        std::vector<float> yOverride(g.blocks.size(), NAN), hOverride(g.blocks.size(), NAN);
        if (!stretched && !effectiveHoverName.empty() && groupHasHoverMatch)
        {
            int anchorGroupIdx = cs.hoverAnchorOrigIdx >= 0 ? cs.hoverAnchorOrigIdx : gi;
            auto& anchorGroup = groups[anchorGroupIdx];
            float targetBottom = 0.0f;
            for (auto& b : anchorGroup.blocks)
                if (b.name == effectiveHoverName) { targetBottom = b.baseY + b.baseH; break; }

            int idx = -1;
            for (int bi = 0; bi < (int)g.blocks.size(); bi++)
                if (g.blocks[bi].name == effectiveHoverName) { idx = bi; break; }

            if (idx != -1)
            {
                float targetH = g.blocks[idx].baseH;
                float targetTopY = targetBottom - targetH;
                yOverride[idx] = targetTopY;
                hOverride[idx] = targetH;

                float yCursor = targetTopY + targetH;
                for (int i = idx - 1; i >= 0; i--)
                {
                    float h = g.blocks[i].baseH;
                    yCursor += h;
                    yOverride[i] = yCursor - h;
                    hOverride[i] = h;
                }
                yCursor = targetTopY;
                for (int i = idx + 1; i < (int)g.blocks.size(); i++)
                {
                    float h = g.blocks[i].baseH;
                    yCursor -= h;
                    yOverride[i] = yCursor;
                    hOverride[i] = h;
                }
            }
        }

        // ---- stretch geometry (grows the matched block from a shared
        // bottom line; reflows the rest of that bar around it) ----
        bool groupHasStretchMatch = false;
        int stretchIdx = -1;
        if (stretched)
        {
            for (int bi = 0; bi < (int)g.blocks.size(); bi++)
                if (g.blocks[bi].name == cs.stretchedName) { stretchIdx = bi; groupHasStretchMatch = true; break; }
        }
        if (groupHasStretchMatch)
        {
            double maxDur = 0.0;
            for (auto& gg : groups)
                for (auto& b : gg.blocks)
                    if (b.name == cs.stretchedName) maxDur = std::max(maxDur, b.dur);
            float targetMaxH = PLOT_H * STRETCH_HEADROOM;
            float plotCenter = TOP_PAD + PLOT_H / 2.0f;
            float sharedBottom = plotCenter + targetMaxH / 2.0f;

            double dur = g.blocks[stretchIdx].dur;
            float newH = maxDur > 0.0 ? (float)(dur / maxDur) * targetMaxH : 0.0f;
            float newY = sharedBottom - newH;
            yOverride[stretchIdx] = newY;
            hOverride[stretchIdx] = newH;

            float yCursor = newY + newH;
            for (int i = stretchIdx - 1; i >= 0; i--)
            {
                float h = g.blocks[i].baseH;
                yOverride[i] = yCursor;
                hOverride[i] = h;
                yCursor += h;
            }
            yCursor = newY;
            for (int i = stretchIdx + 1; i < (int)g.blocks.size(); i++)
            {
                float h = g.blocks[i].baseH;
                yCursor -= h;
                yOverride[i] = yCursor;
                hOverride[i] = h;
            }
        }

        for (int bi = 0; bi < (int)g.blocks.size(); bi++)
        {
            auto& b = g.blocks[bi];
            std::string key = AnimKey(g.run->date, b.name);
            auto& a = cs.anim[key];

            float tx = targetX;
            float ty = !std::isnan(yOverride[bi]) ? yOverride[bi] : b.baseY;
            float th = !std::isnan(hOverride[bi]) ? hOverride[bi] : b.baseH;

            float topacity = 1.0f;
            if (stretched)
            {
                topacity = (b.name == cs.stretchedName) ? 1.0f : 0.25f;
            }
            else if (!effectiveHoverName.empty())
            {
                topacity = groupHasHoverMatch ? 1.0f : 0.35f;
            }

            a.x.dur = ANIM_DUR; a.y.dur = ANIM_DUR; a.h.dur = ANIM_DUR; a.opacity.dur = FADE_DUR;
            a.x.SetTarget(tx, now);
            a.y.SetTarget(ty, now);
            a.h.SetTarget(th, now);
            a.opacity.SetTarget(topacity, now);
            a.x.Update(now); a.y.Update(now); a.h.Update(now); a.opacity.Update(now);

            unsigned int darkC = b.isCore ? CORE_DARK : ROT_DARK;
            unsigned int lightC = b.isCore ? CORE_LIGHT : ROT_LIGHT;
            float baseR, baseG, baseB;
            {
                float dr, dg, db, lr, lg, lb;
                HexToRGB(darkC, dr, dg, db);
                HexToRGB(lightC, lr, lg, lb);
                baseR = dr + (lr - dr) * b.fillT;
                baseG = dg + (lg - dg) * b.fillT;
                baseB = db + (lb - db) * b.fillT;
            }
            bool isFocused = (stretched && b.name == cs.stretchedName && hitBlock && hitGroup == &g) ||
                              (!stretched && !effectiveHoverName.empty() && b.name == effectiveHoverName);
            float fr, fg, fb;
            if (isFocused) HexToRGB(FOCUS, fr, fg, fb); else { fr = baseR; fg = baseG; fb = baseB; }

            a.colR.dur = FILL_DUR; a.colG.dur = FILL_DUR; a.colB.dur = FILL_DUR;
            a.colR.SetTarget(fr, now); a.colG.SetTarget(fg, now); a.colB.SetTarget(fb, now);
            a.colR.Update(now); a.colG.Update(now); a.colB.Update(now);

            ImVec2 p0(origin.x + a.x.cur, origin.y + a.y.cur);
            ImVec2 p1(p0.x + BAR_W, p0.y + a.h.cur);
            ImU32 col = ImGui::ColorConvertFloat4ToU32(ImVec4(a.colR.cur, a.colG.cur, a.colB.cur, a.opacity.cur));
            dl->AddRectFilled(p0, p1, col);
            dl->AddRect(p0, p1, HexColor(0x14161a, a.opacity.cur));
        }
    }

    // ---- resolve a pending jump-to-fastest flash target now that `groups`
    // (and the x each bar animates toward) is known ----
    if (!cs.pendingFlashDate.empty())
    {
        for (auto& g : groups)
        {
            if (g.run->date == cs.pendingFlashDate)
            {
                double blockDurSum = 0.0;
                for (auto& b : g.blocks) blockDurSum += b.dur;
                auto& ganim = cs.groupAnim[g.run->date];
                cs.flashX = ganim.x.cur;
                cs.flashTop = yForSeconds(blockDurSum);
                cs.flashBottom = yForSeconds(0.0);
                break;
            }
        }
        cs.pendingFlashDate.clear();
    }

    // ---- total-time band (top) ----
    {
        ImVec2 p0(origin.x, origin.y + TOTAL_BAND_H - 18.0f);
        ImVec2 p1(origin.x + chartW - 4.0f, origin.y + TOTAL_BAND_H);
        dl->AddRectFilled(p0, p1, HexColor(BAND_BG), 9.0f);
        dl->AddRect(p0, p1, HexColor(BAND_STROKE), 9.0f);
        for (auto& g : groups)
        {
            auto& ganim = cs.groupAnim[g.run->date];
            float cx = origin.x + ganim.x.cur + BAR_W / 2.0f;

            bool groupHasStretched = false;
            double stretchDur = 0.0;
            if (stretched)
            {
                for (auto& b : g.blocks)
                    if (b.name == cs.stretchedName) { groupHasStretched = true; stretchDur = b.dur; break; }
            }
            std::string label = groupHasStretched ? FormatSegmentTime(stretchDur) : FormatRunTime(g.run->total_time, false);

            ImVec2 ts = ImGui::CalcTextSize(label.c_str());
            dl->AddText(ImVec2(cx - ts.x / 2.0f, origin.y + TOTAL_BAND_H - 5.0f - ts.y), HexColor(TEXT_DIM), label.c_str());
        }
    }

    // ---- date band (bottom) ----
    float dateBandY = CHART_H - BOTTOM_PAD + 2.0f;
    {
        ImVec2 p0(origin.x, origin.y + dateBandY);
        ImVec2 p1(origin.x + chartW - 4.0f, origin.y + dateBandY + 18.0f);
        dl->AddRectFilled(p0, p1, HexColor(BAND_BG), 9.0f);
        dl->AddRect(p0, p1, HexColor(BAND_STROKE), 9.0f);
        for (auto& g : groups)
        {
            auto& ganim = cs.groupAnim[g.run->date];
            float cx = origin.x + ganim.x.cur + BAR_W / 2.0f;
            std::string mmdd = g.run->date.size() >= 10 ? g.run->date.substr(5, 5) : g.run->date;
            ImVec2 ts = ImGui::CalcTextSize(mmdd.c_str());
            dl->AddText(ImVec2(cx - ts.x / 2.0f, origin.y + dateBandY + 15.0f - ts.y), HexColor(TEXT_DIM), mmdd.c_str());
        }
    }

    // ---- pin row ----
    float pinRowY = dateBandY + 18.0f + PIN_ROW_GAP;
    {
        ImVec2 p0(origin.x, origin.y + pinRowY);
        ImVec2 p1(origin.x + chartW - 4.0f, origin.y + pinRowY + PIN_ROW_H);
        dl->AddRectFilled(p0, p1, HexColor(BAND_BG), 9.0f);
        dl->AddRect(p0, p1, HexColor(BAND_STROKE), 9.0f);

        for (auto& g : groups)
        {
            auto& ganim = cs.groupAnim[g.run->date];
            float cx = origin.x + ganim.x.cur + BAR_W / 2.0f;
            float cy = origin.y + pinRowY + PIN_ROW_H / 2.0f;
            float boxSize = 12.0f;
            bool isPinned = cs.pinnedDates.count(g.run->date) != 0;

            ImVec2 b0(cx - boxSize / 2.0f, cy - boxSize / 2.0f);
            ImVec2 b1(cx + boxSize / 2.0f, cy + boxSize / 2.0f);

            bool boxHovered = canvasHovered && mouseScreen.x >= b0.x && mouseScreen.x <= b1.x &&
                               mouseScreen.y >= b0.y && mouseScreen.y <= b1.y;
            if (boxHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
            {
                if (isPinned) cs.pinnedDates.erase(g.run->date);
                else if ((int)cs.pinnedDates.size() < WINDOW_SIZE) cs.pinnedDates.insert(g.run->date);
                isPinned = !isPinned; // reflect immediately this frame
            }

            ImU32 strokeCol = HexColor(boxHovered ? ACCENT : 0x4a4f57);
            if (isPinned)
            {
                dl->AddRectFilled(b0, b1, HexColor(ACCENT), 3.0f);
                dl->AddRect(b0, b1, HexColor(ACCENT), 3.0f);
                dl->AddLine(ImVec2(cx - 3.5f, cy), ImVec2(cx - 1.2f, cy + 2.3f), HexColor(BAND_BG), 1.8f);
                dl->AddLine(ImVec2(cx - 1.2f, cy + 2.3f), ImVec2(cx + 3.5f, cy - 2.6f), HexColor(BAND_BG), 1.8f);
            }
            else
            {
                dl->AddRect(b0, b1, strokeCol, 3.0f);
            }
        }
    }

    // ---- click-anywhere-else-while-stretched exits the stretch ----
    if (leftClicked && stretched && hitName.empty() && canvasHovered)
    {
        // pin-row clicks are handled above and shouldn't also exit the
        // stretch; a plain click on empty chart space does.
        bool onPinRow = mouseLocal.y >= pinRowY && mouseLocal.y <= pinRowY + PIN_ROW_H;
        if (!onPinRow) cs.stretchedName.clear();
    }

    // ---- one-time "jump to fastest" flash ----
    if (cs.flashPending)
    {
        if (cs.flashStartTime < 0.0) cs.flashStartTime = now;
        double elapsed = now - cs.flashStartTime;
        float alpha = 0.0f;
        if (elapsed < FLASH_IN) alpha = 0.85f * (float)(elapsed / FLASH_IN);
        else if (elapsed < FLASH_OUT_END) alpha = 0.85f * (1.0f - (float)((elapsed - FLASH_IN) / (FLASH_OUT_END - FLASH_IN)));
        else { cs.flashPending = false; alpha = 0.0f; }

        if (alpha > 0.0f)
        {
            ImVec2 p0(origin.x + cs.flashX, origin.y + cs.flashTop);
            ImVec2 p1(origin.x + cs.flashX + BAR_W, origin.y + cs.flashBottom);
            dl->AddRectFilled(p0, p1, ImGui::ColorConvertFloat4ToU32(ImVec4(1, 1, 1, alpha)));
        }
    }

    // ---- tooltip ----
    if (!hitName.empty() && hitBlock)
    {
        bool allowedWhileStretched = !stretched || hitName == cs.stretchedName;
        if (allowedWhileStretched)
        {
            ImGui::BeginTooltip();
            ImGui::PushStyleColor(ImGuiCol_Text, HexColor(FOCUS));
            ImGui::TextUnformatted(hitName.c_str());
            ImGui::PopStyleColor();

            auto statIt = cs.allTimeStats.find(hitName);
            if (statIt != cs.allTimeStats.end())
            {
                const EvalStat& s = statIt->second;
                ImGui::Text("%d occurrence%s in database", s.count, s.count > 1 ? "s" : "");
                ImGui::Separator();
                ImGui::Text("This time: %s", FormatSegmentTime(hitBlock->dur).c_str());
                std::string diff;
                if (FormatDiff(hitBlock->dur, s.bestDur, diff))
                {
                    ImGui::Text("Best: %s (%s)", FormatSegmentTime(s.bestDur).c_str(), diff.c_str());
                    ImGui::Text("Best run: %s", s.bestDate.c_str());
                }
                else
                {
                    ImGui::Text("Best: %s -- this is it!", FormatSegmentTime(s.bestDur).c_str());
                }
                ImGui::Separator();
                ImGui::Text("Average: %s", FormatSegmentTime(s.totalDur / s.count).c_str());
            }
            else
            {
                ImGui::TextDisabled("0 occurrences");
            }
            ImGui::EndTooltip();
        }
    }

    ImGui::EndChild();
    cs.footerWidth = std::max(cs.footerWidth, ImGui::GetItemRectSize().x);

    // ---- paging button availability (disabled at either end / while every
    // slot is pinned) ----
    bool pagingLocked = availableSlots <= 0;
    bool prevDisabled = pagingLocked || cs.windowStart <= 0;
    bool nextDisabled = pagingLocked || (cs.windowStart + availableSlots >= (int)nonPinned.size());

    ImGui::SameLine();
    ImGui::BeginGroup();
    ImGui::PushStyleVar(ImGuiStyleVar_Alpha, prevDisabled ? 0.4f : 1.0f);
    if (ImGui::Button("<##prevPage") && !prevDisabled)
        PageBy(cs, ImGui::GetIO().KeyShift ? -WINDOW_SIZE : -1);
    ImGui::PopStyleVar();
    ImGui::Dummy(ImVec2(1, 8));
    ImGui::PushStyleVar(ImGuiStyleVar_Alpha, nextDisabled ? 0.4f : 1.0f);
    if (ImGui::Button(">##nextPage") && !nextDisabled)
        PageBy(cs, ImGui::GetIO().KeyShift ? WINDOW_SIZE : 1);
    ImGui::PopStyleVar();
    ImGui::EndGroup();
}

static void DrawControls(EvalState& cs)
{
    ImGui::BeginGroup();
    if (!cs.stretchedName.empty())
    {
        ImGui::TextColored(ImVec4(0.85f, 0.85f, 0.9f, 1.0f), "stretched:");
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(1, 1, 1, 1), "%s", cs.stretchedName.c_str());
        ImGui::SameLine();
    }

    bool jumpDisabled = !cs.stretchedName.empty();
    ImGui::PushStyleVar(ImGuiStyleVar_Alpha, jumpDisabled ? 0.4f : 1.0f);
    if (ImGui::Button("\xE2\x9A\xA1 fastest run") && !jumpDisabled && !cs.allRunsChronological.empty())
    {
        int bestIdx = 0;
        for (int i = 1; i < (int)cs.allRunsChronological.size(); i++)
            if (cs.allRunsChronological[i].total_time < cs.allRunsChronological[bestIdx].total_time)
                bestIdx = i;
        const EvalRun& bestRun = cs.allRunsChronological[bestIdx];
        cs.pendingFlashDate = bestRun.date;
        cs.flashPending = true;
        cs.flashStartTime = -1.0;

        if (!cs.pinnedDates.count(bestRun.date))
        {
            std::vector<const EvalRun*> nonPinned;
            for (auto& r : cs.allRunsChronological)
                if (!cs.pinnedDates.count(r.date)) nonPinned.push_back(&r);
            int availableSlots = std::max(0, WINDOW_SIZE - (int)cs.pinnedDates.size());
            int idxInNonPinned = -1;
            for (int i = 0; i < (int)nonPinned.size(); i++)
                if (nonPinned[i]->date == bestRun.date) { idxInNonPinned = i; break; }
            int maxStart = std::max(0, (int)nonPinned.size() - availableSlots);
            if (idxInNonPinned != -1)
                cs.windowStart = std::max(0, std::min(maxStart, idxInNonPinned - availableSlots / 2));
        }
    }
    ImGui::PopStyleVar();
    ImGui::SameLine();

    ImGui::ColorButton("##coreSwatch", ImVec4(0x6f / 255.0f, 0xb3 / 255.0f, 0xe8 / 255.0f, 1.0f),
                        ImGuiColorEditFlags_NoTooltip | ImGuiColorEditFlags_NoBorder, ImVec2(12, 12));
    ImGui::SameLine();
    ImGui::TextDisabled("Core (always present in window)");
    ImGui::SameLine();
    ImGui::ColorButton("##rotSwatch", ImVec4(0xf0 / 255.0f, 0xa8 / 255.0f, 0x4e / 255.0f, 1.0f),
                        ImGuiColorEditFlags_NoTooltip | ImGuiColorEditFlags_NoBorder, ImVec2(12, 12));
    ImGui::SameLine();
    ImGui::TextDisabled("Rotating");
    ImGui::SameLine();
    ImGui::ColorButton("##focusSwatch", ImVec4(1.0f, 0x4f / 255.0f, 0xb0 / 255.0f, 1.0f),
                        ImGuiColorEditFlags_NoTooltip | ImGuiColorEditFlags_NoBorder, ImVec2(12, 12));
    ImGui::SameLine();
    ImGui::TextDisabled("Hover focus");
    ImGui::EndGroup();
}

// consumes cs.pendingFlashDate once the just-built groups are known -- called
// from DrawChart right after the base layout is built, but since we need the
// flash column's x/top/bottom we resolve it there instead; kept as a helper
// name here for clarity of intent (folded into DrawChart above).

} // namespace EvalTool

void RenderEvaluationWindow()
{
    if (!ShowEvaluation)
        return;

    using namespace EvalTool;
    EvalState& cs = GetState();

    if (!cs.loaded && !cs.loadError)
    {
        std::string err;

        std::string addonDir = GetAddonDir();
        // TODO: swap this path out once real data wiring is available -- for
        // now this is hardcoded to the sample history file per current instructions.
        std::string historyPath = addonDir + "/Fractals_Fake.history";

        if (LoadHistoryFile(historyPath, cs.allRunsChronological, err))
        {
            std::reverse(cs.allRunsChronological.begin(), cs.allRunsChronological.end());
            cs.allTimeStats = ComputeAllTimeStats(cs.allRunsChronological);
            cs.windowStart = std::max(0, (int)cs.allRunsChronological.size() - WINDOW_SIZE);
            cs.loaded = true;
        }
        else
        {
            cs.loadError = true;
            cs.errorMessage = err;
        }
    }

    ImGui::SetNextWindowSize(ImVec2(920, 640), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Evaluation", &ShowEvaluation))
    {
        ImGui::End();
        return;
    }

    if (cs.loadError)
    {
        ImGui::TextColored(ImVec4(0.94f, 0.5f, 0.5f, 1.0f), "%s", cs.errorMessage.c_str());
        ImGui::End();
        return;
    }
    if (!cs.loaded)
    {
        ImGui::TextDisabled("Loading...");
        ImGui::End();
        return;
    }
    if (cs.allRunsChronological.empty())
    {
        ImGui::TextDisabled("That file parsed fine but contains no runs.");
        ImGui::End();
        return;
    }

    DrawControls(cs);
    ImGui::Spacing();
    DrawChart(cs);
    ImGui::Spacing();
    DrawFooterNote(cs);

    ImGui::End();
}