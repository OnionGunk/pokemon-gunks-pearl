#ifndef POKEDIAMOND_GUNK_FIELD_MOVES_H
#define POKEDIAMOND_GUNK_FIELD_MOVES_H

#include "global.h"
#include "bag.h"
#include "pokemon.h"

// Gunk's: HM field moves without teaching them. A Pokémon can use a field move if it knows it, or if it could learn
// it from an HM the player owns (the badge checks are unchanged).
u16 Gunk_GetOwnedHMForMove(Bag *bag, u16 move);
BOOL Gunk_MonKnowsMove(struct Pokemon *mon, u16 move);
BOOL Gunk_MonCanUseFieldMove(struct Pokemon *mon, Bag *bag, u16 move);
// First party slot that knows the move, else the first that can use it through an owned HM; 0xFF if none.
int Gunk_FindPartyMonForFieldMove(struct Party *party, Bag *bag, u16 move);

// Party menu context menu: appends usable HM moves; returns the new entry count.
u8 Gunk_PartyMenuAddHMMoves(u8 *app, u8 *entries, u8 count, u8 fieldMoves);

#endif // POKEDIAMOND_GUNK_FIELD_MOVES_H
