#ifndef POKEDIAMOND_GUNK_SAVE_H
#define POKEDIAMOND_GUNK_SAVE_H

#include "global.h"
#include "constants/species.h"

struct SaveData;

// Gunk's Diamond save entry (SAVE_GUNK, main block): the DexNav and the QoL settings. Saved with every
// normal save, beside the player's data; a new game starts it zeroed.
typedef struct GunkSave {
    u16 dexNavSpecies;    // the species the DexNav searches for, 0 = none chosen
    u16 lastRepel : 9;    // the Repel to offer when one wears off (item ids are below 512)
    u16 lastBall : 5;     // the last Poke Ball thrown, for the Quick Ball (item id; all balls are below 32)
    u16 endlessRepel : 1; // Endless Repel on
    u16 expAll : 1;       // Exp. All on
    u8 searchLevel[SPECIES_ARCEUS + 1]; // DexNav search level per species
    u8 reserved[30];      // spare, zeroed: room for later settings without moving anything
} GunkSave;

u32 GunkSave_sizeof(void);
void GunkSave_Init(GunkSave *gunkSave);
GunkSave *Save_GunkSave_Get(struct SaveData *save);

u16 GunkSave_GetDexNavSpecies(struct SaveData *save);
void GunkSave_SetDexNavSpecies(struct SaveData *save, u16 species);
u8 GunkSave_GetSearchLevel(struct SaveData *save, u16 species);
void GunkSave_IncrementSearchLevel(struct SaveData *save, u16 species);
u16 GunkSave_GetLastRepel(struct SaveData *save);
void GunkSave_SetLastRepel(struct SaveData *save, u16 item);
u16 GunkSave_GetLastBall(struct SaveData *save);
void GunkSave_SetLastBall(struct SaveData *save, u16 item);
BOOL GunkSave_IsEndlessRepelOn(struct SaveData *save);
void GunkSave_SetEndlessRepelOn(struct SaveData *save, BOOL on);
BOOL GunkSave_IsExpAllOn(struct SaveData *save);
void GunkSave_SetExpAllOn(struct SaveData *save, BOOL on);

#endif // POKEDIAMOND_GUNK_SAVE_H
