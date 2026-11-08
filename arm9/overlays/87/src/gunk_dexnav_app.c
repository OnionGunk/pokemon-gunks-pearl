#include "global.h"

#include "gunk/gunk_dexnav_app.h"
#include "gunk/gunk_save.h"

#include "constants/rgb.h"
#include "constants/sndseq.h"
#include "constants/species.h"

#include "bg_window.h"
#include "filesystem.h"
#include "game_init.h"
#include "gf_gfx_loader.h"
#include "gf_gfx_planes.h"
#include "heap.h"
#include "main.h"
#include "msgdata.h"
#include "overlay_manager.h"
#include "pokedex.h"
#include "string16.h"
#include "string_util.h"
#include "text.h"
#include "unk_020051F4.h"

#include "PAD_pad.h"

#include "gunk/dexnav_art.h"

// The DexNav screen (Gunk's Diamond, after Gunk's Platinum's applications/dexnav.c).
// Top screen: the title bar with the area's caught count, the highlighted Pokemon's card and the search card.
// Bottom screen: the LAND and WATER grids. Pokemon icons are drawn on their own BG layer (the icon tiles are
// copied into BG character memory), caught Pokemon in colour, seen ones as a silhouette, unseen as "?".

extern void BeginNormalPaletteFade(u32 pattern, u32 typeTop, u32 typeBottom, u16 colour, u32 duration, u32 framesPer, enum HeapID heapID);
extern BOOL IsPaletteFadeFinished(void);
extern const u8 UNK_020F8074[]; // each species' icon palette (0-2)

#define HEAP_ID_DEXNAV ((enum HeapID)77) // the new-game heap; never in use while the field runs

#define LAYER_TOP_TEXT    GF_BG_LYR_MAIN_0
#define LAYER_TOP_ART     GF_BG_LYR_MAIN_1
#define LAYER_BOTTOM_TEXT GF_BG_LYR_SUB_0
#define LAYER_BOTTOM_ART  GF_BG_LYR_SUB_1
#define LAYER_ICONS       GF_BG_LYR_SUB_2

// BG palette slots: 0 = art, 1 = art with the selected-cell colours, 2 = text, 3-5 = icons, 6 = silhouette
#define PAL_ART        0
#define PAL_SELECTED   1
#define PAL_TEXT       2
#define PAL_ICONS      3
#define PAL_SILHOUETTE 6

#define TEXT_DARK      MAKE_TEXT_COLOR(1, 2, 0)
#define TEXT_ON_BAR    MAKE_TEXT_COLOR(3, 4, 0)

#define CELL_W         40
#define CELL_H         32
#define GRID_LEFT      8
#define LAND_TOP       16
#define WATER_TOP      104

#define ICON_FIRST_FILE 7 // poke_icon.narc: species 0's icon; species n is 7 + n
#define ICON_TILES      16
#define ICON_BYTES      (ICON_TILES * 32)

#define CHAR_CAUGHT 0x01CB // the ball-like mark in the font
#define CHAR_STAR   0x01CA

typedef struct DexNavApp {
    DexNavData *args;
    struct Pokedex *pokedex;
    BgConfig *bgConfig;
    Window topWindow;
    Window bottomWindow;
    int cursor;
    int state;
} DexNavApp;

static const char *const sMethodNames[] = {
    "Tall grass",
    "Grass - Morning",
    "Grass - Night",
    "Swarm",
    "Surfing",
    "Old Rod",
    "Good Rod",
    "Super Rod",
};

static const struct GraphicsModes sGraphicsModes = {
    .dispMode = GX_DISPMODE_GRAPHICS,
    .bgMode = GX_BGMODE_0,
    .subMode = GX_BGMODE_0,
    ._2d3dMode = GX_BG0_AS_2D,
};

static const struct GraphicsBanks sGraphicsBanks = {
    .bg = GX_VRAM_BG_128_A,
    .bgextpltt = GX_VRAM_BGEXTPLTT_NONE,
    .subbg = GX_VRAM_SUB_BG_128_C,
    .subbgextpltt = GX_VRAM_SUB_BGEXTPLTT_NONE,
    .obj = GX_VRAM_OBJ_NONE,
    .objextpltt = GX_VRAM_OBJEXTPLTT_NONE,
    .subobj = GX_VRAM_SUB_OBJ_NONE,
    .subobjextpltt = GX_VRAM_SUB_OBJEXTPLTT_NONE,
    .tex = GX_VRAM_TEX_NONE,
    .texpltt = GX_VRAM_TEXPLTT_NONE,
};

