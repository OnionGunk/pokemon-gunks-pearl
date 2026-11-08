#include "global.h"

#include "constants/heap.h"
#include "constants/items.h"
#include "bag.h"
#include "itemtool.h"
#include "party.h"
#include "pokemon.h"
#include "gunk/field_moves.h"

#define HEAP_ID_FIELD_MOVES HEAP_ID_FIELD

u16 Gunk_GetOwnedHMForMove(Bag *bag, u16 move) {
    for (u16 item = ITEM_HM01; item <= ITEM_HM08; item++) {
        if (TMHMGetMove(item) == move) {
            return Bag_GetQuantity(bag, item, HEAP_ID_FIELD_MOVES) > 0 ? item : ITEM_NONE;
        }
    }

    return ITEM_NONE;
}

BOOL Gunk_MonKnowsMove(struct Pokemon *mon, u16 move) {
    return GetMonData(mon, MON_DATA_MOVE1, NULL) == move
        || GetMonData(mon, MON_DATA_MOVE2, NULL) == move
        || GetMonData(mon, MON_DATA_MOVE3, NULL) == move
        || GetMonData(mon, MON_DATA_MOVE4, NULL) == move;
}

BOOL Gunk_MonCanUseFieldMove(struct Pokemon *mon, Bag *bag, u16 move) {
    if (GetMonData(mon, MON_DATA_IS_EGG, NULL)) {
        return FALSE;
    }

    if (Gunk_MonKnowsMove(mon, move)) {
        return TRUE;
    }

    u16 hm = Gunk_GetOwnedHMForMove(bag, move);
    return hm != ITEM_NONE && sub_0206A13C(mon, ItemToTMHMId(hm));
}

int Gunk_FindPartyMonForFieldMove(struct Party *party, Bag *bag, u16 move) {
    int count = Party_GetCount(party);

    for (int i = 0; i < count; i++) {
        struct Pokemon *mon = Party_GetMonByIndex(party, i);
        if (!GetMonData(mon, MON_DATA_IS_EGG, NULL) && Gunk_MonKnowsMove(mon, move)) {
            return i;
        }
    }

    for (int i = 0; i < count; i++) {
        if (Gunk_MonCanUseFieldMove(Party_GetMonByIndex(party, i), bag, move)) {
            return i;
        }
    }

    return 0xFF;
}

// Party menu (sub_0207117C builds the context menu): after the field moves the Pokémon knows, add the HM moves it
// could learn from an HM the player owns, while there is room (4 field moves). Returns the new entry count.
#define PARTY_APP_ARGS        0x5A4 // args: [0] party, [1] bag
#define PARTY_APP_SELECTED    0xB25 // u8 slot
#define FIELD_MOVES_MAX       4
#define NO_FIELD_MOVE_ENTRY   0xFF

extern u8 sub_02071948(u16 move);                   // the context-menu entry for a field move, 0xFF if none
extern void sub_020727FC(u8 *app, u16 move, u8 idx); // remember the field move behind that entry

u8 Gunk_PartyMenuAddHMMoves(u8 *app, u8 *entries, u8 count, u8 fieldMoves) {
    void **args = *(void ***)(app + PARTY_APP_ARGS);
    struct Pokemon *mon = Party_GetMonByIndex((struct Party *)args[0], app[PARTY_APP_SELECTED]);
    Bag *bag = (Bag *)args[1];

    for (u16 item = ITEM_HM01; item <= ITEM_HM08 && fieldMoves < FIELD_MOVES_MAX; item++) {
        u16 move = TMHMGetMove(item);
        u8 entry = sub_02071948(move);

        if (entry == NO_FIELD_MOVE_ENTRY || Gunk_MonKnowsMove(mon, move) || !Gunk_MonCanUseFieldMove(mon, bag, move)) {
            continue;
        }

        entries[count++] = entry;
        sub_020727FC(app, move, fieldMoves++);
    }

    return count;
}
