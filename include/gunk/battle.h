#ifndef POKEDIAMOND_GUNK_BATTLE_H
#define POKEDIAMOND_GUNK_BATTLE_H

#include "global.h"

// Gunk's: overlay 11's healthbox calls this instead of its gauge stepper (ov11_02257DE8), same arguments.
s32 Gunk_HealthboxUpdateGauge(s32 max, s32 cur, s32 diff, s32 *temp, u8 size, u16 fillOffset);

#endif // POKEDIAMOND_GUNK_BATTLE_H