// text: char 0x00000 (32x24 window = 768 tiles), screen 0xF000; art: char 0x08000, screen 0xF800;
// icons: char 0x10000, screen 0xE800
static const struct BgTemplate sTextTemplate = {
    .x = 0, .y = 0, .bufferSize = 0x800, .baseTile = 0, .size = GF_BG_SCR_SIZE_256x256,
    .colorMode = GX_BG_COLORMODE_16, .screenBase = GX_BG_SCRBASE_0xf000, .charBase = GX_BG_CHARBASE_0x00000,
    .bgExtPltt = GX_BG_EXTPLTT_01, .priority = 0, .areaOver = 0, .dummy = 0, .mosaic = FALSE,
};

static const struct BgTemplate sArtTemplate = {
    .x = 0, .y = 0, .bufferSize = 0x800, .baseTile = 0, .size = GF_BG_SCR_SIZE_256x256,
    .colorMode = GX_BG_COLORMODE_16, .screenBase = GX_BG_SCRBASE_0xf800, .charBase = GX_BG_CHARBASE_0x08000,
    .bgExtPltt = GX_BG_EXTPLTT_01, .priority = 2, .areaOver = 0, .dummy = 0, .mosaic = FALSE,
};

static const struct BgTemplate sIconTemplate = {
    .x = 0, .y = 0, .bufferSize = 0x800, .baseTile = 0, .size = GF_BG_SCR_SIZE_256x256,
    .colorMode = GX_BG_COLORMODE_16, .screenBase = GX_BG_SCRBASE_0xe800, .charBase = GX_BG_CHARBASE_0x10000,
    .bgExtPltt = GX_BG_EXTPLTT_01, .priority = 1, .areaOver = 0, .dummy = 0, .mosaic = FALSE,
};

static const struct WindowTemplate sTopWindow = {
    .bgId = LAYER_TOP_TEXT, .left = 0, .top = 0, .width = 32, .height = 24, .palette = PAL_TEXT, .baseTile = 1,
};

static const struct WindowTemplate sBottomWindow = {
    .bgId = LAYER_BOTTOM_TEXT, .left = 0, .top = 0, .width = 32, .height = 24, .palette = PAL_TEXT, .baseTile = 1,
};

// ---- text helpers ----

static void AppendAscii(struct String *dst, const char *src) {
    for (; *src != '\0'; src++) {
        char c = *src;
        u16 code;

        if (c >= 'A' && c <= 'Z') {
            code = (u16)(0x012B + (c - 'A'));
        } else if (c >= 'a' && c <= 'z') {
            code = (u16)(0x0145 + (c - 'a'));
        } else if (c >= '0' && c <= '9') {
            code = (u16)(0x0121 + (c - '0'));
        } else if (c == '.') {
            code = 0x01AE;
        } else if (c == '/') {
            code = 0x01B1;
        } else if (c == '-') {
            code = 0x01BE;
        } else if (c == '?') {
            code = 0x01AC;
        } else if (c == ':') {
            code = 0x01C4;
        } else if (c == 1) {
            code = 0x0188; // the accented e
        } else {
            code = 0x01DE;
        }

        StrAddChar(dst, code);
    }
}

static void AppendNumber(struct String *dst, int num) {
    struct String *numStr = String_New(8, HEAP_ID_DEXNAV);

    String16_FormatInteger(numStr, num, 3, PRINTING_MODE_LEFT_ALIGN, TRUE);
    StringCat(dst, numStr);
    String_Delete(numStr);
}

static void PrintString(Window *window, struct String *string, int x, int y, u32 color) {
    AddTextPrinterParameterized2(window, 0, string, (u32)x, (u32)y, TEXT_SPEED_NOTRANSFER, color, NULL);
}

static void PrintAscii(Window *window, const char *text, int x, int y, u32 color) {
    struct String *string = String_New(64, HEAP_ID_DEXNAV);

    AppendAscii(string, text);
    PrintString(window, string, x, y, color);
    String_Delete(string);
}

