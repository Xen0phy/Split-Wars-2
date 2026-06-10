// render_timer_stream_seg.cpp
// Segmented-window variant of the streamer timer overlay.
//
// Each split section is its own ImGui window pinned below a single draggable
// anchor. When a split exceeds MAX_VISIBLE_SPLITS, it is evicted from the
// stack and plays a slide-up + fade-out animation before disappearing.
//
// Call RenderTimerOverlayStream() to use this variant (same function name
// as render_timer_stream.cpp so the two files are drop-in swappable via
// CMakeLists.txt).

#include "render_shared.h"
#include "shared.h"
#include "stream_fonts.h"

// ---------------------------------------------------------------------------
// Layout constants
// ---------------------------------------------------------------------------
static constexpr float SSW         = 280.0f;
static constexpr float SHEADER_H   = 22.0f;
static constexpr float STIME_ROW_H = 44.0f;
static constexpr float SACCENT_W   = 6.0f;
static constexpr float SPAD_X      = 10.0f;
static constexpr float SDIFF_PAD_X = 8.0f;
static constexpr float SSEC_H      = SHEADER_H + STIME_ROW_H;
static constexpr float SGAP_H      = 4.0f;
static constexpr int   MAX_VISIBLE_SPLITS = 5;
static constexpr float ANIM_DURATION     = 0.4f; // seconds for slide-out

// ---------------------------------------------------------------------------
// Colors
// ---------------------------------------------------------------------------

// Style-derived colors -- read at render time, follow Nexus theme automatically
static ImVec4 SC_Bg()         { return ImGui::GetStyle().Colors[ImGuiCol_WindowBg];     }
static ImVec4 SC_HeaderBg()   { return ImGui::GetStyle().Colors[ImGuiCol_TitleBg];      }
static ImVec4 SC_TextHeader() { return ImGui::GetStyle().Colors[ImGuiCol_Text];         }
static ImVec4 SC_AccentIdle() { return ImGui::GetStyle().Colors[ImGuiCol_Separator];    }
static ImVec4 SC_TextDim()    { return ImGui::GetStyle().Colors[ImGuiCol_TextDisabled]; }

static ImU32 SToU32(ImVec4 c) { return ImGui::ColorConvertFloat4ToU32(c); }
static ImU32 SToU32Alpha(ImVec4 c, float a)
{
    c.w *= a;
    return ImGui::ColorConvertFloat4ToU32(c);
}

// ---------------------------------------------------------------------------
// Anchor position
// ---------------------------------------------------------------------------
static ImVec2 s_AnchorPos     = { 10.0f, 10.0f };

// ---------------------------------------------------------------------------
// Outgoing split animation state
// When splitStart advances (a split is pushed off the top), we capture its
// display data here and animate it sliding upward + fading out.
// Only one slot needed since splits are evicted one at a time.
// ---------------------------------------------------------------------------
struct OutgoingSlot
{
    bool   active      = false;
    float  timer       = 0.0f;   // 0..ANIM_DURATION
    char   label[64]   = {};
    char   timeBuf[32] = {};
    char   diffMainBuf[32] = {};
    char   diffDecBuf[32]  = {};
    bool   hasDiff     = false;
    ImVec4 accent      = {};
    ImVec4 timeColor   = {};
    ImVec4 diffColor   = {};
    float  startY      = 0.0f;   // Y position at the moment of eviction
};

static OutgoingSlot s_Outgoing;
static int          s_LastSplitStart = -1;

// ---------------------------------------------------------------------------
// Drawing helpers
// ---------------------------------------------------------------------------
static void SDrawHeaderBarAlpha(ImDrawList* dl, ImVec2 pos, float width,
                                const char* label, ImVec4 accent, float alpha,
                                ImFont* headerFont = nullptr, float headerH = 0.0f)
{
    ImVec2 barMax = ImVec2(pos.x + width, pos.y + headerH);
    const float rounding = ImGui::GetStyle().WindowRounding;
    dl->AddRectFilled(pos, barMax, SToU32Alpha(SC_HeaderBg(), alpha), rounding, ImDrawCornerFlags_Top);
    dl->AddRectFilled(pos, ImVec2(pos.x + SACCENT_W, barMax.y), SToU32Alpha(accent, alpha));
    float fontSize = headerFont ? headerFont->FontSize : ImGui::GetFontSize();
    ImVec2 tp = ImVec2(pos.x + SACCENT_W + SPAD_X,
                       pos.y + (headerH - fontSize) * 0.5f);
    if (headerFont)
        dl->AddText(headerFont, fontSize, tp, SToU32Alpha(SC_TextHeader(), alpha), label);
    else
        dl->AddText(tp, SToU32Alpha(SC_TextHeader(), alpha), label);
}

