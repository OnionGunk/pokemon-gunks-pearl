#include "global.h"

#include "gunk/gunk_save.h"
#include "gunk/gunk_test.h"

#include "field_system.h"
#include "heap.h"
#include "party.h"
#include "pokedex.h"
#include "player_data.h"
#include "pokemon.h"
#include "save.h"
#include "save_local_field_data.h"

// Test hooks for the emulator harness (emu/suite). They exist only in test builds (make GUNK_TEST=1); a
// release build compiles this file to empty functions. The harness writes the variables before choosing
// New Game. gGunkTestMode 1: start the new game at gGunkTestLocation (map, x, z) with a gGunkTestSpecies lead
// at gGunkTestLevel, gGunkTestSeen marked as seen and chosen on the DexNav.

#ifdef GUNK_TEST
u16 gGunkTestMode;
u16 gGunkTestLocation[3];
u16 gGunkTestSpecies;
u16 gGunkTestLevel;
u16 gGunkTestSeen;

extern LocalFieldData *Save_LocalFieldData_Get(struct SaveData *save);

BOOL Gunk_TestSkipIntro(void) {
    return gGunkTestMode != 0;
}

void Gunk_TestNewGame(struct SaveData *save) {
    if (gGunkTestMode == 0) {
        return;
    }

    // the intro is skipped, so give the player a name: TEST
    u16 *name = PlayerProfile_GetNamePtr(Save_PlayerData_GetProfile(save));

    name[0] = 0x013E;
    name[1] = 0x012F;
    name[2] = 0x013D;
    name[3] = 0x013E;
    name[4] = 0xFFFF;

    // the new game's spawn: LocalFieldData starts with the player's current location
    u32 *location = (u32 *)Save_LocalFieldData_Get(save);

    location[0] = gGunkTestLocation[0];
    location[1] = (u32)-1;
    location[2] = gGunkTestLocation[1];
    location[3] = gGunkTestLocation[2];
    location[4] = 1;

    if (gGunkTestSpecies != 0) {
        struct Pokemon *mon = AllocMonZeroed((enum HeapID)77); // the new-game overlay's heap

        ZeroMonData(mon);
        CreateMon(mon, gGunkTestSpecies, gGunkTestLevel ? gGunkTestLevel : 50, 32, FALSE, 0, 0, 0);
        Party_AddMon(SaveArray_Party_Get(save), mon);
        Pokedex_SetMonCaughtFlag(Save_Pokedex_Get(save), mon);
        Heap_Free(mon);
    }

    if (gGunkTestSeen != 0) {
        struct Pokemon *mon = AllocMonZeroed((enum HeapID)77); // the new-game overlay's heap

        ZeroMonData(mon);
        CreateMon(mon, gGunkTestSeen, 5, 32, FALSE, 0, 0, 0);
        Pokedex_SetMonSeenFlag(Save_Pokedex_Get(save), mon);
        GunkSave_SetDexNavSpecies(save, gGunkTestSeen);
        Heap_Free(mon);
    }
}
#else
BOOL Gunk_TestSkipIntro(void) {
    return FALSE;
}

void Gunk_TestNewGame(struct SaveData *save) {
#pragma unused(save)
}
#endif
