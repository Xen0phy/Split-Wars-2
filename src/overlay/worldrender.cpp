// worldrender.cpp
// Draws checkpoint zone overlays directly onto the game world using ImGui's
// foreground draw list — the same technique used by most GW2 overlay addons.
//
// Both zone shapes are rendered as dot clouds:
//   Circle zone — a sphere of dots distributed via the golden-angle spiral,
//                 restricted to a configurable latitude band around the sphere.
//                 Dot alpha fades from full at the band centre to transparent
//                 at the upper and lower band edges.
//   Plane zone  — a rectangular grid of dots spanning the plane's width and a
//                 configurable vertical band.  The same centre/up/down band
//                 parameters control vertical extent and the alpha fade.
//
// Band parameters per RoutePoint:
//   bandCenterInput — for Circle: centre latitude in degrees (-90 south … +90 north)
//                     for Plane:  vertical centre offset in metres above point.Y
//   bandUpInput     — extent upward   from centre (fade boundary; degrees / metres)
//   bandDownInput   — extent downward from centre (fade boundary; degrees / metres)
//
// Color convention (set by RenderZones, all user-adjustable in options):
//   ColorStart      — start checkpoint
//   ColorGoal       — goal checkpoint
//   ColorCheckpoint — intermediate checkpoints
//   ColorNull       — Null(Circle) and Null(Plane) decorative zones
//
// Zones are hidden automatically when the in-game map is open, and are
// only drawn for the current map (unless a checkpoint has MapID = 0).

#include "worldrender.h"
#include "shared.h"
#include "dot_sprite.h"
#include "imgui.h"
#include "imgui_internal.h" 
#include <algorithm>
#include <cmath>
#include <deque>
#include <chrono>
#include <unordered_map>

// ---------------------------------------------------------------------------
// BuildCameraBasis
// ---------------------------------------------------------------------------
// Does the camera-dependent, but dot-independent, work that WorldToScreen()
// redoes on every single call: normalising the forward vector,
// deriving right/up via cross products, and computing the FOV-derived
// perspective factor. All of this only depends on the camera and the
// display size for the current frame, so it only needs to run once per
// frame no matter how many thousands of dots get projected against it.
// ---------------------------------------------------------------------------
CameraBasis BuildCameraBasis()
{
    CameraBasis b;

    Vector3 camFront = { GS.CameraFrontX, GS.CameraFrontY, GS.CameraFrontZ };

    // Step 1 — normalise the forward vector.
    float flen = std::sqrt(camFront.X*camFront.X + camFront.Y*camFront.Y + camFront.Z*camFront.Z);
    if (flen < 0.0001f) { b.valid = false; return b; }
    float fx = camFront.X / flen;
    float fy = camFront.Y / flen;
    float fz = camFront.Z / flen;

    // Step 2 — derive the right vector (forward × world-up).
    float rx, ry, rz;
    if (std::abs(fy) > 0.999f)
    {
        rx = 1.0f; ry = 0.0f; rz = 0.0f;
    }
    else
    {
        rx = fz;
        ry = 0.0f;
        rz = -fx;
        float rlen = std::sqrt(rx*rx + rz*rz);
        rx /= rlen;
        rz /= rlen;
    }

    // Step 3 — derive the camera up vector as right × forward.
    float tx = fy*rz - fz*ry;
    float ty = fz*rx - fx*rz;
    float tz = fx*ry - fy*rx;

    b.valid = true;
    b.camX = GS.CameraX; b.camY = GS.CameraY; b.camZ = GS.CameraZ;
    b.rx = rx; b.ry = ry; b.rz = rz;
    b.tx = tx; b.ty = ty; b.tz = tz;
    b.fx = fx; b.fy = fy; b.fz = fz;

    ImGuiIO& io = ImGui::GetIO();
    b.dispW = io.DisplaySize.x;
    b.dispH = io.DisplaySize.y;
    b.aspect = b.dispW / b.dispH;

    float fov = (GS.FOV > 0.01f) ? GS.FOV : 0.873f;
    b.f = 1.0f / std::tan(fov * 0.5f);

    return b;
}

// ---------------------------------------------------------------------------
// ProjectWithBasis
// ---------------------------------------------------------------------------
// The actual per-point projection, against an already-built CameraBasis.
// Just a subtract, three dot products, and a perspective divide — no trig,
// no sqrt, no per-call basis reconstruction. This is what every per-dot
// hot loop should call instead of WorldToScreen().
// ---------------------------------------------------------------------------
bool ProjectWithBasis(const CameraBasis& b, float wx, float wy, float wz, float& sx, float& sy)
{
    if (!b.valid) return false;

    float dx = wx - b.camX;
    float dy = wy - b.camY;
    float dz = wz - b.camZ;

    float vx =  b.rx*dx + b.ry*dy + b.rz*dz;
    float vy =  b.tx*dx + b.ty*dy + b.tz*dz;
    float vz =  b.fx*dx + b.fy*dy + b.fz*dz;

    if (vz <= 0.01f) return false;

    float px =  vx * (b.f / b.aspect) / vz;
    float py = -vy * b.f / vz;

    sx = (px * 0.5f + 0.5f) * b.dispW;
    sy = (py * 0.5f + 0.5f) * b.dispH;

    if (sx < -10000 || sx > 10000 || sy < -10000 || sy > 10000) return false;

    return true;
}

// ---------------------------------------------------------------------------
// WorldToScreen
// ---------------------------------------------------------------------------
// Compatibility wrapper for the occasional non-hot-path caller (e.g. the
// debug UI) that just wants a one-off projection. Builds a CameraBasis and
// throws it away — fine for a handful of calls per frame, but per-dot loops
// must use BuildCameraBasis() once + ProjectWithBasis() per point instead.
// ---------------------------------------------------------------------------
bool WorldToScreen(float wx, float wy, float wz, float& sx, float& sy)
{
    CameraBasis b = BuildCameraBasis();
    return ProjectWithBasis(b, wx, wy, wz, sx, sy);
}

