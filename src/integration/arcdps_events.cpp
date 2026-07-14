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

    // --- Catch-all frequency counter ---
    // Incremented for EVERY event before any specific handler runs, so the
    // debug dump can show exactly which IsStatechange values the Nexus bridge
    // actually delivers — including any that we have no handler for yet.
    // This is how CBTS_APIDELAYED was first confirmed to arrive, before it
    // got the dedicated handler below; the same counter still tracks any
    // other type that hasn't earned a handler yet.
    {
        std::lock_guard<std::mutex> lock(CombatEntriesMutex);
        if (isLocal)
            StatechangeFreqLocal[data->ev->IsStatechange]++;
        else
            StatechangeFreqSquad[data->ev->IsStatechange]++;
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

    if (data->ev->IsStatechange == ArcDPS::CBTS_LOGNPCUPDATE)
    {
        LogNpcUpdateEvent ev = {};
        ev.ArcTime    = data->ev->Time;
        ev.LocalTime  = GetTickCount64();
        ev.SpeciesID  = data->ev->SourceAgent;
        ev.AgentID    = data->ev->DestinationAgent;
        ev.ServerTime = (uint32_t)data->ev->Value;
        ev.IsLocal    = isLocal;

        std::lock_guard<std::mutex> lock(CombatEntriesMutex);
        if (LogNpcUpdateEvents.size() >= 20)
            LogNpcUpdateEvents.erase(LogNpcUpdateEvents.begin());
        LogNpcUpdateEvents.push_back(ev);
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

    // --- APIDELAYED: capture every field verbatim ---
    // ArcDPS fires one of these per event it deemed unsafe for realtime,
    // delivered after squad combat ends.  The field that carries the original
    // ECombatStateChange is undocumented; storing everything lets the debug
    // dump reveal it by inspection across a post-combat burst.
    // Capped at 50 to survive a full post-combat flush without runaway alloc.
    if (data->ev->IsStatechange == ArcDPS::CBTS_APIDELAYED)
    {
        ApiDelayedEvent ev = {};
        ev.ArcTime               = data->ev->Time;
        ev.LocalTime             = GetTickCount64();
        ev.SourceAgent           = data->ev->SourceAgent;
        ev.DestinationAgent      = data->ev->DestinationAgent;
        ev.Value                 = data->ev->Value;
        ev.BuffDamage            = data->ev->BuffDamage;
        ev.OverstackValue        = data->ev->OverstackValue;
        ev.SkillID               = data->ev->SkillID;
        ev.SourceInstanceID      = data->ev->SourceInstanceID;
        ev.DestinationInstanceID = data->ev->DestinationInstanceID;
        ev.SrcMasterInstanceID   = data->ev->SrcMasterInstanceID;
        ev.DstMasterInstanceID   = data->ev->DestinationMasterInstanceID;
        ev.IFF                   = data->ev->IFF;
        ev.Buff                  = data->ev->Buff;
        ev.Result                = data->ev->Result;
        ev.IsActivation          = data->ev->IsActivation;
        ev.IsBuffRemove          = data->ev->IsBuffRemove;
        ev.IsNinety              = data->ev->IsNinety;
        ev.IsFifty               = data->ev->IsFifty;
        ev.IsMoving              = data->ev->IsMoving;
        ev.IsStatechange         = data->ev->IsStatechange;
        ev.IsFlanking            = data->ev->IsFlanking;
        ev.IsShields             = data->ev->IsShields;
        ev.IsOffcycle            = data->ev->IsOffcycle;
        ev.PAD61                 = data->ev->PAD61;
        ev.PAD62                 = data->ev->PAD62;
        ev.PAD63                 = data->ev->PAD63;
        ev.PAD64                 = data->ev->PAD64;
        ev.IsLocal               = isLocal;

        std::lock_guard<std::mutex> lock(CombatEntriesMutex);
        if (ApiDelayedEvents.size() >= 50)
            ApiDelayedEvents.erase(ApiDelayedEvents.begin());
        ApiDelayedEvents.push_back(ev);
        return;
    }

    if (data->ev->IsStatechange != ArcDPS::CBTS_NONE)        return;
    if ((ArcDPS::ECombatResult)data->ev->Result != ArcDPS::CBTR_KILLINGBLOW) return;

    // --- Killing blow: store in debug vector only ---
    // Trigger logic will live in addon.cpp, consuming from KillingBlows.
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
// Idempotent: ArcDPSCollectionEnabled doubles as both the public on/off
// state shown by the checkbox and the guard against double-subscribing or
// unsubscribing-without-subscribing, since nothing else calls these.
// ---------------------------------------------------------------------------
bool ArcDPSCollectionEnabled = false;

void ArcDPS_Subscribe()
{
    if (ArcDPSCollectionEnabled) return;
    APIDefs->Events_Subscribe("EV_ARCDPS_COMBATEVENT_LOCAL_RAW", OnCombatEventLocal);
    APIDefs->Events_Subscribe("EV_ARCDPS_COMBATEVENT_SQUAD_RAW", OnCombatEventSquad);
    ArcDPSCollectionEnabled = true;
}

void ArcDPS_Unsubscribe()
{
    if (!ArcDPSCollectionEnabled) return;
    APIDefs->Events_Unsubscribe("EV_ARCDPS_COMBATEVENT_LOCAL_RAW", OnCombatEventLocal);
    APIDefs->Events_Unsubscribe("EV_ARCDPS_COMBATEVENT_SQUAD_RAW", OnCombatEventSquad);
    ArcDPSCollectionEnabled = false;
}