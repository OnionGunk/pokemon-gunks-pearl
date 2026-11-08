#include "global.h"

#include "gunk/gunk_dexnav.h"
#include "gunk/gunk_dexnav_app.h"
#include "gunk/gunk_save.h"
#include "gunk/gunk_test.h"

#include "constants/battle.h"
#include "constants/pokemon.h"
#include "constants/sndseq.h"
#include "constants/species.h"

#include "battle_setup.h"
#include "bg_window.h"
#include "encounter.h"
#include "field_system.h"
#include "filesystem.h"
#include "font.h"
#include "heap.h"
#include "main.h"
#include "map_matrix.h"
#include "math_util.h"
#include "msgdata.h"
#include "options.h"
#include "party.h"
#include "player_avatar.h"
#include "player_data.h"
#include "pokemon.h"
#include "render_window.h"
#include "save_vars_flags.h"
#include "string16.h"
#include "task.h"
#include "text.h"
#include "text_02054590.h"
#include "unk_020051F4.h"
#include "unk_0205EC84.h"

#include "OS_tick.h"
#include "unk_0204AF24.h"
#include "overlay_manager.h"
#include "poke_overlay.h"
#include "PAD_pad.h"
#include "string_util.h"
#include "map_object.h"

// ---- game functions that are still assembly in pokediamond ----
extern u8 GetMetatileBehavior(FieldSystem *fieldSystem, int x, int z);
extern BOOL sub_02054AFC(u8 behavior);                // has walking encounters
extern BOOL sub_0205481C(u8 behavior);                // surfable water
extern fx32 sub_0204A708(FieldSystem *fieldSystem, int unused, fx32 x, fx32 z, u8 *heightSource);
extern void *ov06_0224B818(FieldSystem *fieldSystem, int x, int z, int shakeType); // the Poke Radar's shaking patch
extern void sub_02064520(void *anim);                 // ends an overworld animation
extern void ov06_0223D2DC(FieldSystem *fieldSystem, struct Pokemon *lead, int unused, void *params);
extern BOOL ov06_0223D1BC(int battler, void *params, struct Pokemon *mon, BattleSetup *setup);
extern BattleSetup *BattleSetup_New(enum HeapID heapID, u32 flags);
extern void BattleSetup_InitFromFieldSystem(BattleSetup *setup, FieldSystem *fieldSystem);
extern void sub_020469B8(FieldSystem *fieldSystem, BattleSetup *setup);
extern void ClearFrameAndWindow2(struct Window *window, BOOL param1);
extern BOOL ov05_021D8320_Vanilla(void *input, FieldSystem *fieldSystem);
extern void ov05_021D8164_Vanilla(void *input, FieldSystem *fieldSystem, u16 pressedKeys, u16 heldKeys);
extern BOOL ov05_021D8954(FieldSystem *fieldSystem);
extern BOOL ov05_021D76C0_Vanilla(void *overlayManager, u32 *state);

#define PLAYER_STATE_SURFING_ 2

#define SEARCH_MIN_DISTANCE   2
#define SEARCH_MAX_DISTANCE   4
#define SEARCH_LOST_DISTANCE  8
#define SEARCH_PLACE_ATTEMPTS 40
// a search lasts 10 seconds of free field time (not menus, scripts or battles); the patch shakes twice a second
#define SEARCH_MS             10000
#define SHAKE_INTERVAL_MS     500
#define MAX_FRAME_MS          100 // a longer gap between two field frames means the field was stopped
#define PAUSED_FRAME_MS       33
#define PATCH_SHAKE_SOFT      0
#define PATCH_SHAKE_SHINY     2

#define NUM_STATS             6
#define MAX_PERFECT_IVS       4
#define DEXNAV_SHINY_ODDS     4096
#define MAX_CANDIDATES        512
#define MAX_SHINY_SCAN_CALLS  0x400000
#define SHINY_SCAN_HISTORY    64
#define LCRNG_MULTIPLIER      0x41C64E6D
#define LCRNG_INCREMENT       0x6073
#define LCRNG_MULTIPLIER_INV  0xEEB9EB65
#define LCRNG_INCREMENT_INV   0x0A3561A1
#define SLOT_ROLL_DIVISOR     656  // rand / (0xFFFF / 100 + 1)
#define NATURE_ROLL_DIVISOR   2622 // rand / (0xFFFF / 25 + 1)
#define NUM_GRASS_SLOTS       12
#define NUM_WATER_SLOTS       5

#define MSG_NARC_NATURES      190
#define MSG_NARC_ABILITIES    552
#define ABILITY_COMPOUND_EYES 14

// the field's message box: the bar borrows its window, tiles and palettes (sub_020545B8 / sub_02054608)
#define FIELD_FRAME_TILE      994
#define FIELD_FRAME_PALETTE   10

// ---- the map's wild encounter table (same layout as Platinum's WildEncounters) ----
typedef struct GrassSlot {
    s8 level;
    u8 padding[3];
    int species;
} GrassSlot;

typedef struct WaterSlot {
    s8 maxLevel;
    s8 minLevel;
    u8 padding[2];
    int species;
} WaterSlot;

typedef struct WildEncounterData {
    int grassRate;
    GrassSlot grass[NUM_GRASS_SLOTS];
    int swarm[2];
    int day[2];
    int night[2];
    int radar[4];
    int forms[5];
    int unownTable;
    int dualSlot[5][2];
    int surfRate;
    WaterSlot surf[NUM_WATER_SLOTS];
    int unusedRate;
    WaterSlot unused[NUM_WATER_SLOTS];
    int oldRodRate;
    WaterSlot oldRod[NUM_WATER_SLOTS];
    int goodRodRate;
    WaterSlot goodRod[NUM_WATER_SLOTS];
    int superRodRate;
    WaterSlot superRod[NUM_WATER_SLOTS];
} WildEncounterData;

