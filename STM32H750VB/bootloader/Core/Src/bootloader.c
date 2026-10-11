/**
  ******************************************************************************
  * @file    bootloader.c
  * @brief   STM32H750VBT6 Professional QSPI Flash Bootloader Implementation
  *          Features:
  *           - Dual-Slot A/B Management & Automatic Rollback
  *           - NV-Config Parameter Sector Read/Write/Restore
  *           - FirmwareHeader_t Secure Image & CRC32 Verification
  *           - Raw Binary Fallback Compatibility
  *           - Pure-Register Assembly Handover (zero stack-tear)
  ******************************************************************************
  */

#include "bootloader.h"
#include "bsp_rgb.h"
#include "bsp_qspi_w25q.h"
#include "usart.h"
#include <stdio.h>
#include <string.h>

typedef void (*pFunction)(void);

static BootConfig_t s_bootConfig;
static bool s_configLoaded = false;

/**
  * @brief  Print formatted startup banner to debug console
  */
void Bootloader_PrintBanner(void)
{
    printf("\r\n");
    printf("============================================================\r\n");
    printf("   %s\r\n", BOOTLOADER_NAME);
    printf("   Version       : %s\r\n", BOOTLOADER_VERSION);
    printf("   Build Date    : %s %s\r\n", __DATE__, __TIME__);
    printf("   Core / Target : ARM Cortex-M7 @ STM32H750VBT6\r\n");
    printf("   Internal Flash: 0x08000000 ~ 0x08020000 (128 KB)\r\n");
    printf("   External Flash: 0x90000000 ~ 0x90800000 (Winbond W25Q64 8MB)\r\n");
    printf("============================================================\r\n\r\n");
}

/**
  * @brief  Print Flash Partition Table Layout
  */
void Bootloader_PrintPartitionTable(void)
{
    printf("------------- Flash Memory Partition Layout (8MB) -------------\r\n");
    printf(" [Slot A - Active]  Addr: 0x%08lX (Offset: 0x%06lX) Size: %lu KB\r\n",
           (unsigned long)SLOT_A_ADDR, (unsigned long)SLOT_A_OFFSET, (unsigned long)(SLOT_A_MAX_SIZE / 1024));
    printf(" [Slot B - Backup]  Addr: 0x%08lX (Offset: 0x%06lX) Size: %lu KB\r\n",
           (unsigned long)SLOT_B_ADDR, (unsigned long)SLOT_B_OFFSET, (unsigned long)(SLOT_B_MAX_SIZE / 1024));
    printf(" [NV-Config Sector] Addr: 0x%08lX (Offset: 0x%06lX) Size: 4 KB\r\n",
           (unsigned long)NV_CONFIG_ADDR, (unsigned long)NV_CONFIG_OFFSET);
    printf(" [User Data Area]   Addr: 0x%08lX (Offset: 0x%06lX) Size: %lu KB\r\n",
           (unsigned long)(QSPI_BASE_ADDR + USER_DATA_OFFSET), (unsigned long)USER_DATA_OFFSET, (unsigned long)(USER_DATA_MAX_SIZE / 1024));
    printf("----------------------------------------------------------------\r\n\r\n");
}

/**
  * @brief  Calculate standard CRC32 (polynomial 0xEDB88320)
  */
uint32_t Bootloader_CalculateCRC32(const uint8_t *data, uint32_t length)
{
    uint32_t crc = 0xFFFFFFFFUL;
    while (length--)
    {
        crc ^= *data++;
        for (int i = 0; i < 8; i++)
        {
            crc = (crc >> 1) ^ (0xEDB88320UL & -(crc & 1));
        }
    }
    return ~crc;
}

/**
  * @brief  Calculate BootConfig internal CRC32
  */
static uint32_t Bootloader_CalcConfigCRC(const BootConfig_t *pCfg)
{
    uint32_t len = (uint32_t)&pCfg->config_crc32 - (uint32_t)pCfg;
    return Bootloader_CalculateCRC32((const uint8_t *)pCfg, len);
}

