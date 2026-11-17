#include "global.h"

#include "gunk/gunk_follower.h"

#include "map_object.h"

// Gunk's Pokemon follower renderer. Every follower graphics ID (0x200 + HeartGold follower index) uses it
// (asm/gunk_follower/renderers.inc). It lives in the arm9 static module: overlay 5 must keep its exact size. It is the
// NPC billboard renderer of ov05_021F0514.s (ov05_021F8C68) with its own update:
// - the animation follows the facing direction (HeartGold's sheets: 4 loops of 2 frames, one per direction),
// - it always animates, so the Pokemon walks in place while it stands,
// - a 2 px bob every 5 frames (as Following Platinum / the Platinum hack),
// - shiny (object param 1, set by gunk_follower.c): every frame of the sequence uses palette 1 (tsure_poke1, HG's
//   shiny palette) instead of 0 (as the Platinum hack: the billboard's frame sequence palette indexes).

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

// the emote bubbles' frame sequence (mmodel 981, HeartGold's a/1/0/3 member 150): its two textures, 4 frames each
const BillboardAnim GunkFollower_EmoteAnims[] = {
    { 0, 15, 0 },
    { 0, 0, 2 },
};

// The NPC billboard renderer's own functions (ov05_021F8C68): its work keeps the billboard at +8, where every other
// reader looks for an object's billboard (ov05_021F0514, used by the reflection over water and others). Stage 1-2
// used the simple renderer (billboard at +0); the reflection then read garbage and the game hung next to water
// (the "can't move after landing from Surf" freeze), as the Platinum hack's ov5_021EB1A0 fix describes.
extern void ov05_021F05C4(LocalMapObject *object); // init: work = { u16 last direction, ..., +8 billboard, +0xC saved }
extern void ov05_021F05F0(LocalMapObject *object); // delete
extern void ov05_021F0604(LocalMapObject *object); // pause
extern void ov05_021F0634(LocalMapObject *object); // refresh
extern void ov05_021F2F48(LocalMapObject *object, void *billboard); // billboard position from the object
extern void ov05_021F19A4(LocalMapObject *object, void *billboard); // draw flag (hidden objects aren't drawn)
extern BOOL ov05_021F2E28(LocalMapObject *object); // flag 22: graphics off

// billboard (arm9 asm/unk_0201F06C.s; Platinum billboard.c names)
extern void sub_0201F830(void *billboard, u16 animNum); // Billboard_SetAnimNum
extern int sub_0201F854(void *billboard, fx32 frames);  // Billboard_AdvanceAnim
extern fx32 sub_0201F880(void *billboard);              // Billboard_GetFrameNum

static void GunkFollower_RenderUpdate(LocalMapObject *object);

// Billboard frame sequence (Platinum BillboardGfxSequence) at +0x94: startFrame, textureIdx, plttIdx (+0x9C),
// seqCount (+0xA0); the per-frame indexes are read by sub_02022244 when the billboard is drawn
#define BILLBOARD_PLTT_IDX(billboard)  (*(const u8 **)((u8 *)(billboard) + 0x9C))
#define BILLBOARD_SEQ_COUNT(billboard) (*(u32 *)((u8 *)(billboard) + 0xA0))

static const u8 sShinyPlttIdx[16] = { 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1 };

const UnkLMOCallbackStruct2 GunkFollower_Renderer = {
    ov05_021F05C4,
    GunkFollower_RenderUpdate,
    ov05_021F05F0,
    ov05_021F0604,
    ov05_021F0634,
};

static void GunkFollower_RenderUpdate(LocalMapObject *object) {
    void *billboard = *(void **)(sub_02058628(object) + 8);
    VecFx32 offset;

    if (ov05_021F2E28(object) == TRUE || billboard == NULL) {
        return;
    }

    if (MapObject_GetParam(object, 1) && BILLBOARD_SEQ_COUNT(billboard) <= NELEMS(sShinyPlttIdx)) {
        BILLBOARD_PLTT_IDX(billboard) = sShinyPlttIdx;
    }

    sub_0201F830(billboard, (u16)MapObject_GetFacingDirection(object));
    sub_0201F854(billboard, FX32_ONE);

    offset.x = 0;
    offset.y = ((sub_0201F880(billboard) / FX32_ONE) % 10 < 5) ? -FX32_CONST(2) : -0x100;
    offset.z = 0;
    sub_02058BB4(object, &offset); // the object's sprite offset (Platinum MapObject_SetSpritePosOffset)

    ov05_021F2F48(object, billboard);
    ov05_021F19A4(object, billboard);
}

// ---- the per-frame graphics update of every object: ov05_021F1D8C's callers (map_object.c sub_02058348,
// ov05_021E7504.s) call this instead
// Vanilla skips paused objects; the follower is paused with everyone in scripts and menus (not pausing it hung
// Surf), but its graphics keep updating, so it keeps walking in place and bobbing.

extern void ov05_021F1D8C(LocalMapObject *object);
void GunkFollower_ObjectDrawUpdate(LocalMapObject *object);

void GunkFollower_ObjectDrawUpdate(LocalMapObject *object) {
    if (MapObject_GetID(object) == GUNK_FOLLOWER_LOCAL_ID && MapObject_GetMovement(object) == GUNK_FOLLOWER_MOVEMENT
        && MapObject_CheckMovementPaused(object)
        && MapObjectManager_GetFlagsBitsMask(MapObject_GetManager(object), MAPOBJECTMANAGERFLAG_UNK2) == 0
        && MapObject_CheckFlag14(object)) {
        GunkFollower_WhilePaused(object);
        sub_020586A0(object);
        return;
    }

    ov05_021F1D8C(object);
}