extern WildEncounterData *sub_02034CE0(FieldSystem *fieldSystem);

typedef struct DexNavSearch {
    u16 species;
    u8 level;
    BOOL active;
    BOOL water;
    BOOL battling;
    BOOL restore; // the field went away with the search running: put the patch and the bar back when it returns
    int x;
    int z;
    u32 mapId;
    void *anim;
    u32 personality;
    u8 ivs[NUM_STATS];
    u8 perfectIVs;
    BOOL shiny;
    u8 searchLevel;
    u16 chainSpecies;
    u32 chainMapId;
    u16 chain;
    u16 elapsedMs;
    OSTick lastTick;
    BOOL rPressed;
    int playerX; // where the player stood on the last free field frame (for the emulator tests)
    int playerZ;
} DexNavSearch;

typedef struct EncounterFrame {
    u32 personality;
    u8 ivs[NUM_STATS];
    u8 slot;
    u8 level;
} EncounterFrame;

typedef struct SearchTable {
    BOOL water;
    u16 slots; // bit n set: slot n can hold the species
    u8 minLevels[NUM_GRASS_SLOTS];
    u8 maxLevels[NUM_GRASS_SLOTS];
} SearchTable;

// Lives in the main binary so it survives the field unloading for menus and battles.
static DexNavSearch sSearch;
static Window sHud;
static BOOL sHudShown;

static int Abs(int value) {
    return value < 0 ? -value : value;
}

static u32 Random32(void) {
    return ((u32)LCRandom() << 16) | LCRandom();
}

static int RandMod(int n) {
    return LCRandom() % n;
}

static u32 ElapsedMs(OSTick ticks) {
    // the tick timer runs at 33.513982 MHz / 64
    return (u32)((ticks * 64) / 33514);
}

// ---- tiles ----

static u32 PlayerMapId(FieldSystem *fieldSystem) {
    return fieldSystem->location->mapId;
}

static struct MapMatrix *GetMapMatrix(FieldSystem *fieldSystem) {
    return *(struct MapMatrix **)((u8 *)fieldSystem + 0x28);
}

// a Poke Radar patch keeps its own tile, so stepping there always starts the battle it shows
static BOOL IsRadarPatchAt(FieldSystem *fieldSystem, int x, int z) {
    u8 *radar = *(u8 **)((u8 *)fieldSystem + 0x90);

    if (radar == NULL) {
        return FALSE;
    }

    for (int i = 0; i < 4; i++) {
        u8 *patch = radar + i * 0x28;

        if (*(u32 *)(patch + 0x28) != 0 && *(int *)(patch + 0x1C) == x && *(int *)(patch + 0x20) == z) {
            return TRUE;
        }
    }

    return FALSE;
}

// Land: exactly the tiles a walking encounter can happen on (tall grass, very tall grass, cave floors, ...).
// Water: surfable tiles.
static BOOL IsSearchableTile(FieldSystem *fieldSystem, int x, int z, fx32 playerHeight) {
    u8 behavior = GetMetatileBehavior(fieldSystem, x, z);
    u8 heightSource;

    if (sSearch.water ? !sub_0205481C(behavior) : (!sub_02054AFC(behavior) || sub_0205481C(behavior))) {
        return FALSE;
    }

    if (IsRadarPatchAt(fieldSystem, x, z)) {
        return FALSE;
    }

    if (sub_0204A708(fieldSystem, 0, FX32_ONE * 16 * x, FX32_ONE * 16 * z, &heightSource) != playerHeight) {
        return FALSE;
    }

    return MapMatrix_GetMapHeader(GetMapMatrix(fieldSystem), x / 32, z / 32) == PlayerMapId(fieldSystem);
}

// ---- legal encounters (Method J, as Diamond rolls a grass or surf encounter) ----

static u16 FrameRand(u32 *state) {
    *state = *state * LCRNG_MULTIPLIER + LCRNG_INCREMENT;
    return (u16)(*state >> 16);
}

static u16 FrameRandMod(u32 *state, u16 n) {
    return FrameRand(state) / ((0xFFFF / n) + 1);
}

static u8 SlotFromRoll(BOOL water, u8 roll) {
    static const u8 sGrassSlotEnds[NUM_GRASS_SLOTS] = { 20, 40, 50, 60, 70, 80, 85, 90, 94, 98, 99, 100 };
    static const u8 sWaterSlotEnds[NUM_WATER_SLOTS] = { 60, 90, 95, 99, 100 };
    const u8 *slotEnds = water ? sWaterSlotEnds : sGrassSlotEnds;
    u8 slot = 0;

    while (roll >= slotEnds[slot]) {
        slot++;
    }

    return slot;
}

// A grass slot can hold its own species, or the morning/day/night (2 and 3) and swarm (0 and 1) replacements,
// which keep the replaced slot's level. A surf slot holds its own species; its level is rolled.
static void GetSearchTable(FieldSystem *fieldSystem, u16 species, BOOL water, SearchTable *table) {
    const WildEncounterData *data = sub_02034CE0(fieldSystem);

    MI_CpuClear8(table, sizeof(SearchTable));
    table->water = water;

    if (water) {
        for (int i = 0; i < NUM_WATER_SLOTS; i++) {
            u8 a = (u8)data->surf[i].minLevel, b = (u8)data->surf[i].maxLevel;

            table->minLevels[i] = a < b ? a : b;
            table->maxLevels[i] = a < b ? b : a;

            if (data->surf[i].species == species) {
                table->slots |= 1 << i;
            }
        }

        return;
    }

    for (int i = 0; i < NUM_GRASS_SLOTS; i++) {
        BOOL match = data->grass[i].species == species;

        if (i == 2 || i == 3) {
            match |= data->day[i - 2] == species || data->night[i - 2] == species;
        } else if (i == 0 || i == 1) {
            match |= data->swarm[i] == species;
        }

        table->minLevels[i] = (u8)data->grass[i].level;
        table->maxLevels[i] = (u8)data->grass[i].level;

        if (match) {
            table->slots |= 1 << i;
        }
    }
}