/**
  * @brief  Reset configuration structure to defaults
  */
void Bootloader_Config_ResetToDefaults(void)
{
    memset(&s_bootConfig, 0, sizeof(BootConfig_t));
    s_bootConfig.magic          = NV_CONFIG_MAGIC;
    s_bootConfig.active_slot    = BOOT_SLOT_A;
    s_bootConfig.upgrade_state  = UPGRADE_STATE_NORMAL;
    s_bootConfig.boot_count     = 0;
    s_bootConfig.max_boot_count = 3;
    s_bootConfig.total_boot_cycles = 1;
    s_bootConfig.config_crc32   = Bootloader_CalcConfigCRC(&s_bootConfig);
}

/**
  * @brief  Save current configuration back to W25Q64 NV-Config sector
  */
int Bootloader_Config_Save(void)
{
    s_bootConfig.config_crc32 = Bootloader_CalcConfigCRC(&s_bootConfig);

    /* Erase Sector 0x780000 */
    if (BSP_W25Qx_SectorErase(NV_CONFIG_OFFSET) != W25Q_OK)
    {
        printf(" [ERR] Failed to erase NV-Config sector!\r\n");
        return -1;
    }

    /* Program buffer to Flash */
    if (BSP_W25Qx_WriteBuffer((const uint8_t *)&s_bootConfig, NV_CONFIG_OFFSET, sizeof(BootConfig_t)) != W25Q_OK)
    {
        printf(" [ERR] Failed to write NV-Config sector!\r\n");
        return -1;
    }

    return 0;
}

/**
  * @brief  Initialize and load NV-Config from external Flash
  */
int Bootloader_Config_Init(void)
{
    if (BSP_W25Qx_ReadBuffer((uint8_t *)&s_bootConfig, NV_CONFIG_OFFSET, sizeof(BootConfig_t)) != W25Q_OK)
    {
        printf(" [ERR] Failed to read NV-Config from Flash!\r\n");
        Bootloader_Config_ResetToDefaults();
        return -1;
    }

    uint32_t calc_crc = Bootloader_CalcConfigCRC(&s_bootConfig);
    if (s_bootConfig.magic != NV_CONFIG_MAGIC || s_bootConfig.config_crc32 != calc_crc)
    {
        printf(" [INFO] NV-Config uninitialized or corrupted. Formatting parameter sector...\r\n");
        Bootloader_Config_ResetToDefaults();
        Bootloader_Config_Save();
    }
    else
    {
        s_bootConfig.total_boot_cycles++;
        Bootloader_Config_Save();
    }

    s_configLoaded = true;
    printf(" [INFO] NV-Config Loaded: Active Slot: %s | Boot Attempts: %u/%u | Cycles: %lu\r\n",
           (s_bootConfig.active_slot == BOOT_SLOT_A) ? "Slot A" : "Slot B",
           s_bootConfig.boot_count, s_bootConfig.max_boot_count,
           (unsigned long)s_bootConfig.total_boot_cycles);

    return 0;
}

BootConfig_t* Bootloader_Config_Get(void)
{
    if (!s_configLoaded)
    {
        Bootloader_Config_Init();
    }
    return &s_bootConfig;
}

/**
  * @brief  Validate whether Stack Pointer falls in legitimate RAM regions
  */
static bool Bootloader_IsValidStack(uint32_t sp)
{
    if (sp & 0x3) return false;

    /* DTCM (128KB: 0x20000000 ~ 0x20020000) */
    if (sp >= 0x20000000UL && sp <= 0x20020000UL) return true;

    /* AXI SRAM (512KB: 0x24000000 ~ 0x24080000) */
    if (sp >= 0x24000000UL && sp <= 0x24080000UL) return true;

    /* SRAM1, SRAM2, SRAM3 (288KB: 0x30000000 ~ 0x30048000) */
    if (sp >= 0x30000000UL && sp <= 0x30048000UL) return true;

    /* SRAM4 (64KB: 0x38000000 ~ 0x38010000) */
    if (sp >= 0x38000000UL && sp <= 0x38010000UL) return true;

    /* Backup SRAM (4KB: 0x38800000 ~ 0x38801000) */
    if (sp >= 0x38800000UL && sp <= 0x38801000UL) return true;

    return false;
}

