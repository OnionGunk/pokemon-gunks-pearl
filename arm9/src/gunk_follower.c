#include "global.h"

#include "gunk/gunk_follower.h"
#include "gunk/gunk_follower_species.h"
#include "gunk/gunk_dexnav_app.h"

#include "constants/global_fieldmap.h"
#include "constants/maps.h"
#include "constants/pokemon.h"
#include "constants/species.h"

#include "field_system.h"
#include "map_object.h"
#include "party.h"
#include "player_avatar.h"
#include "pokemon.h"
#include "save_vars_flags.h"
#include "unk_0205EC84.h"

#include "bg_window.h"
#include "heap.h"
#include "main.h"
#include "player_data.h"
#include "string16.h"
#include "task.h"
#include "text_02054590.h"
#include "unk_020051F4.h"
#include "PAD_pad.h"

// Gunk's Pokemon follower, stage 1. Design: C:\Projects\diamond-tools\notes\followers_plan.md (ported from the
// Platinum hack's Following Platinum port; the sprites are HeartGold's own, see tools/gunk/followers).
//
// Movement type 55 is vanilla follow-player (asm/unk_0205E4EC.s, type 48) plus:
// - it starts hidden on the player's tile and appears once it can step off it (doors: once it has walked out),
// - it waits until the player is a quarter tile into a step, so it trails a little behind,
// - catch-up: when it ends up more than one tile from where the player just was, it snaps there,
// - while the player surfs, cycles, climbs a rock wall or walks with a partner, it hides and waits where it is
//   (no Poke Ball animation yet); back on foot, it returns hidden to the player's tile and comes out as on arrival,
// - when the player steps onto a door or another warp tile, it walks onto the player's tile and hides there, so it
//   doesn't stand in front of the door during the fade (stage 2),
// - it is paused with the other objects during scripts and menus (not pausing it hung Surf), but it keeps
//   animating then (GunkFollower_ObjectDrawUpdate in gunk_follower_render.c),
// - A on it: its cry and a short line (stage 2, GunkFollower_TryTalk).
// The renderer (gunk_follower_render.c) draws a shiny lead with the texture's second palette;
// param 1 of the object holds the shiny flag.

#define TILE_BEHAVIOR_DOOR                0x69
#define TILE_BEHAVIOR_ESCALATOR_FLIP_FACE 0x6A
#define TILE_BEHAVIOR_ESCALATOR           0x6B
#define TILE_BEHAVIOR_ROCK_CLIMB_N_S      0x4B
#define TILE_BEHAVIOR_ROCK_CLIMB_E_W      0x4C

#define GUNK_FOLLOWER_PARAM_SHINY 1

#define FIELD_FRAME_TILE    994 // the field message box (as gunk_dexnav.c)
#define FIELD_FRAME_PALETTE 10
#define CHAR_SPACE_         0x01DE

// Movement data (16 bytes, saved with the object). The first 8 bytes are vanilla follow-player's
// (asm/unk_0205E4EC.s: sub_0205E5B4 and the helpers below read and write them).
typedef struct GunkFollowerMoveData {
    u8 state;      // 0 = waiting for the player to step, 1 = walking
    u8 playerSeen; // set by sub_0205E5B4 once the player's tile has been recorded
    s16 playerX;   // the player's tile when this object last started a walk
    s16 playerZ;
    u16 unk6;
    s16 spawnX;    // the tile it appeared on (hidden until it can leave it)
    s16 spawnZ;
    u8 arriving;
    u8 enteringWarp; // walking onto the player's warp tile; hides when it gets there
    u8 suppressed;   // hidden and waiting where it is (Surf, bike, Rock Climb, a partner)
    u8 padding;
} GunkFollowerMoveData;

