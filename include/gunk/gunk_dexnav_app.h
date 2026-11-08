#ifndef POKEDIAMOND_GUNK_DEXNAV_APP_H
#define POKEDIAMOND_GUNK_DEXNAV_APP_H

#include "global.h"
#include "field_types_def.h"
#include "overlay_manager.h"

struct SaveData;

#define DEXNAV_GRID_COLS     6
#define DEXNAV_GRID_ROWS     2
#define DEXNAV_SECTION_SLOTS (DEXNAV_GRID_COLS * DEXNAV_GRID_ROWS)
#define DEXNAV_TOTAL_SLOTS   (DEXNAV_SECTION_SLOTS * 2)

enum DexNavMethod {
    DEXNAV_METHOD_GRASS = 0,
    DEXNAV_METHOD_MORNING,
    DEXNAV_METHOD_NIGHT,
    DEXNAV_METHOD_SWARM,
    DEXNAV_METHOD_SURF,
    DEXNAV_METHOD_OLD_ROD,
    DEXNAV_METHOD_GOOD_ROD,
    DEXNAV_METHOD_SUPER_ROD,
};

typedef struct DexNavEntry {
    u16 species;
    u8 method;
    u8 minLevel;
    u8 maxLevel;
} DexNavEntry;

// Built by the field before the screen opens: slots 0-11 are land, 12-23 are water.
typedef struct DexNavData {
    struct SaveData *saveData;
    DexNavEntry slots[DEXNAV_TOTAL_SLOTS];
} DexNavData;

// arm9 (src/gunk_dexnav.c): fills a DexNavData from the current map's encounter table
void GunkDexNav_BuildAreaData(FieldSystem *fieldSystem, DexNavData *data);
// arm9: the field system of the running field (NULL before the first field frame)
FieldSystem *GunkDexNav_FieldSystem(void);
void GunkDexNav_SetFieldSystem(FieldSystem *fieldSystem);
// arm9: opens the DexNav screen from a field task and returns to the field
void GunkDexNav_OpenScreen(FieldSystem *fieldSystem);

// overlay 87 (overlays/87/src/gunk_dexnav_app.c): the screen itself
BOOL GunkDexNavApp_Init(struct OverlayManager *manager, u32 *state);
BOOL GunkDexNavApp_Main(struct OverlayManager *manager, u32 *state);
BOOL GunkDexNavApp_Exit(struct OverlayManager *manager, u32 *state);

#endif // POKEDIAMOND_GUNK_DEXNAV_APP_H
