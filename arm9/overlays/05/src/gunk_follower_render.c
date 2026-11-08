#include "global.h"

#include "gunk/gunk_follower.h"

#include "map_object.h"

// Gunk's Pokemon follower renderer (stage 1). Every follower graphics ID (0x200 + HeartGold follower index) uses
// it (overlays/05/asm/gunk_follower/renderers.inc). It is the simple billboard renderer of ov05_021F1B0C.s
// (= Platinum's ov5_021ECA70.c, which the Platinum hack's follower renderer extends) with its own update:
// - the animation follows the facing direction (HeartGold's sheets: 4 loops of 2 frames, one per direction),
// - it always animates, so the Pokemon walks in place while it stands,
// - a 2 px bob every 5 frames (as Following Platinum / the Platinum hack).

typedef struct BillboardAnim {
    u32 startFrame;
    u32 endFrame;
    u32 type; // 0 = loop, 2 = end of list
} BillboardAnim;

// the walk frame sequence (mmodel 414, HeartGold's a/0/8/1 member 292) has 20 frames per direction
const BillboardAnim GunkFollower_BillboardAnims[] = {
    { 0, 19, 0 },  // north (facing 0)
    { 20, 39, 0 }, // south
    { 40, 59, 0 }, // west
    { 60, 79, 0 }, // east
    { 0, 0, 2 },
};

extern void ov05_021F1B0C(LocalMapObject *object); // init: work = { billboard, ... }, sprite offset 0
extern void ov05_021F1B58(LocalMapObject *object); // delete
extern void ov05_021F1B60(LocalMapObject *object); // pause
extern void ov05_021F1B8C(LocalMapObject *object); // refresh
extern void ov05_021F1C48(LocalMapObject *object); // billboard position from the object + draw flag
extern BOOL ov05_021F2E28(LocalMapObject *object); // flag 22: graphics off

// billboard (arm9 asm/unk_0201F06C.s; Platinum billboard.c names)
extern void sub_0201F830(void *billboard, u16 animNum); // Billboard_SetAnimNum
extern int sub_0201F854(void *billboard, fx32 frames);  // Billboard_AdvanceAnim
extern fx32 sub_0201F880(void *billboard);              // Billboard_GetFrameNum

static void GunkFollower_RenderUpdate(LocalMapObject *object);

const UnkLMOCallbackStruct2 GunkFollower_Renderer = {
    ov05_021F1B0C,
    GunkFollower_RenderUpdate,
    ov05_021F1B58,
    ov05_021F1B60,
    ov05_021F1B8C,
};

static void GunkFollower_RenderUpdate(LocalMapObject *object) {
    void *billboard = *(void **)sub_02058628(object);
    VecFx32 offset;

    if (ov05_021F2E28(object) == TRUE || billboard == NULL) {
        return;
    }

    sub_0201F830(billboard, (u16)MapObject_GetFacingDirection(object));
    sub_0201F854(billboard, FX32_ONE);

    offset.x = 0;
    offset.y = ((sub_0201F880(billboard) / FX32_ONE) % 10 < 5) ? -FX32_CONST(2) : -0x100;
    offset.z = 0;
    sub_02058BB4(object, &offset); // the object's sprite offset (Platinum MapObject_SetSpritePosOffset)

    ov05_021F1C48(object);
}
