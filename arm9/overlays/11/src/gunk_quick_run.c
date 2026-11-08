#include "global.h"

#include "MI_memory.h"
#include "PAD_pad.h"
#include "main.h"
#include "gunk/quick_run.h"

// Gunk's Quick Run: in a wild battle, R at the action menu does what touching RUN does (as in Gunk's Platinum).
//
// The command-selection task (ov11_02250A70) reads the bottom screen's menu each frame with ov11_02258E74
// (pokeplatinum's BattleSystem_MenuInput); it now calls this wrapper instead. Pressing the RUN button by hand is
// what the subscreen does on a touch: its processInput, then save the cursor, clear it and hide its sprites.

#define SUB_BATTLE_SYSTEM      0x000 // BattleSystem *
#define SUB_ACTIVE_MENU        0x69F // s8: which entry of the menu config table is shown
#define SUB_PANEL_SLIDING      0x6A2 // u8
#define SUB_CURSOR_RENDERER    0x6EC
#define SUB_CURSOR             0x6F0 // 4 bytes
#define SUB_SUPPRESS_SFX       0x6F4 // u8

#define MENU_ACTION_FIRST      1 // configs 1-4 are the plain FIGHT/BAG/POKeMON/RUN menu (single or partner layout,
#define MENU_ACTION_LAST       4 // first or later turn); Safari, Pal Park and the catching tutorial use others
#define INPUT_RUN              4
#define INPUT_NONE             0xFFFFFFFF

#define BATTLE_TYPE_ROAMER     0x100

extern u32 ov11_02258E74(u8 *subscreen);
extern u32 ov11_02259E64(u8 *subscreen, u32 result, u32 slot);
extern void ov11_0225BB04(u8 *subscreen, u32 index);
extern void ov11_0225D648(void *cursorRenderer);
extern u32 ov11_0222FF74(void *battleSystem); // battle type flags

static BOOL IsWildSingleBattle(u8 *subscreen) {
    u32 type = ov11_0222FF74(*(void **)(subscreen + SUB_BATTLE_SYSTEM));
    return (type & ~BATTLE_TYPE_ROAMER) == 0;
}

u32 Gunk_BattleMenuInput(u8 *subscreen) {
    s8 menu = *(s8 *)(subscreen + SUB_ACTIVE_MENU);

    if ((gSystem.newKeys & PAD_BUTTON_R)
        && menu >= MENU_ACTION_FIRST && menu <= MENU_ACTION_LAST
        && subscreen[SUB_PANEL_SLIDING] != TRUE
        && IsWildSingleBattle(subscreen)) {
        u32 result = ov11_02259E64(subscreen, INPUT_RUN, INPUT_RUN);

        if (result != INPUT_NONE) {
            ov11_0225BB04(subscreen, INPUT_RUN - 1);
            MI_CpuFill8(subscreen + SUB_CURSOR, 0, 4);
            ov11_0225D648(*(void **)(subscreen + SUB_CURSOR_RENDERER));
            subscreen[SUB_SUPPRESS_SFX] = 1;
            return result;
        }
    }

    return ov11_02258E74(subscreen);
}