// ---------------------------------------------------------------------------
// RoutePointIsSet  (file-private helper)
// ---------------------------------------------------------------------------
static bool RoutePointIsSet(const RoutePoint& point)
{
    return point.X != 0.0f || point.Y != 0.0f || point.Z != 0.0f;
}


// ---------------------------------------------------------------------------
// CalcOcclusionState
// ---------------------------------------------------------------------------
// Computed once per frame in RenderZones() and passed into both zone
// renderers so player-occlusion logic isn't duplicated per zone. Takes the
// already-built per-frame CameraBasis so it doesn't pay for its own basis
// reconstruction either.
// ---------------------------------------------------------------------------
OcclusionState CalcOcclusionState(const CameraBasis& basis)
{
    OcclusionState os;

    os.playerOnScreen = ProjectWithBasis(basis,
        GS.PlayerX, GS.PlayerY + 1.0f, GS.PlayerZ,
        os.playerSx, os.playerSy);

    float camToPlayerX = GS.CameraX - GS.PlayerX;
    float camToPlayerY = GS.CameraY - GS.PlayerY;
    float camToPlayerZ = GS.CameraZ - GS.PlayerZ;
    float camToPlayer  = std::sqrt(camToPlayerX*camToPlayerX + camToPlayerY*camToPlayerY + camToPlayerZ*camToPlayerZ);

    // Pixel radius of the occlusion circle — larger when camera is close, smaller when far
    os.occludeRadius = std::clamp(occludePixelRadius / (camToPlayer * 0.5f), 30.0f, occludePixelClamp);

    return os;
}

// Cached UI window rects from the previous frame.
// Updated at the start of RenderZones() so we don't check mid-submission state.
static std::vector<ImRect> s_uiRects;

static void UpdateUIRects()
{
    s_uiRects.clear();
    ImGuiContext& g = *ImGui::GetCurrentContext();
    for (ImGuiWindow* window : g.Windows)
    {
        if (!window->WasActive)  continue;  // WasActive = confirmed visible last frame
        if (window->Hidden)      continue;
        if (window->Flags & ImGuiWindowFlags_NoBackground) continue;
        s_uiRects.push_back(window->Rect());
    }
}

static bool IsOccludedByUI(float sx, float sy)
{
    ImVec2 p(sx, sy);
    for (const ImRect& r : s_uiRects)
        if (r.Contains(p)) return true;
    return false;
}

// Applies occlusion fade to dotAlpha based on the dot's screen position.
static int ApplyOcclusion(int dotAlpha, float sx, float sy, const OcclusionState& os)
{
    if (IsOccludedByUI(sx, sy)) return 0;

    if (!os.playerOnScreen) return dotAlpha;
    float ddx  = sx - os.playerSx;
    float ddy  = sy - os.playerSy;
    float dist = std::sqrt(ddx*ddx + ddy*ddy);
    float occludeFade = std::clamp((dist - os.occludeRadius * 0.6f) / (os.occludeRadius * 1.0f), 0.0f, 1.0f);
    return (int)(dotAlpha * occludeFade);
}

// PI
constexpr float PIf = 3.14159265358979323846f;

// ---------------------------------------------------------------------------
// Sphere-point cache  (file-private)
// ---------------------------------------------------------------------------
// Every camera-independent per-dot value for a sphere zone — the unit
// direction on the sphere, the band falloff, and the raw longitude needed
// by the CircleInteract rotating gap — depends only on the zone's dot count
// and band parameters, never on the camera or player position. That work is
// computed once and cached here rather than recomputed inside the per-dot
// render loop, and stays valid until the config actually changes (e.g. the
// user drags a band slider in the route editor).
//
// The cache is keyed by the RoutePoint's address, since each checkpoint's
// Point lives at a stable address in CurrentRoute.Checkpoints for the
// lifetime of the loaded route. A cheap signature check (density + the
// three band inputs) detects live edits and regenerates only that entry.
// If the route is reloaded and the backing vector reallocates, old entries
// simply go unused; GuardSphereCacheSize() below bounds how much of that
// can accumulate.
// ---------------------------------------------------------------------------
struct SpherePoint
{
    float x, y, z;   // unit direction on the sphere (pre-radius, pre-translate)
    float falloff;   // band falloff: 1.0 at band centre, 0.0 at either edge
    float theta;     // raw longitude, retained for the rotating interaction gap
};

struct SphereCacheEntry
{
    std::vector<SpherePoint> pts;
    int   density    = -1;
    float bandCenter = 0.0f, bandUp = 0.0f, bandDown = 0.0f;
};

static std::unordered_map<const RoutePoint*, SphereCacheEntry> s_sphereCache;

// Defensive bound: if route reloads keep leaving orphaned entries behind
// (backing vector reallocating to a new address each time), just wipe the
// cache rather than let it grow unbounded. Call once per frame.
static void GuardSphereCacheSize()
{
    constexpr size_t kMaxEntries = 512;
    if (s_sphereCache.size() > kMaxEntries)
        s_sphereCache.clear();
}

