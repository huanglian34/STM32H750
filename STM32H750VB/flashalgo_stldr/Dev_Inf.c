#include "Dev_Inf.h"

/* This structure containes information used by ST-LINK Utility to program and erase the device */
#if defined(__ICCARM__)
__root struct StorageInfo const StorageInfo = {
#else
struct StorageInfo const StorageInfo = {
#endif
    "STM32H750VB_W25Q64JV_Dual",    // Device Name + EVAL Borad name
    SPI_FLASH,                 // Device Type
    0x90000000,                // Device Start Address
    0x01000000,                // Device Size in Bytes (16MBytes)
    0x1000,                    // Programming Page Size 4096Bytes
    0xFF,                      // Initial Content of Erased Memory
                               // Specify Size and Address of Sectors (view example below)
    0x00000100, 0x00010000,    // Sector Num : 256 ,Sector Size: 64KBytes
    0x00000000, 0x00000000,
};