// ---------------------------------------------------------------------------
// SFontSet -- the five ImFont* pointers derived from the user's chosen size.
//
//   mainFont        h:m:s  of the running time          (chosen size, e.g. 36)
//   mainMillisFont  .xxx   of the running time          (chosen - 2,  e.g. 34)
//   compFont        h:m:s  of the comparison / diff     (chosen - 2,  e.g. 34)
//   compMillisFont  .xxx   of the comparison / diff     (chosen - 4,  e.g. 32)
//   header          size   of the title bars and button text
//
// All five are resolved once per frame in RenderTimerOverlayStream and passed
// down to every drawing helper so the lookups happen only once.
// ---------------------------------------------------------------------------
struct SFontSet
{
    ImFont* main        = nullptr;  // running h:m:s      (S)
    ImFont* mainMillis  = nullptr;  // running .xxx       (S-2)
    ImFont* comp        = nullptr;  // comparison h:m:s   (S-2)
    ImFont* compMillis  = nullptr;  // comparison .xxx    (S-4)
    ImFont* header      = nullptr;  // title bar + buttons (StreamerHeaderFontSize)
};

// ---------------------------------------------------------------------------
// Helper: split a formatted time string "H:MM:SS.mmm" into two parts.
//   out_main receives everything up to (but not including) the last '.'
//   out_millis receives '.' and everything after it
// If no '.' is found, out_main gets the whole string, out_millis is empty.
// ---------------------------------------------------------------------------
static void SplitTimeAtDot(const char* buf,
                            char* out_main,   int main_sz,
                            char* out_millis, int millis_sz)
{
    const char* dot = strrchr(buf, '.');
    if (dot)
    {
        int len = (int)(dot - buf);
        if (len >= main_sz) len = main_sz - 1;
        strncpy(out_main, buf, len);
        out_main[len] = '\0';
        strncpy(out_millis, dot, millis_sz - 1);
        out_millis[millis_sz - 1] = '\0';
    }
    else
    {
        strncpy(out_main, buf, main_sz - 1);
        out_main[main_sz - 1] = '\0';
        out_millis[0] = '\0';
    }
}

// Draws text three times to produce a shadow + base + gradient overlay effect.
//   Layer 1 (bottom): shadow colour, shifted by StreamerDigitShadowOffset
//   Layer 2 (middle): base colour, rendered at (size - 4) so it sits visually smaller
//   Layer 3 (top):    overlay colour with vertical alpha gradient via vertex recolouring
static void SDrawStyledText(ImDrawList* dl, ImFont* font, float size,
                             ImVec2 pos, float rowTop, float rowHeight,
                             const char* text, float alpha)
{
    if (!text || text[0] == '\0') return;

    ImFont* f       = font ? font : ImGui::GetFont();
    float baseSize  = size - 4.0f;
    float basePosY  = pos.y + (size - baseSize); // baseline-align orange/red to black
    float shadowPosY = basePosY + CMDigitShadowOffset[1]; // anchor shadow to same baseline, then offset

    float shadowX = pos.x;
    float baseX   = pos.x;

    const char* p = text;
    while (*p)
    {
        // Get the codepoint
        unsigned int c = (unsigned char)*p;
        char glyph[5] = { *p, '\0' };
        p++;

        // Measure advance at both sizes
        float shadowAdv = f->CalcTextSizeA(size,     FLT_MAX, 0.0f, glyph, glyph + 1).x;
        float baseAdv   = f->CalcTextSizeA(baseSize, FLT_MAX, 0.0f, glyph, glyph + 1).x;

        // Layer 0: fill
        dl->AddText(f, size, ImVec2(shadowX, basePosY),
            IM_COL32((int)(CMDigitFillColor[0]*255), (int)(CMDigitFillColor[1]*255), (int)(CMDigitFillColor[2]*255), (int)(alpha*255)),
            glyph, glyph + 1);
        
        // Layer 1: shadow
        dl->AddText(f, size, ImVec2(shadowX + CMDigitShadowOffset[0], shadowPosY),
            IM_COL32((int)(CMDigitShadowColor[0]*255), (int)(CMDigitShadowColor[1]*255), (int)(CMDigitShadowColor[2]*255), (int)(alpha*255)),
            glyph, glyph + 1);
        
        // Layer 2: base
        float centerOffset = (shadowAdv - baseAdv) * 0.5f;
        dl->AddText(f, baseSize, ImVec2(baseX + centerOffset, basePosY),
            IM_COL32((int)(CMDigitBaseColor[0]*255), (int)(CMDigitBaseColor[1]*255), (int)(CMDigitBaseColor[2]*255), (int)(alpha*255)),
            glyph, glyph + 1);

        // Layer 3: overlay gradient — color from StreamerDigitOverlay, alpha 0 top to 1 bottom
        int vtxStart = dl->VtxBuffer.Size;
        dl->AddText(f, baseSize, ImVec2(baseX + centerOffset, basePosY), IM_COL32_WHITE, glyph, glyph + 1);
        int vtxEnd = dl->VtxBuffer.Size;
        
        float range = rowHeight > 0.0f ? rowHeight : 1.0f;
        ImDrawVert* verts = dl->VtxBuffer.Data;
        for (int i = vtxStart; i < vtxEnd; i++)
        {
            float t = ImClamp((verts[i].pos.y - rowTop) / range, 0.0f, 1.0f);
            verts[i].col = IM_COL32(
                (int)(CMDigitOverlay[0] * 255),
                (int)(CMDigitOverlay[1] * 255),
                (int)(CMDigitOverlay[2] * 255),
                (int)(t * alpha * 255)
            );
        }

        shadowX += shadowAdv;
        baseX   += shadowAdv; // base X tracks shadow advances so overall width stays consistent
    }
}