static const std::vector<SpherePoint>& GetSpherePoints(const RoutePoint& point, int numDots)
{
    SphereCacheEntry& cache = s_sphereCache[&point];

    bool dirty = cache.density    != numDots ||
                 cache.bandCenter != point.bandCenterInput ||
                 cache.bandUp     != point.bandUpInput ||
                 cache.bandDown   != point.bandDownInput;

    if (!dirty) return cache.pts;

    const float golden = PIf * (3.0f - std::sqrt(5.0f)); // golden angle ~2.399 radians

    const float bandCenter = point.bandCenterInput * (PIf / 180.0f);
    const float bandUp     = point.bandUpInput     * (PIf / 180.0f);
    const float bandDown   = point.bandDownInput   * (PIf / 180.0f);

    const float phiMinClamped = std::max(bandCenter - bandDown, -PIf * 0.5f);
    const float phiMaxClamped = std::min(bandCenter + bandUp,    PIf * 0.5f);

    const float tMin = (std::sin(phiMinClamped) + 1.0f) * 0.5f;
    const float tMax = (std::sin(phiMaxClamped) + 1.0f) * 0.5f;

    cache.pts.clear();
    cache.pts.reserve(numDots);

    for (int i = 0; i < numDots; i++)
    {
        const float t   = tMin + (tMax - tMin) * (float)i / (numDots - 1);
        float phi   = std::asin(-1.0f + 2.0f * t); // latitude within the band
        float theta = golden * i;                    // longitude

        float distFromCenter = phi - bandCenter;
        float normalizedDist = (distFromCenter >= 0.0f)
            ? ((bandUp   > 0.0f) ? distFromCenter /  bandUp   : 0.0f)
            : ((bandDown > 0.0f) ? distFromCenter / -bandDown : 0.0f);
        float falloff = 1.0f - std::abs(normalizedDist);

        float cosPhi = std::cos(phi);

        SpherePoint sp;
        sp.x = cosPhi * std::cos(theta);
        sp.y = std::sin(phi);
        sp.z = cosPhi * std::sin(theta);
        sp.falloff = falloff;
        sp.theta   = theta;
        cache.pts.push_back(sp);
    }

    cache.density    = numDots;
    cache.bandCenter = point.bandCenterInput;
    cache.bandUp     = point.bandUpInput;
    cache.bandDown   = point.bandDownInput;

    return cache.pts;
}

// ---------------------------------------------------------------------------
// Fast dot drawing  (file-private)
// ---------------------------------------------------------------------------
// AddCircleFilled() recomputes an auto segment count, builds a path, and
// emits an anti-aliased fringe on every single call — all of which is
// wasted work for a constant 3px-radius dot repeated tens or hundreds of
// thousands of times a frame. This emits the same filled-circle geometry
// directly via ImDrawList's low-level Prim* API: a small fixed-size unit
// circle (computed once, not per dot) translated and scaled per call, with
// no path building and no AA fringe. At 3px radius the loss of the AA edge
// is not visible; the segment count below (8) is close to what
// AddCircleFilled's own auto-calc would already choose at this radius, so
// the shape is unchanged.
//
// Callers should skip this entirely (not just pass alpha 0) whenever a
// dot's alpha is already zero before occlusion — at high dot densities a
// large fraction of dots are faded out by the band edge or distance fade,
// and there's no reason to pay for a draw call for any of those.
// ---------------------------------------------------------------------------
// Both zone renderers always draw at this fixed pixel radius, so the ring
// geometry is entirely camera/data-independent and can be baked once here
// rather than every one of the up-to-100k dots drawn per frame doing its
// own radius multiply.
constexpr float DOT_RADIUS = 3.0f;

namespace
{
    constexpr int   kDotSegments = 8;
    constexpr float AA_SIZE      = 1.0f; // matches Dear ImGui's default fringe width in pixels

    struct DotRingOffsets
    {
        // Pre-scaled by DOT_RADIUS / (DOT_RADIUS + AA_SIZE) respectively, so
        // the hot loop below only ever does cx + offset (an add), never a
        // multiply — the multiply happens once here at static-init time.
        float innerX[kDotSegments], innerY[kDotSegments];
        float outerX[kDotSegments], outerY[kDotSegments];
        DotRingOffsets()
        {
            const float outerRadius = DOT_RADIUS + AA_SIZE;
            for (int i = 0; i < kDotSegments; i++)
            {
                float a  = (2.0f * PIf * i) / kDotSegments;
                float ux = std::cos(a);
                float uy = std::sin(a);
                innerX[i] = ux * DOT_RADIUS;
                innerY[i] = uy * DOT_RADIUS;
                outerX[i] = ux * outerRadius;
                outerY[i] = uy * outerRadius;
            }
        }
    };
    const DotRingOffsets s_dotRing; // built once at static-init time
}

// ---------------------------------------------------------------------------
// Dot sprite texture
// ---------------------------------------------------------------------------
// A small (6x6, exactly 2*DOT_RADIUS), near-native-size soft-edged white
// circle, requested once from Nexus and used to draw every zone dot as a
// single 4-vertex/6-index textured quad instead of the 17-vertex/42-index
// hand-built triangle fan below. White RGB so the sprite tints to each
// zone's configured color via per-vertex color, exactly like the fan
// version.
//
// Uses the async Textures_LoadFromMemory + callback (not the synchronous
// GetOrCreateFromMemory) for the same reason render_speedo.cpp's texture
// loading does: the synchronous variant can hand back a Texture_t* whose
// Resource is still null while the decode/upload finishes, with nothing
// telling the caller when it becomes ready.
//
// PrimAddFilledDotFan() remains as the fallback used for the handful of
// frames before the sprite finishes loading, so zones still render
// (just without the texture win) rather than being blank at startup.
// ---------------------------------------------------------------------------
static Texture_t* s_dotTexture          = nullptr;
static bool       s_dotTextureRequested = false;

static void OnDotSpriteReceived(const char* /*aIdentifier*/, Texture_t* aTexture)
{
    s_dotTexture = aTexture;
}

static void EnsureDotTextureRequested()
{
    if (s_dotTextureRequested) return;
    s_dotTextureRequested = true;
    APIDefs->Textures_LoadFromMemory("SW2_ZONE_DOT",
        (void*)g_DotSpriteData, g_DotSpriteData_size, OnDotSpriteReceived);
}

// The sprite in dot_sprite.h is baked at exactly 2*DOT_RADIUS (6x6) and
// drawn 1:1 -- no runtime scaling. Tied directly to DOT_RADIUS rather than
// a separate constant, since the sprite is defined to always match it; if
// DOT_RADIUS changes, dot_sprite.h must be regenerated at 2*DOT_RADIUS.
constexpr float kDotQuadHalfSize = DOT_RADIUS;