/**
  * @brief  Validate whether Entry Point PC falls in QSPI XIP flash range
  */
static bool Bootloader_IsValidEntry(uint32_t pc, uint32_t slotBaseAddr)
{
    if ((pc & 1) == 0) return false;
    if (pc == 0xFFFFFFFFUL || pc == 0x00000000UL || pc == 0x88888888UL) return false;

    /* If verifying Slot B, it may hold a native Slot B binary OR a backup snapshot of Slot A */
    if (slotBaseAddr == SLOT_B_ADDR)
    {
        if ((pc >= SLOT_A_ADDR && pc < (SLOT_A_ADDR + SLOT_A_MAX_SIZE)) ||
            (pc >= SLOT_B_ADDR && pc < (SLOT_B_ADDR + SLOT_B_MAX_SIZE)))
        {
            return true;
        }
        return false;
    }

    /* Slot A must fall within Slot A space */
    if (pc < slotBaseAddr || pc >= (slotBaseAddr + SLOT_A_MAX_SIZE)) return false;

    return true;
}

/**
  * @brief  Verify application vector table and optional Firmware Header
  */
BootStatus_t Bootloader_VerifySlot(BootSlot_t slot, uint32_t *pStackPointer, uint32_t *pEntryPoint)
{
    uint32_t slotBase = (slot == BOOT_SLOT_A) ? SLOT_A_ADDR : SLOT_B_ADDR;
    uint32_t slotOffset = (slot == BOOT_SLOT_A) ? SLOT_A_OFFSET : SLOT_B_OFFSET;
    const char *slotName = (slot == BOOT_SLOT_A) ? "Slot A" : "Slot B";

    uint8_t buffer[64];
    if (BSP_W25Qx_ReadBuffer(buffer, slotOffset, 64) != W25Q_OK)
    {
        return BOOT_ERR_FLASH_IO;
    }

    uint32_t sp = *(uint32_t *)&buffer[0];
    uint32_t pc = *(uint32_t *)&buffer[4];

    /* Check if 64-byte Secure Firmware Header is present at the beginning */
    FirmwareHeader_t *pHdr = (FirmwareHeader_t *)buffer;
    bool hasHeader = false;

    if (pHdr->magic == FIRMWARE_MAGIC)
    {
        uint32_t calc_hdr_crc = Bootloader_CalculateCRC32((const uint8_t *)pHdr, 60);
        if (calc_hdr_crc == pHdr->header_crc32)
        {
            hasHeader = true;
            printf(" [INFO] %s: Secure Firmware Header Detected!\r\n", slotName);
            printf("        Version  : v%lu.%lu.%lu\r\n",
                   (unsigned long)((pHdr->fw_version >> 24) & 0xFF),
                   (unsigned long)((pHdr->fw_version >> 16) & 0xFF),
                   (unsigned long)((pHdr->fw_version >> 8) & 0xFF));
            printf("        Size     : %lu bytes\r\n", (unsigned long)pHdr->fw_size);
            printf("        CRC32    : 0x%08lX\r\n", (unsigned long)pHdr->fw_crc32);

            /* Read actual vector table after header */
            uint8_t vtor_buf[8];
            BSP_W25Qx_ReadBuffer(vtor_buf, slotOffset + FIRMWARE_HEADER_SIZE, 8);
            sp = *(uint32_t *)&vtor_buf[0];
            pc = *(uint32_t *)&vtor_buf[4];
        }
    }

    if (pStackPointer) *pStackPointer = sp;
    if (pEntryPoint)   *pEntryPoint   = pc;

    printf(" [INFO] %s Vector Table Probe (Base: 0x%08lX):\r\n", slotName, (unsigned long)slotBase);
    printf("        - Initial MSP : 0x%08lX %s\r\n", (unsigned long)sp,
           Bootloader_IsValidStack(sp) ? "[VALID]" : "[INVALID!]");
    printf("        - Reset Entry : 0x%08lX %s\r\n", (unsigned long)pc,
           Bootloader_IsValidEntry(pc, slotBase) ? "[VALID]" : "[INVALID!]");

    if (!Bootloader_IsValidStack(sp)) return BOOT_ERR_INVALID_STACK;
    if (!Bootloader_IsValidEntry(pc, slotBase)) return BOOT_ERR_INVALID_ENTRY;

    return BOOT_OK;
}

