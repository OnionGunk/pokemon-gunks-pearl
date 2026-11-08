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

#define SAVE_PAGE_MAX    36 // Gunk's: vanilla 32; 21 PC boxes need 22 pages beside the main block's 13
#define SAVE_SLOT_SIZE   (SAVE_PAGE_MAX * SAVE_SECTOR_SIZE)
// the extra saves (Hall of Fame, battle records) follow the main slot in each half of the flash
#define SAVE_EXTRA_FIRST_PAGE SAVE_PAGE_MAX
#define SAVE_SECTOR_SIZE 0x1000

#endif // POKEDIAMOND_CONSTANTS_SAVE_ARRAYS_H
