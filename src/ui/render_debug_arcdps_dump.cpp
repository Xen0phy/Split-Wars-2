// render_debug_arcdps_dump.cpp
// Implements RenderArcDPSDump() — a live read-out of ArcDPS integration state
// and a static reference panel for every enum and struct defined in ArcDPS.h.
//
// Only called when the "ArcDPS Dump" tab in the debug window is active,
// so there is no per-frame overhead when the tab is not visible.
//
// Unlike the Mumble and RTAPI dumps, ArcDPS does not expose a shared-memory
// block that can be polled directly.  Instead it is a plugin-callback host:
// your plugin fills in an Exports/PluginInfo struct that ArcDPS reads, and
// ArcDPS calls the function pointers inside it at the appropriate moments.
// The "live" state that can be shown here is therefore:
//
//   • Whether the ArcDPS DLL is present in the process (GetModuleHandleW).
//   • The PluginInfo fields this addon exports to ArcDPS (set at load time).
//   • Whether the LogFile / LogArc function pointers have been connected.
//   • Static reference tables for every enum defined in ArcDPS.h, useful
//     when decoding raw values arriving in combat-event callbacks.

#include "render_shared.h" // IWYU pragma: keep
#include <unordered_map>
#include <windows.h>

// ---------------------------------------------------------------------------
// Forward declaration — defined in entry.cpp / shared.cpp (see note below).
// ---------------------------------------------------------------------------
// ArcDPS populates these when it calls arc_init() on your plugin.  If your
// addon does not yet call into ArcDPS you can leave these as null/zero and
// the dump will show "Not connected" for those fields.
// To wire them up: in your arc_init() or equivalent initialisation path,
// store the arc_export table pointer and call ArcDPS::LogFile = ...,
// ArcDPS::LogArc = ... before using the Log helpers.
// ---------------------------------------------------------------------------
extern ArcDPS::PluginInfo* ArcDPSExports;   // nullptr until arc_init fires

// ---------------------------------------------------------------------------
// File-private helpers — one per enum in ArcDPS.h
// ---------------------------------------------------------------------------

static const char* StateChangeName(ArcDPS::ECombatStateChange sc)
{
    switch (sc)
    {
        case ArcDPS::CBTS_NONE:               return "CBTS_NONE";
        case ArcDPS::CBTS_ENTERCOMBAT:        return "CBTS_ENTERCOMBAT";
        case ArcDPS::CBTS_EXITCOMBAT:         return "CBTS_EXITCOMBAT";
        case ArcDPS::CBTS_CHANGEUP:           return "CBTS_CHANGEUP";
        case ArcDPS::CBTS_CHANGEDEAD:         return "CBTS_CHANGEDEAD";
        case ArcDPS::CBTS_CHANGEDOWN:         return "CBTS_CHANGEDOWN";
        case ArcDPS::CBTS_SPAWN:              return "CBTS_SPAWN";
        case ArcDPS::CBTS_DESPAWN:            return "CBTS_DESPAWN";
        case ArcDPS::CBTS_HEALTHPCTUPDATE:    return "CBTS_HEALTHPCTUPDATE";
        case ArcDPS::CBTS_SQCOMBATSTART:      return "CBTS_SQCOMBATSTART";
        case ArcDPS::CBTS_LOGEND:             return "CBTS_LOGEND";
        case ArcDPS::CBTS_WEAPSWAP:           return "CBTS_WEAPSWAP";
        case ArcDPS::CBTS_MAXHEALTHUPDATE:    return "CBTS_MAXHEALTHUPDATE";
        case ArcDPS::CBTS_POINTOFVIEW:        return "CBTS_POINTOFVIEW";
        case ArcDPS::CBTS_LANGUAGE:           return "CBTS_LANGUAGE";
        case ArcDPS::CBTS_GWBUILD:            return "CBTS_GWBUILD";
        case ArcDPS::CBTS_SHARDID:            return "CBTS_SHARDID";
        case ArcDPS::CBTS_REWARD:             return "CBTS_REWARD";
        case ArcDPS::CBTS_BUFFINITIAL:        return "CBTS_BUFFINITIAL";
        case ArcDPS::CBTS_POSITION:           return "CBTS_POSITION";
        case ArcDPS::CBTS_VELOCITY:           return "CBTS_VELOCITY";
        case ArcDPS::CBTS_FACING:             return "CBTS_FACING";
        case ArcDPS::CBTS_TEAMCHANGE:         return "CBTS_TEAMCHANGE";
        case ArcDPS::CBTS_ATTACKTARGET:       return "CBTS_ATTACKTARGET";
        case ArcDPS::CBTS_TARGETABLE:         return "CBTS_TARGETABLE";
        case ArcDPS::CBTS_MAPID:              return "CBTS_MAPID";
        case ArcDPS::CBTS_REPLINFO:           return "CBTS_REPLINFO";
        case ArcDPS::CBTS_STACKACTIVE:        return "CBTS_STACKACTIVE";
        case ArcDPS::CBTS_STACKRESET:         return "CBTS_STACKRESET";
        case ArcDPS::CBTS_GUILD:              return "CBTS_GUILD";
        case ArcDPS::CBTS_BUFFINFO:           return "CBTS_BUFFINFO";
        case ArcDPS::CBTS_BUFFFORMULA:        return "CBTS_BUFFFORMULA";
        case ArcDPS::CBTS_SKILLINFO:          return "CBTS_SKILLINFO";
        case ArcDPS::CBTS_SKILLTIMING:        return "CBTS_SKILLTIMING";
        case ArcDPS::CBTS_BREAKBARSTATE:      return "CBTS_BREAKBARSTATE";
        case ArcDPS::CBTS_BREAKBARPERCENT:    return "CBTS_BREAKBARPERCENT";
        case ArcDPS::CBTS_INTEGRITY:          return "CBTS_INTEGRITY";
        case ArcDPS::CBTS_MARKER:             return "CBTS_MARKER";
        case ArcDPS::CBTS_BARRIERPCTUPDATE:   return "CBTS_BARRIERPCTUPDATE";
        case ArcDPS::CBTS_STATRESET:          return "CBTS_STATRESET";
        case ArcDPS::CBTS_EXTENSION:          return "CBTS_EXTENSION";
        case ArcDPS::CBTS_APIDELAYED:         return "CBTS_APIDELAYED";
        case ArcDPS::CBTS_INSTANCESTART:      return "CBTS_INSTANCESTART";
        case ArcDPS::CBTS_RATEHEALTH:         return "CBTS_RATEHEALTH";
        case ArcDPS::CBTS_LAST90BEFOREDOWN:   return "CBTS_LAST90BEFOREDOWN (retired)";
        case ArcDPS::CBTS_EFFECT:             return "CBTS_EFFECT (retired)";
        case ArcDPS::CBTS_IDTOGUID:           return "CBTS_IDTOGUID";
        case ArcDPS::CBTS_LOGNPCUPDATE:       return "CBTS_LOGNPCUPDATE";
        case ArcDPS::CBTS_IDLEEVENT:          return "CBTS_IDLEEVENT";
        case ArcDPS::CBTS_EXTENSIONCOMBAT:    return "CBTS_EXTENSIONCOMBAT";
        case ArcDPS::CBTS_FRACTALSCALE:       return "CBTS_FRACTALSCALE";
        case ArcDPS::CBTS_EFFECT2:            return "CBTS_EFFECT2";
        case ArcDPS::CBTS_RULESET:            return "CBTS_RULESET";
        case ArcDPS::CBTS_SQUADMARKER:        return "CBTS_SQUADMARKER";
        case ArcDPS::CBTS_ARCBUILD:           return "CBTS_ARCBUILD";
        case ArcDPS::CBTS_GLIDER:             return "CBTS_GLIDER";
        case ArcDPS::CBTS_STUNBREAK:          return "CBTS_STUNBREAK";
        case ArcDPS::CBTS_UNKNOWN:            return "CBTS_UNKNOWN";
        default:                              return "Unknown";
    }
}

