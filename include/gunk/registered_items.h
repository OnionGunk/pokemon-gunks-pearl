#ifndef POKEDIAMOND_GUNK_REGISTERED_ITEMS_H
#define POKEDIAMOND_GUNK_REGISTERED_ITEMS_H

#include "global.h"
#include "bag.h"

// Gunk's two registered items: Y uses the latest one, Select the one before it.
// After the field input: Select also uses a registered item; remembers which button was pressed.
void Gunk_FieldRegisteredItemInput(u16 *input, u16 pressedKeys, u32 moveState);
// The registered-item use (sub_0205DB44) asks which item to use; the message for message items asks which was used.
u32 Gunk_RegisteredItemToUse(Bag *bag);
u32 Gunk_RegisteredItemLastUsed(Bag *bag);

#endif // POKEDIAMOND_GUNK_REGISTERED_ITEMS_H