static inline void PrimAddFilledDotSprite(ImDrawList* dl, float cx, float cy, int r255, int g255, int b255, int alpha)
{
    dl->PrimReserve(6, 4);

    ImDrawVert*  vtxWrite   = dl->_VtxWritePtr;
    ImDrawIdx*   idxWrite   = dl->_IdxWritePtr;
    unsigned int vtxBaseIdx = dl->_VtxCurrentIdx;

    const ImU32  col = IM_COL32(r255, g255, b255, alpha);
    const float  h   = kDotQuadHalfSize;

    vtxWrite[0].pos = ImVec2(cx - h, cy - h); vtxWrite[0].uv = ImVec2(0.0f, 0.0f); vtxWrite[0].col = col;
    vtxWrite[1].pos = ImVec2(cx + h, cy - h); vtxWrite[1].uv = ImVec2(1.0f, 0.0f); vtxWrite[1].col = col;
    vtxWrite[2].pos = ImVec2(cx + h, cy + h); vtxWrite[2].uv = ImVec2(1.0f, 1.0f); vtxWrite[2].col = col;
    vtxWrite[3].pos = ImVec2(cx - h, cy + h); vtxWrite[3].uv = ImVec2(0.0f, 1.0f); vtxWrite[3].col = col;

    idxWrite[0] = (ImDrawIdx)(vtxBaseIdx + 0);
    idxWrite[1] = (ImDrawIdx)(vtxBaseIdx + 1);
    idxWrite[2] = (ImDrawIdx)(vtxBaseIdx + 2);
    idxWrite[3] = (ImDrawIdx)(vtxBaseIdx + 0);
    idxWrite[4] = (ImDrawIdx)(vtxBaseIdx + 2);
    idxWrite[5] = (ImDrawIdx)(vtxBaseIdx + 3);

    dl->_VtxWritePtr   += 4;
    dl->_IdxWritePtr   += 6;
    dl->_VtxCurrentIdx += 4;
}

static inline void PrimAddFilledDotFan(ImDrawList* dl, float cx, float cy, int r255, int g255, int b255, int alpha)
{
    // Anti-aliasing fringe: Dear ImGui's own circle/polygon fill isn't just
    // a flat-shaded fan — it adds a ~1px ring of vertices that fade to
    // alpha 0, which is what makes small shapes read as smooth instead of
    // faceted. This reproduces that by hand: an interior fan at full alpha,
    // plus a second ring one unit further out at alpha 0, stitched together
    // with a thin band of triangles.

    const int ringVtx  = kDotSegments;
    const int vtxCount = 1 + ringVtx * 2;  // centre + solid ring + fringe ring
    const int idxCount = ringVtx * 3       // interior fan
                        + ringVtx * 6;     // fringe ring (2 triangles per segment)

    dl->PrimReserve(idxCount, vtxCount);

    ImDrawVert*  vtxWrite   = dl->_VtxWritePtr;
    ImDrawIdx*   idxWrite   = dl->_IdxWritePtr;
    unsigned int vtxBaseIdx = dl->_VtxCurrentIdx;
    ImVec2       uv         = dl->_Data->TexUvWhitePixel;

    ImU32 colSolid = IM_COL32(r255, g255, b255, alpha);
    ImU32 colFade  = IM_COL32(r255, g255, b255, 0);

    vtxWrite[0].pos = ImVec2(cx, cy);
    vtxWrite[0].uv  = uv;
    vtxWrite[0].col = colSolid;

    for (int i = 0; i < ringVtx; i++)
    {
        vtxWrite[1 + i].pos = ImVec2(cx + s_dotRing.innerX[i], cy + s_dotRing.innerY[i]);
        vtxWrite[1 + i].uv  = uv;
        vtxWrite[1 + i].col = colSolid;

        vtxWrite[1 + ringVtx + i].pos = ImVec2(cx + s_dotRing.outerX[i], cy + s_dotRing.outerY[i]);
        vtxWrite[1 + ringVtx + i].uv  = uv;
        vtxWrite[1 + ringVtx + i].col = colFade;
    }

    ImDrawIdx* idx = idxWrite;
    for (int i = 0; i < ringVtx; i++)
    {
        *idx++ = (ImDrawIdx)(vtxBaseIdx);
        *idx++ = (ImDrawIdx)(vtxBaseIdx + 1 + i);
        *idx++ = (ImDrawIdx)(vtxBaseIdx + 1 + ((i + 1) % ringVtx));
    }
    for (int i = 0; i < ringVtx; i++)
    {
        int i1 = (i + 1) % ringVtx;
        unsigned int inner0 = vtxBaseIdx + 1 + i;
        unsigned int inner1 = vtxBaseIdx + 1 + i1;
        unsigned int outer0 = vtxBaseIdx + 1 + ringVtx + i;
        unsigned int outer1 = vtxBaseIdx + 1 + ringVtx + i1;

        *idx++ = (ImDrawIdx)inner0;
        *idx++ = (ImDrawIdx)inner1;
        *idx++ = (ImDrawIdx)outer1;

        *idx++ = (ImDrawIdx)inner0;
        *idx++ = (ImDrawIdx)outer1;
        *idx++ = (ImDrawIdx)outer0;
    }

    dl->_VtxWritePtr   += vtxCount;
    dl->_IdxWritePtr   += idxCount;
    dl->_VtxCurrentIdx += vtxCount;
}

// ---------------------------------------------------------------------------
// DrawDot  (dispatcher)
// ---------------------------------------------------------------------------
// Every zone renderer calls this instead of picking a Prim* function
// directly. Whether the sprite path is used is decided once per frame by
// RenderZones() (s_useSpriteDotsThisFrame) — never per dot — because the
// sprite path requires the draw list's active texture to be our dot
// texture for the whole batch (see the PushTextureID/PopTextureID pair in
// RenderZones()); switching textures per dot would defeat the entire point
// of this by forcing a new draw command every time.
// ---------------------------------------------------------------------------
static bool s_useSpriteDotsThisFrame = false;

