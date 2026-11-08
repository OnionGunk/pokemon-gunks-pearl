#include "global.h"

#include "gunk/battle.h"

// Gunk's: faster HP and EXP bars in battle (as in Gunk's Platinum).
//
// Overlay 11's healthbox steps its gauges once per frame with ov11_02257DE8 (pokeplatinum's UpdateGauge):
// the HP bar moved 1 HP per frame, so big HP pools drained for seconds, and the EXP bar crawled. The healthbox now
// calls this wrapper instead:
// - HP moves at least one pixel per frame whatever the max HP: a full 48-pixel bar drains in 48 frames.
// - EXP takes two steps per frame, twice the vanilla speed.

#define HEALTHBOX_HP_CELLS  6 // 8 pixels each
#define HEALTHBOX_EXP_CELLS 11
#define HP_BAR_PIXELS       (HEALTHBOX_HP_CELLS * 8)
#define GAUGE_DONE          (-1)

extern s32 ov11_02257DE8(s32 max, s32 cur, s32 diff, s32 *temp, u8 size, u16 fillOffset);

s32 Gunk_HealthboxUpdateGauge(s32 max, s32 cur, s32 diff, s32 *temp, u8 size, u16 fillOffset) {
    s32 result;

    if (size == HEALTHBOX_HP_CELLS) {
        s32 perPixel = max / HP_BAR_PIXELS;

        if (perPixel > fillOffset) {
            fillOffset = perPixel;
        }

        return ov11_02257DE8(max, cur, diff, temp, size, fillOffset);
    }

    result = ov11_02257DE8(max, cur, diff, temp, size, fillOffset);

    if (result != GAUGE_DONE) {
        result = ov11_02257DE8(max, cur, diff, temp, size, fillOffset);
    }

    return result;
}
