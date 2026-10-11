/**
  ******************************************************************************
  * @file    bootloader.h
  * @brief   STM32H750VBT6 Professional QSPI Flash Bootloader Header
  *          Features:
  *           - Dual-Slot (A/B) Partitioning (Slot A 3.75MB, Slot B 3.75MB)
  *           - Anti-Brick Automatic Rollback & Boot Attempt Counter
  *           - NV-Config Parameter Sector Persistence (W25Q64 Sector @ 0x780000)
  *           - Secure Firmware Header (0x53544D32 'STM2') & Hardware CRC32 Check
  *           - Downward-compatible raw binary fallback
  *           - Pure-Register Assembly Handover (zero-stack-tear)
  ******************************************************************************
  */

#ifndef __BOOTLOADER_H__
#define __BOOTLOADER_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"
#include <stdint.h>
#include <stdbool.h>

/* Bootloader Meta Information */
#define BOOTLOADER_VERSION            "v1.3.0"
#define BOOTLOADER_NAME               "STM32H750VB QSPI Dual-Slot Bootloader"

/* Flash Partition Table (W25Q64 8MB Layout) */
#define QSPI_BASE_ADDR                (0x90000000UL)

/* Slot A (Active Running Region, 3840 KB = 3.75 MB) */
#define SLOT_A_OFFSET                 (0x00000000UL)
#define SLOT_A_ADDR                   (QSPI_BASE_ADDR + SLOT_A_OFFSET)
#define SLOT_A_MAX_SIZE               (3840 * 1024UL)

/* Slot B (Backup / OTA Staging Region, 3840 KB = 3.75 MB) */
#define SLOT_B_OFFSET                 (0x003C0000UL)
#define SLOT_B_ADDR                   (QSPI_BASE_ADDR + SLOT_B_OFFSET)
#define SLOT_B_MAX_SIZE               (3840 * 1024UL)

/* NV-Config Boot Parameter Sector (4 KB Sector @ Offset 0x780000) */
#define NV_CONFIG_OFFSET              (0x00780000UL)
#define NV_CONFIG_ADDR                (QSPI_BASE_ADDR + NV_CONFIG_OFFSET)
#define NV_CONFIG_MAGIC               (0x43464731UL) /* 'CFG1' */

/* User Data / Filesystem Reserved (508 KB) */
#define USER_DATA_OFFSET              (0x00781000UL)
#define USER_DATA_MAX_SIZE            (508 * 1024UL)

/* Secure Firmware Header Definition (Magic: 'STM2' = 0x53544D32) */
#define FIRMWARE_MAGIC                (0x53544D32UL)
#define HEADER_FORMAT_VER             (0x00010000UL)
#define FIRMWARE_HEADER_SIZE          (64UL)

typedef struct __attribute__((packed)) {
    uint32_t magic;         /* Magic: 0x53544D32 ('STM2') */
    uint32_t header_ver;    /* Header format version (0x00010000) */
    uint32_t fw_version;    /* e.g., 0x01020000 -> v1.2.0 */
    uint32_t fw_size;       /* Payload binary size in bytes */
    uint32_t fw_crc32;      /* CRC32 of payload */
    uint32_t target_slot;   /* 0: Slot A, 1: Slot B, 0xFF: Any */
    uint32_t timestamp;     /* UNIX build timestamp */
    uint8_t  git_hash[8];   /* Git commit or build tag */
    uint8_t  reserved[24];  /* Reserved for signature/expansion */
    uint32_t header_crc32;  /* CRC32 of the first 60 bytes of header */
} FirmwareHeader_t;

/* Slot Identifiers */
typedef enum {
    BOOT_SLOT_A = 0,
    BOOT_SLOT_B = 1,
    BOOT_SLOT_ANY = 0xFF
} BootSlot_t;

/* Upgrade States */
typedef enum {
    UPGRADE_STATE_NORMAL = 0,
    UPGRADE_STATE_PENDING = 1,
    UPGRADE_STATE_TESTING = 2,
    UPGRADE_STATE_FAILED = 3
} UpgradeState_t;

/* NV-Config Persistent Structure (Stored in Flash Sector 0x780000) */
typedef struct __attribute__((packed)) {
    uint32_t magic;             /* NV_CONFIG_MAGIC ('CFG1') */
    uint8_t  active_slot;       /* 0: Slot A, 1: Slot B */
    uint8_t  upgrade_state;     /* UpgradeState_t */
    uint8_t  boot_count;        /* Consecutive boot attempts without confirmation */
    uint8_t  max_boot_count;    /* Maximum allowable boot attempts (default: 3) */
    uint32_t slot_a_size;       /* Slot A verified firmware size */
    uint32_t slot_a_crc;        /* Slot A CRC32 */
    uint32_t slot_b_size;       /* Slot B verified firmware size */
    uint32_t slot_b_crc;        /* Slot B CRC32 */
    uint32_t total_boot_cycles; /* Total power cycles */
    uint8_t  reserved[32];      /* Future extension */
    uint32_t config_crc32;      /* CRC32 of above struct fields */
} BootConfig_t;

/* Bootloader Status Codes */
typedef enum {
    BOOT_OK = 0,
    BOOT_ERR_INVALID_STACK,
    BOOT_ERR_INVALID_ENTRY,
    BOOT_ERR_CRC_FAILED,
    BOOT_ERR_FLASH_IO,
    BOOT_ERR_NO_VALID_IMAGE,
    BOOT_ERR_MEMORY_MAPPED,
    BOOT_ERR_ROLLBACK_FAILED
} BootStatus_t;

/* Exported Functions */
void          Bootloader_PrintBanner(void);
void          Bootloader_PrintPartitionTable(void);
uint32_t      Bootloader_CalculateCRC32(const uint8_t *data, uint32_t length);

/* NV-Config Management */
int           Bootloader_Config_Init(void);
BootConfig_t* Bootloader_Config_Get(void);
int           Bootloader_Config_Save(void);
void          Bootloader_Config_ResetToDefaults(void);

/* Image Verification & Operations */
BootStatus_t  Bootloader_VerifySlot(BootSlot_t slot, uint32_t *pStackPointer, uint32_t *pEntryPoint);
int           Bootloader_CloneSlot(BootSlot_t srcSlot, BootSlot_t dstSlot, uint32_t size);
int           Bootloader_Rollback(void);
void          Bootloader_CheckPendingUpgrade(void);

/* System Handover */
void          Bootloader_JumpToApp(uint32_t targetBaseAddr);
void          Bootloader_Panic(const char *reason);

#ifdef __cplusplus
}
#endif

#endif /* __BOOTLOADER_H__ */