/**
  * @brief  Clone flash contents between slots (e.g. Slot A to B for backup, or B to A for restore)
  */
int Bootloader_CloneSlot(BootSlot_t srcSlot, BootSlot_t dstSlot, uint32_t size)
{
    if (size == 0 || size > SLOT_A_MAX_SIZE) size = SLOT_A_MAX_SIZE;

    uint32_t srcOffset = (srcSlot == BOOT_SLOT_A) ? SLOT_A_OFFSET : SLOT_B_OFFSET;
    uint32_t dstOffset = (dstSlot == BOOT_SLOT_A) ? SLOT_A_OFFSET : SLOT_B_OFFSET;
    const char *srcName = (srcSlot == BOOT_SLOT_A) ? "Slot A" : "Slot B";
    const char *dstName = (dstSlot == BOOT_SLOT_A) ? "Slot A" : "Slot B";

    printf(" [INFO] Cloning %s -> %s (%lu KB)...\r\n", srcName, dstName, (unsigned long)(size / 1024));

    /* Static buffer in BSS to prevent stack overflow on Cortex-M embedded stack */
    static uint8_t s_clone_chunk[W25Q_SECTOR_SIZE];
    uint32_t sectors = (size + W25Q_SECTOR_SIZE - 1) / W25Q_SECTOR_SIZE;

    for (uint32_t s = 0; s < sectors; s++)
    {
        uint32_t curSrc = srcOffset + (s * W25Q_SECTOR_SIZE);
        uint32_t curDst = dstOffset + (s * W25Q_SECTOR_SIZE);

        /* Read sector */
        if (BSP_W25Qx_ReadBuffer(s_clone_chunk, curSrc, W25Q_SECTOR_SIZE) != W25Q_OK)
        {
            printf(" [ERR] Read failed at sector %lu!\r\n", (unsigned long)s);
            return -1;
        }

        /* Erase destination sector */
        if (BSP_W25Qx_SectorErase(curDst) != W25Q_OK)
        {
            printf(" [ERR] Erase failed at sector %lu!\r\n", (unsigned long)s);
            return -1;
        }

        /* Write destination sector */
        if (BSP_W25Qx_WriteBuffer(s_clone_chunk, curDst, W25Q_SECTOR_SIZE) != W25Q_OK)
        {
            printf(" [ERR] Program failed at sector %lu!\r\n", (unsigned long)s);
            return -1;
        }

        if ((s % 16) == 0 || s == (sectors - 1))
        {
            printf("        Progress: %lu / %lu sectors (%lu%%)\r\n",
                   (unsigned long)(s + 1), (unsigned long)sectors, (unsigned long)(((s + 1) * 100) / sectors));
        }
    }

    printf(" [OK]   Cloning %s -> %s completed successfully.\r\n", srcName, dstName);
    return 0;
}

/**
  * @brief  Perform automatic rollback when active slot fails
  */