static void PrintChar(Window *window, u16 code, int x, int y, u32 color) {
    struct String *string = String_New(4, HEAP_ID_DEXNAV);

    StrAddChar(string, code);
    PrintString(window, string, x, y, color);
    String_Delete(string);
}

// ---- slots ----

static void SlotPosition(int slot, int *x, int *y) {
    int section = slot / DEXNAV_SECTION_SLOTS;
    int index = slot % DEXNAV_SECTION_SLOTS;

    *x = GRID_LEFT + (index % DEXNAV_GRID_COLS) * CELL_W;
    *y = (section == 0 ? LAND_TOP : WATER_TOP) + (index / DEXNAV_GRID_COLS) * CELL_H;
}

static u16 SlotSpecies(DexNavApp *app, int slot) {
    return app->args->slots[slot].species;
}

static BOOL SlotIsSeen(DexNavApp *app, int slot) {
    u16 species = SlotSpecies(app, slot);
    return species != SPECIES_NONE && Pokedex_CheckMonSeenFlag(app->pokedex, species);
}

static BOOL SlotIsCaught(DexNavApp *app, int slot) {
    u16 species = SlotSpecies(app, slot);
    return species != SPECIES_NONE && Pokedex_CheckMonCaughtFlag(app->pokedex, species);
}

// land Pokemon are searched for where walking meets them, water Pokemon while surfing; a Pokemon only fished up
// can't be searched for
static BOOL SlotHasSearch(DexNavApp *app, int slot) {
    return SlotSpecies(app, slot) != SPECIES_NONE && (slot < DEXNAV_SECTION_SLOTS || app->args->slots[slot].method == DEXNAV_METHOD_SURF);
}

// only a Pokemon the player has seen can be chosen
static BOOL SlotIsSearchable(DexNavApp *app, int slot) {
    return SlotIsSeen(app, slot) && SlotHasSearch(app, slot);
}

// ---- drawing ----

static void LoadIcons(DexNavApp *app) {
    for (int slot = 0; slot < DEXNAV_TOTAL_SLOTS; slot++) {
        u16 species = SlotSpecies(app, slot);
        u16 map[ICON_TILES];
        int x, y;
        u16 palette;

        if (species == SPECIES_NONE || species > SPECIES_ARCEUS || !SlotIsSeen(app, slot)) {
            continue;
        }

        NNSG2dCharacterData *charData;
        void *file = GfGfxLoader_GetCharData(NARC_POKETOOL_ICONGRA_POKE_ICON, ICON_FIRST_FILE + species, FALSE, &charData, HEAP_ID_DEXNAV);

        // icon tiles go one slot after another; tile 0 stays blank
        BG_LoadCharTilesData(app->bgConfig, LAYER_ICONS, charData->pRawData, ICON_BYTES, (u32)(1 + slot * ICON_TILES));
        Heap_Free(file);

        // caught: the icon's own colours; seen: a silhouette
        palette = (u16)(SlotIsCaught(app, slot) ? PAL_ICONS + UNK_020F8074[species] : PAL_SILHOUETTE);

        for (int i = 0; i < ICON_TILES; i++) {
            map[i] = (u16)((1 + slot * ICON_TILES + i) | (palette << 12));
        }

        SlotPosition(slot, &x, &y);
        LoadRectToBgTilemapRect(app->bgConfig, LAYER_ICONS, map, (u8)(x / 8), (u8)(y / 8), 4, 4);
    }

    BgCommitTilemapBufferToVram(app->bgConfig, LAYER_ICONS);
}

