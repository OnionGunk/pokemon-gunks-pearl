#include "global.h"

#include "gunk/gunk_follower.h"

#include "constants/battle.h"
#include "constants/sndseq.h"

#include "battle_setup.h"
#include "field_system.h"
#include "heap.h"
#include "map_object.h"
#include "pokemon.h"
#include "unk_020051F4.h"
#include "unk_0200CA44.h"

// Gunk's Pokemon follower in battle (stage 5): the Pokemon that walks behind the player enters the battle on foot,
// as in the Platinum hack (Following Platinum): no Poke Ball is shown when the player sends it out, and it slides in
// from the left edge at full size and cries, instead of the ball-open effect.
//
// - Armed when a battle starts from the field (Task_StartBattle in src/encounter.c, the one place every field battle
//   goes through, calls GunkBattle_Arm) if the follower is out and the battle is a plain single battle; it remembers
//   the follower's party slot.
// - The player's trainer still makes the throw motion; the ball it throws is kept invisible
//   (GunkBattle_ThrowBallSetVisible: the ball-visibility calls of the player branch of ov11_02250338, the
//   equivalent of Platinum's Task_ThrowTrainerBall).
// - The send-out task (ov11_0224F2DC, Platinum's Task_ShowEncounter) is started through GunkBattle_Task_ShowEncounter
//   (the task pointer in ov11_0224D738's literal pool). For the follower it runs the slide-in below, otherwise it
//   passes straight on to the vanilla task.
// - Switching it out and back in during the battle still uses the ball (Platinum slides there too; not ported yet).
// Overlay 11 doesn't grow: only a literal word and two bl targets change. Overlays 5/6 are untouched.

// battle (overlay 11/08) functions, named after Platinum's battle_display.c where they match
extern u32 ov11_0222FF74(void *battleSys);                         // BattleSystem_GetBattleType
extern void *ov11_02230068(void *battleSys);                        // BattleSystem_GetPokemonSpriteManager
extern void *ov11_02230E80(void *battleSys);                        // BattleSystem_GetPokemonAnimManager
extern struct SaveChatotSoundClip *ov11_02230E88(void *battleSys, int battler); // BattleSystem_GetChatotCry
extern void *ov11_02253A88(void *battleSys, void *spriteMan, void *template, int x, int y, int z, int yOffset, int height, int shadowX, int shadowSize, int battler, void *animFrames, void *callback); // BattleDisplay_NewPokemonSprite
extern void ov11_022567B8(void *battleSys, int battler, int command); // BattleController_EmitClearCommand
extern void ov11_02256854(void *battleSys, void *unused, void *moveAnim, int one, int animId, int attacker, int defender, int zero); // BattleController_SetMoveAnimation
extern void ov11_02254404(void *battleSys, void *battlerData, void *animSys, void *moveAnim); // BattleDisplay_PlayMoveAnimation
extern void ov11_0224F2DC(SysTask *task, void *data);              // vanilla Task_ShowEncounter
extern void ov08_02229A2C(void *ballRotation);                       // frees the ball
extern u32 ov08_02229A80(void *ballRotation);                        // the ball's current mode (0 = opening/idle)
extern BOOL ov08_022299E8(void *ballRotation);                       // the ball's mode is still running
extern void ov08_02229FE4(void *ballRotation, int visible);
extern void *ov08_02211F5C(int heapID);                              // BattleAnimSystem_New
extern void ov08_02212384(void *animSys);                            // BattleAnimSystem_ExecuteScript
extern BOOL ov08_022123A0(void *animSys);                            // BattleAnimSystem_IsMoveActive
extern void ov08_022123B0(void *animSys);                            // BattleAnimSystem_FreeScriptData
extern void ov08_02212024(void *animSys);                            // BattleAnimSystem_Delete
extern const s16 ov08_0222D538[];                                    // gBattlerEncounterX (x, ...) per battler type
extern const s16 ov08_0222D552[];                                    // gEncounterCoords y (stride 3 s16)
extern const s16 ov08_0222D554[];                                    // gEncounterCoords z
extern BOOL sub_02007390(void *sprite);                               // PokemonSprite_IsAnimActive
extern BOOL sub_02014D7C(void *animMan, int battler);                 // PokemonAnimManager_HasAnimCompleted
extern void sub_02007558(void *sprite, int attr, int value);          // PokemonSprite_SetAttribute
extern int sub_0200782C(void *sprite, int attr);                      // PokemonSprite_GetAttribute

#define MON_SPRITE_X_CENTER            0
#define MON_SPRITE_HIDE                6
#define MON_SPRITE_SCALE_X             12
#define MON_SPRITE_SCALE_Y             13
#define MON_SPRITE_SHADOW_FOLLOW_Y     44

#define FACE_FRONT                     2   // MonShowData face: 2 = the enemy side
#define BATTLE_SOUND_PAN_LEFT          (-117)
#define HEAP_ID_BATTLE                 5
#define MON_SLIDE_SPEED                8
#define MON_SLIDE_START_BACK           (-80)
#define BALL_WAIT_MAX                  180  // frames