static inline void DrawDot(ImDrawList* dl, float cx, float cy, int r255, int g255, int b255, int alpha)
{
    if (s_useSpriteDotsThisFrame)
        PrimAddFilledDotSprite(dl, cx, cy, r255, g255, b255, alpha);
    else
        PrimAddFilledDotFan(dl, cx, cy, r255, g255, b255, alpha);
}

// ---------------------------------------------------------------------------
// RenderZoneCircle
// ---------------------------------------------------------------------------
// Draws a world-space zone indicator for sphere-type triggers.
//
// Dots are distributed evenly across the sphere surface using the
// golden-angle spiral, but only within the latitude band defined by the
// point's band parameters:
//
//   bandCenterInput — centre latitude of the rendered band (-90 south … +90 north)
//   bandUpInput     — how far above the centre the band extends (fade to 0 at edge)
//   bandDownInput   — how far below the centre the band extends (fade to 0 at edge)
//
// All NUM_DOTS dots are placed within this band, so none are wasted outside
// the visible range.  Each dot's alpha is the product of:
//   • Band falloff  — 1.0 at band centre, 0.0 at either edge.
//   • Distance fade — per-dot world-space distance to the player mapped
//                     through ZoneFadeStart/ZoneFadeEnd.
//   • Occlusion     — dots behind the player model are faded via ApplyOcclusion.
// ---------------------------------------------------------------------------
void RenderZoneCircle(const RoutePoint& point, float r, float g, float b,
                      const CameraBasis& basis, const OcclusionState& os)
{
    if (!MumbleLink && !GS.RTAPIAvailable) return;

    ImDrawList* dl = ImGui::GetForegroundDrawList();

    const int NUM_DOTS = point.DotDensity > 0 ? point.DotDensity : 300;

    // Color is constant for the whole zone — only alpha varies per dot —
    // so pack it to 8-bit once here instead of re-casting on every dot.
    const int r255 = (int)(r * 255);
    const int g255 = (int)(g * 255);
    const int b255 = (int)(b * 255);

    // --- Combat pulse (out-of-combat only) ---
    // Mimics a heartbeat: two sharp gaussian bumps (lub-dub) followed by a
    // long flat rest, repeating at ~60 BPM (1-second cycle).
    // Each bump is a gaussian centred at a fixed phase offset within the cycle.
    // The radius offset is the sum of the two bumps, giving a ±0.5 m swing.
    float effectiveRadius = point.RadiusWidth;
    if (point.TriggerType == ETriggerType::CombatArena && !GS.IsInCombat)
    {
        const float BPM         = 20.0f;
        const float cycleSecs   = 60.0f / BPM;                              // 1.0 s per beat
        const float phase       = std::fmod((float)ImGui::GetTime(), cycleSecs) / cycleSecs; // [0,1)

        // Two gaussian peaks within the cycle: "lub" at 8%, "dub" at 22%
        // Width (sigma) kept tight so they decay quickly into the flat rest.
        auto gauss = [](float x, float mu, float sigma) -> float {
            float d = (x - mu) / sigma;
            return std::exp(-0.5f * d * d);
        };

        float lub = gauss(phase, 0.08f, 0.035f);
        float dub = gauss(phase, 0.22f, 0.035f) * 0.65f; // dub is a bit softer

        // Combine and remap so the resting baseline sits at -0.5 m and the
        // peak of "lub" reaches +0.5 m — a total swing of 1 m.
        float pulse = (lub + dub);                   // [0, ~1.65] at peaks, ~0 at rest
        effectiveRadius += pulse * 0.5f - 0.05f;     // offset so rest ≈ nominal radius
    }

    // Camera-independent per-dot data (unit direction, falloff, raw theta) —
    // generated once and reused until density/band settings change.
    const std::vector<SpherePoint>& pts = GetSpherePoints(point, NUM_DOTS);

    // --- Rotating gap (Interact trigger only) ---
    // A fixed arc of longitude is hidden and the whole pattern rotates,
    // giving a "beckoning" sweep effect. The gap edges are softened over a
    // feather band so the cut fades in/out rather than clipping hard.
    // These constants (and rotOffset, which only depends on elapsed time)
    // are the same for every dot this frame, so they're computed once here
    // rather than inside the per-dot loop below.
    const bool  isInteract = (point.TriggerType == ETriggerType::CircleInteract);
    const float gapRad     = 90.0f * (PIf / 180.0f); // degrees of arc to hide
    const float featherRad = 15.0f * (PIf / 180.0f); // soft fade either side
    const float rpm        = 20.0f;                  // rotation speed
    float rotOffset = 0.0f;
    if (isInteract)
        rotOffset = std::fmod((float)ImGui::GetTime() * (rpm / 60.0f) * 2.0f * PIf, 2.0f * PIf);

    for (const SpherePoint& sp : pts)
    {
        float interactAlpha = 1.0f;
        if (isInteract)
        {
            // Advance theta by time, normalise into [0, 2π)
            float thetaNorm = std::fmod(sp.theta - rotOffset + 4.0f * PIf, 2.0f * PIf);

            // [0, gapRad]            → fully hidden
            // [gapRad, gapRad+feath] → leading feather (fade back in)
            // [2π-feath, 2π]         → trailing feather (fade out into gap)
            if (thetaNorm < gapRad + featherRad)
            {
                if (thetaNorm < gapRad)
                    interactAlpha = 0.0f;
                else
                    interactAlpha = (thetaNorm - gapRad) / featherRad; // 0→1
            }
            float trailingDist = 2.0f * PIf - thetaNorm;
            if (trailingDist < featherRad)
                interactAlpha = std::min(interactAlpha, trailingDist / featherRad); // 1→0
        }

        float wx = point.X + sp.x * effectiveRadius;
        float wy = point.Y + sp.y * effectiveRadius;
        float wz = point.Z + sp.z * effectiveRadius;

        // Per-dot distance fade from player position using the global fade range
        float pdx = wx - GS.PlayerX;
        float pdy = wy - GS.PlayerY;
        float pdz = wz - GS.PlayerZ;
        float playerDist = std::sqrt(pdx*pdx + pdy*pdy + pdz*pdz);
        float dotDistAlpha = std::clamp(1.0f - (playerDist - ZoneFadeStart) / (ZoneFadeEnd - ZoneFadeStart), 0.0f, 1.0f);

        int   dotAlpha = (int)(220 * 0.8f * sp.falloff * dotDistAlpha * interactAlpha);
        if (dotAlpha <= 0) continue; // occlusion can only lower this further — skip the projection and draw call entirely

        float sx, sy;
        if (ProjectWithBasis(basis, wx, wy, wz, sx, sy))
        {
            int finalAlpha = ApplyOcclusion(dotAlpha, sx, sy, os);
            if (finalAlpha <= 0) continue;
            DrawDot(dl, sx, sy, r255, g255, b255, finalAlpha);
        }
    }
}

