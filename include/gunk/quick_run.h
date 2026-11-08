#ifndef POKEDIAMOND_GUNK_QUICK_RUN_H
#define POKEDIAMOND_GUNK_QUICK_RUN_H

#include "global.h"

// Gunk's: overlay 11's command-selection task reads the action menu through this (R = RUN in wild battles).
u32 Gunk_BattleMenuInput(u8 *subscreen);
// Gunk's Quick Ball: the "What will X do?" prompt, and the controller's BAG case (returns the next command state).
u32 Gunk_BattlePrompt(void *battleSystem, void *msgLoader, void *message, u32 speed);
u32 Gunk_QuickBallBagCommand(u8 *battlerActionBase);

#endif // POKEDIAMOND_GUNK_QUICK_RUN_H
