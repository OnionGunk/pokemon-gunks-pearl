#include "global.h"

#include "PAD_pad.h"
#include "itemtool.h"
#include "list_menu.h"
#include "list_menu_items.h"
#include "main.h"
#include "unk_020051F4.h"
#include "gunk/bag.h"

// Gunk's: Start in the Bag sorts the open pocket by item number (empty slots last), as in Gunk's Platinum. The list
// is then rebuilt the way the Bag rebuilds it after tossing an item (ov75_021E92BC).

#define BAG_APP_VIEW       0x0C4 // BagView *
#define BAG_APP_LIST_MENU  0x15C // struct ListMenu *
#define BAG_APP_LIST_ITEMS 0x160 // struct ListMenuItem *
#define VIEW_POCKET_INDEX  0x64  // u8: open pocket
#define VIEW_POCKETS       0x04  // 0xC bytes each: slots, cursor pos, scroll, type, count
#define POCKET_SIZE        0x0C
#define POCKET_SLOTS       0x0  // ItemSlot *
#define POCKET_LIST_POS    0x4  // u16
#define POCKET_LIST_TOP    0x6  // u16
#define POCKET_COUNT       0x9  // u8: number of slots
#define LIST_VISIBLE_ROWS  9
#define SE_SORT            0x5DC

extern void ov75_021E7580(u8 *bagApp);                            // fill the list items for the open pocket
extern void ov75_021E7758(u16 *top, u16 *pos, u8 count);          // clamp the list position
extern void ov75_021E7794(u16 *top, u16 *pos, u8 count, u8 rows); // clamp the scroll
extern void ov75_021E77E8(u8 *bagApp, u16 top, u16 pos);          // create the list menu

static void SortSlots(ItemSlot *slots, u32 count) {
    for (u32 i = 0; i + 1 < count; i++) {
        for (u32 j = i + 1; j < count; j++) {
            BOOL swap = slots[i].quantity == 0 ? slots[j].quantity != 0
                                                : (slots[j].quantity != 0 && slots[j].id < slots[i].id);
            if (swap) {
                ItemSlot tmp = slots[i];
                slots[i] = slots[j];
                slots[j] = tmp;
            }
        }
    }
}

BOOL Gunk_BagTrySort(u8 *bagApp) {
    if (!(gSystem.newKeys & PAD_BUTTON_START)) {
        return FALSE;
    }

    u8 *view = *(u8 **)(bagApp + BAG_APP_VIEW);
    u8 *pocket = view + VIEW_POCKETS + POCKET_SIZE * view[VIEW_POCKET_INDEX];
    u16 *top = (u16 *)(pocket + POCKET_LIST_TOP);
    u16 *pos = (u16 *)(pocket + POCKET_LIST_POS);
    u8 count = pocket[POCKET_COUNT];

    SortSlots(*(ItemSlot **)(pocket + POCKET_SLOTS), count);

    DestroyListMenu(*(struct ListMenu **)(bagApp + BAG_APP_LIST_MENU), top, pos);
    ListMenuItems_Delete(*(struct ListMenuItem **)(bagApp + BAG_APP_LIST_ITEMS));
    ov75_021E7580(bagApp);
    ov75_021E7758(top, pos, count);
    ov75_021E7794(top, pos, count, LIST_VISIBLE_ROWS);
    ov75_021E77E8(bagApp, *top, *pos);
    PlaySE(SE_SORT);
    return TRUE;
}