static BOOL GenerateEncounterFrame(u32 seed, const SearchTable *table, EncounterFrame *frame) {
    u32 state = seed;
    u8 slot = SlotFromRoll(table->water, (u8)FrameRandMod(&state, 100));
    u8 level, nature;
    u32 personality;
    u16 ivs1, ivs2;

    if ((table->slots & (1 << slot)) == 0) {
        return FALSE;
    }

    level = table->minLevels[slot];

    if (table->water) {
        level += FrameRand(&state) % (u8)(table->maxLevels[slot] - table->minLevels[slot] + 1);
    }

    nature = (u8)FrameRandMod(&state, 25);

    do {
        personality = FrameRand(&state);
        personality |= (u32)FrameRand(&state) << 16;
    } while (personality % 25 != nature);

    ivs1 = FrameRand(&state);
    ivs2 = FrameRand(&state);

    // HP, Attack, Defense, then Speed, Sp. Atk, Sp. Def, as the box data unpacks them
    frame->ivs[0] = ivs1 & 0x1F;
    frame->ivs[1] = (ivs1 >> 5) & 0x1F;
    frame->ivs[2] = (ivs1 >> 10) & 0x1F;
    frame->ivs[3] = ivs2 & 0x1F;
    frame->ivs[4] = (ivs2 >> 5) & 0x1F;
    frame->ivs[5] = (ivs2 >> 10) & 0x1F;
    frame->personality = personality;
    frame->slot = slot;
    frame->level = level;

    return TRUE;
}

static int CountPerfectIVs(const EncounterFrame *frame) {
    int count = 0;

    for (int stat = 0; stat < NUM_STATS; stat++) {
        count += frame->ivs[stat] == 31;
    }

    return count;
}

static int SumIVs(const EncounterFrame *frame) {
    int sum = 0;

    for (int stat = 0; stat < NUM_STATS; stat++) {
        sum += frame->ivs[stat];
    }

    return sum;
}

static BOOL IsBetterFrame(const EncounterFrame *frame, const EncounterFrame *best) {
    int perfect = CountPerfectIVs(frame);
    int bestPerfect = CountPerfectIVs(best);

    return perfect > bestPerfect || (perfect == bestPerfect && SumIVs(frame) > SumIVs(best));
}

static u32 RollWidth(int roll) {
    return roll < 99 ? SLOT_ROLL_DIVISOR : 0x10000 - 99 * SLOT_ROLL_DIVISOR;
}

static u32 SlotRollValues(const SearchTable *table) {
    u32 count = 0;

    for (int roll = 0; roll < 100; roll++) {
        if (table->slots & (1 << SlotFromRoll(table->water, (u8)roll))) {
            count += RollWidth(roll);
        }
    }

    return count;
}

// A seed whose slot roll lands on one of the table's slots, uniformly among all such seeds: the slot roll's
// 16 bits are drawn from the allowed values and the RNG is stepped back over that call.
static u32 SeedInSlots(const SearchTable *table, u32 slotRollValues) {
    u32 pick = Random32() % slotRollValues;
    u32 rand = 0;

    for (int roll = 0; roll < 100; roll++) {
        u32 width = RollWidth(roll);

        if ((table->slots & (1 << SlotFromRoll(table->water, (u8)roll))) == 0) {
            continue;
        }

        if (pick < width) {
            rand = roll * SLOT_ROLL_DIVISOR + pick;
            break;
        }

        pick -= width;
    }

    return (((rand << 16) | LCRandom()) * LCRNG_MULTIPLIER_INV) + LCRNG_INCREMENT_INV;
}

static BOOL IsShinyPersonality(u32 otId, u32 personality) {
    return (((otId >> 16) ^ (otId & 0xFFFF) ^ (personality >> 16) ^ (personality & 0xFFFF)) < 8);
}

// A shiny encounter, found by walking the RNG until two outputs make a shiny personality, then stepping back
// over the personality rerolls to the nature roll and the slot roll (and a surf level roll) before it.
static BOOL FindShinyFrame(u32 otId, const SearchTable *table, EncounterFrame *frame) {
    u32 seedBack = table->water ? 3 : 2;
    u32 states[SHINY_SCAN_HISTORY];
    u32 state = Random32();
    u16 trainerShiny = (u16)((otId & 0xFFFF) ^ (otId >> 16));

    for (u32 call = 0; call < MAX_SHINY_SCAN_CALLS; call++) {
        u16 high, low;
        u32 personality;
        u8 nature;

        state = state * LCRNG_MULTIPLIER + LCRNG_INCREMENT;
        states[call % SHINY_SCAN_HISTORY] = state;

        high = (u16)(state >> 16);
        low = (u16)(states[(call - 1) % SHINY_SCAN_HISTORY] >> 16);

        if (call < 4 || (u16)(trainerShiny ^ low ^ high) >= 8) {
            continue;
        }

        personality = low | ((u32)high << 16);
        nature = (u8)(personality % 25);

        for (u32 back = 2; back + seedBack <= call && back + seedBack < SHINY_SCAN_HISTORY; back += 2) {
            u32 natureCall = call - back;

            if (back > 2) {
                u32 rejected = (states[(natureCall + 1) % SHINY_SCAN_HISTORY] >> 16) | (states[(natureCall + 2) % SHINY_SCAN_HISTORY] & 0xFFFF0000);

                if (rejected % 25 == nature) {
                    break;
                }
            }

            if ((states[natureCall % SHINY_SCAN_HISTORY] >> 16) / NATURE_ROLL_DIVISOR != nature) {
                continue;
            }

            if (GenerateEncounterFrame(states[(natureCall - seedBack) % SHINY_SCAN_HISTORY], table, frame) && frame->personality == personality) {
                return TRUE;
            }
        }
    }

    return FALSE;
}