static void DrawGrid(DexNavApp *app) {
    Window *window = &app->bottomWindow;
    u16 selected = GunkSave_GetDexNavSpecies(app->args->saveData);
    int x, y;

    // the cursor is a palette swap on the art layer: same tiles, warmer cell colours
    BG_LoadScreenTilemapData(app->bgConfig, LAYER_BOTTOM_ART, sDexNavBottomMap, sizeof(sDexNavBottomMap));
    SlotPosition(app->cursor, &x, &y);
    BgTilemapRectChangePalette(app->bgConfig, LAYER_BOTTOM_ART, (u8)(x / 8), (u8)(y / 8), CELL_W / 8, CELL_H / 8, PAL_SELECTED);
    BgCommitTilemapBufferToVram(app->bgConfig, LAYER_BOTTOM_ART);

    FillWindowPixelBuffer(window, 0);
    PrintAscii(window, "LAND", 12, LAND_TOP - 13, TEXT_DARK);
    PrintAscii(window, "WATER", 12, WATER_TOP - 13, TEXT_DARK);
    PrintAscii(window, "A: Select   B: Close", 112, 178, TEXT_ON_BAR);

    for (int slot = 0; slot < DEXNAV_TOTAL_SLOTS; slot++) {
        u16 species = SlotSpecies(app, slot);

        if (species == SPECIES_NONE) {
            continue;
        }

        SlotPosition(slot, &x, &y);

        if (!SlotIsSeen(app, slot)) {
            PrintAscii(window, "?", x + 17, y + 8, TEXT_DARK);
        }

        if (SlotIsCaught(app, slot)) {
            PrintChar(window, CHAR_CAUGHT, x + 3, y + 1, TEXT_DARK);
        }

        if (species == selected) {
            PrintChar(window, CHAR_STAR, x + 28, y + 1, TEXT_DARK);
        }
    }

    CopyWindowToVram(window);
}

static void DrawDetails(DexNavApp *app) {
    Window *window = &app->topWindow;
    const DexNavEntry *entry = &app->args->slots[app->cursor];
    u16 selected = GunkSave_GetDexNavSpecies(app->args->saveData);
    int numCaught = 0, numSpecies = 0;
    struct String *string;

    for (int i = 0; i < DEXNAV_TOTAL_SLOTS; i++) {
        if (SlotSpecies(app, i) != SPECIES_NONE) {
            numSpecies++;
            numCaught += SlotIsCaught(app, i);
        }
    }

    FillWindowPixelBuffer(window, 0);
    PrintAscii(window, "DexNav", 12, 4, TEXT_ON_BAR);

    string = String_New(64, HEAP_ID_DEXNAV);
    AppendAscii(string, "Caught ");
    AppendNumber(string, numCaught);
    AppendAscii(string, "/");
    AppendNumber(string, numSpecies);
    PrintString(window, string, 172, 4, TEXT_ON_BAR);

    // the highlighted Pokemon's card
    if (numSpecies == 0) {
        PrintAscii(window, "No wild Pok\x01mon live here.", 20, 38, TEXT_DARK);
    } else if (entry->species == SPECIES_NONE) {
        PrintAscii(window, "-----", 20, 38, TEXT_DARK);
    } else {
        BOOL seen = SlotIsSeen(app, app->cursor);
        BOOL caught = SlotIsCaught(app, app->cursor);

        if (seen) {
            struct String *name = GetSpeciesName(entry->species, HEAP_ID_DEXNAV);
            PrintString(window, name, 20, 38, TEXT_DARK);
            String_Delete(name);
        } else {
            PrintAscii(window, "?????", 20, 38, TEXT_DARK);
        }

        PrintAscii(window, caught ? "Caught" : seen ? "Seen" : "Not seen yet", 150, 38, TEXT_DARK);
        PrintAscii(window, "Found", 20, 66, TEXT_DARK);
        PrintAscii(window, sMethodNames[entry->method], 84, 66, TEXT_DARK);

        String_SetEmpty(string);
        AppendAscii(string, "Lv. ");
        AppendNumber(string, entry->minLevel);

        if (entry->maxLevel != entry->minLevel) {
            AppendAscii(string, "-");
            AppendNumber(string, entry->maxLevel);
        }

        PrintAscii(window, "Level", 20, 88, TEXT_DARK);
        PrintString(window, string, 84, 88, TEXT_DARK);

        String_SetEmpty(string);
        AppendAscii(string, "Search Lv. ");
        AppendNumber(string, GunkSave_GetSearchLevel(app->args->saveData, entry->species));
        PrintString(window, string, 20, 108, TEXT_DARK);
    }

    // the search card: what R will look for
    PrintAscii(window, "Search", 16, 136, TEXT_DARK);

    if (selected != SPECIES_NONE) {
        BOOL onLand = FALSE, onWater = FALSE;
        struct String *name;

        for (int i = 0; i < DEXNAV_TOTAL_SLOTS; i++) {
            if (SlotSpecies(app, i) == selected && SlotHasSearch(app, i)) {
                onLand |= i < DEXNAV_SECTION_SLOTS;
                onWater |= i >= DEXNAV_SECTION_SLOTS;
            }
        }

        name = GetSpeciesName(selected, HEAP_ID_DEXNAV);
        PrintString(window, name, 20, 152, TEXT_DARK);
        String_Delete(name);

        if (onWater && onLand) {
            PrintAscii(window, "Press R on land or while surfing.", 20, 168, TEXT_DARK);
        } else if (onWater) {
            PrintAscii(window, "Press R while surfing to search.", 20, 168, TEXT_DARK);
        } else if (onLand) {
            PrintAscii(window, "Press R in grass or caves.", 20, 168, TEXT_DARK);
        } else {
            PrintAscii(window, "It doesn't live around here.", 20, 168, TEXT_DARK);
        }
    } else {
        PrintAscii(window, "Press A on a Pok\x01mon to choose it.", 20, 156, TEXT_DARK);
    }

    String_Delete(string);
    CopyWindowToVram(window);
}

