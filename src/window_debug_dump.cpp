// renderer_debug_dump.cpp
// Implements RenderMumbleDump() — a live read-out of every field in the
// MumbleLink shared-memory block.
//
// Only called when the "Mumble Dump" tab in the debug window is active,
// so there is no per-frame overhead when the tab is not visible.
// If MumbleLink is null the function shows a single warning line and returns.

#include "Mumble.h"
#include "imgui.h"
#include "shared.h"

// ---------------------------------------------------------------------------
// MapTypeName / MountName  (file-private helpers)
// ---------------------------------------------------------------------------
static const char* MapTypeName(Mumble::EMapType t)
{
    switch (t)
    {
        case Mumble::EMapType::AutoRedirect:              return "AutoRedirect";
        case Mumble::EMapType::CharacterCreation:         return "CharacterCreation";
        case Mumble::EMapType::PvP:                       return "PvP";
        case Mumble::EMapType::GvG:                       return "GvG";
        case Mumble::EMapType::Instance:                  return "Instance";
        case Mumble::EMapType::Public:                    return "Public";
        case Mumble::EMapType::Tournament:                return "Tournament";
        case Mumble::EMapType::Tutorial:                  return "Tutorial";
        case Mumble::EMapType::UserTournament:            return "UserTournament";
        case Mumble::EMapType::WvW_EternalBattlegrounds:  return "WvW_EternalBattlegrounds";
        case Mumble::EMapType::WvW_BlueBorderlands:       return "WvW_BlueBorderlands";
        case Mumble::EMapType::WvW_GreenBorderlands:      return "WvW_GreenBorderlands";
        case Mumble::EMapType::WvW_RedBorderlands:        return "WvW_RedBorderlands";
        case Mumble::EMapType::WVW_FortunesVale:          return "WvW_FortunesVale";
        case Mumble::EMapType::WvW_ObsidianSanctum:       return "WvW_ObsidianSanctum";
        case Mumble::EMapType::WvW_EdgeOfTheMists:        return "WvW_EdgeOfTheMists";
        case Mumble::EMapType::Public_Mini:               return "Public_Mini";
        case Mumble::EMapType::BigBattle:                 return "BigBattle";
        case Mumble::EMapType::WvW_Lounge:                return "WvW_Lounge";
        default:                                          return "Unknown";
    }
}

static const char* MountName(Mumble::EMountIndex m)
{
    switch (m)
    {
        case Mumble::EMountIndex::None:          return "None";
        case Mumble::EMountIndex::Jackal:        return "Jackal";
        case Mumble::EMountIndex::Griffon:       return "Griffon";
        case Mumble::EMountIndex::Springer:      return "Springer";
        case Mumble::EMountIndex::Skimmer:       return "Skimmer";
        case Mumble::EMountIndex::Raptor:        return "Raptor";
        case Mumble::EMountIndex::RollerBeetle:  return "Roller Beetle";
        case Mumble::EMountIndex::Warclaw:       return "Warclaw";
        case Mumble::EMountIndex::Skyscale:      return "Skyscale";
        case Mumble::EMountIndex::Skiff:         return "Skiff";
        case Mumble::EMountIndex::SiegeTurtle:   return "Siege Turtle";
        default:                                 return "Unknown";
    }
}

