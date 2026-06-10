#pragma once
// ---------------------------------------------------------------------------
// arcdps_events.h
// ---------------------------------------------------------------------------
// ArcDPS event callback registration and combat event processing.
// Subscribe/unsubscribe via ArcDPS_Subscribe / ArcDPS_Unsubscribe.
// Raw event data is stored in shared globals for the debug dump.
// ---------------------------------------------------------------------------

void ArcDPS_Subscribe();
void ArcDPS_Unsubscribe();