static const char* IFFName(ArcDPS::EIsFriendFoe iff)
{
    switch (iff)
    {
        case ArcDPS::IFF_FRIEND:  return "Friend";
        case ArcDPS::IFF_FOE:     return "Foe";
        case ArcDPS::IFF_UNKNOWN: return "Unknown";
        default:                  return "Unknown";
    }
}

static const char* CombatResultName(ArcDPS::ECombatResult r)
{
    switch (r)
    {
        case ArcDPS::CBTR_NORMAL:      return "Normal";
        case ArcDPS::CBTR_CRIT:        return "Crit";
        case ArcDPS::CBTR_GLANCE:      return "Glance";
        case ArcDPS::CBTR_BLOCK:       return "Block";
        case ArcDPS::CBTR_EVADE:       return "Evade";
        case ArcDPS::CBTR_INTERRUPT:   return "Interrupt";
        case ArcDPS::CBTR_ABSORB:      return "Absorb (Invuln)";
        case ArcDPS::CBTR_BLIND:       return "Blind (Miss)";
        case ArcDPS::CBTR_KILLINGBLOW: return "Killing Blow";
        case ArcDPS::CBTR_DOWNED:      return "Downed";
        case ArcDPS::CBTR_BREAKBAR:    return "Breakbar Damage";
        case ArcDPS::CBTR_ACTIVATION:  return "Activation";
        case ArcDPS::CBTR_CROWDCONTROL:return "Crowd Control";
        case ArcDPS::CBTR_UNKNOWN:     return "Unknown";
        default:                       return "Unknown";
    }
}

static const char* ActivationName(ArcDPS::ECombatActivation a)
{
    switch (a)
    {
        case ArcDPS::ACTV_NONE:              return "None";
        case ArcDPS::ACTV_START:             return "Start";
        case ArcDPS::ACTV_QUICKNESS_UNUSED:  return "Quickness (unused)";
        case ArcDPS::ACTV_CANCEL_FIRE:       return "Cancel+Fire";
        case ArcDPS::ACTV_CANCEL_CANCEL:     return "Cancel+Cancel";
        case ArcDPS::ACTV_RESET:             return "Reset (completed)";
        case ArcDPS::ACTV_UNKNOWN:           return "Unknown";
        default:                             return "Unknown";
    }
}

static const char* BuffRemoveName(ArcDPS::ECombatBuffRemove br)
{
    switch (br)
    {
        case ArcDPS::CBTB_NONE:   return "None";
        case ArcDPS::CBTB_ALL:    return "All stacks (server)";
        case ArcDPS::CBTB_SINGLE: return "Single stack (server)";
        case ArcDPS::CBTB_MANUAL: return "Single stack (arc auto)";
        case ArcDPS::CBTB_UNKNOWN:return "Unknown";
        default:                  return "Unknown";
    }
}

static const char* BuffCycleName(ArcDPS::ECombatBuffCycle bc)
{
    switch (bc)
    {
        case ArcDPS::CBTC_CYCLE:                              return "On tick";
        case ArcDPS::CBTC_NOTCYCLE:                           return "Off-tick (resistable)";
        case ArcDPS::CBTC_NOTCYCLENORESIST:                   return "Off-tick (retired)";
        case ArcDPS::CBTC_NOTCYCLEDMGTOTARGETONHIT:           return "To target on hit";
        case ArcDPS::CBTC_NOTCYCLEDMGTOSOURCEONHIT:           return "To source on hit";
        case ArcDPS::CBTC_NOTCYCLEDMGTOTARGETONSTACKREMOVE:   return "To target on stack remove";
        case ArcDPS::CBTC_UNKNOWN:                            return "Unknown";
        default:                                              return "Unknown";
    }
}

static const char* GWLanguageName(ArcDPS::EGWLanguage l)
{
    switch (l)
    {
        case ArcDPS::GWL_ENG: return "English";
        case ArcDPS::GWL_FRE: return "French";
        case ArcDPS::GWL_GEM: return "German";
        case ArcDPS::GWL_SPA: return "Spanish";
        case ArcDPS::GWL_CN:  return "Chinese";
        default:              return "Unknown";
    }
}

static const char* ContentLocalName(ArcDPS::EContentLocal cl)
{
    switch (cl)
    {
        case ArcDPS::CONTENTLOCAL_EFFECT: return "Effect";
        case ArcDPS::CONTENTLOCAL_MARKER: return "Marker";
        default:                          return "Unknown";
    }
}