static BOOL HandleInput(DexNavApp *app) {
    int section = app->cursor / DEXNAV_SECTION_SLOTS;
    int index = app->cursor % DEXNAV_SECTION_SLOTS;
    int col = index % DEXNAV_GRID_COLS;
    int row = section * DEXNAV_GRID_ROWS + index / DEXNAV_GRID_COLS;
    int oldCursor = app->cursor;

    if ((gSystem.newKeys & PAD_BUTTON_A) && SlotIsSearchable(app, app->cursor)) {
        PlaySE(SEQ_SE_DP_DECIDE);
        GunkSave_SetDexNavSpecies(app->args->saveData, SlotSpecies(app, app->cursor));
        DrawGrid(app);
        DrawDetails(app);
        return FALSE;
    }

    if (gSystem.newKeys & PAD_BUTTON_B) {
        PlaySE(SEQ_SE_DP_DECIDE);
        return TRUE;
    }

    if (gSystem.newAndRepeatedKeys & PAD_KEY_LEFT) {
        col = (col + DEXNAV_GRID_COLS - 1) % DEXNAV_GRID_COLS;
    } else if (gSystem.newAndRepeatedKeys & PAD_KEY_RIGHT) {
        col = (col + 1) % DEXNAV_GRID_COLS;
    } else if (gSystem.newAndRepeatedKeys & PAD_KEY_UP) {
        row = (row + DEXNAV_GRID_ROWS * 2 - 1) % (DEXNAV_GRID_ROWS * 2);
    } else if (gSystem.newAndRepeatedKeys & PAD_KEY_DOWN) {
        row = (row + 1) % (DEXNAV_GRID_ROWS * 2);
    }

    app->cursor = (row / DEXNAV_GRID_ROWS) * DEXNAV_SECTION_SLOTS + (row % DEXNAV_GRID_ROWS) * DEXNAV_GRID_COLS + col;

    if (app->cursor != oldCursor) {
        PlaySE(SEQ_SE_DP_SELECT);
        DrawGrid(app);
        DrawDetails(app);
    }

    return FALSE;
}

// ---- setup ----

static void VBlankCallback(void *data) {
    DexNavApp *app = data;

    DoScheduledBgGpuUpdates(app->bgConfig);
    OS_SetIrqCheckFlag(OS_IE_VBLANK);
}

