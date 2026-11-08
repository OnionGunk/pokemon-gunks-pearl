#include "global.h"

#include "PAD_pad.h"
#include "bag.h"
#include "gunk/registered_items.h"

// Gunk's two registered items (see Bag_SetRegisteredItem): Y uses the latest, Select the one registered before it.

static BOOL sUseSecond;
static u16 sLastUsed;

BOOL Gunk_FieldRegisteredItemButton(u32 newKeys) {
    if (newKeys & PAD_BUTTON_Y) {
        sUseSecond = FALSE;
        return TRUE;
    }

    if (newKeys & PAD_BUTTON_SELECT) {
        sUseSecond = TRUE;
        return TRUE;
    }

    return FALSE;
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