static u32 PlayerOtId(FieldSystem *fieldSystem) {
    return PlayerProfile_GetTrainerID(Save_PlayerData_GetProfile(fieldSystem->saveData));
}

// Higher search levels and longer chains roll more encounters and keep the one with the best IVs, and give
// more shiny rolls. A shiny is kept as found: a better-IV encounter from the extra rolls would not be shiny.
static BOOL RollHiddenMon(FieldSystem *fieldSystem) {
    u32 otId = PlayerOtId(fieldSystem);
    int shinyRolls = 1 + sSearch.searchLevel / 10 + sSearch.chain / 5;
    int candidates = 1 + sSearch.searchLevel / 2 + sSearch.chain * 2;
    SearchTable table;
    EncounterFrame frame, best;
    int found = 0;
    u32 slotRollValues;

    GetSearchTable(fieldSystem, sSearch.species, sSearch.water, &table);

    if (table.slots == 0) {
        return FALSE;
    }

    if (candidates > MAX_CANDIDATES) {
        candidates = MAX_CANDIDATES;
    }

    sSearch.shiny = FALSE;

    for (int i = 0; i < shinyRolls; i++) {
        if (RandMod(DEXNAV_SHINY_ODDS) == 0 || Gunk_TestForceShiny()) {
            sSearch.shiny = TRUE;
            break;
        }
    }

    if (sSearch.shiny) {
        sSearch.shiny = FindShinyFrame(otId, &table, &best) && IsShinyPersonality(otId, best.personality);
        found = sSearch.shiny;
    }

    slotRollValues = SlotRollValues(&table);

    for (; !sSearch.shiny && found < candidates; found++) {
        GenerateEncounterFrame(SeedInSlots(&table, slotRollValues), &table, &frame);

        if (found == 0 || IsBetterFrame(&frame, &best)) {
            best = frame;
        }
    }

    if (found == 0) {
        return FALSE;
    }

    sSearch.personality = best.personality;
    sSearch.level = best.level;
    sSearch.perfectIVs = (u8)CountPerfectIVs(&best);

    for (int stat = 0; stat < NUM_STATS; stat++) {
        sSearch.ivs[stat] = best.ivs[stat];
    }

    return TRUE;
}

static u16 HiddenMonAbility(void) {
    u16 ability1 = (u16)GetMonBaseStat(sSearch.species, BASE_ABILITY_1);
    u16 ability2 = (u16)GetMonBaseStat(sSearch.species, BASE_ABILITY_2);

    return (ability2 != 0 && (sSearch.personality & 1)) ? ability2 : ability1;
}

// ---- text ----

#define CHAR_SPACE_ 0x01DE
#define CHAR_STAR_  0x01CA
#define CHAR_DASH_  0x01BE

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
        } else if (c == ',') {
            code = 0x01AD;
        } else if (c == '!') {
            code = 0x01AB;
        } else if (c == '?') {
            code = 0x01AC;
        } else if (c == '/') {
            code = 0x01B1;
        } else if (c == '-') {
            code = CHAR_DASH_;
        } else if (c == '\'') {
            code = 0x01B3;
        } else if (c == ':') {
            code = 0x01C4;
        } else if (c == 1) { //  marks the accented e of Pokemon
            code = 0x0188;
        } else if (c == '\n') {
            code = 0xE000;
        } else {
            code = CHAR_SPACE_;
        }

        StrAddChar(dst, code);
    }
}

static void AppendNumber(struct String *dst, int num, enum HeapID heapID) {
    struct String *numStr = String_New(8, heapID);

    String16_FormatInteger(numStr, num, 3, PRINTING_MODE_LEFT_ALIGN, TRUE);
    StringCat(dst, numStr);
    String_Delete(numStr);
}

static void AppendFromBank(struct String *dst, u32 bank, u32 entry, enum HeapID heapID) {
    struct MsgData *msgData = NewMsgDataFromNarc(MSGDATA_LOAD_LAZY, NARC_MSGDATA_MSG, (s32)bank, heapID);
    struct String *str = NewString_ReadMsgData(msgData, entry);

    StringCat(dst, str);
    String_Delete(str);
    DestroyMsgData(msgData);
}

// ---- the search bar: the field message box's window, frame and palettes ----

static void PrintRight(struct String *string, int y) {
    int width = (int)sub_02002E14(0, string, 0);

    AddTextPrinterParameterized(&sHud, 0, string, (u32)(27 * 8 - width - 2), (u32)y, TEXT_SPEED_NOTRANSFER, NULL);
}