// ---------------------------------------------------------------------------
// Core drawing primitive used by both the live and the fade-out path.
//
// timeBuf      : full formatted running time (e.g. "0:01:23.456")
// showMillis   : whether milliseconds are present in timeBuf
// diffMainBuf  : h:m:s part of diff (may be empty)
// diffDecBuf   : .xxx part of diff  (may be empty)
// hasDiff      : whether to draw diff at all
// fonts        : resolved SFontSet (nullptrs fall back gracefully)
// alpha        : overall transparency (1.0 = opaque)
// ---------------------------------------------------------------------------
static void SDrawTimeRowAlpha(ImDrawList* dl, ImVec2 pos, float width,
                              const char* timeBuf,
                              const char* diffMainBuf, const char* diffDecBuf, bool hasDiff,
                              ImVec4 timeColor, ImVec4 diffColor,
                              const SFontSet& fonts, float alpha)
{
    ImVec2 rowMax = ImVec2(pos.x + width, pos.y + STIME_ROW_H);
    dl->AddRectFilled(pos, rowMax, SToU32Alpha(SC_Bg(), alpha), ImGui::GetStyle().WindowRounding, ImDrawCornerFlags_Bot);

    float bigScale = ImGui::GetCurrentWindow()->FontWindowScale;

    // --- Running time: split into h:m:s and .xxx, draw at different sizes ---
    {
        ImFont* fMain   = fonts.main;
        ImFont* fMillis = fonts.mainMillis;

        // Determine rendered pixel sizes
        float sizeMain   = (fMain ? fMain->FontSize : ImGui::GetFontSize()) * bigScale;
        // Use the millis font size if it is a distinct rasterization; if the fallback
        // collapsed mainMillis to the same pointer as main, render millis 2px smaller.
        float sizeMillis;
        if (fMillis && fMillis != fMain)
            sizeMillis = fMillis->FontSize * bigScale;
        else
            sizeMillis = sizeMain - 2.0f * bigScale;

        // Split timeBuf at the decimal point
        char hms[32] = {}, ms[16] = {};
        SplitTimeAtDot(timeBuf, hms, sizeof(hms), ms, sizeof(ms));

        // Vertically centre the larger (main) part in the row
        float timeY = pos.y + (STIME_ROW_H - sizeMain) * 0.5f;
        ImU32 tc    = SToU32Alpha(timeColor, alpha);

        // Draw h:m:s
        float curX = pos.x + SPAD_X;
        if (CrashMode)
            SDrawStyledText(dl, fMain, sizeMain, ImVec2(curX, timeY), pos.y, STIME_ROW_H, hms, alpha);
        else
            if (fMain)
                dl->AddText(fMain, sizeMain, ImVec2(curX, timeY), tc, hms);
            else
                dl->AddText(ImVec2(curX, timeY), tc, hms);

        // Draw .xxx aligned to bottom of h:m:s baseline
        if (ms[0] != '\0')
        {
            float hmsW = fMain
                ? fMain->CalcTextSizeA(sizeMain, FLT_MAX, 0.0f, hms).x
                : ImGui::GetFont()->CalcTextSizeA(sizeMain, FLT_MAX, 0.0f, hms).x;
            float msY = timeY + (sizeMain - sizeMillis);
            if (CrashMode)
                SDrawStyledText(dl, fMillis ? fMillis : ImGui::GetFont(), sizeMillis,
                                ImVec2(curX + hmsW, msY), pos.y, STIME_ROW_H, ms, alpha);
            else
                if (fMillis)
                    dl->AddText(fMillis, sizeMillis, ImVec2(curX + hmsW, msY), tc, ms);
                else
                    dl->AddText(ImGui::GetFont(), sizeMillis, ImVec2(curX + hmsW, msY), tc, ms);
        }
    }

    // --- Comparison / diff: h:m:s at comp size, .xxx at compMillis size ---
    if (hasDiff && diffMainBuf[0] != '\0')
    {
        ImFont* fComp       = fonts.comp;
        ImFont* fCompMillis = fonts.compMillis;

        float sizeComp       = (fComp ? fComp->FontSize : ImGui::GetFontSize()) * bigScale;
        float sizeCompMillis;
        if (fCompMillis && fCompMillis != fComp)
            sizeCompMillis = fCompMillis->FontSize * bigScale;
        else
            sizeCompMillis = sizeComp - 2.0f * bigScale;

        ImU32 dc    = SToU32Alpha(diffColor, alpha);
        float diffY = pos.y + (STIME_ROW_H - sizeComp) * 0.5f;

        // Measure both parts to right-align the whole diff block
        float mainW = fComp
            ? fComp->CalcTextSizeA(sizeComp, FLT_MAX, 0.0f, diffMainBuf).x
            : ImGui::GetFont()->CalcTextSizeA(sizeComp, FLT_MAX, 0.0f, diffMainBuf).x;
        float decW  = (diffDecBuf[0] != '\0')
            ? (fCompMillis
                ? fCompMillis->CalcTextSizeA(sizeCompMillis, FLT_MAX, 0.0f, diffDecBuf).x
                : ImGui::GetFont()->CalcTextSizeA(sizeCompMillis, FLT_MAX, 0.0f, diffDecBuf).x)
            : 0.0f;

        float startX = pos.x + width - mainW - decW - SDIFF_PAD_X;

        // Draw h:m:s part of diff
        if (fComp)
            dl->AddText(fComp, sizeComp, ImVec2(startX, diffY), dc, diffMainBuf);
        else
            dl->AddText(ImGui::GetFont(), sizeComp, ImVec2(startX, diffY), dc, diffMainBuf);

        // Draw .xxx part of diff, baseline-aligned
        if (diffDecBuf[0] != '\0')
        {
            float decY = diffY + (sizeComp - sizeCompMillis);
            if (fCompMillis)
                dl->AddText(fCompMillis, sizeCompMillis, ImVec2(startX + mainW, decY), dc, diffDecBuf);
            else
                dl->AddText(ImGui::GetFont(), sizeCompMillis, ImVec2(startX + mainW, decY), dc, diffDecBuf);
        }
    }
}