extern BOOL sub_0205E5B4(LocalMapObject *object, GunkFollowerMoveData *data); // TRUE once the player exists
extern BOOL sub_0205E608(LocalMapObject *object, GunkFollowerMoveData *data); // TRUE if the player left the recorded tile
extern void sub_0205E63C(LocalMapObject *object, GunkFollowerMoveData *data); // records the player's tile
extern BOOL sub_0205E698(LocalMapObject *object);                             // starts the walk to the player's previous tile
extern BOOL sub_0205E598(LocalMapObject *object, GunkFollowerMoveData *data); // ends the walk when it's done
extern void sub_02059D1C(LocalMapObject *object);                             // recomputes the height from the terrain
extern BOOL sub_0205995C_Vanilla(LocalMapObject *object, u32 x, s32 y, u32 z);
extern u8 GetMetatileBehavior(FieldSystem *fieldSystem, s32 x, s32 z);
extern BOOL sub_0205481C(u8 behavior);                          // surfable water
extern u32 sub_02059E80(u32 x, u32 z, u32 targetX, u32 targetZ); // direction from a tile to another
extern u32 sub_0205E65C(LocalMapObject *object);                 // follow-player's walk speed (from the player's)
extern u32 sub_0205AFDC(u32 direction, u32 action);              // the action turned to a direction
extern void sub_0205AE0C(LocalMapObject *object, u32 action);    // starts a movement action
extern void FieldSystem_GetFacingObject(FieldSystem *fieldSystem, LocalMapObject **object);
extern void ClearFrameAndWindow2(struct Window *window, BOOL param1);

static void GunkFollower_MoveInit(LocalMapObject *object);
static BOOL GunkFollower_PlayerIsMoving(LocalMapObject *player);
static void GunkFollower_MoveUpdate(LocalMapObject *object);

const UnkLMOCallbackStruct GunkFollower_Movement = {
    3, // as follow-player (UNK_020F5F48)
    GunkFollower_MoveInit,
    GunkFollower_MoveUpdate,
    sub_02058ED0,
    sub_02058ED4,
};

// no follower on link and Underground maps
static const u16 sExcludedMaps[] = {
    MAP_UNDERGROUND,
    MAP_WIFI_SINGLE_BATTLE_AREA,
    MAP_WIFI_MULTI_BATTLE_AREA,
    MAP_RECORD,
    MAP_UNION_ROOM,
};

static BOOL GunkFollower_IsMapAllowed(u32 mapId) {
    for (int i = 0; i < NELEMS(sExcludedMaps); i++) {
        if (mapId == sExcludedMaps[i]) {
            return FALSE;
        }
    }

    return TRUE;
}

static struct Pokemon *GunkFollower_GetLead(FieldSystem *fieldSystem) {
    struct Party *party = SaveArray_Party_Get(fieldSystem->saveData);
    int count = Party_GetCount(party);
    struct Pokemon *firstHatched = NULL;

    for (int i = 0; i < count; i++) {
        struct Pokemon *mon = Party_GetMonByIndex(party, i);

        if (GetMonData(mon, MON_DATA_IS_EGG, NULL)) {
            continue;
        }

        if (GetMonData(mon, MON_DATA_HP, NULL) != 0) {
            return mon;
        }

        if (firstHatched == NULL) {
            firstHatched = mon;
        }
    }

    return firstHatched;
}

// HeartGold's FollowMon_GetSpriteID: the species' sprite, + 1 for females of species that look different,
// otherwise + form
static u32 GunkFollower_SpriteForMon(struct Pokemon *mon) {
    u32 species = GetMonData(mon, MON_DATA_SPECIES, NULL);
    u32 index;
    u8 flags;

    if (species == SPECIES_NONE || species > NATIONAL_DEX_COUNT) {
        return 0;
    }

    index = sGunkFollowerIndex[species];
    flags = sGunkFollowerFlags[species - 1];

    if (flags & 0x80) {
        if (GetMonGender(mon) == MON_FEMALE) {
            index++;
        }
    } else {
        u32 form = GetMonData(mon, MON_DATA_FORM, NULL);

        if (form <= (flags & 0x7F)) {
            index += form;
        }
    }

    if (index >= GUNK_FOLLOWER_COUNT) {
        return 0;
    }

    return GUNK_FOLLOWER_FIRST_GFX + index;
}