static void DrawHud(FieldSystem *fieldSystem) {
    enum HeapID heapID = HEAP_ID_FIELD;
    struct String *line, *name;

    if (!sHudShown) {
        sub_020545B8(fieldSystem->bgConfig, &sHud, 3);
        sub_02054608(&sHud, Save_PlayerData_GetOptionsAddr(fieldSystem->saveData));
        sHudShown = TRUE;
    }

    FillWindowPixelBuffer(&sHud, 15);

    // line 1: species, level and a shiny star; the search level on the right
    line = String_New(96, heapID);
    name = GetSpeciesName(sSearch.species, heapID);
    StringCat(line, name);
    String_Delete(name);
    AppendAscii(line, "  Lv. ");
    AppendNumber(line, sSearch.level, heapID);

    if (sSearch.shiny) {
        StrAddChar(line, CHAR_SPACE_);
        StrAddChar(line, CHAR_STAR_);
    }

    AddTextPrinterParameterized(&sHud, 0, line, 0, 0, TEXT_SPEED_NOTRANSFER, NULL);

    String_SetEmpty(line);
    AppendAscii(line, "Search Lv. ");
    AppendNumber(line, sSearch.searchLevel, heapID);
    PrintRight(line, 0);

    // line 2: nature and ability; the chain and one star per perfect IV on the right
    String_SetEmpty(line);
    AppendFromBank(line, MSG_NARC_NATURES, sSearch.personality % 25, heapID);
    AppendAscii(line, "  ");
    AppendFromBank(line, MSG_NARC_ABILITIES, HiddenMonAbility(), heapID);
    AddTextPrinterParameterized(&sHud, 0, line, 0, 16, TEXT_SPEED_NOTRANSFER, NULL);

    String_SetEmpty(line);
    AppendAscii(line, "Chain ");
    AppendNumber(line, sSearch.chain, heapID);
    StrAddChar(line, CHAR_SPACE_);
    StrAddChar(line, CHAR_SPACE_);

    for (int i = 0; i < MAX_PERFECT_IVS; i++) {
        StrAddChar(line, i < sSearch.perfectIVs ? CHAR_STAR_ : CHAR_DASH_);
    }

    PrintRight(line, 16);
    String_Delete(line);

    DrawFrameAndWindow2(&sHud, FALSE, FIELD_FRAME_TILE, FIELD_FRAME_PALETTE);
    ScheduleWindowCopyToVram(&sHud);
}

static void RemoveHud(void) {
    if (sHudShown) {
        ClearFrameAndWindow2(&sHud, FALSE);
        ClearWindowTilemapAndCopyToVram(&sHud);
        RemoveWindow(&sHud);
        sHudShown = FALSE;
    }
}

static void StartPatchAnim(FieldSystem *fieldSystem) {
    if (sSearch.anim != NULL) {
        sub_02064520(sSearch.anim);
    }

    sSearch.anim = ov06_0224B818(fieldSystem, sSearch.x, sSearch.z, sSearch.shiny ? PATCH_SHAKE_SHINY : PATCH_SHAKE_SOFT);
}

static void EndSearch(BOOL removeFieldObjects) {
    if (removeFieldObjects) {
        if (sSearch.anim != NULL) {
            sub_02064520(sSearch.anim);
        }

        RemoveHud();
    }

    sSearch.anim = NULL;
    sSearch.active = FALSE;
    sSearch.restore = FALSE;
    sHudShown = FALSE;
}

BOOL GunkDexNav_IsActive(void) {
    return sSearch.active;
}

// ---- a message box in the field (refusals, "got away") ----

typedef struct DexNavMessage {
    const char *text;
    struct String *string;
    u8 printer;
} DexNavMessage;

static BOOL Task_DexNavMessage(TaskManager *taskManager) {
    FieldSystem *fieldSystem = TaskManager_GetFieldSystem(taskManager);
    DexNavMessage *message = TaskManager_GetEnvironment(taskManager);
    u32 *state = TaskManager_GetStatePtr(taskManager);

    switch (*state) {
    case 0:
        MapObjectManager_PauseAllMovement(fieldSystem->mapObjectManager);
        RemoveHud();
        sub_020545B8(fieldSystem->bgConfig, &sHud, 3);
        sub_02054608(&sHud, Save_PlayerData_GetOptionsAddr(fieldSystem->saveData));
        message->string = String_New(256, HEAP_ID_FIELD);
        AppendAscii(message->string, message->text);
        message->printer = (u8)sub_02054658(&sHud, message->string, Save_PlayerData_GetOptionsAddr(fieldSystem->saveData), 1);
        (*state)++;
        break;
    case 1:
        if (sub_020546C8(message->printer)) {
            (*state)++;
        }
        break;
    case 2:
        if (gSystem.newKeys & (PAD_BUTTON_A | PAD_BUTTON_B)) {
            ClearFrameAndWindow2(&sHud, FALSE);
            RemoveWindow(&sHud);
            String_Delete(message->string);
            Heap_Free(message);
            MapObjectManager_UnpauseAllMovement(fieldSystem->mapObjectManager);
            if (sSearch.active) {
                DrawHud(fieldSystem);
            }
            return TRUE;
        }
        break;
    }

    return FALSE;
}

static void ShowMessage(FieldSystem *fieldSystem, const char *text) {
    DexNavMessage *message = Heap_Alloc(HEAP_ID_FIELD, sizeof(DexNavMessage));

    message->text = text;
    message->string = NULL;
    message->printer = 0;
    FieldSystem_CreateTask(fieldSystem, Task_DexNavMessage, message);
}

// ---- starting a search ----

typedef enum SearchRefusal {
    SEARCH_STARTED,
    SEARCH_NONE_CHOSEN,
    SEARCH_NOT_HERE,
    SEARCH_NO_GRASS,
    SEARCH_NO_WATER,
    SEARCH_ON_WATER_ONLY,
    SEARCH_ON_LAND_ONLY,
    SEARCH_SAFARI,
} SearchRefusal;

static const char *const sRefusalTexts[] = {
    "",
    "Choose a Pok\x01mon on the DexNav\nscreen first.",
    "That Pok\x01mon can't be found\naround here.",
    "There's no grass close enough\nto search.",
    "There's no water close enough\nto search.",
    "That Pok\x01mon lives in the water.\nSurf to search for it.",
    "That Pok\x01mon lives on land.\nSearch for it in the grass.",
    "The DexNav can't search in the\nGreat Marsh.",
};