// Convenience wrappers at full alpha for normal rendering
static float SDrawHeaderBar(ImDrawList* dl, ImVec2 pos, float width,
                             const char* label, ImVec4 accent, ImFont* headerFont = nullptr,
                             float headerH = 0.0f)
{
    SDrawHeaderBarAlpha(dl, pos, width, label, accent, 1.0f, headerFont, headerH);
    return pos.y + headerH;
}

static float SDrawTimeRow(ImDrawList* dl, ImVec2 pos, float width,
                          double timeVal, bool showMillis,
                          bool hasDiff, double diff, bool isSplit,
                          ImVec4 timeColor, const SFontSet& fonts)
{
    char timeBuf[32], diffBuf[32];
    FormatTime(timeBuf, sizeof(timeBuf), timeVal, showMillis);

    char mainPart[32] = {}, decPart[32] = {};
    bool diffValid = false;
    ImVec4 diffColor = {};

    if (hasDiff)
    {
        diffValid = FormatDiff(diffBuf, sizeof(diffBuf), diff, isSplit, StreamerShowRunningMillis);
        diffColor = (diff < 0.0)
            ? ImVec4(ColorAhead[0],  ColorAhead[1],  ColorAhead[2],  1.0f)
            : ImVec4(ColorBehind[0], ColorBehind[1], ColorBehind[2], 1.0f);

        if (diffValid)
        {
            const char* dot = strrchr(diffBuf, '.');
            if (dot)
            {
                int len = (int)(dot - diffBuf);
                strncpy(mainPart, diffBuf, len);
                mainPart[len] = '\0';
                strncpy(decPart, dot, sizeof(decPart) - 1);
            }
            else
            {
                strncpy(mainPart, diffBuf, sizeof(mainPart) - 1);
            }
        }
    }

    SDrawTimeRowAlpha(dl, pos, width,
                      timeBuf,
                      (hasDiff && diffValid) ? mainPart : "",
                      (hasDiff && diffValid) ? decPart  : "",
                      hasDiff && diffValid,
                      timeColor, diffColor, fonts, 1.0f);
    return pos.y + STIME_ROW_H;
}

// ---------------------------------------------------------------------------
// BeginSection -- opens one pinned/anchor section window
// ---------------------------------------------------------------------------
static bool BeginSection(const char* id, ImVec2 pos, bool isAnchor)
{
    if (isAnchor)
        ImGui::SetNextWindowPos(pos, ImGuiCond_Once);
    else
        ImGui::SetNextWindowPos(pos, ImGuiCond_Always);

    ImGui::SetNextWindowBgAlpha(0.0f);

    ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoDecoration       |
        ImGuiWindowFlags_NoFocusOnAppearing |
        ImGuiWindowFlags_NoNav              |
        ImGuiWindowFlags_NoScrollbar        |
        ImGuiWindowFlags_NoScrollWithMouse  |
        ImGuiWindowFlags_NoResize;

    if (!isAnchor) flags |= ImGuiWindowFlags_NoMove;

    return ImGui::Begin(id, nullptr, flags);
}

