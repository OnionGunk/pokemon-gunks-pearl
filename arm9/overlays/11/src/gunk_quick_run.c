#include "global.h"

#include "constants/items.h"
#include "MI_memory.h"
#include "PAD_pad.h"
#include "bag.h"
#include "main.h"
#include "party.h"
#include "pokemon_storage_system.h"
#include "save.h"
#include "unk_020051F4.h"
#include "gunk/gunk_save.h"
#include "gunk/quick_run.h"

// Gunk's Quick Run and Quick Ball in wild battles (as in Gunk's Platinum).
//
// The command-selection task (ov11_02250A70) reads the bottom screen's menu each frame with ov11_02258E74
// (pokeplatinum's BattleSystem_MenuInput); it now calls Gunk_BattleMenuInput instead:
// - R does what touching RUN does.
// - Tap L to throw the ball shown in the prompt; hold L and press Left or Right to pick another. The prompt
//   ("What will X do?") gets a second line, "L: Great Ball x12" (Gunk_BattlePrompt). The ball is remembered
//   (GunkSave lastBall), and the last one picked is offered first.
// Pressing a button by hand is what the subscreen does on a touch: its processInput, then save the cursor, clear it
// and hide its sprites. A Quick Ball throw presses BAG, and the controller (Gunk_QuickBallBagCommand) then skips the
// Bag and uses the ball the way the Bag does after a ball is picked.

#define SUB_BATTLE_SYSTEM      0x000 // BattleSystem *
#define SUB_ACTIVE_MENU        0x69F // s8: which entry of the menu config table is shown
#define SUB_PANEL_SLIDING      0x6A2 // u8
#define SUB_CURSOR_RENDERER    0x6EC
#define SUB_CURSOR             0x6F0 // 4 bytes
#define SUB_SUPPRESS_SFX       0x6F4 // u8

#define MENU_ACTION_FIRST      1 // configs 1-4 are the plain FIGHT/BAG/POKeMON/RUN menu (single or partner layout,
#define MENU_ACTION_LAST       4 // first draw or redrawn); Safari, Pal Park and the catching tutorial use others
#define INPUT_BAG              2
#define INPUT_RUN              4
#define INPUT_NONE             0xFFFFFFFF

#define BATTLE_TYPE_ROAMER     0x100
#define BATTLE_SYS_PC          0x64 // PCStorage *

#define HEAP_ID_BATTLE         ((enum HeapID)5)
#define BALL_FIRST             ITEM_MASTER_BALL
#define BALL_LAST              ITEM_CHERISH_BALL
#define MAX_SHOWN_COUNT        99
#define SE_SELECT              0x5DC

#define CONTROL_ITEM           0xE
#define COMMAND_ITEM_SELECT    6   // open the Bag
#define COMMAND_WAIT           0xC // the item is chosen
#define ACTION_PICK_COMMAND    0x21A8
#define ACTION_TEMP_VALUE      0x21B0
#define ITEM_CATEGORY_BALLS    2

// battle message (pokeplatinum's BattleMessage)
typedef struct BattlePromptMessage {
    u8 commandCode;
    u8 tags;
    u16 id;
    int params[6];
    int digits;
    int battler;
} BattlePromptMessage;

#define TAG_NICKNAME_NUM       17
#define MSG_PROMPT_BALL_FIRST  1260 // narc_0324: "What will X do?\nL: <ball> x<count>", one per ball

extern u32 ov11_02258E74(u8 *subscreen);
extern u32 ov11_02259E64(u8 *subscreen, u32 result, u32 slot);
extern void ov11_0225BB04(u8 *subscreen, u32 index);
extern void ov11_0225D648(void *cursorRenderer);
extern u32 ov11_0222FF74(void *battleSystem);            // battle type flags
extern Bag *ov11_02230214(void *battleSystem);           // the battle's copy of the Bag
extern int ov11_0222FFC8(void *battleSystem, int side);   // party count
extern u32 ov11_0223178C(void *battleSystem, void *msgLoader, BattlePromptMessage *msg, u32 speed);

static u16 sBall;
static BOOL sLHeld;
static BOOL sCycled;
static BOOL sThrow;
static BOOL sPromptShown;
static void *sPromptBattleSystem;
static void *sPromptLoader;
static u32 sPromptSpeed;
static BattlePromptMessage sPrompt;

static BOOL IsWildSingleBattle(void *battleSystem) {
    return (ov11_0222FF74(battleSystem) & ~BATTLE_TYPE_ROAMER) == 0;
}

static u16 BallCount(void *battleSystem, u16 ball) {
    return Bag_GetQuantity(ov11_02230214(battleSystem), ball, HEAP_ID_BATTLE);
}

static BOOL IsUsableBall(void *battleSystem, u16 ball) {
    return ball >= BALL_FIRST && ball <= BALL_LAST && ball != ITEM_SAFARI_BALL && BallCount(battleSystem, ball) > 0;
}

// The ball to offer: the one picked this battle, else the last one used, else the first one owned.
static BOOL ChooseBall(void *battleSystem) {
    if (IsUsableBall(battleSystem, sBall)) {
        return TRUE;
    }

    sBall = GunkSave_GetLastBall(sub_020225F8());

    if (IsUsableBall(battleSystem, sBall)) {
        return TRUE;
    }

    for (sBall = BALL_FIRST; sBall <= BALL_LAST; sBall++) {
        if (IsUsableBall(battleSystem, sBall)) {
            return TRUE;
        }
    }

    sBall = ITEM_NONE;
    return FALSE;
}