// ---------------------------------------------------------------------------
// RenderZonePlane
// ---------------------------------------------------------------------------
// Draws a world-space zone indicator for Plane triggers as a grid of dots.
//
// Dots are laid out on the plane surface in a regular grid:
//   • Horizontally: evenly spaced across the full RadiusWidth.
//   • Vertically:   evenly spaced within the height band defined by the
//                   point's band parameters (in metres, not degrees):
//
//       bandCenterInput — vertical centre offset in metres above point.Y
//       bandUpInput     — metres above centre where alpha reaches 0
//       bandDownInput   — metres below centre where alpha reaches 0
//
// Dot density is derived automatically from DOT_SPACING so wider or taller
// planes fill in without needing a manual dot count.
//
// Each dot's alpha is the product of band falloff (1.0 at centre, 0.0 at
// edges), a per-dot distance fade from the player through ZoneFadeStart/
// ZoneFadeEnd, and occlusion fade via ApplyOcclusion — matching the sphere
// zone's visual language exactly.
// ---------------------------------------------------------------------------
void RenderZonePlane(const RoutePoint& point, float r, float g, float b,
                     const CameraBasis& basis, const OcclusionState& os)
{
    if (!MumbleLink && !GS.RTAPIAvailable) return;

    ImDrawList* dl = ImGui::GetForegroundDrawList();

    // Along-plane direction vector from PlaneAngle
    float angleRad = point.PlaneAngle * PIf / 180.0f;
    float px = -std::sin(angleRad);
    float pz =  std::cos(angleRad);

    float halfWidth = point.RadiusWidth * 0.5f;

    // Reuse band fields as world-space height values (metres, not degrees)
    float bandCenter = 0.0f;                  // bandCenterInput not used for Plane — use Y directly instead
    float bandUp     = point.bandUpInput;     // height above centre → fade to 0
    float bandDown   = point.bandDownInput;   // depth below centre → fade to 0

    float yMin = point.Y + bandCenter - bandDown;
    float yMax = point.Y + bandCenter + bandUp;
    float yCtr = point.Y + bandCenter;

    const float e30   = std::log(2.0f)  / std::log(200.0f / 30.0f);
    const float e1000 = std::log(0.2f)  / std::log(200.0f / 1000.0f);

    float t = std::clamp((point.DotDensity - 30.0f) / 970.0f, 0.0f, 1.0f);
    float e = e30 + (e1000 - e30) * t;

    float DOT_SPACING = std::pow(200.0f / point.DotDensity, e);
    int   cols = std::max(2, (int)(point.RadiusWidth / DOT_SPACING) + 1);
    int   rows = std::max(2, (int)((bandUp + bandDown) / DOT_SPACING) + 1);

    // Pack color to 8-bit once (only alpha varies per dot below).
    const int r255 = (int)(r * 255);
    const int g255 = (int)(g * 255);
    const int b255 = (int)(b * 255);

    for (int ci = 0; ci < cols; ci++)
    {
        // t in [0,1] along the width axis
        float t  = (cols > 1) ? (float)ci / (cols - 1) : 0.5f;
        float wx = point.X + px * (-halfWidth + t * point.RadiusWidth);
        float wz = point.Z + pz * (-halfWidth + t * point.RadiusWidth);

        for (int ri = 0; ri < rows; ri++)
        {
            float s  = (rows > 1) ? (float)ri / (rows - 1) : 0.5f;
            float wy = yMin + s * (yMax - yMin);

            // Falloff: 1.0 at band centre, 0.0 at top and bottom edges
            float distFromCenter = wy - yCtr;
            float normalizedDist = (distFromCenter >= 0.0f)
                ? ((bandUp   > 0.0f) ? distFromCenter /  bandUp   : 0.0f)
                : ((bandDown > 0.0f) ? distFromCenter / -bandDown : 0.0f);

            float falloff  = 1.0f - std::abs(normalizedDist);

            // Per-dot distance fade from player position using the global fade range
            float pdx = wx - GS.PlayerX;
            float pdy = wy - GS.PlayerY;
            float pdz = wz - GS.PlayerZ;
            float playerDist = std::sqrt(pdx*pdx + pdy*pdy + pdz*pdz);
            float dotDistAlpha = std::clamp(1.0f - (playerDist - ZoneFadeStart) / (ZoneFadeEnd - ZoneFadeStart), 0.0f, 1.0f);

            int   dotAlpha = (int)(220 * falloff * dotDistAlpha);
            if (dotAlpha <= 0) continue;

            float sx, sy;
            if (ProjectWithBasis(basis, wx, wy, wz, sx, sy))
            {
                int finalAlpha = ApplyOcclusion(dotAlpha, sx, sy, os);
                if (finalAlpha <= 0) continue;
                DrawDot(dl, sx, sy, r255, g255, b255, finalAlpha);
            }
        }
    }
}

