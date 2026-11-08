#include "global.h"

#include "gunk/gunk_dexnav_app.h"
#include "gunk/gunk_save.h"

#include "constants/species.h"

#include "heap.h"
#include "pokedex.h"
#include "save.h"
#include "string16.h"

// The DexNav Poketch app: up to 12 of the area's Pokemon (land first, then water) on the Pokemon History app's
// 4x3 grid. Caught: the icon in the Poketch's shades; seen: a silhouette; unseen: a silhouetted Unown "?".
// Touching a seen Pokemon chooses it for the DexNav (R then searches for it) and plays its cry.

#define MAX_ICONS          12
#define STATUS_UNSEEN      0
#define STATUS_SEEN        1
#define STATUS_CAUGHT      2
#define SILHOUETTE_PALETTE 3 // the Poketch fills OBJ palette 3 with its darkest shade
#define UNOWN_QUESTION     27

typedef struct PoketchDexNavEntry {
    u32 species;
    u32 formAndStatus; // form in the low byte, status above it
} PoketchDexNavEntry;

extern u32 sub_0206B6D4(u32 species, u32 isEgg, u32 form); // icon tiles index
extern u32 sub_0206B7BC(u32 species, u32 form, u32 isEgg); // icon palette
extern void ov20_02252B48(u32 species);                    // the Poketch plays a cry

void GunkPoketchDexNav_Fill(u8 *handler);
u32 GunkPoketchDexNav_IconIndex(PoketchDexNavEntry *entry);
u32 GunkPoketchDexNav_IconPalette(PoketchDexNavEntry *entry);
void GunkPoketchDexNav_Touched(PoketchDexNavEntry *entry);
struct String *GunkPoketchDexNav_Title(NarcId narc, u32 group, u32 num, enum HeapID heapID);

static u32 Status(const PoketchDexNavEntry *entry) {
    return entry->formAndStatus >> 8;
}

void GunkPoketchDexNav_Fill(u8 *handler) {
    PoketchDexNavEntry *entries = (PoketchDexNavEntry *)(handler + 4);
    struct SaveData *save = sub_020225F8();
    struct Pokedex *pokedex = Save_Pokedex_Get(save);
    FieldSystem *fieldSystem = GunkDexNav_FieldSystem();
    DexNavData *data;
    u32 count = 0;

    if (fieldSystem != NULL) {
        data = Heap_AllocAtEnd(HEAP_ID_POKETCH_APP, sizeof(DexNavData));
        GunkDexNav_BuildAreaData(fieldSystem, data);

        for (int i = 0; i < DEXNAV_TOTAL_SLOTS && count < MAX_ICONS; i++) {
            u16 species = data->slots[i].species;
            u32 status;

            if (species == SPECIES_NONE) {
                continue;
            }

            status = Pokedex_CheckMonCaughtFlag(pokedex, species) ? STATUS_CAUGHT
                : Pokedex_CheckMonSeenFlag(pokedex, species)       ? STATUS_SEEN
                                                                   : STATUS_UNSEEN;
            entries[count].species = species;
            entries[count].formAndStatus = status << 8;
            count++;
        }

        Heap_Free(data);
    }

    *(u32 *)(handler + 0x64) = count;
}

u32 GunkPoketchDexNav_IconIndex(PoketchDexNavEntry *entry) {
    if (Status(entry) == STATUS_UNSEEN) {
        return sub_0206B6D4(SPECIES_UNOWN, FALSE, UNOWN_QUESTION);
    }

    return sub_0206B6D4(entry->species, FALSE, entry->formAndStatus & 0xFF);
}

u32 GunkPoketchDexNav_IconPalette(PoketchDexNavEntry *entry) {
    if (Status(entry) != STATUS_CAUGHT) {
        return SILHOUETTE_PALETTE;
    }

    return sub_0206B7BC(entry->species, entry->formAndStatus & 0xFF, FALSE);
}

void GunkPoketchDexNav_Touched(PoketchDexNavEntry *entry) {
    if (Status(entry) == STATUS_UNSEEN) {
        return;
    }

    GunkSave_SetDexNavSpecies(sub_020225F8(), (u16)entry->species);
    ov20_02252B48(entry->species);
}

struct String *GunkPoketchDexNav_Title(NarcId narc, u32 group, u32 num, enum HeapID heapID) {
#pragma unused(narc, group, num)
    static const u16 sTitle[] = { 0x012E, 0x0149, 0x015C, 0x0138, 0x0145, 0x015A, 0xFFFF }; // DexNav
    struct String *title = String_New(8, heapID);

    CopyU16ArrayToString(title, (u16 *)sTitle);
    return title;
}
