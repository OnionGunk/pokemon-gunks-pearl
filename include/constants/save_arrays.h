#ifndef POKEDIAMOND_CONSTANTS_SAVE_ARRAYS_H
#define POKEDIAMOND_CONSTANTS_SAVE_ARRAYS_H

#define SAVE_BAG         3
#define SAVE_FLAGS       4
#define SAVE_DAYCARE     8
#define SAVE_SAFARI_ZONE 27
#define SAVE_EASY_CHAT   34
#define SAVE_GUNK        35 // Gunk's: DexNav and QoL state (main block)
#define SAVE_PC_STORAGE  36 // vanilla 35: moved one down so the main block stays contiguous
#define SAVE_BLOCK_NUM   37

#define SAVE_PAGE_MAX    32
#define SAVE_SECTOR_SIZE 0x1000

#endif // POKEDIAMOND_CONSTANTS_SAVE_ARRAYS_H
