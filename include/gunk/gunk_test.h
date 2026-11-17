#ifndef POKEDIAMOND_GUNK_TEST_H
#define POKEDIAMOND_GUNK_TEST_H

#include "global.h"
#include "field_types_def.h"

struct SaveData;

// test builds only (make GUNK_TEST=1); empty in a release build
void Gunk_TestNewGame(struct SaveData *save);
BOOL Gunk_TestSkipIntro(void);
BOOL Gunk_TestForceShiny(void);
BOOL Gunk_TestDexNavKeys(FieldSystem *fieldSystem);
BOOL Gunk_TestWarp(FieldSystem *fieldSystem); // Pearl: warp anywhere (test builds)

#endif // POKEDIAMOND_GUNK_TEST_H
