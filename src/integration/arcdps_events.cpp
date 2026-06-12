// ---------------------------------------------------------------------------
// arcdps_events.cpp
// ---------------------------------------------------------------------------
// Owns all ArcDPS Nexus event callbacks. Entry.cpp calls ArcDPS_Subscribe /
// ArcDPS_Unsubscribe on load/unload. Everything else lives here.
// ---------------------------------------------------------------------------
#include "arcdps_events.h"
#include "shared.h"
#include <cstring>
#include <mutex>

// ---------------------------------------------------------------------------
// EvCombatData
// ---------------------------------------------------------------------------
// Layout of the event args pointer Nexus passes to combat callbacks.
// Both LOCAL_RAW and SQUAD_RAW use the same struct.
// ---------------------------------------------------------------------------
struct EvCombatData
{
    ArcDPS::CombatEvent* ev;
    ArcDPS::AgentShort*  src;
    ArcDPS::AgentShort*  dst;
    const char*          skillname;
    uint64_t             id;
    uint64_t             revision;
};

// ---------------------------------------------------------------------------
// OnCombatEventInternal
// ---------------------------------------------------------------------------
static void OnCombatEventInternal(void* aEventArgs, bool isLocal)
{
    if (!aEventArgs) return;
    EvCombatData* data = (EvCombatData*)aEventArgs;

    if (!data->ev)
    {
        if (data->src && data->src->Specialization == 1)
        {
            HasTarget     = true;
            LastTarget.ID = data->src->ID;
            if (data->src->Name)
                strncpy(LastTarget.Name, data->src->Name, sizeof(LastTarget.Name) - 1);
            else
                LastTarget.Name[0] = '\0';
        }
        return;
    }

    if (data->ev->IsStatechange == ArcDPS::CBTS_SQCOMBATSTART)
    {
        SqCombatStartEvent ev = {};
        ev.ArcTime   = data->ev->Time;
        ev.LocalTime = GetTickCount64();
        ev.IsLocal   = isLocal;

        std::lock_guard<std::mutex> lock(CombatEntriesMutex);
        SqCombatStartEvents.push_back(ev);
        return;
    }

    if (data->ev->IsStatechange == ArcDPS::CBTS_ENTERCOMBAT)
    {
        SquadCombatEntry entry = {};
        entry.AgentID        = data->ev->SourceAgent;
        entry.ArcTimeEnter   = data->ev->Time;
        entry.LocalTimeEnter = GetTickCount64();
        entry.HasExited      = false;
        entry.IsLocal        = isLocal;
        if (data->src && data->src->Name)
            strncpy(entry.Name, data->src->Name, sizeof(entry.Name) - 1);

        std::lock_guard<std::mutex> lock(CombatEntriesMutex);
        if (CombatEntries.size() < 100)
            CombatEntries.push_back(entry);
        InCombat = true;
        return;
    }

    if (data->ev->IsStatechange == ArcDPS::CBTS_EXITCOMBAT)
    {
        std::lock_guard<std::mutex> lock(CombatEntriesMutex);
        for (auto& e : CombatEntries)
        {
            if (e.AgentID == data->ev->SourceAgent && e.IsLocal == isLocal && !e.HasExited)
            {
                e.ArcTimeExit   = data->ev->Time;
                e.LocalTimeExit = GetTickCount64();
                e.HasExited     = true;
                break;
            }
        }
        // Only clear the flag if it's the local player leaving combat
        if (isLocal)
            InCombat = false;
        return;
    }

    if (data->ev->IsStatechange == ArcDPS::CBTS_REWARD)
    {
        RewardEvent ev = {};
        ev.ArcTime   = data->ev->Time;
        ev.LocalTime = GetTickCount64();
        ev.AgentID   = data->ev->DestinationAgent;
        ev.IsLocal   = isLocal;

        std::lock_guard<std::mutex> lock(CombatEntriesMutex);
        if (RewardEvents.size() >= 20)
            RewardEvents.erase(RewardEvents.begin());
        RewardEvents.push_back(ev);
        return;
    }

    if (data->ev->IsStatechange != ArcDPS::CBTS_NONE)        return;
    if ((ArcDPS::ECombatResult)data->ev->Result != ArcDPS::CBTR_KILLINGBLOW) return;

    // -----------------------------------------------------------------------
    // Killing blow — store in debug vector only.
    // Trigger logic will live in addon.cpp, consuming from KillingBlows.
    // -----------------------------------------------------------------------
    {
        KillingBlowEvent ev = {};
        ev.ArcTime     = data->ev->Time;
        ev.LocalTime   = GetTickCount64();
        ev.SourceAgent = data->ev->SourceAgent;
        ev.DestAgent   = data->ev->DestinationAgent;
        ev.IFF         = (ArcDPS::EIsFriendFoe)data->ev->IFF;
        ev.IsLocal     = isLocal;
        if (data->dst && data->dst->Name)
            strncpy(ev.DestName, data->dst->Name, sizeof(ev.DestName) - 1);
        if (data->src && data->src->Name)
            strncpy(ev.SourceName, data->src->Name, sizeof(ev.SourceName) - 1);

        std::lock_guard<std::mutex> lock(CombatEntriesMutex);
        if (KillingBlows.size() >= 20)
            KillingBlows.erase(KillingBlows.begin());
        KillingBlows.push_back(ev);
    }
}

// ---------------------------------------------------------------------------
// OnCombatEventLocal / OnCombatEventSquad
// ---------------------------------------------------------------------------
static void OnCombatEventLocal(void* aEventArgs)
{
    OnCombatEventInternal(aEventArgs, true);
}

static void OnCombatEventSquad(void* aEventArgs)
{
    OnCombatEventInternal(aEventArgs, false);
}

// ---------------------------------------------------------------------------
// ArcDPS_Subscribe / ArcDPS_Unsubscribe
// ---------------------------------------------------------------------------
void ArcDPS_Subscribe()
{
    APIDefs->Events_Subscribe("EV_ARCDPS_COMBATEVENT_LOCAL_RAW", OnCombatEventLocal);
    APIDefs->Events_Subscribe("EV_ARCDPS_COMBATEVENT_SQUAD_RAW", OnCombatEventSquad);
}

void ArcDPS_Unsubscribe()
{
    APIDefs->Events_Unsubscribe("EV_ARCDPS_COMBATEVENT_LOCAL_RAW", OnCombatEventLocal);
    APIDefs->Events_Unsubscribe("EV_ARCDPS_COMBATEVENT_SQUAD_RAW", OnCombatEventSquad);
}