#include "global.h"

#include "PAD_pad.h"
#include "bag.h"
#include "gunk/registered_items.h"

// Gunk's two registered items (see Bag_SetRegisteredItem): Y uses the latest, Select the one registered before it.

static BOOL sUseSecond;
static u16 sLastUsed;

#define FIELD_INPUT_REGISTERED_ITEM 0x8 // the field input bit Y sets (ov05_021D8164)

// After the vanilla field input (ov05_021D8164) has run: Select also uses a registered item, under the same
// condition as Y (standing still or a step just finished). Overlay 5 itself is left byte-identical: growing it
// shifts the overlays placed after it and breaks the catch screens.
void Gunk_FieldRegisteredItemInput(u16 *input, u16 pressedKeys, u32 moveState) {
    if (moveState != 0 && moveState != 3) {
        return;
    }

    if (pressedKeys & PAD_BUTTON_Y) {
        sUseSecond = FALSE;
    } else if (pressedKeys & PAD_BUTTON_SELECT) {
        sUseSecond = TRUE;
        *input |= FIELD_INPUT_REGISTERED_ITEM;
    }
}

u32 Gunk_RegisteredItemToUse(Bag *bag) {
    sLastUsed = sUseSecond ? Bag_GetSecondRegisteredItem(bag) : Bag_GetRegisteredItem(bag);
    sUseSecond = FALSE;
    return sLastUsed;
}

u32 Gunk_RegisteredItemLastUsed(Bag *bag) {
#pragma unused(bag)
    return sLastUsed;
}