u32 GunkFollower_GetLeadSprite(FieldSystem *fieldSystem) {
    struct Pokemon *lead;

    if (!GunkFollower_IsMapAllowed(fieldSystem->location->mapId)) {
        return 0;
    }

    lead = GunkFollower_GetLead(fieldSystem);
    return lead == NULL ? 0 : GunkFollower_SpriteForMon(lead);
}

static BOOL GunkFollower_IsLeadShiny(FieldSystem *fieldSystem) {
    struct Pokemon *lead = GunkFollower_GetLead(fieldSystem);

    return lead != NULL && MonIsShiny(lead);
}

LocalMapObject *GunkFollower_Get(FieldSystem *fieldSystem) {
    LocalMapObject *object;

    if (fieldSystem->mapObjectManager == NULL) {
        return NULL;
    }

    object = MapObjectManager_GetFirstActiveObjectByID(fieldSystem->mapObjectManager, GUNK_FOLLOWER_LOCAL_ID);

    if (object != NULL && MapObject_GetMovement(object) != GUNK_FOLLOWER_MOVEMENT) {
        return NULL;
    }

    return object;
}

static GunkFollowerMoveData *GunkFollower_Data(LocalMapObject *object) {
    return (GunkFollowerMoveData *)sub_020585B0(object);
}

static LocalMapObject *GunkFollower_PlayerObject(LocalMapObject *object) {
    return PlayerAvatar_GetMapObject(FieldSystem_GetPlayerAvatar(MapObject_GetFieldSystem(object)));
}

// hidden on its current tile until it can step off it
static void GunkFollower_SetArriving(LocalMapObject *object) {
    GunkFollowerMoveData *data = GunkFollower_Data(object);

    data->arriving = TRUE;
    data->spawnX = (s16)MapObject_GetXCoord(object);
    data->spawnZ = (s16)MapObject_GetZCoord(object);
    MapObject_SetVisible(object, TRUE); // the "visible" flag bit hides the object
}

// flags every follower needs; the restore from the save clears some of them
static void GunkFollower_SetFlags(LocalMapObject *object) {
    // as HeartGold's FollowMon_CreateMapObject: kept across map borders, height follows the terrain
    MapObject_SetFlagsBits(object, (MapObjectFlagBits)(MAPOBJECTFLAG_UNK13 | MAPOBJECTFLAG_KEEP));
    MapObject_ClearFlagsBits(object, (MapObjectFlagBits)(MAPOBJECTFLAG_UNK8 | MAPOBJECTFLAG_UNK7));
    MapObject_SetFlag29(object, TRUE);
    // can't be talked to yet (stage 3 adds the talk script)
    MapObject_SetFlag19(object, TRUE);
}

static LocalMapObject *GunkFollower_Spawn(FieldSystem *fieldSystem, u32 sprite, u32 x, s32 y, u32 z, u32 direction, BOOL arriving) {
    LocalMapObject *object = MapObject_Create(fieldSystem->mapObjectManager, x, z, direction, sprite, GUNK_FOLLOWER_MOVEMENT, MAP_NOTHING);

    if (object == NULL) {
        return NULL;
    }

    MapObject_SetID(object, GUNK_FOLLOWER_LOCAL_ID);
    MapObject_SetType(object, 0);
    MapObject_SetEventFlag(object, 0);
    MapObject_SetScriptID(object, 0);
    MapObject_SetParam(object, 0, 2);
    MapObject_SetParam(object, GunkFollower_IsLeadShiny(fieldSystem), GUNK_FOLLOWER_PARAM_SHINY);
    MapObject_SetXRange(object, -1);
    MapObject_SetYRange(object, -1);
    GunkFollower_SetFlags(object);
    MapObject_SetPositionFromXYZAndDirection(object, x, y, z, direction);

    if (arriving) {
        GunkFollower_SetArriving(object);
    }

    return object;
}

