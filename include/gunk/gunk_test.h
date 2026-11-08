#ifndef POKEDIAMOND_GUNK_TEST_H
#define POKEDIAMOND_GUNK_TEST_H

#include "global.h"

struct SaveData;

// test builds only (make GUNK_TEST=1); empty in a release build
void Gunk_TestNewGame(struct SaveData *save);
BOOL Gunk_TestSkipIntro(void);

#endif // POKEDIAMOND_GUNK_TEST_H