#define FOLLOWER_STATE_FIRST           0x40 // our states start here, vanilla's are 0-7 and 0xFF

// Diamond's MonShowData (overlay 11), the fields used here
#define SHOW_BATTLE_SYS(d)    (*(void **)((u8 *)(d) + 0x00))
#define SHOW_BATTLER_DATA(d)  (*(u8 **)((u8 *)(d) + 0x04))
#define SHOW_TEMPLATE(d)      ((void *)((u8 *)(d) + 0x14))
#define SHOW_ANIM_SYS(d)      (*(void **)((u8 *)(d) + 0x24))
#define SHOW_COMMAND(d)       (*((u8 *)(d) + 0x80))
#define SHOW_BATTLER(d)       (*((u8 *)(d) + 0x81))
#define SHOW_BATTLER_TYPE(d)  (*((u8 *)(d) + 0x82))
#define SHOW_STATE(d)         (*((u8 *)(d) + 0x83))
#define SHOW_WAIT(d)          (*((u8 *)(d) + 0x96)) // vanilla's frame counter, 0 at the start
#define SHOW_FACE(d)          (*((u8 *)(d) + 0x84))
#define SHOW_Y_OFFSET(d)      (*((u8 *)(d) + 0x85))
#define SHOW_SPECIES(d)       (*(u16 *)((u8 *)(d) + 0x86))
#define SHOW_CRY_MOD(d)       (*(u32 *)((u8 *)(d) + 0x88))
#define SHOW_PARTY_SLOT(d)    (*((u8 *)(d) + 0x8C))
#define SHOW_NATURE(d)        (*((u8 *)(d) + 0x8D))
#define SHOW_HEIGHT(d)        (*((s8 *)(d) + 0x90))
#define SHOW_SHADOW_X(d)      (*((s8 *)(d) + 0x91))
#define SHOW_IS_SHINY(d)      (*((u8 *)(d) + 0x92))
#define SHOW_SHADOW_SIZE(d)   (*((u8 *)(d) + 0x93))
// BattlerData
#define BATTLER_MON_SPRITE(b) (*(void **)((b) + 0x20))
#define BATTLER_BALL(b)       (*(void **)((b) + 0x88))

#define GUNK_BATTLE_NO_FOLLOWER 0xFF

static u8 sFollowerSlot = GUNK_BATTLE_NO_FOLLOWER; // party slot of the follower that walks into this battle

void GunkBattle_ThrowBallSetVisible(void *ballRotation, int visible);
void GunkBattle_Task_ShowEncounter(SysTask *task, void *data);

void GunkBattle_Arm(BattleSetup *setup, FieldSystem *fieldSystem) {
    sFollowerSlot = GUNK_BATTLE_NO_FOLLOWER;
    GunkFollower_OnBattleStart(fieldSystem);

    if (setup->flags & (BATTLE_TYPE_DOUBLES | BATTLE_TYPE_LINK | BATTLE_TYPE_MULTI | BATTLE_TYPE_INGAME_PARTNER | BATTLE_TYPE_SAFARI | BATTLE_TYPE_TOWER | BATTLE_TYPE_PAL_PARK | BATTLE_TYPE_TUTORIAL)) {
        return;
    }

    sFollowerSlot = GunkFollower_GetVisibleLeadSlot(fieldSystem);
}

// the player's thrown ball: invisible while the follower walks in
void GunkBattle_ThrowBallSetVisible(void *ballRotation, int visible) {
    ov08_02229FE4(ballRotation, sFollowerSlot != GUNK_BATTLE_NO_FOLLOWER ? FALSE : visible);
}

static BOOL GunkBattle_IsFollowerSendOut(void *data) {
    if (sFollowerSlot == GUNK_BATTLE_NO_FOLLOWER || SHOW_FACE(data) == FACE_FRONT) {
        return FALSE;
    }

    if (ov11_0222FF74(SHOW_BATTLE_SYS(data)) & (BATTLE_TYPE_DOUBLES | BATTLE_TYPE_LINK | BATTLE_TYPE_MULTI)) {
        return FALSE;
    }

    return SHOW_PARTY_SLOT(data) == sFollowerSlot;
}

