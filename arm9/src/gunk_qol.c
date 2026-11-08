#include "global.h"

#include "constants/heap.h"
#include "bag.h"
#include "field_system.h"
#include "itemtool.h"
#include "message_format.h"
#include "pokemon.h"
#include "save.h"
#include "gunk/gunk_save.h"
#include "gunk/qol.h"

// Gunk's Repel prompt, Endless Repel and Exp. All toggles (as in Gunk's Platinum).
// - When a Repel wears off and another of the same kind is in the Bag, the "wore off" script (common 2032) asks
//   whether to use another.
// - Endless Repel (key item): switched on, the Repel effect never runs out; switched off, it ends at once.
// - Exp. All (key item): switched on, party members that didn't battle get half the EXP (battle side, overlay 11).

#define ITEM_ATTR_REPEL_STEPS   2
#define SCRIPT_ENDLESS_REPEL    2050 // common scripts (scr_seq 205)
#define SCRIPT_EXP_ALL          2051
#define ENDLESS_REPEL_STEPS     2    // the step update takes one per step; it is refilled before it reaches 0

extern void *FieldSysGetAttrAddr(FieldSystem *fieldSystem, int idx);
extern void *Save_Roamers_Get(struct SaveData *save);
extern u8 *sub_0202AB4C(void *roamers); // the Repel step counter
extern void ov75_021E6FEC(void *bagApp, u8 steps);
extern void sub_0205CF34(void *context, void *arg1, u16 script); // item use from the Bag: run a script
extern void sub_0205CF78(void *context, u16 script);            // item use from the field: run a script

static struct SaveData *Save(void) {
    return sub_020225F8();
}

u8 *Gunk_RepelStepCounter(void *roamers) {
    u8 *steps = sub_0202AB4C(roamers);

    if (GunkSave_IsEndlessRepelOn(Save()) && *steps < ENDLESS_REPEL_STEPS) {
        *steps = ENDLESS_REPEL_STEPS;
    }

    return steps;
}

void Gunk_BagUseRepel(void *bagApp, u8 steps, u16 item) {
    ov75_021E6FEC(bagApp, steps);
    GunkSave_SetLastRepel(Save(), item);
}

BOOL Gunk_ScrCmd_CheckRepelAgain(ScriptContext *ctx) {
    u16 *result = ScriptGetVarPointer(ctx);
    struct SaveData *save = ctx->fieldSystem->saveData;
    u16 item = GunkSave_GetLastRepel(save);

    *result = item != ITEM_NONE && Bag_HasItem(Save_Bag_Get(save), item, 1, HEAP_ID_FIELD) && !GunkSave_IsEndlessRepelOn(save);

    if (*result) {
        MessageFormat **messageFormat = FieldSysGetAttrAddr(ctx->fieldSystem, SCRIPTENV_MESSAGE_FORMAT);
        BufferItemName(*messageFormat, 0, item);
    }

    return FALSE;
}

BOOL Gunk_ScrCmd_UseRepelAgain(ScriptContext *ctx) {
    struct SaveData *save = ctx->fieldSystem->saveData;
    u16 item = GunkSave_GetLastRepel(save);

    if (Bag_TakeItem(Save_Bag_Get(save), item, 1, HEAP_ID_FIELD)) {
        *sub_0202AB4C(Save_Roamers_Get(save)) = (u8)GetItemAttr(item, ITEM_ATTR_REPEL_STEPS, HEAP_ID_FIELD);
    }

    return FALSE;
}

BOOL Gunk_ScrCmd_ToggleEndlessRepel(ScriptContext *ctx) {
    u16 *result = ScriptGetVarPointer(ctx);
    struct SaveData *save = ctx->fieldSystem->saveData;
    BOOL on = !GunkSave_IsEndlessRepelOn(save);

    GunkSave_SetEndlessRepelOn(save, on);

    if (!on) {
        *sub_0202AB4C(Save_Roamers_Get(save)) = 0;
    }

    *result = on;
    return FALSE;
}

BOOL Gunk_ScrCmd_ToggleExpAll(ScriptContext *ctx) {
    u16 *result = ScriptGetVarPointer(ctx);
    struct SaveData *save = ctx->fieldSystem->saveData;
    BOOL on = !GunkSave_IsExpAllOn(save);

    GunkSave_SetExpAllOn(save, on);
    *result = on;
    return FALSE;
}

void Gunk_EndlessRepelBagUse(void *context, void *arg1) {
    sub_0205CF34(context, arg1, SCRIPT_ENDLESS_REPEL);
}

BOOL Gunk_EndlessRepelFieldUse(void *context) {
    sub_0205CF78(context, SCRIPT_ENDLESS_REPEL);
    return FALSE;
}

void Gunk_ExpAllBagUse(void *context, void *arg1) {
    sub_0205CF34(context, arg1, SCRIPT_EXP_ALL);
}

BOOL Gunk_ExpAllFieldUse(void *context) {
    sub_0205CF78(context, SCRIPT_EXP_ALL);
    return FALSE;
}

// usable anywhere in the field
int Gunk_ToggleItemCheck(u32 *context) {
#pragma unused(context)
    return 0;
}

// ---- Exp. All, battle side (overlay 11's EXP task, ov11_0223A79C) ----


#define BATTLE_CTX_EXP_PER_MON   0x9C // EXP for each Pokémon that battled

// The task picks each party Pokémon that battled or holds an Exp. Share; with Exp. All on, every Pokémon is picked
// (fainted ones and level 100s are skipped later by the task itself).
u32 Gunk_ExpRecipientHoldEffect(u16 item, u32 attr, enum HeapID heapID, struct Pokemon *mon) {
    u32 holdEffect = GetItemAttr(item, attr, heapID);

    if (holdEffect != HOLD_EFFECT_EXP_SHARE && GunkSave_IsExpAllOn(Save()) && !GetMonData(mon, MON_DATA_IS_EGG, NULL)) {
        return HOLD_EFFECT_EXP_SHARE;
    }

    return holdEffect;
}

// A picked Pokémon that didn't battle and holds no Exp. Share would get 0: with Exp. All on it gets half.
u32 Gunk_ExpAllAmount(u32 exp, u8 *battleCtx) {
    if (exp == 0 && GunkSave_IsExpAllOn(Save())) {
        exp = *(u32 *)(battleCtx + BATTLE_CTX_EXP_PER_MON) / 2;
        if (exp == 0) {
            exp = 1;
        }
    }

    return exp;
}
