#ifndef POKEDIAMOND_GUNK_FOLLOWER_H
#define POKEDIAMOND_GUNK_FOLLOWER_H

#include "global.h"
#include "field_system.h"
#include "map_object.h"
#include "battle_setup.h"

// Gunk's Pokemon follower, stage 1 (C:\Projects\diamond-tools\notes\followers_plan.md):
// the party lead walks behind the player, drawn with HeartGold's overworld sprite of that Pokemon.
// - One ordinary map object: local ID 0xFD, movement type 55 (GunkFollower_Movement), graphics ID
//   0x200 + HeartGold follower index (arm9 copies of overlay 5's graphics tables in asm/gunk_follower_tables.s, rows from gen_tables.py).
// - It is a map object like any other, so it is saved and restored with the map's objects (no save change).

#define GUNK_FOLLOWER_LOCAL_ID 0xFD
#define GUNK_FOLLOWER_MOVEMENT 55

// Movement type 55 (entry 55 of UNK_020F6364, asm/unk_02058EE8.s).
extern const UnkLMOCallbackStruct GunkFollower_Movement;

// The emote bubbles over the follower (stage 6): HeartGold's fuki_* textures, in a/1/0/3 order.
enum GunkFollowerEmote {
    GUNK_EMOTE_HEART,       // fuki_heart
    GUNK_EMOTE_NEUTRAL,     // fuki_kao1
    GUNK_EMOTE_ANGRY,       // fuki_kao2
    GUNK_EMOTE_SAD,         // fuki_kao3
    GUNK_EMOTE_HAPPY,       // fuki_kao4
    GUNK_EMOTE_GRUMPY,      // fuki_kao5
    GUNK_EMOTE_MUSIC,       // fuki_onpu
    GUNK_EMOTE_QUESTION,    // fuki_hatena
    GUNK_EMOTE_EXCLAMATION, // fuki_bikkuri
    GUNK_EMOTE_SWEAT,       // fuki_ase
    GUNK_EMOTE_CRY,         // fuki_komari
    GUNK_EMOTE_POISON,      // fuki_doku
    GUNK_EMOTE_SILENCE,     // fuki_chinmoku
    GUNK_EMOTE_SLEEP,       // fuki_pikon
    GUNK_FOLLOWER_EMOTE_COUNT
};

// The follower object, or NULL.
LocalMapObject *GunkFollower_Get(FieldSystem *fieldSystem);
// The graphics ID for the party lead (first Pokemon able to battle, else the first hatched one), 0 = none.
u32 GunkFollower_GetLeadSprite(FieldSystem *fieldSystem);

// Map objects were just created or restored (asm/unk_02048904.s sub_02048AE4 / sub_02048B50, after the player):
// creates the follower on the player's tile, or updates a restored one to the current lead.
void GunkFollower_OnObjectsCreated(FieldSystem *fieldSystem);
// The map's objects are about to be removed (asm/unk_02048904.s sub_02048B34): ends the Poke Ball and bubble tasks.
void GunkFollower_OnObjectsDeleted(void);

// Every overworld input frame (Gunk_FieldInputUpdate): spawns the follower once the party has a lead,
// switches its sprite when the lead changes, removes it when there's no lead or on excluded maps.
void GunkFollower_FieldUpdate(FieldSystem *fieldSystem);

// ScrCmd ApplyMovement (src/scrcmd.c), with the object it moves: a script moving the player puts the follower back into
// its Poke Ball until the script ends.
void GunkFollower_OnScriptMovement(FieldSystem *fieldSystem, LocalMapObject *object);

// ScrCmd_Surf / RockClimb / Waterfall, before their field task: removes the follower (it comes back on foot).
void GunkFollower_OnFieldMove(FieldSystem *fieldSystem);

// Its graphics update while it is paused with the other objects (GunkFollower_ObjectDrawUpdate): hides it on a warp tile.
void GunkFollower_WhilePaused(LocalMapObject *object);

// Gunk_FieldInputProcess, before the vanilla input: A on the follower plays its cry and shows a short line.
BOOL GunkFollower_TryTalk(void *input, FieldSystem *fieldSystem);

// The party slot of the lead while it walks visibly behind the player, else 0xFF (the battle's walk-in).
u8 GunkFollower_GetVisibleLeadSlot(FieldSystem *fieldSystem);

// A battle starts (GunkBattle_Arm): if the follower is out, it shows a bubble once it's back on the field.
void GunkFollower_OnBattleStart(FieldSystem *fieldSystem);

// Task_StartBattle (src/encounter.c), before the battle starts: the follower walks into a single battle
// instead of coming out of a Poke Ball (src/gunk_follower_battle.c).
void GunkBattle_Arm(BattleSetup *setup, FieldSystem *fieldSystem);

// Wraps the object-at-tile collision check (vanilla now sub_0205995C_Vanilla): the player never collides with
// the follower.
BOOL sub_0205995C(LocalMapObject *object, u32 x, s32 y, u32 z);

#endif // POKEDIAMOND_GUNK_FOLLOWER_H