void GunkBattle_Task_ShowEncounter(SysTask *task, void *data) {
    u8 *battlerData;
    void *sprite;
    u8 animFrames[0x20];
    u8 moveAnim[0x58];

    if (SHOW_STATE(data) == 0) {
        if (!GunkBattle_IsFollowerSendOut(data)) {
            if (SHOW_FACE(data) != FACE_FRONT) {
                sFollowerSlot = GUNK_BATTLE_NO_FOLLOWER; // the player's first send-out was someone else
            }
            ov11_0224F2DC(task, data);
            return;
        }

        SHOW_STATE(data) = FOLLOWER_STATE_FIRST;
    } else if (SHOW_STATE(data) < FOLLOWER_STATE_FIRST) {
        ov11_0224F2DC(task, data);
        return;
    }

    battlerData = SHOW_BATTLER_DATA(data);

    switch (SHOW_STATE(data)) {
    case FOLLOWER_STATE_FIRST: {
        int type = SHOW_BATTLER_TYPE(data);

        // at full size, just off the left edge of the screen
        sub_02069010(animFrames, SHOW_SPECIES(data));
        sprite = ov11_02253A88(SHOW_BATTLE_SYS(data), ov11_02230068(SHOW_BATTLE_SYS(data)), SHOW_TEMPLATE(data),
            ov08_0222D538[type * 2], ov08_0222D552[type * 3], ov08_0222D554[type * 3],
            SHOW_Y_OFFSET(data), SHOW_HEIGHT(data), SHOW_SHADOW_X(data), SHOW_SHADOW_SIZE(data), SHOW_BATTLER(data), animFrames, NULL);
        BATTLER_MON_SPRITE(battlerData) = sprite;
        sub_02007558(sprite, MON_SPRITE_SCALE_X, 0x100);
        sub_02007558(sprite, MON_SPRITE_SCALE_Y, 0x100);
        sub_02007558(sprite, MON_SPRITE_SHADOW_FOLLOW_Y, FALSE);
        sub_02007558(sprite, MON_SPRITE_X_CENTER, MON_SLIDE_START_BACK);
        sub_02007558(sprite, MON_SPRITE_HIDE, FALSE);
        sub_020054A8(SEQ_SE_DP_NIGERU, BATTLE_SOUND_PAN_LEFT);
        SHOW_STATE(data)++;
        break;
    }
    case FOLLOWER_STATE_FIRST + 1: {
        int target = ov08_0222D538[SHOW_BATTLER_TYPE(data) * 2];
        int x;

        sprite = BATTLER_MON_SPRITE(battlerData);
        x = sub_0200782C(sprite, MON_SPRITE_X_CENTER) + MON_SLIDE_SPEED;

        if (x < target) {
            sub_02007558(sprite, MON_SPRITE_X_CENTER, x);
            break;
        }

        // arrived: its cry and its entry animation (as the vanilla task does for the player's side)
        sub_02007558(sprite, MON_SPRITE_X_CENTER, target);
        sub_02069FB0(ov11_02230E88(SHOW_BATTLE_SYS(data), SHOW_BATTLER(data)), SHOW_CRY_MOD(data), SHOW_SPECIES(data), BATTLE_SOUND_PAN_LEFT, 0x7F, 0, (enum HeapID)HEAP_ID_BATTLE);
        sub_02069038((u32)ov11_02230E80(SHOW_BATTLE_SYS(data)), (u32)sprite, SHOW_SPECIES(data), SHOW_FACE(data), SHOW_NATURE(data), 0, SHOW_BATTLER(data));
        SHOW_STATE(data)++;
        break;
    }
    case FOLLOWER_STATE_FIRST + 2:
        sprite = BATTLER_MON_SPRITE(battlerData);

        if (sub_02014D7C(ov11_02230E80(SHOW_BATTLE_SYS(data)), SHOW_BATTLER(data)) != TRUE || sub_02007390(sprite)) {
            break;
        }

        // the invisible ball the trainer threw: wait until it has landed and finished opening, then free it (as the
        // vanilla task). Freeing it while it still opens cuts its palette flash short and leaves the blend task
        // running, which turns the Bag's screen black. The frame limit is only a safety net.
        if (BATTLER_BALL(battlerData) != NULL) {
            if ((ov08_02229A80(BATTLER_BALL(battlerData)) != 0 || ov08_022299E8(BATTLER_BALL(battlerData)))
                && SHOW_WAIT(data) < BALL_WAIT_MAX) {
                SHOW_WAIT(data)++;
                break;
            }

            ov08_02229A2C(BATTLER_BALL(battlerData));
            BATTLER_BALL(battlerData) = NULL;
        }

        sFollowerSlot = GUNK_BATTLE_NO_FOLLOWER;

        if (SHOW_IS_SHINY(data)) {
            SHOW_ANIM_SYS(data) = ov08_02211F5C(HEAP_ID_BATTLE);
            ov11_02256854(SHOW_BATTLE_SYS(data), NULL, moveAnim, 1, 11, SHOW_BATTLER(data), SHOW_BATTLER(data), 0);
            ov11_02254404(SHOW_BATTLE_SYS(data), battlerData, SHOW_ANIM_SYS(data), moveAnim);
            SHOW_STATE(data)++;
        } else {
            SHOW_STATE(data) = FOLLOWER_STATE_FIRST + 4;
        }
        break;
    case FOLLOWER_STATE_FIRST + 3:
        ov08_02212384(SHOW_ANIM_SYS(data));

        if (ov08_022123A0(SHOW_ANIM_SYS(data)) == FALSE) {
            ov08_022123B0(SHOW_ANIM_SYS(data));
            ov08_02212024(SHOW_ANIM_SYS(data));
            SHOW_STATE(data)++;
        }
        break;
    default:
        ov11_022567B8(SHOW_BATTLE_SYS(data), SHOW_BATTLER(data), SHOW_COMMAND(data));
        Heap_Free(data);
        SysTask_Destroy(task);
        break;
    }
}
