#ifndef POKEDIAMOND_GUNK_OPTIONS_H
#define POKEDIAMOND_GUNK_OPTIONS_H

#include "global.h"

// Gunk's: called by overlay 60 after it loads the Options rows; adds INSTANT to the TEXT SPEED row.
void Gunk_OptionsAddInstant(u8 *menu);

// Gunk's: width of a packed Options choice for spacing (overlay 60 adds its 12 px gap after this). TEXT SPEED's
// four choices only fit with a narrower gap.
u32 Gunk_OptionsChoiceWidth(u32 fontId, struct String *str, u32 row);

#endif // POKEDIAMOND_GUNK_OPTIONS_H
