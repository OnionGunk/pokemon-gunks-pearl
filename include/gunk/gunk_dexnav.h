#ifndef POKEDIAMOND_GUNK_DEXNAV_H
#define POKEDIAMOND_GUNK_DEXNAV_H

#include "global.h"
#include "battle_setup.h"
#include "field_types_def.h"
#include "task.h"

// The DexNav search (Gunk's Diamond, ported from Gunk's Platinum's dexnav_search.c).
// R in the field looks for the DexNav's chosen species: a shaking patch appears in the grass (or cave floor,
// or on the water while surfing), and the bar at the bottom of the top screen shows what is hiding there.
// Walk into the patch within 10 seconds of free field time to battle it.

// field hooks (overlay 5 asm calls these; see arm9/src/gunk_dexnav.c)
void Gunk_FieldInputUpdate(void *input, FieldSystem *fieldSystem, u16 pressedKeys, u16 heldKeys);
BOOL Gunk_FieldInputProcess(void *input, FieldSystem *fieldSystem);
BOOL Gunk_FieldStepEncounter(FieldSystem *fieldSystem);
BOOL Gunk_FieldExit(void *overlayManager, u32 *state);

// encounter hooks (src/encounter.c)
void GunkDexNav_OnWildEncounter(BattleSetup *setup);
void GunkDexNav_OnBattleEnd(u32 winFlag);

BOOL GunkDexNav_IsActive(void);

#endif // POKEDIAMOND_GUNK_DEXNAV_H