static SearchRefusal TryStartSearch(FieldSystem *fieldSystem) {
    u16 species = GunkSave_GetDexNavSpecies(fieldSystem->saveData);
    SearchTable land, water;
    int playerX, playerZ, span;
    fx32 playerHeight;

    if (species == SPECIES_NONE) {
        return SEARCH_NONE_CHOSEN;
    }

    if (Save_VarsFlags_CheckSafariSysFlag(Save_VarsFlags_Get(fieldSystem->saveData))) {
        return SEARCH_SAFARI;
    }

    sSearch.water = PlayerAvatar_GetState(fieldSystem->playerAvatar) == PLAYER_STATE_SURFING_;
    GetSearchTable(fieldSystem, species, FALSE, &land);
    GetSearchTable(fieldSystem, species, TRUE, &water);

    if (land.slots == 0 && water.slots == 0) {
        return SEARCH_NOT_HERE;
    }

    if (sSearch.water && water.slots == 0) {
        return SEARCH_ON_LAND_ONLY;
    }

    if (!sSearch.water && land.slots == 0) {
        return SEARCH_ON_WATER_ONLY;
    }

    // a running search for something else is dropped for the new one
    if (sSearch.active) {
        EndSearch(TRUE);
    }

    sSearch.species = species;
    playerX = (int)PlayerAvatar_GetXCoord(fieldSystem->playerAvatar);
    playerZ = (int)PlayerAvatar_GetZCoord(fieldSystem->playerAvatar);
    playerHeight = PlayerAvatar_GetPositionVector(fieldSystem->playerAvatar)->y;
    span = SEARCH_MAX_DISTANCE * 2 + 1;

    for (int attempt = 0; attempt < SEARCH_PLACE_ATTEMPTS; attempt++) {
        int dx = RandMod(span) - SEARCH_MAX_DISTANCE;
        int dz = RandMod(span) - SEARCH_MAX_DISTANCE;

        if (Abs(dx) < SEARCH_MIN_DISTANCE && Abs(dz) < SEARCH_MIN_DISTANCE) {
            continue;
        }

        if (!IsSearchableTile(fieldSystem, playerX + dx, playerZ + dz, playerHeight)) {
            continue;
        }

        sSearch.x = playerX + dx;
        sSearch.z = playerZ + dz;
        sSearch.mapId = PlayerMapId(fieldSystem);

        if (sSearch.chainSpecies != sSearch.species || sSearch.chainMapId != sSearch.mapId) {
            sSearch.chain = 0;
        }

        sSearch.searchLevel = GunkSave_GetSearchLevel(fieldSystem->saveData, sSearch.species);

        if (!RollHiddenMon(fieldSystem)) {
            return SEARCH_NOT_HERE;
        }

        sSearch.anim = NULL;
        StartPatchAnim(fieldSystem);
        sSearch.active = TRUE;
        sSearch.elapsedMs = 0;
        sSearch.lastTick = OS_GetTick();
        DrawHud(fieldSystem);
        return SEARCH_STARTED;
    }

    return sSearch.water ? SEARCH_NO_WATER : SEARCH_NO_GRASS;
}

// ---- per frame ----

// Every frame the field is free: the 10 seconds run out and the patch shakes again every half second. When
// time runs out the Pokemon gets away: the search ends and the chain breaks.
static BOOL Tick(FieldSystem *fieldSystem) {
    OSTick now;
    u32 ms;
    u16 before;

    if (!sSearch.active) {
        return FALSE;
    }

    if (sSearch.restore) {
        sSearch.restore = FALSE;

        if (PlayerMapId(fieldSystem) != sSearch.mapId) {
            sSearch.chain = 0;
            EndSearch(FALSE);
            return FALSE;
        }

        sSearch.anim = NULL;
        StartPatchAnim(fieldSystem);
        DrawHud(fieldSystem);
        sSearch.lastTick = OS_GetTick();
    }

    now = OS_GetTick();
    ms = ElapsedMs(now - sSearch.lastTick);

    if (ms > MAX_FRAME_MS) {
        ms = PAUSED_FRAME_MS;
    }

    before = sSearch.elapsedMs;
    sSearch.lastTick = now;
    sSearch.elapsedMs += ms;

    if (sSearch.elapsedMs >= SEARCH_MS) {
        sSearch.chain = 0;
        EndSearch(TRUE);
        ShowMessage(fieldSystem, "The Pok\x01mon got away...");
        return TRUE;
    }

    if (sSearch.elapsedMs / SHAKE_INTERVAL_MS != before / SHAKE_INTERVAL_MS) {
        StartPatchAnim(fieldSystem);
    }

    return FALSE;
}

void Gunk_FieldInputUpdate(void *input, FieldSystem *fieldSystem, u16 pressedKeys, u16 heldKeys) {
    u32 moveState;

    ov05_021D8164_Vanilla(input, fieldSystem, pressedKeys, heldKeys);
    moveState = PlayerAvatar_GetUnk14(fieldSystem->playerAvatar);
    // the same moment the game takes A, X and Y: standing still or a step just finished
    sSearch.rPressed = (pressedKeys & PAD_BUTTON_R) && (moveState == 0 || moveState == 3);
}

BOOL Gunk_FieldInputProcess(void *input, FieldSystem *fieldSystem) {
    SearchRefusal result;

    sSearch.playerX = (int)PlayerAvatar_GetXCoord(fieldSystem->playerAvatar);
    sSearch.playerZ = (int)PlayerAvatar_GetZCoord(fieldSystem->playerAvatar);

    if (Tick(fieldSystem)) {
        return TRUE;
    }

    if (ov05_021D8320_Vanilla(input, fieldSystem)) {
        return TRUE;
    }

    if (!sSearch.rPressed) {
        return FALSE;
    }

    sSearch.rPressed = FALSE;
    PlaySE(SEQ_SE_DP_WIN_OPEN);

    if (Gunk_TestDexNavKeys(fieldSystem)) {
        return TRUE;
    }

    result = TryStartSearch(fieldSystem);

    if (result != SEARCH_STARTED) {
        ShowMessage(fieldSystem, sRefusalTexts[result]);
    }

    return TRUE;
}

// ---- the patch's battle ----

static struct Pokemon *CreateEncounterMon(FieldSystem *fieldSystem, void *params) {
    struct Pokemon *mon = AllocMonZeroed(HEAP_ID_FIELD);