// ---------------------------------------------------------------------------
// RenderArcDPSDump
// ---------------------------------------------------------------------------
// Shows ArcDPS integration status, this plugin's exported PluginInfo, the
// log function-pointer states, and static reference tables for all ArcDPS
// enums.  Called only when the ArcDPS Dump tab is active — no overhead
// otherwise.
// ---------------------------------------------------------------------------
void RenderArcDPSDump()
{
    // -------------------------------------------------------------------------
    // Availability — check whether arcdps.dll is present in the process.
    // GetModuleHandleW does not increment the reference count, so no
    // FreeLibrary is needed.
    // -------------------------------------------------------------------------
    if (ImGui::CollapsingHeader("Availability"))
    {
    HMODULE arcMod = GetModuleHandleW(L"arcdps.dll");
    bool arcLoaded = arcMod != nullptr;

    ImGui::TextColored(
        arcLoaded ? ImVec4(0.2f, 1.0f, 0.2f, 1.0f)
                  : ImVec4(1.0f, 0.3f, 0.3f, 1.0f),
        "arcdps.dll : %s", arcLoaded ? "Loaded" : "Not found in process");

    ImGui::Text("LogFile ptr : %s (0x%p)",
        ArcDPS::LogFile ? "Set"  : "nullptr",
        ArcDPS::LogFile);
    ImGui::Text("LogArc ptr  : %s (0x%p)",
        ArcDPS::LogArc  ? "Set"  : "nullptr",
        ArcDPS::LogArc);
    }

    // -------------------------------------------------------------------------
    // Struct sizes — useful sanity-check when verifying ABI compatibility.
    // -------------------------------------------------------------------------
    if (ImGui::CollapsingHeader("Struct Sizes"))
    {
    ImGui::Text("sizeof(PluginInfo)   : %zu bytes", sizeof(ArcDPS::PluginInfo));
    ImGui::Text("sizeof(CombatEvent)  : %zu bytes", sizeof(ArcDPS::CombatEvent));
    ImGui::Text("sizeof(AgentShort)   : %zu bytes", sizeof(ArcDPS::AgentShort));
    ImGui::Text("sizeof(UISettings)   : %zu bytes", sizeof(ArcDPS::UISettings));
    ImGui::Text("sizeof(Modifiers)    : %zu bytes", sizeof(ArcDPS::Modifiers));
    }

    auto FormatArcTime = [](uint64_t ms) -> std::string {
        uint64_t totalSec = ms / 1000;
        uint64_t millis   = ms % 1000;
        uint64_t seconds  = totalSec % 60;
        uint64_t minutes  = (totalSec / 60) % 60;
        uint64_t hours    = (totalSec / 3600) % 24;
        char buf[32];
        snprintf(buf, sizeof(buf), "%02llu:%02llu:%02llu.%03llu", hours, minutes, seconds, millis);
        return buf;
    };

    auto FormatOffset = [](uint64_t arcMs, uint64_t localMs) -> std::string {
        int64_t delta = (int64_t)localMs - (int64_t)arcMs;
        char buf[32];
        snprintf(buf, sizeof(buf), "%+lldms", (long long)delta);
        return buf;
    };

    // -------------------------------------------------------------------------
    // Last Killing Blow / Current Target / Combat Target
    // populated by OnCombatSquad() in entry.cpp
    // -------------------------------------------------------------------------
    if (ImGui::CollapsingHeader("Killing Blows"))
    {
        static bool filterFoe = true;
    
        if (ImGui::Button("Clear##killingblows"))
        {
            std::lock_guard<std::mutex> lock(CombatEntriesMutex);
            KillingBlows.clear();
        }
        ImGui::SameLine();
        ImGui::Checkbox("Foe only", &filterFoe);
    
        std::lock_guard<std::mutex> lock(CombatEntriesMutex);
    
        if (KillingBlows.empty())
        {
            ImGui::TextDisabled("No killing blows yet.");
        }
        else
        {
            if (ImGui::BeginTable("##killingblows", 6, ImGuiTableFlags_Borders | ImGuiTableFlags_SizingStretchSame))
            {
                ImGui::TableSetupColumn("Source");
                ImGui::TableSetupColumn("Killer");
                ImGui::TableSetupColumn("Victim");
                ImGui::TableSetupColumn("IFF");
                ImGui::TableSetupColumn("Arc Time");
                ImGui::TableSetupColumn("Offset");
                ImGui::TableHeadersRow();
    
                for (auto& e : KillingBlows)
                {
                    if (filterFoe && e.IFF != ArcDPS::IFF_FOE) continue;
    
                    // look up killer name from CombatEntries
                    std::string killerName = e.SourceName[0] ? e.SourceName : "(unknown)";
                    for (auto& ce : CombatEntries)
                    {
                        if (ce.AgentID == e.SourceAgent && ce.Name[0])
                        {
                            killerName = ce.Name;
                            break;
                        }
                    }
    
                    ImVec4 color = e.IsLocal
                        ? ImVec4(0.4f, 0.8f, 1.0f, 1.0f)
                        : ImVec4(1.0f, 0.8f, 0.4f, 1.0f);
    
                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0); ImGui::TextColored(color, "%s", e.IsLocal ? "LOCAL" : "SQUAD");
                    ImGui::TableSetColumnIndex(1); ImGui::TextColored(color, "%s", killerName.c_str());
                    ImGui::TableSetColumnIndex(2); ImGui::TextColored(color, "%s", e.DestName[0] ? e.DestName : "(unknown)");
                    ImGui::TableSetColumnIndex(3); ImGui::TextColored(color, "%s", IFFName(e.IFF));
                    ImGui::TableSetColumnIndex(4); ImGui::TextColored(color, "%s", FormatArcTime(e.ArcTime).c_str());
                    ImGui::TableSetColumnIndex(5); ImGui::TextColored(color, "%s", FormatOffset(e.ArcTime, e.LocalTime).c_str());
                }
    
                ImGui::EndTable();
            }
        }
    }

    if (ImGui::CollapsingHeader("Reward Event"))
    {
        if (ImGui::Button("Clear##rewardevents"))
        {
            std::lock_guard<std::mutex> lock(CombatEntriesMutex);
            RewardEvents.clear();
        }
    
        std::lock_guard<std::mutex> lock(CombatEntriesMutex);
    
        if (RewardEvents.empty())
        {
            ImGui::TextDisabled("No despawn events yet.");
        }
        else
        {
            if (ImGui::BeginTable("##rewardevents", 5, ImGuiTableFlags_Borders | ImGuiTableFlags_SizingStretchSame))
            {
                ImGui::TableSetupColumn("Source");
                ImGui::TableSetupColumn("Name");
                ImGui::TableSetupColumn("Agent ID");
                ImGui::TableSetupColumn("Arc Time");
                ImGui::TableSetupColumn("Offset");
                ImGui::TableHeadersRow();
    
                for (auto& e : RewardEvents)
                {
                    ImVec4 color = e.IsLocal
                        ? ImVec4(0.4f, 0.8f, 1.0f, 1.0f)
                        : ImVec4(1.0f, 0.8f, 0.4f, 1.0f);
    
                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0); ImGui::TextColored(color, "%s", e.IsLocal ? "LOCAL" : "SQUAD");
                    ImGui::TableSetColumnIndex(1); ImGui::TextColored(color, "%s", e.Name[0] ? e.Name : "(unknown)");
                    ImGui::TableSetColumnIndex(2); ImGui::TextColored(color, "0x%llX", (unsigned long long)e.AgentID);
                    ImGui::TableSetColumnIndex(3); ImGui::TextColored(color, "%s", FormatArcTime(e.ArcTime).c_str());
                    ImGui::TableSetColumnIndex(4); ImGui::TextColored(color, "%s", FormatOffset(e.ArcTime, e.LocalTime).c_str());
                }
    
                ImGui::EndTable();
            }
        }
    }

    // -------------------------------------------------------------------------
    // Log NPC Update
    // CBTS_LOGNPCUPDATE fires (realtime: yes) when ArcDPS switches which NPC
    // it considers the active log boss.  src_agent is the species id of the
    // new boss; dst_agent is the related agent id; value (as uint32) is the
    // server unix timestamp at the transition.
    // -------------------------------------------------------------------------
    if (ImGui::CollapsingHeader("Log NPC Update"))
    {
        if (ImGui::Button("Clear##lognpcupdate"))
        {
            std::lock_guard<std::mutex> lock(CombatEntriesMutex);
            LogNpcUpdateEvents.clear();
        }

        std::lock_guard<std::mutex> lock(CombatEntriesMutex);

        if (LogNpcUpdateEvents.empty())
        {
            ImGui::TextDisabled("No LOGNPCUPDATE events yet.");
        }
        else
        {
            if (ImGui::BeginTable("##lognpcupdate", 5, ImGuiTableFlags_Borders | ImGuiTableFlags_SizingStretchSame))
            {
                ImGui::TableSetupColumn("Source");
                ImGui::TableSetupColumn("Species ID");
                ImGui::TableSetupColumn("Agent ID");
                ImGui::TableSetupColumn("Arc Time");
                ImGui::TableSetupColumn("Offset");
                ImGui::TableHeadersRow();

                for (auto& e : LogNpcUpdateEvents)
                {
                    ImVec4 color = e.IsLocal
                        ? ImVec4(0.4f, 0.8f, 1.0f, 1.0f)
                        : ImVec4(1.0f, 0.8f, 0.4f, 1.0f);

                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0); ImGui::TextColored(color, "%s", e.IsLocal ? "LOCAL" : "SQUAD");
                    ImGui::TableSetColumnIndex(1); ImGui::TextColored(color, "0x%llX", (unsigned long long)e.SpeciesID);
                    ImGui::TableSetColumnIndex(2); ImGui::TextColored(color, "0x%llX", (unsigned long long)e.AgentID);
                    ImGui::TableSetColumnIndex(3); ImGui::TextColored(color, "%s", FormatArcTime(e.ArcTime).c_str());
                    ImGui::TableSetColumnIndex(4); ImGui::TextColored(color, "%s", FormatOffset(e.ArcTime, e.LocalTime).c_str());
                }

                ImGui::EndTable();
            }
        }
    }

    // -------------------------------------------------------------------------
    // Statechange frequency table — catch-all diagnostic
    //
    // Every combat event increments a slot in StatechangeFreqLocal[256] or
    // StatechangeFreqSquad[256] BEFORE any specific handler runs, so this
    // table shows the complete picture of what the Nexus bridge actually
    // delivers — including values we have no named handler for.
    //
    // How to use:
    //   1. Fight a few bosses with the debug window open.
    //   2. Check this table after combat ends.
    //   3. Any row with a non-zero count that appears only post-combat
    //      (i.e. not seen mid-fight) is a candidate for CBTS_APIDELAYED
    //      or another delayed delivery mechanism.
    //   4. Rows coloured orange are values not present in our CBTS enum —
    //      those are the most interesting unknowns.
    // -------------------------------------------------------------------------
    if (ImGui::CollapsingHeader("Statechange Frequency (catch-all)"))
    {
        if (ImGui::Button("Clear##scfreq"))
        {
            std::lock_guard<std::mutex> lock(CombatEntriesMutex);
            StatechangeFreq_Clear();
        }
        ImGui::SameLine();
        ImGui::TextDisabled("Counts every IsStatechange byte delivered by the Nexus bridge.");

        // Map raw uint8 values to a display name where we know them.
        // Values not in this table show as "??" in orange.
        static const std::unordered_map<uint8_t, const char*> kCBTSNames = {
            {0,  "NONE"},          {1,  "ENTERCOMBAT"},  {2,  "EXITCOMBAT"},
            {3,  "CHANGEUP"},      {4,  "CHANGEDEAD"},   {5,  "CHANGEDOWN"},
            {6,  "SPAWN"},         {7,  "DESPAWN"},      {8,  "HEALTHPCTUPDATE"},
            {9,  "LOGSTART"},      {10, "LOGEND"},       {11, "WEAPSWAP"},
            {12, "MAXHEALTHUPDATE"},{13,"POINTOFVIEW"},  {14, "LANGUAGE"},
            {15, "GWBUILD"},       {16, "SHARDID"},      {17, "REWARD"},
            {18, "BUFFAPPLY"},     {19, "BUFFREMOVE"},   {20, "ACTIVATION"},
            {21, "BUFFACTIVE"},    {22, "BUFFFORMULA"},  {23, "TAGCHANGE"},
            {24, "STACKACTIVE"},   {25, "STACKRESET"},   {26, "GUILD"},
            {27, "BUFFINFO"},      {28, "BUFFFORMULA2"}, {29, "DEBUGNAME"},
            {30, "DOGTAG"},        {31, "STATSRESET"},   {32, "RETIREMENT"},
            {33, "SPAWN2"},        {34, "BREAKBARSTATE"},{35, "BREAKBARPERCENT"},
            {36, "ERRORCODE"},     {37, "APIDELAYED"},   {38, "TICKRATE"},
            {39, "LAST90BEFOREDOWN"},{40,"EFFECT"},      {41, "IDTOSKILL"},
            {42, "UNKNOWN42"},     {43, "UNKNOWN43"},
        };
        // Also surface the numeric value ArcDPS.h assigns to CBTS_APIDELAYED
        // in this build so we can cross-check against what actually arrives.
        ImGui::TextDisabled("CBTS_APIDELAYED in this ArcDPS.h = %u",
            (unsigned)ArcDPS::CBTS_APIDELAYED);

        ImGui::Spacing();

        std::lock_guard<std::mutex> lock(CombatEntriesMutex);

        // Build a list of only the rows with at least one hit, plus always
        // show CBTS_APIDELAYED so its zero is visible and not buried.
        constexpr ImGuiTableFlags tflags =
            ImGuiTableFlags_Borders        |
            ImGuiTableFlags_SizingFixedFit |
            ImGuiTableFlags_RowBg;

        if (ImGui::BeginTable("##scfreq", 4, tflags))
        {
            ImGui::TableSetupColumn("Val",   ImGuiTableColumnFlags_WidthFixed, 34.f);
            ImGui::TableSetupColumn("Name",  ImGuiTableColumnFlags_WidthFixed, 160.f);
            ImGui::TableSetupColumn("LOCAL", ImGuiTableColumnFlags_WidthFixed, 64.f);
            ImGui::TableSetupColumn("SQUAD", ImGuiTableColumnFlags_WidthFixed, 64.f);
            ImGui::TableHeadersRow();

            for (int i = 0; i < 256; i++)
            {
                uint32_t loc = StatechangeFreqLocal[i];
                uint32_t sqd = StatechangeFreqSquad[i];
                bool isApidelayed = (i == (int)ArcDPS::CBTS_APIDELAYED);

                // Show rows that have hits, or always show APIDELAYED slot
                if (loc == 0 && sqd == 0 && !isApidelayed)
                    continue;

                auto it = kCBTSNames.find((uint8_t)i);
                bool known = (it != kCBTSNames.end());
                const char* name = known ? it->second : "??";

                // Unknown values → orange; APIDELAYED at zero → grey; else white
                ImVec4 nameColor;
                if (!known)
                    nameColor = ImVec4(1.0f, 0.5f, 0.1f, 1.0f); // orange = unknown
                else if (isApidelayed && loc == 0 && sqd == 0)
                    nameColor = ImVec4(0.5f, 0.5f, 0.5f, 1.0f); // grey = expected but absent
                else
                    nameColor = ImVec4(1.0f, 1.0f, 1.0f, 1.0f);

                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0); ImGui::TextColored(nameColor, "%d", i);
                ImGui::TableSetColumnIndex(1); ImGui::TextColored(nameColor, "%s", name);
                ImGui::TableSetColumnIndex(2);
                if (loc > 0) ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), "%u", loc);
                else         ImGui::TextDisabled("0");
                ImGui::TableSetColumnIndex(3);
                if (sqd > 0) ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.4f, 1.0f), "%u", sqd);
                else         ImGui::TextDisabled("0");
            }

            ImGui::EndTable();
        }
    }

    // -------------------------------------------------------------------------
    // APIDELAYED — forensic field dump
    //
    // CBTS_APIDELAYED (realtime: yes) fires once per event that ArcDPS deemed
    // unsafe for realtime, delivered in a burst after squad combat ends.
    // The EVTC spec says CBTS_HEALTHPCTUPDATE (value 8) comes through this
    // channel, which would give us an exact kill timestamp post-combat.
    //
    // The problem: the field that carries the *original* ECombatStateChange is
    // not documented.  This table dumps every byte of every APIDELAYED event
    // so we can identify the pattern by inspection:
    //   • Look for a column where a value of 8 (CBTS_HEALTHPCTUPDATE) appears
    //     consistently alongside DestinationAgent == 0 (= 0% * 10000).
    //   • Candidates: Value (int32), IFF, Buff, Result, IsActivation,
    //     IsBuffRemove, IsNinety, IsFifty, IsMoving, IsFlanking, PAD61-64.
    //   • Fields matching any known CBTS value are highlighted in yellow.
    // -------------------------------------------------------------------------
    if (ImGui::CollapsingHeader("APIDELAYED (forensic)"))
    {
        if (ImGui::Button("Clear##apidelayed"))
        {
            std::lock_guard<std::mutex> lock(CombatEntriesMutex);
            ApiDelayedEvents.clear();
        }

        ImGui::SameLine();
        ImGui::TextDisabled("(%zu / 50 slots used)", ApiDelayedEvents.size());
        ImGui::SameLine();
        ImGui::TextDisabled("|");
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.3f, 1.0f), "Yellow");
        ImGui::SameLine();
        ImGui::TextDisabled("= field value matches a CBTS constant");

        ImGui::Spacing();
        ImGui::TextDisabled("Looking for: a column that shows 8 (CBTS_HEALTHPCTUPDATE) when DstAgent == 0.");
        ImGui::Spacing();

        // Helper: return yellow if a byte value matches any ECombatStateChange
        // that is interesting (non-zero, not CBTS_UNKNOWN), white otherwise.
        auto SCHighlight = [](uint8_t v) -> ImVec4 {
            if (v == 0) return ImVec4(1,1,1,1);
            // Any plausible SC value 1..57 that isn't CBTS_NONE/CBTS_UNKNOWN
            if (v >= 1 && v <= (uint8_t)ArcDPS::CBTS_STUNBREAK)
                return ImVec4(1.0f, 1.0f, 0.3f, 1.0f); // yellow
            return ImVec4(1,1,1,1);
        };

        std::lock_guard<std::mutex> lock(CombatEntriesMutex);

        if (ApiDelayedEvents.empty())
        {
            ImGui::TextDisabled("No APIDELAYED events captured yet — fight a boss, then exit combat.");
        }
        else
        {
            // Outer child with horizontal scroll so the wide table doesn't clip
            ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, 1.0f);
            if (ImGui::BeginChild("##apidelayed_scroll", ImVec2(0, 320), true,
                ImGuiWindowFlags_HorizontalScrollbar))
            {
                constexpr ImGuiTableFlags tflags =
                    ImGuiTableFlags_Borders        |
                    ImGuiTableFlags_SizingFixedFit |
                    ImGuiTableFlags_ScrollX        |
                    ImGuiTableFlags_RowBg;

                // 24 columns: #, Src, ArcTime, Offset, SrcInst, DstInst, DstAgent(hex),
                //             Value, SkillID, IFF, Buff, Result, IsAct, IsBufRm,
                //             IsNinety, IsFifty, IsMoving, IsFlanking, IsShields,
                //             IsOffcycle, PAD61, PAD62, PAD63, PAD64
                if (ImGui::BeginTable("##apidelayed_table", 24, tflags))
                {
                    ImGui::TableSetupScrollFreeze(1, 1); // keep # column and header visible
                    ImGui::TableSetupColumn("#",          ImGuiTableColumnFlags_WidthFixed, 30.f);
                    ImGui::TableSetupColumn("Src",        ImGuiTableColumnFlags_WidthFixed, 46.f);
                    ImGui::TableSetupColumn("ArcTime",    ImGuiTableColumnFlags_WidthFixed, 90.f);
                    ImGui::TableSetupColumn("Offset",     ImGuiTableColumnFlags_WidthFixed, 62.f);
                    ImGui::TableSetupColumn("SrcInst",    ImGuiTableColumnFlags_WidthFixed, 56.f);
                    ImGui::TableSetupColumn("DstInst",    ImGuiTableColumnFlags_WidthFixed, 56.f);
                    ImGui::TableSetupColumn("DstAgent",   ImGuiTableColumnFlags_WidthFixed, 80.f);
                    ImGui::TableSetupColumn("Value",      ImGuiTableColumnFlags_WidthFixed, 52.f);
                    ImGui::TableSetupColumn("SkillID",    ImGuiTableColumnFlags_WidthFixed, 56.f);
                    ImGui::TableSetupColumn("IFF",        ImGuiTableColumnFlags_WidthFixed, 34.f);
                    ImGui::TableSetupColumn("Buff",       ImGuiTableColumnFlags_WidthFixed, 34.f);
                    ImGui::TableSetupColumn("Result",     ImGuiTableColumnFlags_WidthFixed, 40.f);
                    ImGui::TableSetupColumn("IsAct",      ImGuiTableColumnFlags_WidthFixed, 40.f);
                    ImGui::TableSetupColumn("IsBufRm",    ImGuiTableColumnFlags_WidthFixed, 52.f);
                    ImGui::TableSetupColumn("Is90",       ImGuiTableColumnFlags_WidthFixed, 34.f);
                    ImGui::TableSetupColumn("Is50",       ImGuiTableColumnFlags_WidthFixed, 34.f);
                    ImGui::TableSetupColumn("IsMov",      ImGuiTableColumnFlags_WidthFixed, 40.f);
                    ImGui::TableSetupColumn("IsFlnk",     ImGuiTableColumnFlags_WidthFixed, 42.f);
                    ImGui::TableSetupColumn("IsShld",     ImGuiTableColumnFlags_WidthFixed, 40.f);
                    ImGui::TableSetupColumn("IsOff",      ImGuiTableColumnFlags_WidthFixed, 38.f);
                    ImGui::TableSetupColumn("P61",        ImGuiTableColumnFlags_WidthFixed, 30.f);
                    ImGui::TableSetupColumn("P62",        ImGuiTableColumnFlags_WidthFixed, 30.f);
                    ImGui::TableSetupColumn("P63",        ImGuiTableColumnFlags_WidthFixed, 30.f);
                    ImGui::TableSetupColumn("P64",        ImGuiTableColumnFlags_WidthFixed, 30.f);
                    ImGui::TableHeadersRow();

                    int idx = 0;
                    for (auto& e : ApiDelayedEvents)
                    {
                        ImVec4 rowColor = e.IsLocal
                            ? ImVec4(0.4f, 0.8f, 1.0f, 1.0f)
                            : ImVec4(1.0f, 0.8f, 0.4f, 1.0f);

                        // Highlight the DstAgent column when it is 0
                        // (0% health = 0 * 10000 = 0) — the expected kill signal
                        ImVec4 dstAgentColor = (e.DestinationAgent == 0)
                            ? ImVec4(0.3f, 1.0f, 0.3f, 1.0f)
                            : rowColor;

                        ImGui::TableNextRow();
                        ImGui::TableSetColumnIndex(0);  ImGui::TextColored(rowColor,    "%d",      idx++);
                        ImGui::TableSetColumnIndex(1);  ImGui::TextColored(rowColor,    "%s",      e.IsLocal ? "LOC" : "SQD");
                        ImGui::TableSetColumnIndex(2);  ImGui::TextColored(rowColor,    "%s",      FormatArcTime(e.ArcTime).c_str());
                        ImGui::TableSetColumnIndex(3);  ImGui::TextColored(rowColor,    "%s",      FormatOffset(e.ArcTime, e.LocalTime).c_str());
                        ImGui::TableSetColumnIndex(4);  ImGui::TextColored(rowColor,    "%u",      e.SourceInstanceID);
                        ImGui::TableSetColumnIndex(5);  ImGui::TextColored(rowColor,    "%u",      e.DestinationInstanceID);
                        ImGui::TableSetColumnIndex(6);  ImGui::TextColored(dstAgentColor, "%llX", (unsigned long long)e.DestinationAgent);
                        ImGui::TableSetColumnIndex(7);  ImGui::TextColored(SCHighlight((uint8_t)e.Value), "%d", e.Value);
                        ImGui::TableSetColumnIndex(8);  ImGui::TextColored(rowColor,    "%u",      e.SkillID);
                        ImGui::TableSetColumnIndex(9);  ImGui::TextColored(SCHighlight(e.IFF),          "%u", e.IFF);
                        ImGui::TableSetColumnIndex(10); ImGui::TextColored(SCHighlight(e.Buff),         "%u", e.Buff);
                        ImGui::TableSetColumnIndex(11); ImGui::TextColored(SCHighlight(e.Result),       "%u", e.Result);
                        ImGui::TableSetColumnIndex(12); ImGui::TextColored(SCHighlight(e.IsActivation), "%u", e.IsActivation);
                        ImGui::TableSetColumnIndex(13); ImGui::TextColored(SCHighlight(e.IsBuffRemove), "%u", e.IsBuffRemove);
                        ImGui::TableSetColumnIndex(14); ImGui::TextColored(SCHighlight(e.IsNinety),     "%u", e.IsNinety);
                        ImGui::TableSetColumnIndex(15); ImGui::TextColored(SCHighlight(e.IsFifty),      "%u", e.IsFifty);
                        ImGui::TableSetColumnIndex(16); ImGui::TextColored(SCHighlight(e.IsMoving),     "%u", e.IsMoving);
                        ImGui::TableSetColumnIndex(17); ImGui::TextColored(SCHighlight(e.IsFlanking),   "%u", e.IsFlanking);
                        ImGui::TableSetColumnIndex(18); ImGui::TextColored(SCHighlight(e.IsShields),    "%u", e.IsShields);
                        ImGui::TableSetColumnIndex(19); ImGui::TextColored(SCHighlight(e.IsOffcycle),   "%u", e.IsOffcycle);
                        ImGui::TableSetColumnIndex(20); ImGui::TextColored(SCHighlight(e.PAD61),        "%u", e.PAD61);
                        ImGui::TableSetColumnIndex(21); ImGui::TextColored(SCHighlight(e.PAD62),        "%u", e.PAD62);
                        ImGui::TableSetColumnIndex(22); ImGui::TextColored(SCHighlight(e.PAD63),        "%u", e.PAD63);
                        ImGui::TableSetColumnIndex(23); ImGui::TextColored(SCHighlight(e.PAD64),        "%u", e.PAD64);
                    }

                    ImGui::EndTable();
                }
            }
            ImGui::EndChild();
            ImGui::PopStyleVar();
        }
    }
    {
        if (!HasTarget)
            ImGui::TextDisabled("No target selected yet.");
        else
        {
            ImGui::Text("ID   : 0x%llX", (unsigned long long)LastTarget.ID);
        }
    }

    if (ImGui::CollapsingHeader("Squad Combat Entries"))
    {
        if (ImGui::Button("Clear##combatentries"))
        {
            std::lock_guard<std::mutex> lock(CombatEntriesMutex);
            CombatEntries.clear();
            SqCombatStartEvents.clear();
            InCombat = false;
        }

        ImGui::Separator();
        {
            std::lock_guard<std::mutex> lock(CombatEntriesMutex);
            if (SqCombatStartEvents.empty())
            {
                ImGui::TextDisabled("SQCOMBATSTART : not fired yet");
            }
            else
            {
                for (auto& ev : SqCombatStartEvents)
                {
                    ImVec4 color = ev.IsLocal
                        ? ImVec4(0.4f, 0.8f, 1.0f, 1.0f)
                        : ImVec4(1.0f, 0.8f, 0.4f, 1.0f);
                    ImGui::TextColored(color, "SQCOMBATSTART  %s  Arc: %s  Offset: %s",
                        ev.IsLocal ? "LOCAL" : "SQUAD",
                        FormatArcTime(ev.ArcTime).c_str(),
                        FormatOffset(ev.ArcTime, ev.LocalTime).c_str());
                }
            }
        }
        ImGui::Separator();
    
        ImGui::SameLine();
        ImGui::Text("In Combat : %s", InCombat ? "yes" : "no");
        ImGui::Separator();
    
        std::lock_guard<std::mutex> lock(CombatEntriesMutex);
    
        if (CombatEntries.empty())
        {
            ImGui::TextDisabled("No entries yet.");
        }
        else
        {
            // legend
            ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), "■ LOCAL_RAW");
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.4f, 1.0f), "■ SQUAD_RAW");
    
            uint64_t firstArc = CombatEntries[0].ArcTimeEnter;
    
            if (ImGui::BeginTable("##combatentries", 6, ImGuiTableFlags_Borders | ImGuiTableFlags_SizingStretchSame))
            {
                ImGui::TableSetupColumn("Name");
                ImGui::TableSetupColumn("Source");
                ImGui::TableSetupColumn("Arc Enter");
                ImGui::TableSetupColumn("Local Enter");
                ImGui::TableSetupColumn("Arc Exit");
                ImGui::TableSetupColumn("Local Exit");
                ImGui::TableHeadersRow();
    
                for (auto& e : CombatEntries)
                {
                    ImVec4 color = e.IsLocal
                        ? ImVec4(0.4f, 0.8f, 1.0f, 1.0f)   // blue  = LOCAL_RAW
                        : ImVec4(1.0f, 0.8f, 0.4f, 1.0f);  // amber = SQUAD_RAW
    
                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0); ImGui::TextColored(color, "%s", e.Name[0] ? e.Name : "(unknown)");
                    ImGui::TableSetColumnIndex(1); ImGui::TextColored(color, "%s", e.IsLocal ? "LOCAL" : "SQUAD");
                    ImGui::TableSetColumnIndex(2); ImGui::TextColored(color, "%s", FormatArcTime(e.ArcTimeEnter).c_str());
                    ImGui::TableSetColumnIndex(3); ImGui::TextColored(color, "%s", FormatOffset(e.ArcTimeEnter, e.LocalTimeEnter).c_str());
                    ImGui::TableSetColumnIndex(4);
                    if (e.HasExited) ImGui::TextColored(color, "%s", FormatArcTime(e.ArcTimeExit).c_str());
                    else             ImGui::TextDisabled("still in");
                    ImGui::TableSetColumnIndex(5);
                    if (e.HasExited) ImGui::TextColored(color, "%s", FormatOffset(e.ArcTimeExit, e.LocalTimeExit).c_str());
                    else             ImGui::TextDisabled("still in");
                }
    
                ImGui::EndTable();
            }
        }
    }

    // =========================================================================
    // information tables, no live data
    // =========================================================================
    if (ImGui::CollapsingHeader("Data Tables - No Live Data"))
    {
        // -------------------------------------------------------------------------
        // CombatEvent field layout — handy when parsing raw events in callbacks.
        // -------------------------------------------------------------------------
        if (ImGui::CollapsingHeader("CombatEvent Field Layout"))
        {
            // Use a table so offsets and sizes are visually aligned.
            if (ImGui::BeginTable("##evtfields", 3,
                ImGuiTableFlags_Borders | ImGuiTableFlags_SizingStretchSame))
            {
                ImGui::TableSetupColumn("Field");
                ImGui::TableSetupColumn("Type");
                ImGui::TableSetupColumn("Notes");
                ImGui::TableHeadersRow();

                auto Row = [](const char* field, const char* type, const char* notes)
                {
                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0); ImGui::TextUnformatted(field);
                    ImGui::TableSetColumnIndex(1); ImGui::TextUnformatted(type);
                    ImGui::TableSetColumnIndex(2); ImGui::TextUnformatted(notes);
                };

                Row("Time",                      "uint64",  "server timestamp (ms)");
                Row("SourceAgent",               "uint64",  "source agent id");
                Row("DestinationAgent",          "uint64",  "destination agent id");
                Row("Value",                     "int32",   "effect value / statechange payload");
                Row("BuffDamage",                "int32",   "buff damage if buff==1");
                Row("OverstackValue",            "uint32",  "buff overstacking value");
                Row("SkillID",                   "uint32",  "skill/buff def id");
                Row("SourceInstanceID",          "uint16",  "src volatile id this map");
                Row("DestinationInstanceID",     "uint16",  "dst volatile id this map");
                Row("SrcMasterInstanceID",       "uint16",  "src master volatile id");
                Row("DestinationMasterInstanceID","uint16", "dst master volatile id");
                Row("IFF",                       "uint8",   "EIsFriendFoe");
                Row("Buff",                      "uint8",   "1 = buff event");
                Row("Result",                    "uint8",   "ECombatResult / buff formula result");
                Row("IsActivation",              "uint8",   "ECombatActivation");
                Row("IsBuffRemove",              "uint8",   "ECombatBuffRemove");
                Row("IsNinety",                  "uint8",   "src hp > 90%");
                Row("IsFifty",                   "uint8",   "dst hp < 50%");
                Row("IsMoving",                  "uint8",   "src is moving");
                Row("IsStatechange",             "uint8",   "ECombatStateChange (0 = not statechange)");
                Row("IsFlanking",                "uint8",   "strike hit from flank");
                Row("IsShields",                 "uint8",   "buff damage hit shields");
                Row("IsOffcycle",                "uint8",   "buff is off-cycle (ECombatBuffCycle)");
                Row("PAD61-PAD64",               "uint8×4", "padding / future use");

                ImGui::EndTable();
            }
        }

        // -------------------------------------------------------------------------
        // ECombatStateChange — full enum table with descriptions.
        // -------------------------------------------------------------------------
        if (ImGui::CollapsingHeader("ECombatStateChange"))
        {
            if (ImGui::BeginTable("##statechange", 3,
                ImGuiTableFlags_Borders | ImGuiTableFlags_SizingStretchSame))
            {
                ImGui::TableSetupColumn("Value");
                ImGui::TableSetupColumn("Name");
                ImGui::TableSetupColumn("Meaning");
                ImGui::TableHeadersRow();

                struct { ArcDPS::ECombatStateChange val; const char* desc; } rows[] =
                {
                    { ArcDPS::CBTS_NONE,             "Not a state-change event" },
                    { ArcDPS::CBTS_ENTERCOMBAT,      "Agent entered combat (val=prof, buff_dmg=elite)" },
                    { ArcDPS::CBTS_EXITCOMBAT,       "Agent left combat" },
                    { ArcDPS::CBTS_CHANGEUP,         "Agent is alive" },
                    { ArcDPS::CBTS_CHANGEDEAD,        "Agent is dead" },
                    { ArcDPS::CBTS_CHANGEDOWN,        "Agent is downed" },
                    { ArcDPS::CBTS_SPAWN,             "Agent entered tracking" },
                    { ArcDPS::CBTS_DESPAWN,           "Agent left tracking" },
                    { ArcDPS::CBTS_HEALTHPCTUPDATE,   "Health pct (dst_agent = pct*10000)" },
                    { ArcDPS::CBTS_SQCOMBATSTART,     "Squad combat start (val=server ts)" },
                    { ArcDPS::CBTS_LOGEND,            "Squad combat stop (val=server ts)" },
                    { ArcDPS::CBTS_WEAPSWAP,          "Weapon set changed (dst=new, val=old)" },
                    { ArcDPS::CBTS_MAXHEALTHUPDATE,   "Max health changed (dst_agent=new max)" },
                    { ArcDPS::CBTS_POINTOFVIEW,       "Recording player" },
                    { ArcDPS::CBTS_LANGUAGE,          "Text language id (src_agent=id)" },
                    { ArcDPS::CBTS_GWBUILD,           "Game build (src_agent=build)" },
                    { ArcDPS::CBTS_SHARDID,           "Server shard id (src_agent=id)" },
                    { ArcDPS::CBTS_REWARD,            "Reward (dst_agent=id, val=type)" },
                    { ArcDPS::CBTS_BUFFINITIAL,       "Buff already active at log start" },
                    { ArcDPS::CBTS_POSITION,          "Position changed (dst_agent=float[3])" },
                    { ArcDPS::CBTS_VELOCITY,          "Velocity changed (dst_agent=float[3])" },
                    { ArcDPS::CBTS_FACING,            "Facing changed (dst_agent=float[2])" },
                    { ArcDPS::CBTS_TEAMCHANGE,        "Team changed (dst=new, val=old)" },
                    { ArcDPS::CBTS_ATTACKTARGET,      "Attack target ↔ gadget association" },
                    { ArcDPS::CBTS_TARGETABLE,        "Targetable state (dst_agent=new)" },
                    { ArcDPS::CBTS_MAPID,             "Map id (src_agent=id)" },
                    { ArcDPS::CBTS_REPLINFO,          "Internal (ignore)" },
                    { ArcDPS::CBTS_STACKACTIVE,       "Buff instance active (dst=inst, val=dur)" },
                    { ArcDPS::CBTS_STACKRESET,        "Buff instance reset (val=new dur)" },
                    { ArcDPS::CBTS_GUILD,             "Agent guild (dst_agent=guid[16])" },
                    { ArcDPS::CBTS_BUFFINFO,          "Buff metadata (skillid=def, various)" },
                    { ArcDPS::CBTS_BUFFFORMULA,       "Buff formula (time=float[9])" },
                    { ArcDPS::CBTS_SKILLINFO,         "Skill metadata (time=float[4])" },
                    { ArcDPS::CBTS_SKILLTIMING,       "Skill timing (src=type, dst=ms)" },
                    { ArcDPS::CBTS_BREAKBARSTATE,     "Breakbar state (dst_agent=new)" },
                    { ArcDPS::CBTS_BREAKBARPERCENT,   "Breakbar pct (value=float)" },
                    { ArcDPS::CBTS_INTEGRITY,         "Error message (time=char[32])" },
                    { ArcDPS::CBTS_MARKER,            "Agent marker (val=markerdef id)" },
                    { ArcDPS::CBTS_BARRIERPCTUPDATE,  "Barrier pct (dst_agent=pct*10000)" },
                    { ArcDPS::CBTS_STATRESET,         "Arc stats reset (src=species id)" },
                    { ArcDPS::CBTS_EXTENSION,         "Extension use (unmanaged)" },
                    { ArcDPS::CBTS_APIDELAYED,        "Post-combat delayed api event" },
                    { ArcDPS::CBTS_INSTANCESTART,     "Map instance start (src=ms ago)" },
                    { ArcDPS::CBTS_RATEHEALTH,        "Tick health (src=25-tickrate)" },
                    { ArcDPS::CBTS_LAST90BEFOREDOWN,  "Retired (240529+)" },
                    { ArcDPS::CBTS_EFFECT,            "Retired (230716+)" },
                    { ArcDPS::CBTS_IDTOGUID,          "Content id→guid (src=guid[16])" },
                    { ArcDPS::CBTS_LOGNPCUPDATE,      "Log boss changed (src=species, val=ts)" },
                    { ArcDPS::CBTS_IDLEEVENT,         "Internal (ignore)" },
                    { ArcDPS::CBTS_EXTENSIONCOMBAT,   "Extension combat (unmanaged)" },
                    { ArcDPS::CBTS_FRACTALSCALE,      "Fractal scale (src_agent=scale)" },
                    { ArcDPS::CBTS_EFFECT2,           "Graphical effect (see docs)" },
                    { ArcDPS::CBTS_RULESET,           "Ruleset (src=bitmask pve/wvw/pvp)" },
                    { ArcDPS::CBTS_SQUADMARKER,       "Squad marker (src=float[3], skillid=idx)" },
                    { ArcDPS::CBTS_ARCBUILD,          "Arc build string (src=char*)" },
                    { ArcDPS::CBTS_GLIDER,            "Glider (val: 1=deploy, 0=stow)" },
                    { ArcDPS::CBTS_STUNBREAK,         "Stun break (val=remaining dur)" },
                    { ArcDPS::CBTS_UNKNOWN,           "Unknown / newer than this list" },
                };

                for (auto& r : rows)
                {
                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0); ImGui::Text("%d",   (int)r.val);
                    ImGui::TableSetColumnIndex(1); ImGui::TextUnformatted(StateChangeName(r.val));
                    ImGui::TableSetColumnIndex(2); ImGui::TextUnformatted(r.desc);
                }

                ImGui::EndTable();
            }
        }

        // -------------------------------------------------------------------------
        // EIsFriendFoe
        // -------------------------------------------------------------------------
        if (ImGui::CollapsingHeader("EIsFriendFoe"))
        {
            if (ImGui::BeginTable("##iff", 2, ImGuiTableFlags_Borders | ImGuiTableFlags_SizingStretchSame))
            {
                ImGui::TableSetupColumn("Value");
                ImGui::TableSetupColumn("Name");
                ImGui::TableHeadersRow();

                ArcDPS::EIsFriendFoe iffs[] = { ArcDPS::IFF_FRIEND, ArcDPS::IFF_FOE, ArcDPS::IFF_UNKNOWN };
                for (auto v : iffs)
                {
                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0); ImGui::Text("%d", (int)v);
                    ImGui::TableSetColumnIndex(1); ImGui::TextUnformatted(IFFName(v));
                }

                ImGui::EndTable();
            }
        }

        // -------------------------------------------------------------------------
        // ECombatResult
        // -------------------------------------------------------------------------
        if (ImGui::CollapsingHeader("ECombatResult"))
        {
            if (ImGui::BeginTable("##result", 2, ImGuiTableFlags_Borders | ImGuiTableFlags_SizingStretchSame))
            {
                ImGui::TableSetupColumn("Value");
                ImGui::TableSetupColumn("Name");
                ImGui::TableHeadersRow();

                ArcDPS::ECombatResult results[] = {
                    ArcDPS::CBTR_NORMAL, ArcDPS::CBTR_CRIT, ArcDPS::CBTR_GLANCE,
                    ArcDPS::CBTR_BLOCK, ArcDPS::CBTR_EVADE, ArcDPS::CBTR_INTERRUPT,
                    ArcDPS::CBTR_ABSORB, ArcDPS::CBTR_BLIND, ArcDPS::CBTR_KILLINGBLOW,
                    ArcDPS::CBTR_DOWNED, ArcDPS::CBTR_BREAKBAR, ArcDPS::CBTR_ACTIVATION,
                    ArcDPS::CBTR_CROWDCONTROL, ArcDPS::CBTR_UNKNOWN
                };
                for (auto v : results)
                {
                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0); ImGui::Text("%d", (int)v);
                    ImGui::TableSetColumnIndex(1); ImGui::TextUnformatted(CombatResultName(v));
                }

                ImGui::EndTable();
            }
        }

        // -------------------------------------------------------------------------
        // ECombatActivation
        // -------------------------------------------------------------------------
        if (ImGui::CollapsingHeader("ECombatActivation"))
        {
            if (ImGui::BeginTable("##actv", 2, ImGuiTableFlags_Borders | ImGuiTableFlags_SizingStretchSame))
            {
                ImGui::TableSetupColumn("Value");
                ImGui::TableSetupColumn("Name");
                ImGui::TableHeadersRow();

                ArcDPS::ECombatActivation actvs[] = {
                    ArcDPS::ACTV_NONE, ArcDPS::ACTV_START, ArcDPS::ACTV_QUICKNESS_UNUSED,
                    ArcDPS::ACTV_CANCEL_FIRE, ArcDPS::ACTV_CANCEL_CANCEL,
                    ArcDPS::ACTV_RESET, ArcDPS::ACTV_UNKNOWN
                };
                for (auto v : actvs)
                {
                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0); ImGui::Text("%d", (int)v);
                    ImGui::TableSetColumnIndex(1); ImGui::TextUnformatted(ActivationName(v));
                }

                ImGui::EndTable();
            }
        }

        // -------------------------------------------------------------------------
        // ECombatBuffRemove
        // -------------------------------------------------------------------------
        if (ImGui::CollapsingHeader("ECombatBuffRemove"))
        {
            if (ImGui::BeginTable("##buffremove", 2, ImGuiTableFlags_Borders | ImGuiTableFlags_SizingStretchSame))
            {
                ImGui::TableSetupColumn("Value");
                ImGui::TableSetupColumn("Name");
                ImGui::TableHeadersRow();

                ArcDPS::ECombatBuffRemove brs[] = {
                    ArcDPS::CBTB_NONE, ArcDPS::CBTB_ALL,
                    ArcDPS::CBTB_SINGLE, ArcDPS::CBTB_MANUAL, ArcDPS::CBTB_UNKNOWN
                };
                for (auto v : brs)
                {
                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0); ImGui::Text("%d", (int)v);
                    ImGui::TableSetColumnIndex(1); ImGui::TextUnformatted(BuffRemoveName(v));
                }

                ImGui::EndTable();
            }
        }

        // -------------------------------------------------------------------------
        // ECombatBuffCycle
        // -------------------------------------------------------------------------
        if (ImGui::CollapsingHeader("ECombatBuffCycle"))
        {
            if (ImGui::BeginTable("##buffcycle", 2, ImGuiTableFlags_Borders | ImGuiTableFlags_SizingStretchSame))
            {
                ImGui::TableSetupColumn("Value");
                ImGui::TableSetupColumn("Name");
                ImGui::TableHeadersRow();

                ArcDPS::ECombatBuffCycle bcs[] = {
                    ArcDPS::CBTC_CYCLE, ArcDPS::CBTC_NOTCYCLE,
                    ArcDPS::CBTC_NOTCYCLENORESIST,
                    ArcDPS::CBTC_NOTCYCLEDMGTOTARGETONHIT,
                    ArcDPS::CBTC_NOTCYCLEDMGTOSOURCEONHIT,
                    ArcDPS::CBTC_NOTCYCLEDMGTOTARGETONSTACKREMOVE,
                    ArcDPS::CBTC_UNKNOWN
                };
                for (auto v : bcs)
                {
                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0); ImGui::Text("%d", (int)v);
                    ImGui::TableSetColumnIndex(1); ImGui::TextUnformatted(BuffCycleName(v));
                }

                ImGui::EndTable();
            }
        }

        // -------------------------------------------------------------------------
        // EGWLanguage
        // -------------------------------------------------------------------------
        if (ImGui::CollapsingHeader("EGWLanguage"))
        {
            if (ImGui::BeginTable("##lang", 2, ImGuiTableFlags_Borders | ImGuiTableFlags_SizingStretchSame))
            {
                ImGui::TableSetupColumn("Value");
                ImGui::TableSetupColumn("Name");
                ImGui::TableHeadersRow();

                struct { ArcDPS::EGWLanguage v; } langs[] = {
                    { ArcDPS::GWL_ENG }, { ArcDPS::GWL_FRE },
                    { ArcDPS::GWL_GEM }, { ArcDPS::GWL_SPA }, { ArcDPS::GWL_CN }
                };
                for (auto& r : langs)
                {
                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0); ImGui::Text("%d", (int)r.v);
                    ImGui::TableSetColumnIndex(1); ImGui::TextUnformatted(GWLanguageName(r.v));
                }

                ImGui::EndTable();
            }
        }

        // -------------------------------------------------------------------------
        // EContentLocal
        // -------------------------------------------------------------------------
        if (ImGui::CollapsingHeader("EContentLocal"))
        {
            if (ImGui::BeginTable("##contentlocal", 2, ImGuiTableFlags_Borders | ImGuiTableFlags_SizingStretchSame))
            {
                ImGui::TableSetupColumn("Value");
                ImGui::TableSetupColumn("Name");
                ImGui::TableHeadersRow();

                ArcDPS::EContentLocal cls[] = {
                    ArcDPS::CONTENTLOCAL_EFFECT, ArcDPS::CONTENTLOCAL_MARKER
                };
                for (auto v : cls)
                {
                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0); ImGui::Text("%d", (int)v);
                    ImGui::TableSetColumnIndex(1); ImGui::TextUnformatted(ContentLocalName(v));
                }

                ImGui::EndTable();
            }
        }

        // -------------------------------------------------------------------------
        // Plugin exports — the PluginInfo struct this addon hands to ArcDPS.
        // ArcDPSExports is set during arc_init(); it remains null until then.
        // Unused due to using Nexus and no integrating ArcDPS directly
        // -------------------------------------------------------------------------
        if (ImGui::CollapsingHeader("Plugin Exports (PluginInfo)"))
        {
            if (!ArcDPSExports)
            {
                ImGui::TextDisabled("ArcDPSExports is null — arc_init() has not fired yet.");
            }
            else
            {
                const ArcDPS::PluginInfo& pi = *ArcDPSExports;
                ImGui::Text("Size          : %zu",   (size_t)pi.Size);
                ImGui::Text("Signature     : 0x%08X (%u)", pi.Signature, pi.Signature);
                ImGui::Text("ImGuiVersion  : %u",    pi.ImGuiVersion);
                ImGui::Text("Name          : %s",    pi.Name  ? pi.Name  : "(null)");
                ImGui::Text("Build         : %s",    pi.Build ? pi.Build : "(null)");

                ImGui::Spacing();
                ImGui::Text("Callback pointers:");
                ImGui::Text("  WndProc              : %s", pi.WndProc              ? "Set" : "nullptr");
                ImGui::Text("  CombatSquadCallback  : %s", pi.CombatSquadCallback  ? "Set" : "nullptr");
                ImGui::Text("  ImGuiRenderCallback  : %s", pi.ImGuiRenderCallback  ? "Set" : "nullptr");
                ImGui::Text("  ImGuiOptions         : %s", pi.ImGuiOptions         ? "Set" : "nullptr");
                ImGui::Text("  CombatLocalCallback  : %s", pi.CombatLocalCallback  ? "Set" : "nullptr");
                ImGui::Text("  WndProcFiltered      : %s", pi.WndProcFiltered      ? "Set" : "nullptr");
                ImGui::Text("  ImGuiWindows         : %s", pi.ImGuiWindows         ? "Set" : "nullptr");
            }
        }
    }
}