// ---------------------------------------------------------------------------
// RenderZoneMap
// ---------------------------------------------------------------------------
// Draws a screen-space indicator for MapChange trigger zones as a dot field
// in the upper-left corner of the screen.
//
// Layout:
//   • Dots are arranged on a regular grid with SPACING pixel pitch, starting
//     at (DOT_RADIUS, DOT_RADIUS) so edge dots are fully visible.
//   • Only dots where x * y < C are drawn, producing a hyperbolic cutout
//     with C = 1200 controlling the curve's openness.
//
// Alpha modulation (multiplicative):
//   • Hyperbola fade — 1.0 near the corner, falling to 0 at the curve edge,
//                      driven by (x * y) / C.
//   • Arm fade       — dots along the axis arms fade toward the arm tips,
//                      driven by max(x, y) / ARM_LEN.  This ensures both
//                      the hyperbola edge and the arm ends dissolve smoothly
//                      rather than clipping hard.
//   • Mouse repel    — dots within MouseRepelRadius px of the cursor fade to 0,
//                      with a soft falloff over the inner half of that radius.
//   • UI occlusion   — dots covered by any active ImGui window are hidden
//                      entirely via IsOccludedByUI, using the rect snapshot
//                      taken by UpdateUIRects() at the start of RenderZones().
//
// The color follows the same ColorStart/ColorGoal/ColorCheckpoint convention
// as the other zone renderers and is passed in by RenderZones.
// ---------------------------------------------------------------------------
void RenderZoneMap(const RoutePoint& point, float r, float g, float b)
{
    ImDrawList* dl  = ImGui::GetForegroundDrawList();
    ImVec2 mousePos = ImGui::GetMousePos();

    const float C            = point.HyperbolaC * 100;
    const float SPACING      = 6.0f;
    const float DOT_RADIUS   = 3.0f;
    const float ARM_LEN      = 300.0f;
    const float POWER        = 1.5f;
    const float MouseRepelRadius = 50.0f;

    // Pack color to 8-bit once (only alpha varies per dot below).
    const int r255 = (int)(r * 255);
    const int g255 = (int)(g * 255);
    const int b255 = (int)(b * 255);

    float x = DOT_RADIUS;
    while (x < ARM_LEN)
    {
        float y = DOT_RADIUS;
        while (y < ARM_LEN)
        {
            if (x * y < C)
            {
                float t_hyp    = (x * y) / C;
                float hyp_fade = std::pow(1.0f - t_hyp, POWER);

                float t_arm    = std::max(x, y) / ARM_LEN;
                float arm_fade = std::pow(std::max(0.0f, 1.0f - t_arm), POWER);

                float alpha = hyp_fade * arm_fade;
                if (alpha > 0.02f)
                {
                    // UI occlusion
                    if (IsOccludedByUI(x, y)) { y += SPACING; continue; }

                    // Mouse repel
                    float mdx        = x - mousePos.x;
                    float mdy        = y - mousePos.y;
                    float mouseDist  = std::sqrt(mdx * mdx + mdy * mdy);
                    float mouseAlpha = std::clamp(
                        (mouseDist - MouseRepelRadius * 0.5f) / (MouseRepelRadius * 0.5f),
                        0.0f, 1.0f);

                    int a = (int)(alpha * 255.0f * mouseAlpha);
                    if (a > 0)
                    {
                        DrawDot(dl, x, y, r255, g255, b255, a);
                    }
                }
            }
            y += SPACING;
        }
        x += SPACING;
    }
}

