#ifndef POKEDIAMOND_GUNK_BAG_H
#define POKEDIAMOND_GUNK_BAG_H

#include "global.h"

// Gunk's: called by the Bag (overlay 75) each frame its item list takes input. Start sorts the open pocket;
// returns TRUE when it did, so the list skips its own input this frame.
BOOL Gunk_BagTrySort(u8 *bagApp);

#endif // POKEDIAMOND_GUNK_BAG_H