static void InitBgs(DexNavApp *app) {
    static const u16 sSilhouette[16] = {
        0x0000, 0x294A, 0x294A, 0x294A, 0x294A, 0x294A, 0x294A, 0x294A,
        0x294A, 0x294A, 0x294A, 0x294A, 0x294A, 0x294A, 0x294A, 0x294A,
    };

    GfGfx_SetBanks(&sGraphicsBanks);
    app->bgConfig = BgConfig_Alloc(HEAP_ID_DEXNAV);
    SetBothScreensModesAndDisable(&sGraphicsModes);

    InitBgFromTemplate(app->bgConfig, LAYER_TOP_ART, &sArtTemplate, GF_BG_TYPE_TEXT);
    InitBgFromTemplate(app->bgConfig, LAYER_BOTTOM_ART, &sArtTemplate, GF_BG_TYPE_TEXT);
    BG_LoadCharTilesData(app->bgConfig, LAYER_TOP_ART, sDexNavTiles, sizeof(sDexNavTiles), 0);
    BG_LoadCharTilesData(app->bgConfig, LAYER_BOTTOM_ART, sDexNavTiles, sizeof(sDexNavTiles), 0);
    BG_LoadScreenTilemapData(app->bgConfig, LAYER_TOP_ART, sDexNavTopMap, sizeof(sDexNavTopMap));
    BgCommitTilemapBufferToVram(app->bgConfig, LAYER_TOP_ART);

    InitBgFromTemplate(app->bgConfig, LAYER_TOP_TEXT, &sTextTemplate, GF_BG_TYPE_TEXT);
    BgClearTilemapBufferAndCommit(app->bgConfig, LAYER_TOP_TEXT);
    InitBgFromTemplate(app->bgConfig, LAYER_BOTTOM_TEXT, &sTextTemplate, GF_BG_TYPE_TEXT);
    BgClearTilemapBufferAndCommit(app->bgConfig, LAYER_BOTTOM_TEXT);
    BG_ClearCharDataRange(LAYER_TOP_TEXT, 0x20, 0, HEAP_ID_DEXNAV);
    BG_ClearCharDataRange(LAYER_BOTTOM_TEXT, 0x20, 0, HEAP_ID_DEXNAV);

    // icons sit 4 px right of the 8 px tile grid, centred in their 40 px cells
    InitBgFromTemplate(app->bgConfig, LAYER_ICONS, &sIconTemplate, GF_BG_TYPE_TEXT);
    BgClearTilemapBufferAndCommit(app->bgConfig, LAYER_ICONS);
    BG_ClearCharDataRange(LAYER_ICONS, 0x20, 0, HEAP_ID_DEXNAV);
    BgSetPosTextAndCommit(app->bgConfig, LAYER_ICONS, BG_POS_OP_SET_X, -4);

    BG_LoadPlttData(GF_PAL_LOCATION_MAIN_BG, (void *)sDexNavPalBase, sizeof(sDexNavPalBase), GF_PAL_SLOT_0_OFFSET);
    BG_LoadPlttData(GF_PAL_LOCATION_MAIN_BG, (void *)sDexNavPalSelected, sizeof(sDexNavPalSelected), GF_PAL_SLOT_1_OFFSET);
    BG_LoadPlttData(GF_PAL_LOCATION_MAIN_BG, (void *)sDexNavPalText, sizeof(sDexNavPalText), GF_PAL_SLOT_2_OFFSET);
    BG_LoadPlttData(GF_PAL_LOCATION_SUB_BG, (void *)sDexNavPalBase, sizeof(sDexNavPalBase), GF_PAL_SLOT_0_OFFSET);
    BG_LoadPlttData(GF_PAL_LOCATION_SUB_BG, (void *)sDexNavPalSelected, sizeof(sDexNavPalSelected), GF_PAL_SLOT_1_OFFSET);
    BG_LoadPlttData(GF_PAL_LOCATION_SUB_BG, (void *)sDexNavPalText, sizeof(sDexNavPalText), GF_PAL_SLOT_2_OFFSET);
    BG_LoadPlttData(GF_PAL_LOCATION_SUB_BG, (void *)sSilhouette, sizeof(sSilhouette), GF_PAL_SLOT_6_OFFSET);
    GfGfxLoader_GXLoadPal(NARC_POKETOOL_ICONGRA_POKE_ICON, 0, GF_PAL_LOCATION_SUB_BG, GF_PAL_SLOT_3_OFFSET, 0x60, HEAP_ID_DEXNAV);

    ResetAllTextPrinters();
    AddWindow(app->bgConfig, &app->topWindow, &sTopWindow);
    AddWindow(app->bgConfig, &app->bottomWindow, &sBottomWindow);
}

