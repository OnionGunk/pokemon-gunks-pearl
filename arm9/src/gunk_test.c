#include "global.h"

#include "gunk/gunk_save.h"
#include "gunk/gunk_test.h"
#include "gunk/gunk_dexnav_app.h"

#include "main.h"
#include "PAD_pad.h"

#include "bag.h"
#include "constants/items.h"
#include "field_system.h"
#include "heap.h"
#include "party.h"
#include "pokedex.h"
#include "poketch.h"
#include "player_data.h"
#include "pokemon.h"
#include "save.h"
#include "save_vars_flags.h"
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
u16 gGunkTestPartner; // Pearl: a second party Pokemon (level 5), e.g. for Exp. All tests
u16 gGunkTestShiny; // 1: every DexNav search rolls the shiny path
u16 gGunkTestBadges; // 1: all eight badges (Surf and the other field moves work)
u16 gGunkTestMove;   // a move for the lead's first slot (57 = Surf)
u16 gGunkTestFlags[4]; // story flags to set (0 = none), e.g. 0x115 for the Route 202 tutorial

BOOL Gunk_TestForceShiny(void) {
    return gGunkTestShiny != 0;
}

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

    if (gGunkTestBadges) {
        for (int badge = 0; badge < 8; badge++) {
            PlayerProfile_SetBadgeFlag(Save_PlayerData_GetProfile(save), (u32)badge);
        }
    }

    for (int i = 0; i < 4; i++) {
        if (gGunkTestFlags[i] != 0) {
            Save_VarsFlags_SetFlagInArray(Save_VarsFlags_Get(save), gGunkTestFlags[i]);
        }
    }

    // the Pokétch with the DexNav app showing (the watch, then the app)
    {
        struct Poketch *poketch = Save_Poketch_Get(save);

        Save_Poketch_Give(poketch);
        Save_Poketch_UnlockApp(poketch, POKETCH_DIGITAL_WATCH);
        Save_Poketch_UnlockApp(poketch, POKETCH_DEXNAV);
        poketch->selectedApp = POKETCH_DEXNAV;
    }

    // Master Balls for catching in the legality tests
    Bag_AddItem(Save_Bag_Get(save), ITEM_MASTER_BALL, 99, (enum HeapID)77);
    Bag_AddItem(Save_Bag_Get(save), ITEM_DEXNAV, 1, (enum HeapID)77);
    // Pearl: QoL key items and Repels for the Repel prompt / Endless Repel / Exp. All tests
    Bag_AddItem(Save_Bag_Get(save), ITEM_ENDLESS_REPEL, 1, (enum HeapID)77);
    Bag_AddItem(Save_Bag_Get(save), ITEM_EXP_ALL, 1, (enum HeapID)77);
    Bag_AddItem(Save_Bag_Get(save), ITEM_REPEL, 3, (enum HeapID)77);
    Bag_AddItem(Save_Bag_Get(save), ITEM_POKE_BALL, 5, (enum HeapID)77); // Pearl: a second ball type for Quick Ball
    Bag_AddItem(Save_Bag_Get(save), ITEM_HONEY, 3, (enum HeapID)77); // Pearl: honey tree test
    Bag_AddItem(Save_Bag_Get(save), ITEM_POKE_RADAR, 1, (enum HeapID)77); // Pearl: radar chain test

    if (gGunkTestSpecies != 0) {
        struct Pokemon *mon = AllocMonZeroed((enum HeapID)77); // the new-game overlay's heap

        ZeroMonData(mon);
        CreateMon(mon, gGunkTestSpecies, gGunkTestLevel ? gGunkTestLevel : 50, 32, FALSE, 0, 0, 0);

        if (gGunkTestMove != 0) {
            u32 move = gGunkTestMove;
            SetMonData(mon, MON_DATA_MOVE1, &move);
        }

        Party_AddMon(SaveArray_Party_Get(save), mon);
        Pokedex_SetMonCaughtFlag(Save_Pokedex_Get(save), mon);
        Heap_Free(mon);
    }

    // Pearl: the player has the Pokedex (catching without it leaves the registration screen stuck)
    Pokedex_SetSinnohDexFlag(Save_Pokedex_Get(save));

    if (gGunkTestPartner != 0) {
        struct Pokemon *mon = AllocMonZeroed((enum HeapID)77);

        ZeroMonData(mon);
        CreateMon(mon, gGunkTestPartner, 5, 32, FALSE, 0, 0, 0);
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
// L held with R opens the DexNav screen (in normal play the DexNav key item does)
BOOL Gunk_TestDexNavKeys(FieldSystem *fieldSystem) {
    if (gSystem.heldKeys & PAD_BUTTON_L) {
        GunkDexNav_OpenScreen(fieldSystem);
        return TRUE;
    }

    return FALSE;
}
#else
BOOL Gunk_TestForceShiny(void) {
    return FALSE;
}

BOOL Gunk_TestDexNavKeys(FieldSystem *fieldSystem) {
#pragma unused(fieldSystem)
    return FALSE;
}

BOOL Gunk_TestSkipIntro(void) {
    return FALSE;
}

void Gunk_TestNewGame(struct SaveData *save) {
#pragma unused(save)
}
#endif