// Surf (and Waterfall), the bike, Rock Climb, a partner walking with the player: the follower hides and waits where
// it is (ScrCmd_Surf / RockClimb / Waterfall hide it before their field task, GunkFollower_OnFieldMove) and comes
// back hidden on the player's tile once the player is on foot again (GunkFollower_UpdateSuppressed). The object
// itself stays, so nothing is created or loaded while the field runs.
static BOOL GunkFollower_IsFieldSuppressed(FieldSystem *fieldSystem, BOOL checkTile) {
    s32 state = PlayerAvatar_GetState(fieldSystem->playerAvatar);
    LocalMapObject *player;
    u8 behavior;

    if (state == PLAYER_STATE_CYCLING || state == PLAYER_STATE_SURFING) {
        return TRUE;
    }

    if (Save_VarsFlags_CheckHaveFollower(Save_VarsFlags_Get(fieldSystem->saveData))) {
        return TRUE;
    }

    if (!checkTile) {
        return FALSE;
    }

    // Rock Climb: the climb moves the player over the wall's tiles
    player = PlayerAvatar_GetMapObject(fieldSystem->playerAvatar);
    behavior = GetMetatileBehavior(fieldSystem, (s32)MapObject_GetXCoord(player), (s32)MapObject_GetZCoord(player));

    return behavior == TILE_BEHAVIOR_ROCK_CLIMB_N_S || behavior == TILE_BEHAVIOR_ROCK_CLIMB_E_W;
}

void GunkFollower_OnFieldMove(FieldSystem *fieldSystem) {
    LocalMapObject *follower = GunkFollower_Get(fieldSystem);
    GunkFollowerMoveData *data;

    if (follower == NULL) {
        return;
    }

    // hidden before the field move's movement starts (the follower is paused with the script); its movement keeps it
    // hidden and waiting where it is until the player is back on foot (GunkFollower_UpdateSuppressed)
    data = GunkFollower_Data(follower);
    data->suppressed = TRUE;
    data->arriving = FALSE;
    data->enteringWarp = FALSE;
    MapObject_SetVisible(follower, TRUE);
}

static void GunkFollower_SpawnOnPlayer(FieldSystem *fieldSystem, u32 sprite) {
    PlayerAvatar *avatar = fieldSystem->playerAvatar;
    LocalMapObject *player = PlayerAvatar_GetMapObject(avatar);

    GunkFollower_Spawn(fieldSystem, sprite, PlayerAvatar_GetXCoord(avatar), MapObject_GetYCoord(player), PlayerAvatar_GetZCoord(avatar), PlayerAvatar_GetFacingDirection(avatar), TRUE);
}

void GunkFollower_OnObjectsCreated(FieldSystem *fieldSystem) {
    LocalMapObject *follower = GunkFollower_Get(fieldSystem);
    u32 sprite = GunkFollower_GetLeadSprite(fieldSystem);

    // the field's objects exist now: the DexNav Pokétch app (loaded before the first field frame) can read the map
    GunkDexNav_SetFieldSystem(fieldSystem);

    if (follower == NULL) {
        if (sprite != 0) {
            GunkFollower_SpawnOnPlayer(fieldSystem, sprite);
        }
        return;
    }

    // restored from the save: no graphics exist yet, so the sprite can simply be swapped
    if (sprite == 0) {
        MapObject_Remove(follower);
        return;
    }

    MapObject_SetSpriteID(follower, sprite);
    MapObject_SetParam(follower, GunkFollower_IsLeadShiny(fieldSystem), GUNK_FOLLOWER_PARAM_SHINY);
    GunkFollower_SetFlags(follower);

    // the restore clears the hidden flag; still arriving (saved in the movement data) = hidden
    if (GunkFollower_Data(follower)->arriving) {
        MapObject_SetVisible(follower, TRUE);
    }
}

void GunkFollower_FieldUpdate(FieldSystem *fieldSystem) {
    // Nothing at run time: the follower object is only created, removed and given the lead's graphics while the map's
    // objects are built (GunkFollower_OnObjectsCreated: after a door or warp, a battle, a menu, loading the game), so
    // no graphics are loaded while the field runs. Surf, the bike and Rock Climb hide it instead of removing it.
#pragma unused(fieldSystem)
}

// ---- movement type 55

static void GunkFollower_MoveInit(LocalMapObject *object) {
    GunkFollowerMoveData *data = (GunkFollowerMoveData *)sub_0205858C(object, sizeof(GunkFollowerMoveData));

    // as follow-player's init (sub_0205E4EC)
    sub_0205E5B4(object, data);
    sub_02058544(object, 0);
    MapObject_ClearSingleMovement(object);
    MapObject_ClearFlag18(object, FALSE); // sets flag 18: others don't collide with it
}

