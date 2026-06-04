// window_rtapi_dump.cpp
// Implements RenderRTAPIDump() — a live read-out of every field in the
// RTAPI RealTimeData shared-memory block.
//
// Only called when the "RTAPI Dump" tab in the debug window is active,
// so there is no per-frame overhead when the tab is not visible.
// If RTAPIData is null the function shows a single warning line and returns.

#include "RTAPI.hpp"
#include "imgui.h"
#include "shared.h"

// ---------------------------------------------------------------------------
// File-private helpers
// ---------------------------------------------------------------------------
static const char* GameStateName(RTAPI::EGameState s)
{
    switch (s)
    {
        case RTAPI::EGameState::CharacterSelection: return "CharacterSelection";
        case RTAPI::EGameState::CharacterCreation:  return "CharacterCreation";
        case RTAPI::EGameState::Cinematic:          return "Cinematic";
        case RTAPI::EGameState::LoadingScreen:      return "LoadingScreen";
        case RTAPI::EGameState::Gameplay:           return "Gameplay";
        default:                                    return "Unknown";
    }
}

static const char* GameLanguageName(RTAPI::EGameLanguage l)
{
    switch (l)
    {
        case RTAPI::EGameLanguage::English: return "English";
        case RTAPI::EGameLanguage::Korean:  return "Korean";
        case RTAPI::EGameLanguage::French:  return "French";
        case RTAPI::EGameLanguage::German:  return "German";
        case RTAPI::EGameLanguage::Spanish: return "Spanish";
        case RTAPI::EGameLanguage::Chinese: return "Chinese";
        default:                            return "Unknown";
    }
}

static const char* TimeOfDayName(RTAPI::ETimeOfDay t)
{
    switch (t)
    {
        case RTAPI::ETimeOfDay::Dawn:  return "Dawn";
        case RTAPI::ETimeOfDay::Day:   return "Day";
        case RTAPI::ETimeOfDay::Dusk:  return "Dusk";
        case RTAPI::ETimeOfDay::Night: return "Night";
        default:                       return "Unknown";
    }
}

static const char* MapTypeName(RTAPI::EMapType t)
{
    switch (t)
    {
        case RTAPI::EMapType::AutoRedirect:             return "AutoRedirect";
        case RTAPI::EMapType::CharacterCreation:        return "CharacterCreation";
        case RTAPI::EMapType::PvP:                      return "PvP";
        case RTAPI::EMapType::GvG:                      return "GvG";
        case RTAPI::EMapType::Instance:                 return "Instance";
        case RTAPI::EMapType::Public:                   return "Public";
        case RTAPI::EMapType::Tournament:               return "Tournament";
        case RTAPI::EMapType::Tutorial:                 return "Tutorial";
        case RTAPI::EMapType::UserTournament:           return "UserTournament";
        case RTAPI::EMapType::WvW_EternalBattlegrounds: return "WvW_EternalBattlegrounds";
        case RTAPI::EMapType::WvW_BlueBorderlands:      return "WvW_BlueBorderlands";
        case RTAPI::EMapType::WvW_GreenBorderlands:     return "WvW_GreenBorderlands";
        case RTAPI::EMapType::WvW_RedBorderlands:       return "WvW_RedBorderlands";
        case RTAPI::EMapType::WVW_FortunesVale:         return "WvW_FortunesVale";
        case RTAPI::EMapType::WvW_ObsidianSanctum:      return "WvW_ObsidianSanctum";
        case RTAPI::EMapType::WvW_EdgeOfTheMists:       return "WvW_EdgeOfTheMists";
        case RTAPI::EMapType::Public_Mini:              return "Public_Mini";
        case RTAPI::EMapType::BigBattle:                return "BigBattle";
        case RTAPI::EMapType::WvW_Lounge:               return "WvW_Lounge";
        default:                                        return "Unknown";
    }
}

static const char* GroupTypeName(RTAPI::EGroupType g)
{
    switch (g)
    {
        case RTAPI::EGroupType::None:      return "None";
        case RTAPI::EGroupType::Party:     return "Party";
        case RTAPI::EGroupType::RaidSquad: return "RaidSquad";
        case RTAPI::EGroupType::Squad:     return "Squad";
        default:                           return "Unknown";
    }
}

// ECharacterState is a bitmask — helpers to test individual flags.
static bool CharState(RTAPI::ECharacterState val, RTAPI::ECharacterState flag)
{
    return ((uint32_t)val & (uint32_t)flag) != 0;
}

