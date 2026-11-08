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

// Gunk's Pokemon follower, stage 1. Design: C:\Projects\diamond-tools\notes\followers_plan.md (ported from the
// Platinum hack's Following Platinum port; the sprites are HeartGold's own, see tools/gunk/followers).
//
// Movement type 55 is vanilla follow-player (asm/unk_0205E4EC.s, type 48) plus:
// - it starts hidden on the player's tile and appears once it can step off it (doors: once it has walked out),
// - it waits until the player is a quarter tile into a step, so it trails a little behind,
// - catch-up: when it ends up more than one tile from where the player just was, it snaps there,
// - while the player surfs or cycles, or walks with a partner, it stays hidden on the player's tile.

#define TILE_BEHAVIOR_DOOR                0x69
#define TILE_BEHAVIOR_ESCALATOR_FLIP_FACE 0x6A
#define TILE_BEHAVIOR_ESCALATOR           0x6B

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
    u8 padding[3];
} GunkFollowerMoveData;

extern BOOL sub_0205E5B4(LocalMapObject *object, GunkFollowerMoveData *data); // TRUE once the player exists
extern BOOL sub_0205E608(LocalMapObject *object, GunkFollowerMoveData *data); // TRUE if the player left the recorded tile
extern void sub_0205E63C(LocalMapObject *object, GunkFollowerMoveData *data); // records the player's tile
extern BOOL sub_0205E698(LocalMapObject *object);                             // starts the walk to the player's previous tile
extern BOOL sub_0205E598(LocalMapObject *object, GunkFollowerMoveData *data); // ends the walk when it's done
extern void sub_02059D1C(LocalMapObject *object);                             // recomputes the height from the terrain
extern BOOL sub_0205995C_Vanilla(LocalMapObject *object, u32 x, s32 y, u32 z);
extern u8 GetMetatileBehavior(FieldSystem *fieldSystem, s32 x, s32 z);

static void GunkFollower_MoveInit(LocalMapObject *object);
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
    MapObject_SetXRange(object, -1);
    MapObject_SetYRange(object, -1);
    GunkFollower_SetFlags(object);
    MapObject_SetPositionFromXYZAndDirection(object, x, y, z, direction);

    if (arriving) {
        GunkFollower_SetArriving(object);
    }

    return object;
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
    GunkFollower_SetFlags(follower);

    // the restore clears the hidden flag; still arriving (saved in the movement data) = hidden
    if (GunkFollower_Data(follower)->arriving) {
        MapObject_SetVisible(follower, TRUE);
    }
}

void GunkFollower_FieldUpdate(FieldSystem *fieldSystem) {
    LocalMapObject *follower;
    u32 sprite;

    if (fieldSystem->mapObjectManager == NULL || fieldSystem->playerAvatar == NULL) {
        return;
    }

    follower = GunkFollower_Get(fieldSystem);
    sprite = GunkFollower_GetLeadSprite(fieldSystem);

    if (follower == NULL) {
        if (sprite != 0) {
            GunkFollower_SpawnOnPlayer(fieldSystem, sprite);
        }
        return;
    }

    if (sprite == 0) {
        MapObject_Remove(follower);
        return;
    }

    // a new lead: swap the graphics the way the script command for object graphics does
    if (MapObject_GetSpriteID(follower) != sprite && !MapObject_CheckSingleMovement(follower)) {
        sub_02057634(follower, sprite);
    }
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
    FieldSystem *fieldSystem = MapObject_GetFieldSystem(object);
    s32 state = PlayerAvatar_GetState(fieldSystem->playerAvatar);

    if (state == PLAYER_STATE_CYCLING || state == PLAYER_STATE_SURFING) {
        return TRUE;
    }

    return Save_VarsFlags_CheckHaveFollower(Save_VarsFlags_Get(fieldSystem->saveData));
}

static void GunkFollower_StayOnPlayer(LocalMapObject *object, LocalMapObject *player, GunkFollowerMoveData *data) {
    if (MapObject_GetXCoord(object) != MapObject_GetXCoord(player) || MapObject_GetZCoord(object) != MapObject_GetZCoord(player)) {
        MapObject_SetPositionFromXYZAndDirection(object, MapObject_GetXCoord(player), MapObject_GetYCoord(player), MapObject_GetZCoord(player), MapObject_GetFacingDirection(player));
        sub_02059D1C(object);
    }

    GunkFollower_SetArriving(object);
    sub_0205E63C(object, data);
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
        if (behavior == TILE_BEHAVIOR_DOOR || behavior == TILE_BEHAVIOR_ESCALATOR || behavior == TILE_BEHAVIOR_ESCALATOR_FLIP_FACE) {
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
        if (GunkFollower_IsSuppressed(object)) {
            GunkFollower_StayOnPlayer(object, player, data);
            return;
        }

        GunkFollower_UpdateArrival(object, player, data);
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