// the player is between tiles (its position isn't its tile's centre)
static BOOL GunkFollower_PlayerIsMoving(LocalMapObject *player) {
    VecFx32 pos;

    MapObject_CopyPositionVector(player, &pos);
    return pos.x != (fx32)(MapObject_GetXCoord(player) * FX32_CONST(16) + FX32_CONST(8))
        || pos.z != (fx32)(MapObject_GetZCoord(player) * FX32_CONST(16) + FX32_CONST(8));
}

// the player is at least a quarter tile into the step from its previous tile
static BOOL GunkFollower_PlayerStepStarted(LocalMapObject *player) {
    VecFx32 pos;
    fx32 dx, dz;

    MapObject_CopyPositionVector(player, &pos);
    dx = pos.x - (fx32)(MapObject_GetPreviousXCoord(player) * FX32_CONST(16) + FX32_CONST(8));
    dz = pos.z - (fx32)(MapObject_GetPreviousZCoord(player) * FX32_CONST(16) + FX32_CONST(8));

    if (dx < 0) {
        dx = -dx;
    }

    if (dz < 0) {
        dz = -dz;
    }

    return dx + dz >= FX32_CONST(4);
}

// more than one tile from the player's previous tile (warps, ledges, scripted moves): snap there
static void GunkFollower_CatchUp(LocalMapObject *object, LocalMapObject *player) {
    s32 dx = (s32)MapObject_GetXCoord(object) - (s32)MapObject_GetPreviousXCoord(player);
    s32 dz = (s32)MapObject_GetZCoord(object) - (s32)MapObject_GetPreviousZCoord(player);

    if (dx < 0) {
        dx = -dx;
    }

    if (dz < 0) {
        dz = -dz;
    }

    if (dx + dz > 1) {
        MapObject_SetPositionFromXYZAndDirection(object, MapObject_GetPreviousXCoord(player), MapObject_GetPreviousYCoord(player), MapObject_GetPreviousZCoord(player), MapObject_GetFacingDirection(object));
        sub_02059D1C(object);
    }
}

// while surfing, cycling or walking with a partner it waits hidden on the player's tile
static BOOL GunkFollower_IsSuppressed(LocalMapObject *object) {
    return GunkFollower_IsFieldSuppressed(MapObject_GetFieldSystem(object), TRUE);
}

// Surf, bike, Rock Climb, a partner: it hides and waits where it is (it doesn't move at all, so it never shares the
// player's tile or the Surf blob's while the player hops on or off), as the Platinum hack's "recall": once the player
// is back on foot and has finished its step, it comes back hidden on the player's tile and appears as on arrival.
// (Stage 2 first moved it onto the player's tile every frame; that hung Surf.)
static BOOL GunkFollower_UpdateSuppressed(LocalMapObject *object, LocalMapObject *player, GunkFollowerMoveData *data) {
    if (GunkFollower_IsSuppressed(object)) {
        if (!data->suppressed) {
            data->suppressed = TRUE;
            data->arriving = FALSE;
            data->enteringWarp = FALSE;
        }

        MapObject_SetVisible(object, TRUE);
        return TRUE;
    }

    if (!data->suppressed) {
        return FALSE;
    }

    if (GunkFollower_PlayerIsMoving(player)) {
        return TRUE; // the landing hop or the last step isn't finished yet
    }

    data->suppressed = FALSE;
    MapObject_SetPositionFromXYZAndDirection(object, MapObject_GetXCoord(player), MapObject_GetYCoord(player), MapObject_GetZCoord(player), MapObject_GetFacingDirection(player));
    sub_02059D1C(object);
    GunkFollower_SetArriving(object);
    sub_0205E63C(object, data);
    return TRUE;
}

