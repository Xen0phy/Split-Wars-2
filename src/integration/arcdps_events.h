// ---------------------------------------------------------------------------
// arcdps_events.h
// ---------------------------------------------------------------------------
// ArcDPS event callback registration and combat event processing.
// Subscribe/unsubscribe via ArcDPS_Subscribe / ArcDPS_Unsubscribe.
// Raw event data is stored in shared globals for the debug dump.
//
// ArcDPSCollectionEnabled is intentionally NOT a persisted setting (it is
// not part of settings_table.h). It always starts false on addon load and
// is only ever flipped by the checkbox in the ArcDPS Dump debug tab — a
// deliberate "right now" research toggle, not a standing preference that
// should linger on silently across sessions.
// ---------------------------------------------------------------------------

#pragma once

extern bool ArcDPSCollectionEnabled;

void ArcDPS_Subscribe();
void ArcDPS_Unsubscribe();