// ---------------------------------------------------------------------------
// RenderTimerOverlayStream
// ---------------------------------------------------------------------------
void RenderTimerOverlayStream()
{
    if (!ShowTimer) return;

    const auto& splits    = SpeedrunTimer.GetSplits();
    double      elapsed   = SpeedrunTimer.GetElapsedSeconds();
    double      grand     = DisplayedGrandTotal;
    bool        running   = SpeedrunTimer.IsRunning();
    bool        finished  = SpeedrunTimer.IsFinished();
    bool        hasBest   = !BestRun.empty();
    int         numSplits = (int)splits.size();

    const Checkpoint* goalCp = GetGoal(CurrentRoute);
    bool goalIsAllCheckpoints = goalCp && goalCp->Point.TriggerType == ETriggerType::AllCheckpoints;
    bool goalIsCombatArena    = goalCp && goalCp->Point.TriggerType == ETriggerType::CombatArena;
    bool manualStop = finished && numSplits > 0 &&
                      strcmp(splits[numSplits - 1].Name, "Manual Stop") == 0;

    bool showCurrentSeg = running || (finished && !goalIsAllCheckpoints && !goalIsCombatArena && !manualStop);
    bool showTotal      = (running || finished) && numSplits > 0 && TimerDisplayMode != TimerMode::Split;
    bool showGrandRow   = ShowGrandTotal && (running || finished) && grand > 0.0;
    bool showPostRun    = finished && RunFinished;
    bool showIdle       = !running && !finished;

    // Build the four-font set for this frame.
    // mainFont (S) is the user's chosen size; others derive from it in -2 steps.
    // GetStreamFont() returns nullptr if the atlas hasn't delivered yet -- all
    // drawing helpers fall back gracefully to ImGui's current font in that case.
    SFontSet fonts;
    {
        const std::string& name = StreamerFontName;
        float S  = (float)StreamerFontSize;        // e.g. 36
        float S2 = S - 4.0f;                       // e.g. 32  (running millis + comp hms)
        float S4 = S - 8.0f;                       // e.g. 28  (comp millis)
        fonts.main       = name.empty() ? nullptr : GetStreamFont(name, S);
        fonts.mainMillis = name.empty() ? nullptr : GetStreamFont(name, S2);
        fonts.comp       = fonts.mainMillis;        // running millis == comp hms
        fonts.compMillis = name.empty() ? nullptr : GetStreamFont(name, S4);
        fonts.header     = name.empty() ? nullptr : GetStreamFont(name, (float)StreamerHeaderFontSize);

        // Fallback chain: if a specific size isn't ready yet, use the nearest
        // available slot from GetStreamerFont() so the overlay is never blank.
        if (!fonts.main) fonts.main = GetStreamerFont();
        if (!fonts.mainMillis) fonts.mainMillis = fonts.main;
        if (!fonts.comp)       fonts.comp       = fonts.mainMillis;
        if (!fonts.compMillis) fonts.compMillis = fonts.comp;
        if (!fonts.header)     fonts.header     = ImGui::GetFont();
    }
    float headerFontSize = fonts.header ? fonts.header->FontSize : ImGui::GetFontSize();
    float dynamicHeaderH = headerFontSize + 6.0f; // 3px padding top and bottom
    float dynamicSecH = dynamicHeaderH + STIME_ROW_H;
    float   dt      = ImGui::GetIO().DeltaTime;

    // -------------------------------------------------------------------------
    // Detect eviction: splitStart advanced since last frame -> capture outgoing
    // -------------------------------------------------------------------------
    int splitStart = std::max(0, numSplits - MAX_VISIBLE_SPLITS);

    if (splitStart > 0 && splitStart != s_LastSplitStart && s_LastSplitStart >= 0)
    {
        // The split at s_LastSplitStart just got pushed out -- capture it
        int i = s_LastSplitStart;
        if (i < numSplits)
        {
            double splitTime = (TimerDisplayMode == TimerMode::Split)
                ? splits[i].Timestamp
                : (i == 0 ? splits[i].Timestamp : splits[i].Timestamp - splits[i-1].Timestamp);

            double bestSplitTime = 0.0;
            if (hasBest && i < (int)BestRun.size())
                bestSplitTime = (TimerDisplayMode == TimerMode::Segment)
                    ? (i == 0 ? BestRun[i].Timestamp : BestRun[i].Timestamp - BestRun[i-1].Timestamp)
                    : BestRun[i].Timestamp;

            double diffCurrent = (TimerDisplayMode == TimerMode::LiveSplit) ? splits[i].Timestamp : splitTime;
            double diffBest    = (TimerDisplayMode == TimerMode::LiveSplit)
                ? (hasBest && i < (int)BestRun.size() ? BestRun[i].Timestamp : 0.0)
                : bestSplitTime;
            double diff = (hasBest && i < (int)BestRun.size()) ? diffCurrent - diffBest : 0.0;

            // Fill outgoing slot
            s_Outgoing.active = true;
            s_Outgoing.timer  = 0.0f;
            strncpy(s_Outgoing.label, splits[i].Name, sizeof(s_Outgoing.label) - 1);
            FormatTime(s_Outgoing.timeBuf, sizeof(s_Outgoing.timeBuf), splitTime, true);

            ImVec4 accent = (hasBest && i < (int)BestRun.size())
                ? (diff <= 0.0
                    ? ImVec4(ColorAhead[0],  ColorAhead[1],  ColorAhead[2],  1.0f)
                    : ImVec4(ColorBehind[0], ColorBehind[1], ColorBehind[2], 1.0f))
                : SC_AccentIdle();
            s_Outgoing.accent    = accent;
            s_Outgoing.timeColor = TimeColor(diffCurrent, diffBest, false);
            s_Outgoing.hasDiff   = hasBest && i < (int)BestRun.size() && std::abs(diff) > 0.0005;
            s_Outgoing.diffColor = (diff < 0.0)
                ? ImVec4(ColorAhead[0],  ColorAhead[1],  ColorAhead[2],  1.0f)
                : ImVec4(ColorBehind[0], ColorBehind[1], ColorBehind[2], 1.0f);

            // Split diff into main + decimal parts
            char diffBuf[32] = {};
            s_Outgoing.diffMainBuf[0] = '\0';
            s_Outgoing.diffDecBuf[0]  = '\0';
            if (s_Outgoing.hasDiff && FormatDiff(diffBuf, sizeof(diffBuf), diff, true,StreamerShowRunningMillis))
            {
                const char* dot = strrchr(diffBuf, '.');
                if (dot)
                {
                    int len = (int)(dot - diffBuf);
                    strncpy(s_Outgoing.diffMainBuf, diffBuf, len);
                    s_Outgoing.diffMainBuf[len] = '\0';
                    strncpy(s_Outgoing.diffDecBuf, dot, sizeof(s_Outgoing.diffDecBuf) - 1);
                }
                else
                {
                    strncpy(s_Outgoing.diffMainBuf, diffBuf, sizeof(s_Outgoing.diffMainBuf) - 1);
                }
            }

            s_Outgoing.startY = s_AnchorPos.y;
        }
    }
    s_LastSplitStart = splitStart;

    // -------------------------------------------------------------------------
    // Tick + render the outgoing animation window
    // Uses a dedicated ImGui window positioned above the anchor.
    // -------------------------------------------------------------------------
    if (s_Outgoing.active)
    {
        s_Outgoing.timer += dt;
        float t     = s_Outgoing.timer / ANIM_DURATION;       // 0..1
        if (t >= 1.0f)
        {
            t = 1.0f;
            s_Outgoing.active = false;
        }

        // Ease out: slow start, fast end (t^2)
        float ease  = t * t;
        float alpha = 1.0f - ease;
        // Slide upward by one full section height
        float slideY = s_Outgoing.startY - ease * (dynamicSecH + SGAP_H);

        ImGui::SetNextWindowPos(ImVec2(s_AnchorPos.x, slideY), ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(SSW, dynamicSecH));
        ImGui::SetNextWindowBgAlpha(0.0f);
        if (ImGui::Begin("##SW2Seg_Out", nullptr,
            ImGuiWindowFlags_NoDecoration       |
            ImGuiWindowFlags_NoFocusOnAppearing |
            ImGuiWindowFlags_NoNav              |
            ImGuiWindowFlags_NoScrollbar        |
            ImGuiWindowFlags_NoScrollWithMouse  |
            ImGuiWindowFlags_NoResize           |
            ImGuiWindowFlags_NoMove))
        {
            ImDrawList* dl = ImGui::GetWindowDrawList();
            ImVec2      wp = ImGui::GetWindowPos();

            SDrawHeaderBarAlpha(dl, wp, SSW, s_Outgoing.label, s_Outgoing.accent, alpha, fonts.header, dynamicHeaderH);
            SDrawTimeRowAlpha(dl, ImVec2(wp.x, wp.y + dynamicHeaderH), SSW,
                              s_Outgoing.timeBuf,
                              s_Outgoing.diffMainBuf, s_Outgoing.diffDecBuf, s_Outgoing.hasDiff,
                              s_Outgoing.timeColor, s_Outgoing.diffColor,
                              fonts, alpha);
        }
        ImGui::End();
    }

    // -------------------------------------------------------------------------
    // Main stack
    // -------------------------------------------------------------------------
    int   sectionIdx = 0;
    char  wid[32];
    float nextY = s_AnchorPos.y;

    auto NextPos = [&]() -> ImVec2 { return ImVec2(s_AnchorPos.x, nextY); };

    // --- Idle placeholder ---
    // Uses the same window ID as the first section (##SW2Seg_0) so ImGui
    // keeps its position when the run starts -- no jump on first split.
    if (showIdle)
    {
        ImGui::SetNextWindowSize(ImVec2(SSW, dynamicSecH));
        if (BeginSection("##SW2Seg_0", NextPos(), true))
        {
            s_AnchorPos = ImGui::GetWindowPos();
            ImDrawList* dl = ImGui::GetWindowDrawList();
            ImVec2      wp = ImGui::GetWindowPos();
            const ImGuiStyle& st = ImGui::GetStyle();

            const char* headerLabel = CurrentRoute.IsValid
                ? CurrentRouteName.c_str()
                : "NO ROUTE SET";
            SDrawHeaderBarAlpha(dl, wp, SSW, headerLabel, SC_AccentIdle(), 1.0f, fonts.header, dynamicHeaderH);

            ImVec2 rowMin = ImVec2(wp.x, wp.y + dynamicHeaderH);
            ImVec2 rowMax = ImVec2(wp.x + SSW, wp.y + dynamicSecH);
            dl->AddRectFilled(rowMin, rowMax, SToU32(SC_Bg()),
                              st.WindowRounding, ImDrawCornerFlags_Bot);

            if (st.WindowBorderSize > 0.0f)
                dl->AddRect(wp, rowMax, SToU32(st.Colors[ImGuiCol_Border]),
                            st.WindowRounding, ImDrawCornerFlags_All, st.WindowBorderSize);

            float btnW = SSW - SPAD_X * 2.0f;
            if (fonts.header) ImGui::PushFont(fonts.header);
            float btnH = ImGui::GetFrameHeight();
            ImGui::SetCursorScreenPos(ImVec2(
                wp.x + SPAD_X,
                rowMin.y + (STIME_ROW_H - btnH) * 0.5f));
            if (ImGui::Button("Browse Routes", ImVec2(btnW, btnH)))
                ShowRouteBrowser = !ShowRouteBrowser;
            if (fonts.header) ImGui::PopFont();
        }
        ImGui::End();
        return;
    }

    auto RenderSec = [&](const char* label, ImVec4 accent,
                         double timeVal, bool showMillis,
                         bool hasDiff, double diff, bool isSplit,
                         ImVec4 timeColor)
    {
        snprintf(wid, sizeof(wid), "##SW2Seg_%d", sectionIdx);
        bool isAnchor = (sectionIdx == 0);

        ImGui::SetNextWindowSize(ImVec2(SSW, dynamicSecH));
        if (BeginSection(wid, NextPos(), isAnchor))
        {
            if (isAnchor) s_AnchorPos = ImGui::GetWindowPos();
            ImDrawList* dl = ImGui::GetWindowDrawList();
            ImVec2 wp = ImGui::GetWindowPos();
            float curY = wp.y;
            curY = SDrawHeaderBar(dl, ImVec2(wp.x, curY), SSW, label, accent, fonts.header, dynamicHeaderH);
            SDrawTimeRow(dl, ImVec2(wp.x, curY), SSW,
                         timeVal, showMillis, hasDiff, diff, isSplit, timeColor, fonts);
            // Border around the whole section, respecting Nexus style
            const ImGuiStyle& st = ImGui::GetStyle();
            if (st.WindowBorderSize > 0.0f)
                dl->AddRect(wp, ImVec2(wp.x + SSW, wp.y + dynamicSecH),
                            SToU32(st.Colors[ImGuiCol_Border]),
                            st.WindowRounding, ImDrawCornerFlags_All, st.WindowBorderSize);
        }
        ImGui::End();

        nextY = s_AnchorPos.y + (sectionIdx + 1) * (dynamicSecH + SGAP_H);
        sectionIdx++;
    };

    // --- Completed split rows ---
    for (int i = splitStart; i < numSplits; i++)
    {
        double splitTime = (TimerDisplayMode == TimerMode::Split)
            ? splits[i].Timestamp
            : (i == 0 ? splits[i].Timestamp : splits[i].Timestamp - splits[i-1].Timestamp);

        double bestSplitTime = 0.0;
        if (hasBest && i < (int)BestRun.size())
            bestSplitTime = (TimerDisplayMode == TimerMode::Segment)
                ? (i == 0 ? BestRun[i].Timestamp : BestRun[i].Timestamp - BestRun[i-1].Timestamp)
                : BestRun[i].Timestamp;

        double diffCurrent = (TimerDisplayMode == TimerMode::LiveSplit) ? splits[i].Timestamp : splitTime;
        double diffBest    = (TimerDisplayMode == TimerMode::LiveSplit)
            ? (hasBest && i < (int)BestRun.size() ? BestRun[i].Timestamp : 0.0)
            : bestSplitTime;
        double diff = (hasBest && i < (int)BestRun.size()) ? diffCurrent - diffBest : 0.0;

        ImVec4 accent = (hasBest && i < (int)BestRun.size())
            ? (diff <= 0.0
                ? ImVec4(ColorAhead[0],  ColorAhead[1],  ColorAhead[2],  1.0f)
                : ImVec4(ColorBehind[0], ColorBehind[1], ColorBehind[2], 1.0f))
            : SC_AccentIdle();

        RenderSec(splits[i].Name, accent,
                  splitTime, true,
                  hasBest && i < (int)BestRun.size() && std::abs(diff) > 0.0005, diff, true,
                  TimeColor(diffCurrent, diffBest, false));
    }

    // --- Current segment row ---
    if (showCurrentSeg)
    {
        double segmentStart = numSplits > 0 ? splits[numSplits-1].Timestamp : 0.0;
        double segmentTime  = (TimerDisplayMode == TimerMode::Split) ? elapsed : (elapsed - segmentStart);
        bool   hasDiff      = hasBest && numSplits < (int)BestRun.size();
        double bestSegTime  = 0.0;

        if (hasDiff)
            bestSegTime = (TimerDisplayMode == TimerMode::Segment)
                ? (numSplits == 0 ? BestRun[0].Timestamp : BestRun[numSplits].Timestamp - BestRun[numSplits-1].Timestamp)
                : BestRun[numSplits].Timestamp;

        double diffCurSeg  = (TimerDisplayMode == TimerMode::LiveSplit) ? elapsed : segmentTime;
        double diffBestSeg = bestSegTime;
        double diff        = hasDiff ? diffCurSeg - diffBestSeg : 0.0;

        const char* segLabel = running
            ? (CurrentRouteName.empty() ? "CURRENT SEGMENT" : CurrentRouteName.c_str())
            : (goalCp && goalCp->Name[0] != '\0' ? goalCp->Name : "GOAL");

        ImVec4 accent = hasDiff
            ? (diff <= 0.0
                ? ImVec4(ColorAhead[0],  ColorAhead[1],  ColorAhead[2],  1.0f)
                : ImVec4(ColorBehind[0], ColorBehind[1], ColorBehind[2], 1.0f))
            : SC_AccentIdle();

        RenderSec(segLabel, accent,
                  segmentTime, !running || StreamerShowRunningMillis,
                  hasDiff && std::abs(diff) > 0.0005, diff, finished,
                  TimeColor(diffCurSeg, diffBestSeg, running));
    }

    // --- Total row ---
    if (showTotal)
    {
        double bestTotal = hasBest ? BestRun.back().Timestamp : 0.0;
        double totalDiff = hasBest ? elapsed - bestTotal : 0.0;

        ImVec4 accent = hasBest
            ? (totalDiff <= 0.0
                ? ImVec4(ColorAhead[0],  ColorAhead[1],  ColorAhead[2],  1.0f)
                : ImVec4(ColorBehind[0], ColorBehind[1], ColorBehind[2], 1.0f))
            : SC_AccentIdle();

        RenderSec("TOTAL", accent,
                  elapsed, !running || StreamerShowRunningMillis,
                  hasBest && std::abs(totalDiff) > 0.0005, totalDiff, finished,
                  TimeColor(elapsed, bestTotal, running));
    }

    // --- Grand Total row ---
    if (showGrandRow)
        RenderSec("GRAND TOTAL", SC_AccentIdle(), grand, true, false, 0.0, false, SC_TextDim());

    // --- Post-run panel ---
    // Styled as a section: "RUN FINISHED" header bar + two full-width buttons in the row.
    if (showPostRun)
    {
        if (fonts.header) ImGui::PushFont(fonts.header);
        float btnH = ImGui::GetFrameHeight();
        float btnGap = ImGui::GetStyle().ItemSpacing.y;
        float rowH   = btnH * 2.0f + btnGap + SPAD_X * 2.0f;
        float panelH = dynamicHeaderH + rowH;

        snprintf(wid, sizeof(wid), "##SW2Seg_%d", sectionIdx);
        ImGui::SetNextWindowPos(NextPos(), ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(SSW, panelH));
        ImGui::SetNextWindowBgAlpha(0.0f);
        ImGui::Begin(wid, nullptr,
            ImGuiWindowFlags_NoDecoration       |
            ImGuiWindowFlags_NoFocusOnAppearing |
            ImGuiWindowFlags_NoNav              |
            ImGuiWindowFlags_NoScrollbar        |
            ImGuiWindowFlags_NoScrollWithMouse  |
            ImGuiWindowFlags_NoResize           |
            ImGuiWindowFlags_NoMove);

        ImDrawList*       dl = ImGui::GetWindowDrawList();
        ImVec2            wp = ImGui::GetWindowPos();
        const ImGuiStyle& st = ImGui::GetStyle();

        // Header bar
        SDrawHeaderBarAlpha(dl, wp, SSW, "RUN FINISHED", SC_AccentIdle(), 1.0f, fonts.header, dynamicHeaderH);

        // Row background
        ImVec2 rowMin = ImVec2(wp.x, wp.y + dynamicHeaderH);
        ImVec2 rowMax = ImVec2(wp.x + SSW, wp.y + panelH);
        dl->AddRectFilled(rowMin, rowMax, SToU32(SC_Bg()),
                          st.WindowRounding, ImDrawCornerFlags_Bot);

        // Border
        if (st.WindowBorderSize > 0.0f)
            dl->AddRect(wp, rowMax, SToU32(st.Colors[ImGuiCol_Border]),
                        st.WindowRounding, ImDrawCornerFlags_All, st.WindowBorderSize);

        // Buttons — full width, stacked
        float btnW = SSW - SPAD_X * 2.0f;
        float btnX = wp.x + SPAD_X;
        float btnY = rowMin.y + SPAD_X;

        ImGui::SetCursorScreenPos(ImVec2(btnX, btnY));
        if (ImGui::Button("Save as best##postrun", ImVec2(btnW, btnH)))
        {
            BestRun = splits;
            if (finished && !manualStop && !goalIsAllCheckpoints &&
                (!goalCp || goalCp->Point.TriggerType != ETriggerType::CombatArena))
            {
                decltype(BestRun)::value_type goalEntry{};
                goalEntry.Timestamp = elapsed;
                const char* goalName = (goalCp && goalCp->Name[0] != '\0') ? goalCp->Name : "Goal";
                std::strncpy(goalEntry.Name, goalName, sizeof(goalEntry.Name) - 1);
                BestRun.push_back(goalEntry);
            }
            BestRunIndex = 0;
            if (!CurrentHistoryPath.empty())
                SaveHistory(CurrentHistoryPath, HistoryRuns, SegmentRecords, BestRunIndex);
            RunFinished = false;
        }

        ImGui::SetCursorScreenPos(ImVec2(btnX, btnY + btnH + btnGap));
        if (ImGui::Button("Reset Timer##postrun", ImVec2(btnW, btnH)))
        {
            FullReset();
            RunFinished = false;
        }
        
        if (fonts.header) ImGui::PopFont();

        ImGui::End();
    }
}