// warp tiles: stairs, building exits, warp panel, door, escalators, warps
static BOOL GunkFollower_IsWarpTile(u8 behavior) {
    return behavior == 0x5E || behavior == 0x5F || (behavior >= 0x62 && behavior <= 0x65) || behavior == 0x67
        || behavior == TILE_BEHAVIOR_DOOR || behavior == TILE_BEHAVIOR_ESCALATOR_FLIP_FACE || behavior == TILE_BEHAVIOR_ESCALATOR
        || (behavior >= 0x6C && behavior <= 0x6F);
}

// tiles it may not be seen on: it only appears once it has walked off them (doors, water after landing, rock walls)
static BOOL GunkFollower_MustWalkOff(u8 behavior) {
    return GunkFollower_IsWarpTile(behavior) || sub_0205481C(behavior)
        || behavior == TILE_BEHAVIOR_ROCK_CLIMB_N_S || behavior == TILE_BEHAVIOR_ROCK_CLIMB_E_W;
}

// The player stepped onto a door or another warp tile: walk onto it after them and hide there (the fade follows).
// Returns TRUE when it took over this frame's movement.
static BOOL GunkFollower_UpdateWarpEntry(LocalMapObject *object, LocalMapObject *player, GunkFollowerMoveData *data) {
    u32 x = MapObject_GetXCoord(object);
    u32 z = MapObject_GetZCoord(object);
    u32 playerX = MapObject_GetXCoord(player);
    u32 playerZ = MapObject_GetZCoord(player);
    s32 dx, dz;

    if (data->enteringWarp) {
        // the walk onto the warp tile is done
        data->enteringWarp = FALSE;
        GunkFollower_SetArriving(object);
        sub_0205E63C(object, data);
        return TRUE;
    }

    if (data->arriving || (x == playerX && z == playerZ)) {
        return FALSE;
    }

    if (!GunkFollower_IsWarpTile(GetMetatileBehavior(MapObject_GetFieldSystem(object), (s32)playerX, (s32)playerZ))) {
        return FALSE;
    }

    dx = (s32)x - (s32)playerX;
    dz = (s32)z - (s32)playerZ;

    if (dx < 0) {
        dx = -dx;
    }

    if (dz < 0) {
        dz = -dz;
    }

    if (dx + dz == 1 && !GunkFollower_PlayerIsMoving(player)) {
        sub_0205AE0C(object, sub_0205AFDC(sub_02059E80(x, z, playerX, playerZ), sub_0205E65C(object)));
        MapObject_SetSingleMovement(object);
        data->state = 1;
        data->enteringWarp = TRUE;
        sub_0205E63C(object, data);
        return TRUE;
    }

    if (dx + dz > 1) {
        // too far behind to walk in before the fade: wait hidden on the warp tile
        MapObject_SetPositionFromXYZAndDirection(object, playerX, MapObject_GetYCoord(player), playerZ, MapObject_GetFacingDirection(object));
        sub_02059D1C(object);
        GunkFollower_SetArriving(object);
        sub_0205E63C(object, data);
        return TRUE;
    }

    return FALSE; // the player's step onto the warp tile isn't finished yet: follow normally
}

// appears once the player has stepped off the arrival tile; on a door or escalator only once it has walked out
static void GunkFollower_UpdateArrival(LocalMapObject *object, LocalMapObject *player, GunkFollowerMoveData *data) {
    FieldSystem *fieldSystem;
    u8 behavior;

    if (!data->arriving) {
        return;
    }

    fieldSystem = MapObject_GetFieldSystem(object);
    behavior = GetMetatileBehavior(fieldSystem, data->spawnX, data->spawnZ);

    if (MapObject_GetXCoord(object) == (u32)data->spawnX && MapObject_GetZCoord(object) == (u32)data->spawnZ) {
        if (GunkFollower_MustWalkOff(behavior)) {
            return;
        }

        if (MapObject_GetXCoord(player) == (u32)data->spawnX && MapObject_GetZCoord(player) == (u32)data->spawnZ) {
            return;
        }
    }

    data->arriving = FALSE;
    MapObject_SetVisible(object, FALSE);
}

