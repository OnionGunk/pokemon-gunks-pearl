#ifndef POKEDIAMOND_GUNK_REGISTERED_ITEMS_H
#define POKEDIAMOND_GUNK_REGISTERED_ITEMS_H

#include "global.h"
#include "bag.h"

// Gunk's two registered items: Y uses the latest one, Select the one before it.
// Field input (overlay 5) asks whether this frame's new keys use a registered item, and remembers which button.
BOOL Gunk_FieldRegisteredItemButton(u32 newKeys);
// The registered-item use (sub_0205DB44) asks which item to use; the message for message items asks which was used.
u32 Gunk_RegisteredItemToUse(Bag *bag);
u32 Gunk_RegisteredItemLastUsed(Bag *bag);

#endif // POKEDIAMOND_GUNK_REGISTERED_ITEMS_H
