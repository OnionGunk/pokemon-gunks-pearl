#include "global.h"

#include "PAD_pad.h"
#include "bg_window.h"
#include "main.h"
#include "msgdata.h"
#include "pokemon.h"
#include "string16.h"
#include "unk_020051F4.h"
#include "gunk/summary.h"

// Gunk's IV/EV view (as in Gunk's Platinum): A on the Skills page cycles the six numbers through stats, IVs and EVs,
// and the ability description says which is shown. The vanilla page draws first; in IV/EV mode the numbers and the
// description are then reprinted with the summary's own helpers.

#define SUMMARY_WINDOWS     0x244 // Window *: 0 HP, 1 Atk, 2 Def, 3 Sp. Atk, 4 Sp. Def, 5 Speed, 6 ability, 7 description
#define SUMMARY_MSGDATA     0x674 // struct MsgData * (bank narc_0404)
#define SUMMARY_STRING      0x680 // struct String *: what sub_0207D998 prints
#define SUMMARY_PAGE        0x68C // s8
#define PAGE_SKILLS         2
#define WINDOW_SIZE         0x10
#define WINDOW_DESCRIPTION  7
#define STAT_COUNT          6
#define MSG_NUMBER          0x78 // "{STRVAR_1 52, 0}"
#define MSG_SHOWING_IVS     187
#define MSG_SHOWING_EVS     188
#define COLOR_BLACK         0x10200
#define ALIGN_LEFT          0
#define ALIGN_RIGHT         1
#define SE_SELECT           0x5DD

enum { SHOW_STATS, SHOW_IVS, SHOW_EVS, SHOW_COUNT };

extern void sub_0207AB0C(u8 *summary, struct Pokemon *mon, u8 *monData);
extern void sub_0207E200(u8 *summary);
extern void sub_0207DA48(u8 *summary, u32 msgId, u32 value, u32 digits, u32 padding);
extern void sub_0207D998(u8 *summary, Window *window, u32 color, u32 align);

static u8 sShow;
static u8 sIVs[STAT_COUNT];
static u8 sEVs[STAT_COUNT];

void Gunk_SummaryLoadMon(u8 *summary, struct Pokemon *mon, u8 *monData) {
    // the Skills page's order: HP, Attack, Defense, Sp. Atk, Sp. Def, Speed
    static const u8 ivIds[STAT_COUNT] = { MON_DATA_HP_IV, MON_DATA_ATK_IV, MON_DATA_DEF_IV, MON_DATA_SPATK_IV, MON_DATA_SPDEF_IV, MON_DATA_SPEED_IV };
    static const u8 evIds[STAT_COUNT] = { MON_DATA_HP_EV, MON_DATA_ATK_EV, MON_DATA_DEF_EV, MON_DATA_SPATK_EV, MON_DATA_SPDEF_EV, MON_DATA_SPEED_EV };

    sub_0207AB0C(summary, mon, monData);

    for (int i = 0; i < STAT_COUNT; i++) {
        sIVs[i] = (u8)GetMonData(mon, ivIds[i], NULL);
        sEVs[i] = (u8)GetMonData(mon, evIds[i], NULL);
    }

    sShow = SHOW_STATS;
}

BOOL Gunk_SummaryHandleInput(u8 *summary) {
    if (*(s8 *)(summary + SUMMARY_PAGE) != PAGE_SKILLS || !(gSystem.newKeys & PAD_BUTTON_A)) {
        return FALSE;
    }

    sShow = (sShow + 1) % SHOW_COUNT;
    PlaySE(SE_SELECT);
    Gunk_SummaryDrawSkills(summary);
    return TRUE;
}

void Gunk_SummaryDrawSkills(u8 *summary) {
    sub_0207E200(summary);

    if (sShow == SHOW_STATS) {
        return;
    }

    Window *windows = *(Window **)(summary + SUMMARY_WINDOWS);
    const u8 *values = sShow == SHOW_IVS ? sIVs : sEVs;

    for (int i = 0; i < STAT_COUNT; i++) {
        FillWindowPixelBuffer(&windows[i], 0);
        sub_0207DA48(summary, MSG_NUMBER, values[i], 3, 0);
        sub_0207D998(summary, &windows[i], COLOR_BLACK, ALIGN_RIGHT);
        ScheduleWindowCopyToVram(&windows[i]);
    }

    struct String *text = NewString_ReadMsgData(*(struct MsgData **)(summary + SUMMARY_MSGDATA), sShow == SHOW_IVS ? MSG_SHOWING_IVS : MSG_SHOWING_EVS);
    StringCopy(*(struct String **)(summary + SUMMARY_STRING), text);
    String_Delete(text);
    FillWindowPixelBuffer(&windows[WINDOW_DESCRIPTION], 0);
    sub_0207D998(summary, &windows[WINDOW_DESCRIPTION], COLOR_BLACK, ALIGN_LEFT);
    ScheduleWindowCopyToVram(&windows[WINDOW_DESCRIPTION]);
}