static BOOL GunkFollower_StepState(LocalMapObject *object, LocalMapObject *player, GunkFollowerMoveData *data) {
    MapObject_ClearSingleMovement(object);
    MapObject_ClearFlag3(object);

    if (sub_0205E608(object, data) == TRUE && GunkFollower_PlayerStepStarted(player)) {
        sub_0205E63C(object, data);
        GunkFollower_CatchUp(object, player);

        if (sub_0205E698(object) == TRUE) {
            MapObject_SetSingleMovement(object);
            data->state++;
            return TRUE;
        }
    }

    return FALSE;
}

static void GunkFollower_MoveUpdate(LocalMapObject *object) {
    GunkFollowerMoveData *data = GunkFollower_Data(object);
    LocalMapObject *player;


    if (sub_0205E5B4(object, data) == FALSE) {
        return;
    }

    MapObject_ClearFlag18(object, FALSE);
    player = GunkFollower_PlayerObject(object);

    if (data->state == 0) {
        if (GunkFollower_UpdateSuppressed(object, player, data)) {
            return;
        }

        if (GunkFollower_UpdateWarpEntry(object, player, data)) {
            if (data->state == 0) {
                return;
            }
        } else {
            GunkFollower_UpdateArrival(object, player, data);
        }
    }

    for (;;) {
        BOOL again;

        if (data->state == 0) {
            again = GunkFollower_StepState(object, player, data);
        } else {
            again = sub_0205E598(object, data);
        }

        if (!again) {
            break;
        }
    }
}

// ---- while it is paused with the other objects (scripts, menus, field tasks such as a door's)

// Called every frame its graphics are updated while it is paused (gunk_follower_render.c). When the player has
// stepped onto a door or another warp tile during the pause (the door field task moves the player itself), it
// hides on the player's tile, so it isn't seen in front of the door during the fade.
void GunkFollower_WhilePaused(LocalMapObject *object) {
    GunkFollowerMoveData *data = GunkFollower_Data(object);
    LocalMapObject *player = GunkFollower_PlayerObject(object);
    u32 playerX = MapObject_GetXCoord(player);
    u32 playerZ = MapObject_GetZCoord(player);

    if (data->arriving || data->suppressed || data->state != 0 || MapObject_CheckVisible(object)) {
        return;
    }

    if (MapObject_GetXCoord(object) == playerX && MapObject_GetZCoord(object) == playerZ) {
        return;
    }

    if (!GunkFollower_IsWarpTile(GetMetatileBehavior(MapObject_GetFieldSystem(object), (s32)playerX, (s32)playerZ))) {
        return;
    }

    MapObject_SetPositionFromXYZAndDirection(object, playerX, MapObject_GetYCoord(player), playerZ, MapObject_GetFacingDirection(object));
    sub_02059D1C(object);
    GunkFollower_SetArriving(object);
    sub_0205E63C(object, data);
}

// ---- collision: the player walks through the follower

BOOL sub_0205995C(LocalMapObject *object, u32 x, s32 y, u32 z) {
    FieldSystem *fieldSystem = MapObject_GetFieldSystem(object);
    LocalMapObject *follower;
    BOOL wasSkipped;
    BOOL ret;

    if (fieldSystem == NULL || fieldSystem->playerAvatar == NULL || PlayerAvatar_GetMapObject(fieldSystem->playerAvatar) != object) {
        return sub_0205995C_Vanilla(object, x, y, z);
    }

    follower = GunkFollower_Get(fieldSystem);

    if (follower == NULL) {
        return sub_0205995C_Vanilla(object, x, y, z);
    }

    // the vanilla check skips objects with flag 18 set
    wasSkipped = MapObject_TestFlagsBits(follower, MAPOBJECTFLAG_UNK18);
    MapObject_SetFlagsBits(follower, MAPOBJECTFLAG_UNK18);
    ret = sub_0205995C_Vanilla(object, x, y, z);

    if (!wasSkipped) {
        MapObject_ClearFlagsBits(follower, MAPOBJECTFLAG_UNK18);
    }

    return ret;
}

// ---- talking to the follower (stage 2): its cry and a short line

typedef struct GunkFollowerTalk {
    struct String *string;
    u8 printer;
} GunkFollowerTalk;

static Window sTalkWindow;