// ---------------------------------------------------------------------------
// RenderMumbleDump
// ---------------------------------------------------------------------------
// Displays every field of the MumbleLink block in labeled sections.
// Called only when the Mumble Dump tab is active — no overhead otherwise.
// ---------------------------------------------------------------------------
void RenderMumbleDump()
{
    if (!MumbleLink)
    {
        ImGui::TextDisabled("MumbleLink is not available.");
        return;
    }

    const Mumble::Data&    ml  = *MumbleLink;
    const Mumble::Context& ctx = ml.Context;

    // Helper: convert wchar_t field to a displayable char buffer.
    auto wToChar = [](const wchar_t* src, char* dst, int dstSize)
    {
        wcstombs(dst, src, dstSize - 1);
        dst[dstSize - 1] = '\0';
    };

    char strBuf[512];

    // -------------------------------------------------------------------------
    // UI
    // -------------------------------------------------------------------------
    ImGui::CollapsingHeader("UI");
    ImGui::Text("UIVersion : %u", ml.UIVersion);
    ImGui::Text("UITick    : %u", ml.UITick);

    // -------------------------------------------------------------------------
    // Strings
    // -------------------------------------------------------------------------
    ImGui::CollapsingHeader("Strings");
    wToChar(ml.Name, strBuf, sizeof(strBuf));
    ImGui::Text("Name     : %s", strBuf);
    wToChar(ml.Identity, strBuf, sizeof(strBuf));
    ImGui::Text("Identity : %s", strBuf);

    // -------------------------------------------------------------------------
    // Avatar (player)
    // -------------------------------------------------------------------------
    ImGui::CollapsingHeader("Avatar");
    ImGui::Text("Position : %.4f  %.4f  %.4f", ml.AvatarPosition.X, ml.AvatarPosition.Y, ml.AvatarPosition.Z);
    ImGui::Text("Front    : %.4f  %.4f  %.4f", ml.AvatarFront.X,    ml.AvatarFront.Y,    ml.AvatarFront.Z);
    ImGui::Text("Top      : %.4f  %.4f  %.4f", ml.AvatarTop.X,      ml.AvatarTop.Y,      ml.AvatarTop.Z);

    // -------------------------------------------------------------------------
    // Camera
    // -------------------------------------------------------------------------
    ImGui::CollapsingHeader("Camera");
    ImGui::Text("Position : %.4f  %.4f  %.4f", ml.CameraPosition.X, ml.CameraPosition.Y, ml.CameraPosition.Z);
    ImGui::Text("Front    : %.4f  %.4f  %.4f", ml.CameraFront.X,    ml.CameraFront.Y,    ml.CameraFront.Z);
    ImGui::Text("Top      : %.4f  %.4f  %.4f", ml.CameraTop.X,      ml.CameraTop.Y,      ml.CameraTop.Z);

    // -------------------------------------------------------------------------
    // Context — map / instance
    // -------------------------------------------------------------------------
    ImGui::CollapsingHeader("Context — Map");
    ImGui::Text("MapID         : %u",   ctx.MapID);
    ImGui::Text("MapType       : %s (%u)", MapTypeName(ctx.MapType), (unsigned)ctx.MapType);
    ImGui::Text("ShardID       : %u",   ctx.ShardID);
    ImGui::Text("InstanceID    : %u",   ctx.InstanceID);
    ImGui::Text("BuildID       : %u",   ctx.BuildID);
    ImGui::Text("ProcessID     : %u",   ctx.ProcessID);
    ImGui::Text("ContextLength : %u",   ml.ContextLength);

    // -------------------------------------------------------------------------
    // Context — flags
    // -------------------------------------------------------------------------
    ImGui::CollapsingHeader("Context — Flags");
    ImGui::Text("IsMapOpen          : %s", ctx.IsMapOpen          ? "Yes" : "No");
    ImGui::Text("IsCompassTopRight  : %s", ctx.IsCompassTopRight  ? "Yes" : "No");
    ImGui::Text("IsCompassRotating  : %s", ctx.IsCompassRotating  ? "Yes" : "No");
    ImGui::Text("IsGameFocused      : %s", ctx.IsGameFocused      ? "Yes" : "No");
    ImGui::Text("IsCompetitive      : %s", ctx.IsCompetitive      ? "Yes" : "No");
    ImGui::Text("IsTextboxFocused   : %s", ctx.IsTextboxFocused   ? "Yes" : "No");
    ImGui::Text("IsInCombat         : %s", ctx.IsInCombat         ? "Yes" : "No");

    // -------------------------------------------------------------------------
    // Context — mount
    // -------------------------------------------------------------------------
    ImGui::CollapsingHeader("Context — Mount");
    ImGui::Text("MountIndex : %s (%u)", MountName(ctx.MountIndex), (unsigned)ctx.MountIndex);

    // -------------------------------------------------------------------------
    // Compass
    // -------------------------------------------------------------------------
    ImGui::CollapsingHeader("Compass");
    ImGui::Text("Size           : %u x %u",   ctx.Compass.Width, ctx.Compass.Height);
    ImGui::Text("Rotation       : %.4f rad",  ctx.Compass.Rotation);
    ImGui::Text("PlayerPosition : %.2f  %.2f", ctx.Compass.PlayerPosition.X, ctx.Compass.PlayerPosition.Y);
    ImGui::Text("Center         : %.2f  %.2f", ctx.Compass.Center.X,         ctx.Compass.Center.Y);
    ImGui::Text("Scale          : %.4f",       ctx.Compass.Scale);

    // -------------------------------------------------------------------------
    // Server address
    // Raw bytes — may be sockaddr_in (IPv4) or sockaddr_in6 (IPv6).
    // For IPv4 (family = 2): bytes 0-1 = family, 2-3 = port (BE), 4-7 = IP.
    // -------------------------------------------------------------------------
    ImGui::CollapsingHeader("Server Address");
    const unsigned char* addr   = ctx.ServerAddress;
    uint16_t             family = (uint16_t)(addr[0] | (addr[1] << 8));
    if (family == 2) // AF_INET
    {
        uint16_t port = (uint16_t)((addr[2] << 8) | addr[3]);
        ImGui::Text("IPv4 : %u.%u.%u.%u : %u", addr[4], addr[5], addr[6], addr[7], port);
    }
    else
    {
        // Show first 16 bytes as hex for IPv6 or unknown families.
        ImGui::Text("Family : %u (non-IPv4)", family);
        ImGui::Text("Raw    : %02X %02X %02X %02X %02X %02X %02X %02X "
                              "%02X %02X %02X %02X %02X %02X %02X %02X",
            addr[0],  addr[1],  addr[2],  addr[3],
            addr[4],  addr[5],  addr[6],  addr[7],
            addr[8],  addr[9],  addr[10], addr[11],
            addr[12], addr[13], addr[14], addr[15]);
    }
}