// a caught Pokémon would have nowhere to go: leave it to the Bag, which says so
static BOOL HasRoomForCatch(void *battleSystem) {
    struct PCStorage *pc = *(struct PCStorage **)((u8 *)battleSystem + BATTLE_SYS_PC);
    int box, slot;

    return ov11_0222FFC8(battleSystem, 0) < PARTY_SIZE || PCStorage_FindFirstEmptySlot(pc, &box, &slot);
}

static u32 PrintPrompt(void) {
    BattlePromptMessage msg = sPrompt;
    u16 count = BallCount(sPromptBattleSystem, sBall);

    msg.tags = TAG_NICKNAME_NUM;
    msg.id = MSG_PROMPT_BALL_FIRST + (sBall - BALL_FIRST);
    msg.params[1] = count > MAX_SHOWN_COUNT ? MAX_SHOWN_COUNT : count;
    msg.digits = 2;
    return ov11_0223178C(sPromptBattleSystem, sPromptLoader, &msg, sPromptSpeed);
}

// "What will X do?" (overlay 11 prints it through here): in a wild battle with a ball to throw, add the Quick Ball line.
u32 Gunk_BattlePrompt(void *battleSystem, void *msgLoader, void *message, u32 speed) {
    sPromptShown = FALSE;
    sLHeld = FALSE;

    if (!IsWildSingleBattle(battleSystem) || !ChooseBall(battleSystem)) {
        return ov11_0223178C(battleSystem, msgLoader, message, speed);
    }

    sPromptShown = TRUE;
    sPromptBattleSystem = battleSystem;
    sPromptLoader = msgLoader;
    sPromptSpeed = speed;
    sPrompt = *(BattlePromptMessage *)message;
    return PrintPrompt();
}

static void CycleBall(int step) {
    u16 ball = sBall;

    for (int i = BALL_FIRST; i <= BALL_LAST; i++) {
        ball += step;

        if (ball > BALL_LAST) {
            ball = BALL_FIRST;
        } else if (ball < BALL_FIRST) {
            ball = BALL_LAST;
        }

        if (IsUsableBall(sPromptBattleSystem, ball)) {
            break;
        }
    }

    if (ball != sBall) {
        sBall = ball;
        GunkSave_SetLastBall(sub_020225F8(), sBall);
        PlaySE(SE_SELECT);
        PrintPrompt();
    }
}

static u32 PressActionButton(u8 *subscreen, u32 input) {
    u32 result = ov11_02259E64(subscreen, input, input);

    if (result != INPUT_NONE) {
        ov11_0225BB04(subscreen, input - 1);
        MI_CpuFill8(subscreen + SUB_CURSOR, 0, 4);
        ov11_0225D648(*(void **)(subscreen + SUB_CURSOR_RENDERER));
        subscreen[SUB_SUPPRESS_SFX] = 1;
    }

    return result;
}

u32 Gunk_BattleMenuInput(u8 *subscreen) {
    void *battleSystem = *(void **)(subscreen + SUB_BATTLE_SYSTEM);
    s8 menu = *(s8 *)(subscreen + SUB_ACTIVE_MENU);
    BOOL ready = menu >= MENU_ACTION_FIRST && menu <= MENU_ACTION_LAST && subscreen[SUB_PANEL_SLIDING] != TRUE
        && IsWildSingleBattle(battleSystem);

    if (!ready) {
        sLHeld = FALSE;
        return ov11_02258E74(subscreen);
    }

    // Quick Ball: tap L to throw, hold L and press Left/Right to pick another ball
    if (sPromptShown) {
        if (!sLHeld && (gSystem.newKeys & PAD_BUTTON_L)) {
            sLHeld = TRUE;
            sCycled = FALSE;
            return INPUT_NONE;
        }

        if (sLHeld) {
            if (gSystem.heldKeys & PAD_BUTTON_L) {
                if (gSystem.newKeys & PAD_KEY_LEFT) {
                    CycleBall(-1);
                    sCycled = TRUE;
                } else if (gSystem.newKeys & PAD_KEY_RIGHT) {
                    CycleBall(1);
                    sCycled = TRUE;
                }
                return INPUT_NONE;
            }

            sLHeld = FALSE;

            if (!sCycled && IsUsableBall(battleSystem, sBall) && HasRoomForCatch(battleSystem)) {
                u32 result = PressActionButton(subscreen, INPUT_BAG);

                if (result != INPUT_NONE) {
                    sThrow = TRUE;
                    GunkSave_SetLastBall(sub_020225F8(), sBall);
                    sPromptShown = FALSE;
                }
                return result;
            }
            return INPUT_NONE;
        }
    }

    // Quick Run: R does what RUN does
    if (gSystem.newKeys & PAD_BUTTON_R) {
        u32 result = PressActionButton(subscreen, INPUT_RUN);

        if (result != INPUT_NONE) {
            sPromptShown = FALSE;
            return result;
        }
    }

    u32 input = ov11_02258E74(subscreen);

    if (input != INPUT_NONE) {
        sPromptShown = FALSE;
    }

    return input;
}

// The battle controller's BAG case: a Quick Ball throw skips the Bag and uses the ball (item | category 2 << 16, as
// the Bag returns it). Returns the next command state.
u32 Gunk_QuickBallBagCommand(u8 *battlerActionBase) {
    *(u32 *)(battlerActionBase + ACTION_PICK_COMMAND) = CONTROL_ITEM;

    if (sThrow) {
        sThrow = FALSE;
        *(u32 *)(battlerActionBase + ACTION_TEMP_VALUE) = sBall | (ITEM_CATEGORY_BALLS << 16);
        return COMMAND_WAIT;
    }

    return COMMAND_ITEM_SELECT;
}