static void GunkFollower_AppendAscii(struct String *dst, const char *src) {
    for (; *src != '\0'; src++) {
        char c = *src;
        u16 code;

        if (c >= 'A' && c <= 'Z') {
            code = (u16)(0x012B + (c - 'A'));
        } else if (c >= 'a' && c <= 'z') {
            code = (u16)(0x0145 + (c - 'a'));
        } else if (c == '.') {
            code = 0x01AE;
        } else if (c == '!') {
            code = 0x01AB;
        } else if (c == '\'') {
            code = 0x01B3;
        } else {
            code = CHAR_SPACE_;
        }

        StrAddChar(dst, code);
    }
}

static BOOL Task_GunkFollowerTalk(TaskManager *taskManager) {
    FieldSystem *fieldSystem = TaskManager_GetFieldSystem(taskManager);
    GunkFollowerTalk *talk = TaskManager_GetEnvironment(taskManager);
    u32 *state = TaskManager_GetStatePtr(taskManager);

    switch (*state) {
    case 0:
        MapObjectManager_PauseAllMovement(fieldSystem->mapObjectManager); // the follower itself keeps animating
        sub_020545B8(fieldSystem->bgConfig, &sTalkWindow, 3);
        sub_02054608(&sTalkWindow, Save_PlayerData_GetOptionsAddr(fieldSystem->saveData));
        talk->printer = (u8)sub_02054658(&sTalkWindow, talk->string, Save_PlayerData_GetOptionsAddr(fieldSystem->saveData), 1);
        (*state)++;
        break;
    case 1:
        if (sub_020546C8(talk->printer)) {
            (*state)++;
        }
        break;
    case 2:
        if (gSystem.newKeys & (PAD_BUTTON_A | PAD_BUTTON_B)) {
            ClearFrameAndWindow2(&sTalkWindow, FALSE);
            RemoveWindow(&sTalkWindow);
            String_Delete(talk->string);
            Heap_Free(talk);
            MapObjectManager_UnpauseAllMovement(fieldSystem->mapObjectManager);
            return TRUE;
        }
        break;
    }

    return FALSE;
}

BOOL GunkFollower_TryTalk(void *input, FieldSystem *fieldSystem) {
    LocalMapObject *follower;
    LocalMapObject *facing = NULL;
    struct Pokemon *lead;
    GunkFollowerTalk *talk;
    u32 friendship;

    // FieldInput's first halfword, bit 0: A was pressed and the player may interact (as the vanilla talk check)
    if ((*(u16 *)input & 1) == 0) {
        return FALSE;
    }

    follower = GunkFollower_Get(fieldSystem);

    if (follower == NULL || MapObject_CheckVisible(follower)) {
        return FALSE;
    }

    FieldSystem_GetFacingObject(fieldSystem, &facing);

    if (facing != follower) {
        return FALSE;
    }

    lead = GunkFollower_GetLead(fieldSystem);

    if (lead == NULL) {
        return FALSE;
    }

    // face the player (directions 0 N, 1 S, 2 W, 3 E: the opposite is dir ^ 1)
    MapObject_SetFacingDirection(follower, PlayerAvatar_GetFacingDirection(fieldSystem->playerAvatar) ^ 1);
    sub_02005578((u16)GetMonData(lead, MON_DATA_SPECIES, NULL));

    talk = Heap_Alloc(HEAP_ID_FIELD, sizeof(GunkFollowerTalk));
    talk->printer = 0;
    talk->string = String_New(128, HEAP_ID_FIELD);
    GetMonData(lead, MON_DATA_NICKNAME_STRING, talk->string);
    friendship = GetMonData(lead, MON_DATA_FRIENDSHIP, NULL);

    if (friendship >= 200) {
        GunkFollower_AppendAscii(talk->string, " is overjoyed to be with you!");
    } else if (friendship >= 100) {
        GunkFollower_AppendAscii(talk->string, " is happy to be with you!");
    } else {
        GunkFollower_AppendAscii(talk->string, " is walking along with you.");
    }

    FieldSystem_CreateTask(fieldSystem, Task_GunkFollowerTalk, talk);
    return TRUE;
}
