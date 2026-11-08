#include "global.h"

// Gunk's DexNav Pokétch app (app 25), a copy of the Pokemon History app (overlay 49) showing the area's Pokemon
extern void Poketch_InitApp(void *func1, void *func2);
extern void gpdn_02254854();
extern void gpdn_02254974();

static void gpdn_02254840(void) {
    Poketch_InitApp(gpdn_02254854, gpdn_02254974);
}

#define NitroStaticInit gpdn_02254840
#include "sinit.h"