int Bootloader_Rollback(void)
{
    printf("\r\n [WARN] !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!\r\n");
    printf(" [WARN] System Boot Failure Detected! Initiating Rollback...\r\n");
    printf(" [WARN] !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!\r\n\r\n");

    /* Verify backup Slot B first */
    uint32_t sp = 0, pc = 0;
    if (Bootloader_VerifySlot(BOOT_SLOT_B, &sp, &pc) != BOOT_OK)
    {
        printf(" [CRITICAL] Backup Slot B is also invalid! Cannot rollback.\r\n");
        return -1;
    }

    /* Restore Slot B to Slot A */
    if (Bootloader_CloneSlot(BOOT_SLOT_B, BOOT_SLOT_A, s_bootConfig.slot_b_size ? s_bootConfig.slot_b_size : 512 * 1024) != 0)
    {
        printf(" [CRITICAL] Rollback copy operation failed!\r\n");
        return -1;
    }

    /* Reset boot counter and save */
    s_bootConfig.boot_count = 0;
    s_bootConfig.upgrade_state = UPGRADE_STATE_NORMAL;
    s_bootConfig.active_slot = BOOT_SLOT_A;
    Bootloader_Config_Save();

    printf(" [OK] Rollback complete! System restored to backup firmware.\r\n");
    return 0;
}

static void Jump_To_App(uint32_t sp, uint32_t pc)
{
    __asm volatile(
        "msr msp, %0\n"
        "mov r2, #0\n"
        "msr control, r2\n"
        "isb\n"
        "bx %1\n"
        :
        : "r"(sp), "r"(pc)
        : "r2"
    );
}

/**
  * @brief  Handover control to Application at targetBaseAddr
  */
void Bootloader_JumpToApp(uint32_t targetBaseAddr)
{
    uint32_t actualBase = targetBaseAddr;

    /* Check if application area starts with 64-byte secure firmware header */
    FirmwareHeader_t *pHdr = (FirmwareHeader_t *)targetBaseAddr;
    if (pHdr->magic == FIRMWARE_MAGIC)
    {
        uint32_t calc_hdr_crc = Bootloader_CalculateCRC32((const uint8_t *)pHdr, 60);
        if (calc_hdr_crc == pHdr->header_crc32)
        {
            actualBase += FIRMWARE_HEADER_SIZE;
        }
    }

    volatile uint32_t *vtor = (volatile uint32_t *)actualBase;
    uint32_t sp = vtor[0];
    uint32_t pc = vtor[1];

    if (!Bootloader_IsValidStack(sp) || !Bootloader_IsValidEntry(pc, targetBaseAddr))
    {
        Bootloader_Panic("Vector table corrupted during handover!");
        return;
    }

    printf("\r\n [OK]   Vector Verification Passed. Preparing Handover @ 0x%08lX...\r\n", (unsigned long)actualBase);

    /* Flush UART transmission buffer before handover */
    while (__HAL_UART_GET_FLAG(&huart1, UART_FLAG_TC) == RESET);

    /* Indicate jump via Green LED */
    BSP_RGB_SetColor(RGB_COLOR_GREEN);

    /* Disable Global Interrupts */
    __disable_irq();

    /* Disable and reset SysTick timer */
    SysTick->CTRL = 0;
    SysTick->LOAD = 0;
    SysTick->VAL  = 0;

    /* Clear all NVIC interrupt enable & pending registers */
    for (int i = 0; i < 8; i++)
    {
        NVIC->ICER[i] = 0xFFFFFFFFUL;
        NVIC->ICPR[i] = 0xFFFFFFFFUL;
    }

    /* Relocate Interrupt Vector Table to application base */
    SCB->VTOR = actualBase;

    /* Memory and Instruction Barriers */
    __DSB();
    __ISB();

    /* Jump to Application Entry via pure registers */
    Jump_To_App(sp, pc);

    /* Should never reach here */
    while (1);
}

/**
  * @brief  Trigger safe panic lockup and diagnostic LED
  */
void Bootloader_Panic(const char *reason)
{
    printf("\r\n [CRITICAL ERROR] Bootloader Panic: %s\r\n", reason ? reason : "Unknown");
    printf("   -> Execution halted safely in Bootloader.\r\n");
    printf("   -> Type 'help' in CLI or re-flash application.\r\n\r\n");

    BSP_RGB_SetColor(RGB_COLOR_RED);

    while (1)
    {
        HAL_Delay(500);
        BSP_RGB_Toggle(1, 0, 0); // Blink Red LED
    }
}
