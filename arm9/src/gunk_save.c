#include "global.h"

#include "gunk/gunk_save.h"

#include "MI_memory.h"
#include "save.h"

#define MAX_SEARCH_LEVEL 255

u32 GunkSave_sizeof(void) {
    return sizeof(GunkSave);
}

void GunkSave_Init(GunkSave *gunkSave) {
    MI_CpuClear8(gunkSave, sizeof(GunkSave));
}

GunkSave *Save_GunkSave_Get(struct SaveData *save) {
    return SaveArray_Get(save, SAVE_GUNK);
}

u16 GunkSave_GetDexNavSpecies(struct SaveData *save) {
    return Save_GunkSave_Get(save)->dexNavSpecies;
}

void GunkSave_SetDexNavSpecies(struct SaveData *save, u16 species) {
    Save_GunkSave_Get(save)->dexNavSpecies = species;
}

u8 GunkSave_GetSearchLevel(struct SaveData *save, u16 species) {
    return species <= SPECIES_ARCEUS ? Save_GunkSave_Get(save)->searchLevel[species] : 0;
}

void GunkSave_IncrementSearchLevel(struct SaveData *save, u16 species) {
    GunkSave *gunkSave = Save_GunkSave_Get(save);

    if (species <= SPECIES_ARCEUS && gunkSave->searchLevel[species] < MAX_SEARCH_LEVEL) {
        gunkSave->searchLevel[species]++;
    }
}

u16 GunkSave_GetLastRepel(struct SaveData *save) {
    return Save_GunkSave_Get(save)->lastRepel;
}

void GunkSave_SetLastRepel(struct SaveData *save, u16 item) {
    Save_GunkSave_Get(save)->lastRepel = item;
}

u16 GunkSave_GetLastBall(struct SaveData *save) {
    return Save_GunkSave_Get(save)->lastBall;
}

void GunkSave_SetLastBall(struct SaveData *save, u16 item) {
    if (item < 32) {
        Save_GunkSave_Get(save)->lastBall = item;
    }
}

BOOL GunkSave_IsEndlessRepelOn(struct SaveData *save) {
    return Save_GunkSave_Get(save)->endlessRepel;
}

void GunkSave_SetEndlessRepelOn(struct SaveData *save, BOOL on) {
    Save_GunkSave_Get(save)->endlessRepel = on ? 1 : 0;
}

BOOL GunkSave_IsExpAllOn(struct SaveData *save) {
    return Save_GunkSave_Get(save)->expAll;
}

void GunkSave_SetExpAllOn(struct SaveData *save, BOOL on) {
    Save_GunkSave_Get(save)->expAll = on ? 1 : 0;
}
