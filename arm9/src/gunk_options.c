#include "global.h"

#include "font.h"
#include "msgdata.h"
#include "string16.h"
#include "gunk/options.h"

// Gunk's: the Options screen's TEXT SPEED row gets a fourth choice, INSTANT (see Options_GetTextFrameDelay).
//
// The Options overlay (overlay 60) reads each row's choice strings from message bank 0xCE in one running sequence
// (rows' strings are consecutive), so a fourth string can't be inserted in place without shifting every later row.
// Instead the overlay loads the vanilla three and then calls this, which appends INSTANT from the end of the bank.

#define OPTIONS_ROW_SIZE        0x54 // each row's block in the Options screen's work data
#define OPTIONS_ROW_NUM_CHOICES 0x5C // u16
#define OPTIONS_ROW_CHOICES     0x60 // struct String *[]
#define OPTIONS_MSGDATA         0x20 // struct MsgData *
#define OPTIONS_TEXT_SPEED_ROW  0
#define MSG_OPTIONS_INSTANT     42 // narc_0206.gmm

void Gunk_OptionsAddInstant(u8 *menu) {
    u8 *row = menu + OPTIONS_ROW_SIZE * OPTIONS_TEXT_SPEED_ROW;
    u16 *numChoices = (u16 *)(row + OPTIONS_ROW_NUM_CHOICES);
    struct String **choices = (struct String **)(row + OPTIONS_ROW_CHOICES);

    choices[*numChoices] = NewString_ReadMsgData(*(struct MsgData **)(menu + OPTIONS_MSGDATA), MSG_OPTIONS_INSTANT);
    (*numChoices)++;
}

#define OPTIONS_PACKED_GAP      12 // overlay 60's gap between packed choices
#define TEXT_SPEED_GAP          6  // SLOW MID FAST INSTANT fit inside the window with this

u32 Gunk_OptionsChoiceWidth(u32 fontId, struct String *str, u32 row) {
    u32 width = sub_02002E14(fontId, str, 0);

    if (row == OPTIONS_TEXT_SPEED_ROW) {
        width -= OPTIONS_PACKED_GAP - TEXT_SPEED_GAP;
    }

    return width;
}