    ZeroMonData(mon);
    CreateMon(mon, sSearch.species, sSearch.level, 32, TRUE, (int)sSearch.personality, 1, (int)PlayerOtId(fieldSystem));

    for (int stat = 0; stat < NUM_STATS; stat++) {
        u32 iv = sSearch.ivs[stat];
        SetMonData(mon, MON_DATA_HP_IV + stat, &iv);
    }

    CalcMonLevelAndStats(mon);
    (void)params;
    return mon;
}

// Stepping into the patch: the hidden Pokemon is added to a wild battle the way the game adds one (held item,
// with Compound Eyes; Shellos's sea and Unown's letter from the map), then its search level and the chain go up.
static void StartPatchBattle(FieldSystem *fieldSystem) {
    u8 params[0x20];
    struct Pokemon *lead = Party_GetMonByIndex(SaveArray_Party_Get(fieldSystem->saveData), 0);
    BattleSetup *setup = BattleSetup_New(HEAP_ID_FIELD, 0);
    struct Pokemon *mon;

    BattleSetup_InitFromFieldSystem(setup, fieldSystem);
    MI_CpuClear8(params, sizeof(params));
    ov06_0223D2DC(fieldSystem, lead, 0, params);
    mon = CreateEncounterMon(fieldSystem, params);
    ov06_0223D1BC(1, params, mon, setup);
    Heap_Free(mon);

    GunkSave_IncrementSearchLevel(fieldSystem->saveData, sSearch.species);

    if (sSearch.chain < 999) {
        sSearch.chain++;
    }

    sSearch.chainSpecies = sSearch.species;
    sSearch.chainMapId = sSearch.mapId;
    EndSearch(TRUE);
    sub_020469B8(fieldSystem, setup); // marks the battle as the patch's own (GunkDexNav_OnWildEncounter)
    sSearch.battling = TRUE;
}

BOOL Gunk_FieldStepEncounter(FieldSystem *fieldSystem) {
    if (sSearch.active) {
        int x = (int)PlayerAvatar_GetXCoord(fieldSystem->playerAvatar);
        int z = (int)PlayerAvatar_GetZCoord(fieldSystem->playerAvatar);

        if (PlayerMapId(fieldSystem) != sSearch.mapId || Abs(x - sSearch.x) > SEARCH_LOST_DISTANCE || Abs(z - sSearch.z) > SEARCH_LOST_DISTANCE) {
            // walking away ends the search and breaks the chain
            sSearch.chain = 0;
            EndSearch(TRUE);
        } else if (x == sSearch.x && z == sSearch.z) {
            StartPatchBattle(fieldSystem);
            return TRUE;
        } else if (sHudShown) {
            // a message box may have borrowed the bar's tiles: put the frame and text back
            DrawFrameAndWindow2(&sHud, FALSE, FIELD_FRAME_TILE, FIELD_FRAME_PALETTE);
            ScheduleWindowCopyToVram(&sHud);
        }
    }

    return ov05_021D8954(fieldSystem);
}

// A wild battle is starting (a random encounter, fishing, honey, or the patch's own): a different species from
// the chain's breaks the chain.
void GunkDexNav_OnWildEncounter(BattleSetup *setup) {
    u16 species = (u16)GetMonData(Party_GetMonByIndex(setup->party[1], 0), MON_DATA_SPECIES, NULL);

    sSearch.battling = FALSE;

    if (species != sSearch.chainSpecies) {
        sSearch.chain = 0;
    }
}

// A wild battle is over: running from the patch's own Pokemon breaks the chain; catching or defeating keeps it.
void GunkDexNav_OnBattleEnd(u32 winFlag) {
    if (sSearch.battling && winFlag == BATTLE_OUTCOME_PLAYER_FLED) {
        sSearch.chain = 0;
    }

    sSearch.battling = FALSE;
}

// The patch and the bar live in the field's heap and BG layers: take them down while the field still exists,
// and put them back on the first free frame after it returns.
BOOL Gunk_FieldExit(void *overlayManager, u32 *state) {
    if (*state == 0 && sSearch.active) {
        EndSearch(TRUE);
        sSearch.active = TRUE;
        sSearch.restore = TRUE;
    }

    return ov05_021D76C0_Vanilla(overlayManager, state);
}

// ---- the DexNav screen ----

static void AddSlot(DexNavEntry *slots, int *count, int species, int method, int minLevel, int maxLevel) {
    if (species == SPECIES_NONE) {
        return;
    }

    for (int i = 0; i < *count; i++) {
        if (slots[i].species == species) {
            slots[i].minLevel = (u8)(minLevel < slots[i].minLevel ? minLevel : slots[i].minLevel);
            slots[i].maxLevel = (u8)(maxLevel > slots[i].maxLevel ? maxLevel : slots[i].maxLevel);
            return;
        }
    }

    if (*count >= DEXNAV_SECTION_SLOTS) {
        return;
    }

    slots[*count].species = (u16)species;
    slots[*count].method = (u8)method;
    slots[*count].minLevel = (u8)minLevel;
    slots[*count].maxLevel = (u8)maxLevel;
    (*count)++;
}

static int MinLevel(const WaterSlot *slot) {
    return slot->minLevel < slot->maxLevel ? slot->minLevel : slot->maxLevel;
}

static int MaxLevel(const WaterSlot *slot) {
    return slot->minLevel < slot->maxLevel ? slot->maxLevel : slot->minLevel;
}