// ---------------------------------------------------------------------------
// RenderZones
// ---------------------------------------------------------------------------
// Entry point called every frame from AddonRender(). Iterates the active
// route and dispatches each checkpoint to the right shape renderer.
//
// Filtering rules:
//   • Hidden when the in-game world map is open (zones would clutter the map UI).
//   • MapChange checkpoints are drawn via RenderZoneMap (screen-space corner).
//   • Checkpoints with MapID != 0 are only drawn on their configured map.
//   • Unplaced checkpoints (all-zero position) are skipped.
//
// Draw order: start → goal → intermediates.
// The start and goal are handled separately so they are always drawn first
// and can't be skipped by the intermediate checkpoint loop.
// ---------------------------------------------------------------------------
void RenderZones()
{
    if (!ShowZones) return;
    // GS.IsMapOpen is always sourced from Mumble (RTAPI does not expose this
    // flag); skip rendering while the in-game map is fullscreen.
    if (GS.IsMapOpen) return;
    if (GS.IsLoading) return;

    UpdateUIRects(); // snapshot UI window rects before drawing any dots

    // Kick off the dot-sprite texture load the first time we render (no-op
    // once requested). Decided once per frame, not per dot: the sprite path
    // needs the draw list's active texture to be our dot texture for the
    // whole batch, so every RenderZoneCircle/Plane/Map dot this frame must
    // agree on which path is active. See DrawDot()'s comment for why.
    EnsureDotTextureRequested();
    s_useSpriteDotsThisFrame = (s_dotTexture != nullptr && s_dotTexture->Resource != nullptr);

    ImDrawList* dl = ImGui::GetForegroundDrawList();
    if (s_useSpriteDotsThisFrame)
        dl->PushTextureID((ImTextureID)s_dotTexture->Resource);

    // Camera basis and player-occlusion state are both genuinely once-per-
    // frame quantities, computed exactly once here and threaded through
    // every zone this frame rather than each zone rebuilding them itself.
    CameraBasis    basis = BuildCameraBasis();
    OcclusionState os    = CalcOcclusionState(basis);

    GuardSphereCacheSize();

    // Map ID and camera position come from GS, populated each frame from
    // whichever source is active (RTAPI or Mumble).
    unsigned int currMapID = GS.MapID;

    auto shouldRender = [&](const RoutePoint& p) -> bool
    {
        if (!RoutePointIsSet(p))                     return false;
        if (p.MapID == 0)                                   return true;
        
        if (p.TriggerType == ETriggerType::AllCheckpoints ||
            p.TriggerType == ETriggerType::MovementStart)  return false;
        // MapChange zones render on any map when MapID == 0, or on their configured map only.
        if (p.TriggerType == ETriggerType::MapChange)       return p.MapID == 0 || currMapID == p.MapID;
        return currMapID == p.MapID;
    };

    // Rolling 1-second average render time for the selected debug checkpoint.
    // The deque and last-known index are kept as statics so they survive frames.
    static std::deque<std::pair<float,float>> s_timingSamples; // {ms, timestamp}
    static int s_lastTimedIndex = -1;
    
    auto renderPoint = [&](const RoutePoint& p, float r, float g, float b, int idx)
    {
        if (!shouldRender(p)) return;

        // Hide triggered checkpoints — but never hide Null types since they
        // are purely decorative and have no triggered state.
        bool isNull = (p.TriggerType == ETriggerType::NullCircle ||
                       p.TriggerType == ETriggerType::NullPlane);
        if (!isNull && idx >= 0 && idx < (int)CheckpointStates.size() && CheckpointStates[idx].triggered)
            return;
    
        // MapChange zones are screen-space — skip world-space distance culling.
        if (p.TriggerType != ETriggerType::MapChange)
        {
        // Broad-phase cull: skip the zone only when the player is so far from
        // the zone centre that even the nearest dot on the surface is beyond
        // ZoneFadeEnd.  The extent used depends on trigger type:
        //   Circle — sphere radius
        //   Plane  — half-diagonal of width × height band, so corner dots are covered
        float extent;
        if (p.TriggerType == ETriggerType::Plane)
        {
            float halfW = p.RadiusWidth * 0.5f;
            float halfH = (p.bandUpInput + p.bandDownInput) * 0.5f; // metres
            extent = std::sqrt(halfW*halfW + halfH*halfH);
        }
        else
        {
            extent = p.RadiusWidth;
        }
        float fdx = GS.PlayerX - p.X;
        float fdy = GS.PlayerY - p.Y;
        float fdz = GS.PlayerZ - p.Z;
        float fdist = std::sqrt(fdx*fdx + fdy*fdy + fdz*fdz);
        if (fdist > ZoneFadeEnd + extent) return;
        } // end broad-phase cull
    
        bool isTimed = ShowDebug && (idx == ZoneRenderSelectedIndex);
    
        if (isTimed)
        {
            // Clear samples if selection changed.
            if (s_lastTimedIndex != idx)
            {
                s_timingSamples.clear();
                s_lastTimedIndex = idx;
            }
    
            auto t0 = std::chrono::high_resolution_clock::now();
    
            if (p.TriggerType == ETriggerType::Plane || p.TriggerType == ETriggerType::NullPlane)
                RenderZonePlane(p, r, g, b, basis, os);
            else if (p.TriggerType == ETriggerType::MapChange)
                RenderZoneMap(p, r, g, b);
            else
                RenderZoneCircle(p, r, g, b, basis, os);
    
            float ms = std::chrono::duration<float, std::milli>(
                std::chrono::high_resolution_clock::now() - t0).count();
    
            float now = (float)ImGui::GetTime();
            s_timingSamples.push_back({ ms, now });
    
            // Drop samples older than 1 second.
            while (!s_timingSamples.empty() && (now - s_timingSamples.front().second) > 1.0f)
                s_timingSamples.pop_front();
    
            // Update the shared average.
            float sum = 0.0f;
            for (auto& s : s_timingSamples) sum += s.first;
            ZoneRenderAvgMs = s_timingSamples.empty() ? 0.0f : sum / (float)s_timingSamples.size();
        }
        else
        {
            if (p.TriggerType == ETriggerType::Plane || p.TriggerType == ETriggerType::NullPlane)
                RenderZonePlane(p, r, g, b, basis, os);
            else if (p.TriggerType == ETriggerType::MapChange)
                RenderZoneMap(p, r, g, b);
            else
                RenderZoneCircle(p, r, g, b, basis, os);
        }
    };

    // Render all start checkpoints
    for (int i = 0; i < (int)CurrentRoute.Checkpoints.size(); i++)
    {
        const CheckpointState& cp = CurrentRoute.Checkpoints[i];
        if (!cp.IsStart) continue;
        renderPoint(cp.Point, ColorStart[0], ColorStart[1], ColorStart[2], i);
    }

    // Render all goal checkpoints
    for (int i = 0; i < (int)CurrentRoute.Checkpoints.size(); i++)
    {
        const CheckpointState& cp = CurrentRoute.Checkpoints[i];
        if (!cp.IsGoal) continue;
        renderPoint(cp.Point, ColorGoal[0], ColorGoal[1], ColorGoal[2], i);
    }

    // Render intermediate checkpoints
    for (int i = 0; i < (int)CurrentRoute.Checkpoints.size(); i++)
    {
        const CheckpointState& cp = CurrentRoute.Checkpoints[i];
        if (cp.IsStart || cp.IsGoal) continue;
        bool isNull = (cp.Point.TriggerType == ETriggerType::NullCircle ||
                       cp.Point.TriggerType == ETriggerType::NullPlane);
        float r = isNull ? ColorNull[0] : ColorCheckpoint[0];
        float g = isNull ? ColorNull[1] : ColorCheckpoint[1];
        float b = isNull ? ColorNull[2] : ColorCheckpoint[2];
        renderPoint(cp.Point, r, g, b, i);
    }

    if (s_useSpriteDotsThisFrame)
        dl->PopTextureID();
}