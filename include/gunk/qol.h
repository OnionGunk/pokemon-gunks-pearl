#ifndef POKEDIAMOND_GUNK_QOL_H
#define POKEDIAMOND_GUNK_QOL_H

#include "global.h"
#include "scrcmd.h"

// Gunk's Repel prompt, Endless Repel and Exp. All (settings in GunkSave).

// Script commands appended to gScriptCmdTable (opcodes 721-724):
BOOL Gunk_ScrCmd_CheckRepelAgain(ScriptContext *ctx);   // 721 (p): var = 1 if the last Repel can be used again; buffers its name in 0
BOOL Gunk_ScrCmd_UseRepelAgain(ScriptContext *ctx);     // 722: uses one of the last Repel
BOOL Gunk_ScrCmd_ToggleEndlessRepel(ScriptContext *ctx); // 723 (p): var = new state
BOOL Gunk_ScrCmd_ToggleExpAll(ScriptContext *ctx);      // 724 (p): var = new state

// Repel step update (overlay 6) gets the step counter through this; Endless Repel keeps it from running out.
u8 *Gunk_RepelStepCounter(void *roamers);
// The Bag's Repel use: sets the steps (as ov75_021E6FEC) and remembers the Repel.
void Gunk_BagUseRepel(void *bagApp, u8 steps, u16 item);

// Item use (table UNK_020F7260 entries 24 and 25): run the toggle scripts.
void Gunk_EndlessRepelBagUse(void *context, void *arg1);
BOOL Gunk_EndlessRepelFieldUse(void *context);
void Gunk_ExpAllBagUse(void *context, void *arg1);
BOOL Gunk_ExpAllFieldUse(void *context);
int Gunk_ToggleItemCheck(u32 *context);

// Exp. All in battle (overlay 11's EXP task).
struct Pokemon;
u32 Gunk_ExpRecipientHoldEffect(u16 item, u32 attr, enum HeapID heapID, struct Pokemon *mon);
u32 Gunk_ExpAllAmount(u32 exp, u8 *battleCtx);

#endif // POKEDIAMOND_GUNK_QOL_H