// The land and water halves of the grid, from the current map's encounter table (surf before the rods, so a
// species on both keeps "Surfing").
void GunkDexNav_BuildAreaData(FieldSystem *fieldSystem, DexNavData *data) {
    const WildEncounterData *encounters = sub_02034CE0(fieldSystem);
    DexNavEntry *land = &data->slots[0];
    DexNavEntry *water = &data->slots[DEXNAV_SECTION_SLOTS];
    const WaterSlot *waterTables[4];
    static const u8 sWaterMethods[4] = { DEXNAV_METHOD_SURF, DEXNAV_METHOD_OLD_ROD, DEXNAV_METHOD_GOOD_ROD, DEXNAV_METHOD_SUPER_ROD };
    int numLand = 0, numWater = 0, i;
    int grassMin = 100, grassMax = 0;

    MI_CpuClear8(data->slots, sizeof(data->slots));
    data->saveData = fieldSystem->saveData;

    for (i = 0; i < NUM_GRASS_SLOTS; i++) {
        const GrassSlot *slot = &encounters->grass[i];

        if (slot->species != SPECIES_NONE) {
            grassMin = slot->level < grassMin ? slot->level : grassMin;
            grassMax = slot->level > grassMax ? slot->level : grassMax;
        }

        AddSlot(land, &numLand, slot->species, DEXNAV_METHOD_GRASS, slot->level, slot->level);
    }

    for (i = 0; i < 2; i++) {
        AddSlot(land, &numLand, encounters->day[i], DEXNAV_METHOD_MORNING, grassMin, grassMax);
        AddSlot(land, &numLand, encounters->night[i], DEXNAV_METHOD_NIGHT, grassMin, grassMax);
    }

    for (i = 0; i < 2; i++) {
        AddSlot(land, &numLand, encounters->swarm[i], DEXNAV_METHOD_SWARM, grassMin, grassMax);
    }

    waterTables[0] = encounters->surf;
    waterTables[1] = encounters->oldRod;
    waterTables[2] = encounters->goodRod;
    waterTables[3] = encounters->superRod;

    for (int table = 0; table < 4; table++) {
        for (i = 0; i < NUM_WATER_SLOTS; i++) {
            const WaterSlot *slot = &waterTables[table][i];
            AddSlot(water, &numWater, slot->species, sWaterMethods[table], MinLevel(slot), MaxLevel(slot));
        }
    }
}

FS_EXTERN_OVERLAY(OVERLAY_87);

static const struct OverlayManagerTemplate sDexNavAppTemplate = {
    GunkDexNavApp_Init,
    GunkDexNavApp_Main,
    GunkDexNavApp_Exit,
    FS_OVERLAY_ID(OVERLAY_87),
};

extern void sub_0204AF3C(TaskManager *taskManager);
extern void sub_02046500(TaskManager *taskManager, u32 template, u32 args);

typedef struct DexNavScreenTask {
    int state;
    DexNavData data;
} DexNavScreenTask;

// Fade out, leave the field, run the DexNav screen, come back.
static BOOL Task_DexNavScreen(TaskManager *taskManager) {
    FieldSystem *fieldSystem = TaskManager_GetFieldSystem(taskManager);
    DexNavScreenTask *task = TaskManager_GetEnvironment(taskManager);

    switch (task->state) {
    case 0:
        MapObjectManager_PauseAllMovement(fieldSystem->mapObjectManager);
        GunkDexNav_BuildAreaData(fieldSystem, &task->data);
        CallTask_FadeToBlack(taskManager);
        task->state++;
        break;
    case 1:
        sub_0204AF3C(taskManager);
        task->state++;
        break;
    case 2:
        sub_02046500(taskManager, (u32)&sDexNavAppTemplate, (u32)&task->data);
        task->state++;
        break;
    case 3:
        CallTask_RestoreOverworld(taskManager);
        task->state++;
        break;
    case 4:
        CallTask_FadeFromBlack(taskManager);
        task->state++;
        break;
    case 5:
        MapObjectManager_UnpauseAllMovement(fieldSystem->mapObjectManager);
        Heap_Free(task);
        return TRUE;
    }

    return FALSE;
}

void GunkDexNav_OpenScreen(FieldSystem *fieldSystem) {
    DexNavScreenTask *task = Heap_Alloc(HEAP_ID_FIELD, sizeof(DexNavScreenTask));

    MI_CpuClear8(task, sizeof(DexNavScreenTask));
    FieldSystem_CreateTask(fieldSystem, Task_DexNavScreen, task);
}

// ---- the DexNav key item (item use table entry 23, asm/unk_0205CE48.s) ----

extern void StartMenu_SetExitTaskFunc(void *startMenu, TaskFunc func);
extern BOOL sub_02036A78(TaskManager *taskManager); // frees the app's data and reopens the Bag

typedef struct ItemUseContext {
    TaskManager *taskManager;
} ItemUseContext;

void GunkDexNav_ItemBagUse(ItemUseContext *context);
BOOL GunkDexNav_ItemFieldUse(FieldSystem **context);
int GunkDexNav_ItemCheck(u32 *context);

// From the Bag: like the Town Map, the screen opens over the Bag and B returns to it.
void GunkDexNav_ItemBagUse(ItemUseContext *context) {
    FieldSystem *fieldSystem = TaskManager_GetFieldSystem(context->taskManager);
    u8 *startMenu = TaskManager_GetEnvironment(context->taskManager);
    DexNavData *data = Heap_AllocAtEnd(HEAP_ID_FIELD, sizeof(DexNavData));

    GunkDexNav_BuildAreaData(fieldSystem, data);
    sub_020373D4(fieldSystem, (struct OverlayManagerTemplate *)&sDexNavAppTemplate, data);
    *(void **)(startMenu + 0x1F8) = data;
    StartMenu_SetExitTaskFunc(startMenu, sub_02036A78);
}

// Registered to Select: straight from the field.
BOOL GunkDexNav_ItemFieldUse(FieldSystem **context) {
    GunkDexNav_OpenScreen(*context);
    return FALSE;
}

// usable anywhere in the field (2: not now, as the Poke Radar answers in the Underground)
int GunkDexNav_ItemCheck(u32 *context) {
    return context[1] == 1 ? 2 : 0;
}