static void FreeBgs(DexNavApp *app) {
    RemoveWindow(&app->topWindow);
    RemoveWindow(&app->bottomWindow);
    ToggleBgLayer(LAYER_TOP_TEXT, GX_PLANE_TOGGLE_OFF);
    ToggleBgLayer(LAYER_TOP_ART, GX_PLANE_TOGGLE_OFF);
    ToggleBgLayer(LAYER_BOTTOM_TEXT, GX_PLANE_TOGGLE_OFF);
    ToggleBgLayer(LAYER_BOTTOM_ART, GX_PLANE_TOGGLE_OFF);
    ToggleBgLayer(LAYER_ICONS, GX_PLANE_TOGGLE_OFF);
    FreeBgTilemapBuffer(app->bgConfig, LAYER_TOP_TEXT);
    FreeBgTilemapBuffer(app->bgConfig, LAYER_TOP_ART);
    FreeBgTilemapBuffer(app->bgConfig, LAYER_BOTTOM_TEXT);
    FreeBgTilemapBuffer(app->bgConfig, LAYER_BOTTOM_ART);
    FreeBgTilemapBuffer(app->bgConfig, LAYER_ICONS);
    Heap_Free(app->bgConfig);
}

BOOL GunkDexNavApp_Init(struct OverlayManager *manager, u32 *state) {
#pragma unused(state)
    DexNavApp *app;

    // the icons take 24 x 512 bytes of file buffers at most at once; 64 KB leaves a wide margin
    Heap_Create(HEAP_ID_MAIN, HEAP_ID_DEXNAV, 0x10000);
    app = OverlayManager_CreateAndGetData(manager, sizeof(DexNavApp), HEAP_ID_DEXNAV);
    MI_CpuClear8(app, sizeof(DexNavApp));
    app->args = (DexNavData *)OverlayManager_GetField18(manager);
    app->pokedex = Save_Pokedex_Get(app->args->saveData);

    // start the cursor on the first occupied slot
    for (int i = 0; i < DEXNAV_TOTAL_SLOTS; i++) {
        if (SlotSpecies(app, i) != SPECIES_NONE) {
            app->cursor = i;
            break;
        }
    }

    Main_SetVBlankIntrCB(NULL, NULL);
    Main_SetHBlankIntrCB(NULL, NULL);
    GfGfx_DisableEngineAPlanes();
    GfGfx_DisableEngineBPlanes();
    GX_SetVisiblePlane(0);
    GXS_SetVisiblePlane(0);
    SetKeyRepeatTimers(4, 8);
    gSystem.screensFlipped = FALSE;
    GfGfx_SwapDisplay();

    InitBgs(app);
    LoadIcons(app);

    Main_SetVBlankIntrCB(VBlankCallback, app);
    GfGfx_BothDispOn();

    return TRUE;
}

BOOL GunkDexNavApp_Main(struct OverlayManager *manager, u32 *state) {
    DexNavApp *app = OverlayManager_GetData(manager);

    switch (*state) {
    case 0:
        DrawGrid(app);
        DrawDetails(app);
        ToggleBgLayer(LAYER_TOP_TEXT, GX_PLANE_TOGGLE_ON);
        ToggleBgLayer(LAYER_TOP_ART, GX_PLANE_TOGGLE_ON);
        ToggleBgLayer(LAYER_BOTTOM_TEXT, GX_PLANE_TOGGLE_ON);
        ToggleBgLayer(LAYER_BOTTOM_ART, GX_PLANE_TOGGLE_ON);
        ToggleBgLayer(LAYER_ICONS, GX_PLANE_TOGGLE_ON);
        BeginNormalPaletteFade(0, 1, 1, RGB_BLACK, 6, 1, HEAP_ID_DEXNAV);
        *state = 1;
        break;
    case 1:
        if (IsPaletteFadeFinished()) {
            *state = 2;
        }
        break;
    case 2:
        if (HandleInput(app)) {
            BeginNormalPaletteFade(0, 0, 0, RGB_BLACK, 6, 1, HEAP_ID_DEXNAV);
            *state = 3;
        }
        break;
    case 3:
        if (IsPaletteFadeFinished()) {
            return TRUE;
        }
        break;
    }

    return FALSE;
}

BOOL GunkDexNavApp_Exit(struct OverlayManager *manager, u32 *state) {
#pragma unused(state)
    DexNavApp *app = OverlayManager_GetData(manager);

    Main_SetVBlankIntrCB(NULL, NULL);
    FreeBgs(app);
    OverlayManager_FreeData(manager);
    Heap_Destroy(HEAP_ID_DEXNAV);

    return TRUE;
}