// ---------------------------------------------------------------------------
// RenderRTAPIDump
// ---------------------------------------------------------------------------
// Displays every field of the RTAPI RealTimeData block in labeled sections.
// Called only when the RTAPI Dump tab is active — no overhead otherwise.
// ---------------------------------------------------------------------------
void RenderRTAPIDump()
{
    if (!RTAPIData)
    {
        ImGui::TextDisabled("RTAPI is not available.");
        return;
    }

    const RTAPI::RealTimeData& rt = *RTAPIData;

    // -------------------------------------------------------------------------
    // Game
    // -------------------------------------------------------------------------
    ImGui::CollapsingHeader("Game");
    ImGui::Text("GameBuild : %u",    rt.GameBuild);
    ImGui::Text("GameState : %s (%u)", GameStateName(rt.GameState), (unsigned)rt.GameState);
    ImGui::Text("Language  : %s (%u)", GameLanguageName(rt.Language), (unsigned)rt.Language);

    // -------------------------------------------------------------------------
    // World / Instance
    // -------------------------------------------------------------------------
    ImGui::CollapsingHeader("World");
    ImGui::Text("TimeOfDay : %s (%u)", TimeOfDayName(rt.TimeOfDay), (unsigned)rt.TimeOfDay);
    ImGui::Text("MapID     : %u",      rt.MapID);
    ImGui::Text("MapType   : %s (%u)", MapTypeName(rt.MapType), (unsigned)rt.MapType);
    ImGui::Text("IPAddress : %u.%u.%u.%u",
        rt.IPAddress[0], rt.IPAddress[1], rt.IPAddress[2], rt.IPAddress[3]);
    ImGui::Text("Cursor    : %.4f  %.4f  %.4f",
        rt.Cursor[0], rt.Cursor[1], rt.Cursor[2]);

    // -------------------------------------------------------------------------
    // Squad markers — only show non-zero ones to reduce noise.
    // -------------------------------------------------------------------------
    ImGui::CollapsingHeader("Squad Markers");
    bool anySet = false;
    for (int i = 0; i < 8; i++)
    {
        float x = rt.SquadMarkers[i][0];
        float y = rt.SquadMarkers[i][1];
        float z = rt.SquadMarkers[i][2];
        if (x != 0.0f || y != 0.0f || z != 0.0f)
        {
            ImGui::Text("[%d] : %.4f  %.4f  %.4f", i, x, y, z);
            anySet = true;
        }
    }
    if (!anySet)
        ImGui::TextDisabled("  (all markers unset)");

    // -------------------------------------------------------------------------
    // Group
    // -------------------------------------------------------------------------
    ImGui::CollapsingHeader("Group");
    ImGui::Text("GroupType        : %s (%u)", GroupTypeName(rt.GroupType), (unsigned)rt.GroupType);
    ImGui::Text("GroupMemberCount : %u",      rt.GroupMemberCount);

    // -------------------------------------------------------------------------
    // Player
    // -------------------------------------------------------------------------
    ImGui::CollapsingHeader("Player");
    ImGui::Text("AccountName   : %s", rt.AccountName);
    ImGui::Text("CharacterName : %s", rt.CharacterName);
    ImGui::Text("Position      : %.4f  %.4f  %.4f",
        rt.CharacterPosition[0], rt.CharacterPosition[1], rt.CharacterPosition[2]);
    ImGui::Text("Facing        : %.4f  %.4f  %.4f",
        rt.CharacterFacing[0], rt.CharacterFacing[1], rt.CharacterFacing[2]);
    ImGui::Text("Profession          : %u", rt.Profession);
    ImGui::Text("EliteSpecialization : %u", rt.EliteSpecialization);
    ImGui::Text("MountIndex          : %u", rt.MountIndex);
    ImGui::Text("Level               : %u", rt.CharacterLevel);
    ImGui::Text("Effective Level     : %u", rt.CharacterEffectiveLevel);

    // -------------------------------------------------------------------------
    // Character state — bitmask, show each flag individually.
    // -------------------------------------------------------------------------
    ImGui::CollapsingHeader("Character State");
    ImGui::Text("IsAlive      : %s", CharState(rt.CharacterState, RTAPI::ECharacterState::IsAlive)      ? "Yes" : "No");
    ImGui::Text("IsDowned     : %s", CharState(rt.CharacterState, RTAPI::ECharacterState::IsDowned)     ? "Yes" : "No");
    ImGui::Text("IsInCombat   : %s", CharState(rt.CharacterState, RTAPI::ECharacterState::IsInCombat)   ? "Yes" : "No");
    ImGui::Text("IsSwimming   : %s", CharState(rt.CharacterState, RTAPI::ECharacterState::IsSwimming)   ? "Yes" : "No");
    ImGui::Text("IsUnderwater : %s", CharState(rt.CharacterState, RTAPI::ECharacterState::IsUnderwater) ? "Yes" : "No");
    ImGui::Text("IsGliding    : %s", CharState(rt.CharacterState, RTAPI::ECharacterState::IsGliding)    ? "Yes" : "No");
    ImGui::Text("IsFlying     : %s", CharState(rt.CharacterState, RTAPI::ECharacterState::IsFlying)     ? "Yes" : "No");

    // -------------------------------------------------------------------------
    // Camera
    // -------------------------------------------------------------------------
    ImGui::CollapsingHeader("Camera");
    ImGui::Text("Position     : %.4f  %.4f  %.4f",
        rt.CameraPosition[0], rt.CameraPosition[1], rt.CameraPosition[2]);
    ImGui::Text("Facing       : %.4f  %.4f  %.4f",
        rt.CameraFacing[0], rt.CameraFacing[1], rt.CameraFacing[2]);
    ImGui::Text("FOV          : %.4f rad", rt.CameraFOV);
    ImGui::Text("ActionCamera : %s", rt.IsActionCamera ? "Yes" : "No");
}
