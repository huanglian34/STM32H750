/**
  ******************************************************************************
  * @file    boot_cli.c
  * @brief   Interactive Command Line Interface (CLI) for Bootloader
  *          Features:
  *           - Slot management commands ('slots', 'backup', 'restore', 'verify')
  *           - Ymodem download with slot targeting ('ymodem a', 'ymodem b')
  *           - NV-Config parameter inspection and reset ('config')
  *           - Hex dump & Flash operations ('read', 'erase')
  *           - Flexible boot handover ('boot a', 'boot b')
  ******************************************************************************
  */

#include "boot_cli.h"
#include "usart.h"
#include "bsp_rgb.h"
#include "bsp_qspi_w25q.h"
#include "bootloader.h"
#include "ymodem.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#define CLI_BUFFER_SIZE 128

static void Boot_CLI_Json(void);

/**
  * @brief  Check if user wants to intercept automatic boot
  */
bool Boot_CLI_CheckIntercept(uint32_t timeoutSeconds)
{
    printf(" [WAIT] Press [SPACE] or [ENTER] within %lu seconds to enter Bootloader Console...\r\n", (unsigned long)timeoutSeconds);

    for (uint32_t s = timeoutSeconds; s > 0; s--)
    {
        printf("        Boot in %lu s... (press any key to pause)\r", (unsigned long)s);
        uint32_t start_tick = HAL_GetTick();

        while ((HAL_GetTick() - start_tick) < 1000)
        {
            if (USART1->ISR & (USART_ISR_ORE | USART_ISR_FE | USART_ISR_NE))
            {
                USART1->ICR = USART_ICR_ORECF | USART_ICR_FECF | USART_ICR_NECF;
            }

            if ((USART1->ISR & USART_ISR_RXNE_RXFNE) != 0)
            {
                uint8_t rx_char = (uint8_t)(USART1->RDR & 0xFF);
                if (rx_char == ' ' || rx_char == '\r' || rx_char == '\n' || rx_char == 'c' || rx_char == 0x7F)
                {
                    printf("\r\n\r\n [INFO] Automatic Boot Intercepted! Handshake Synced.\r\n");
                    printf("[HANDSHAKE_ACK]\r\n");
                    Boot_CLI_Json();
                    return true;
                }
            }
        }
    }

    printf("\r\n [INFO] Countdown finished. Proceeding with Automatic Boot.\r\n\r\n");
    return false;
}

static void Boot_CLI_ReadLine(char *buffer, uint32_t maxLen)
{
    uint32_t idx = 0;
    memset(buffer, 0, maxLen);

    while (1)
    {
        /* Clear any Overrun or Framing error flags if hardware was flooded */
        if (USART1->ISR & (USART_ISR_ORE | USART_ISR_FE | USART_ISR_NE))
        {
            USART1->ICR = USART_ICR_ORECF | USART_ICR_FECF | USART_ICR_NECF;
        }

        if ((USART1->ISR & USART_ISR_RXNE_RXFNE) != 0)
        {
            uint8_t ch = (uint8_t)(USART1->RDR & 0xFF);

            if (ch == '\r' || ch == '\n')
            {
                putchar('\r');
                putchar('\n');
                buffer[idx] = '\0';
                return;
            }
            else if (ch == '\b' || ch == 127) // Backspace
            {
                if (idx > 0)
                {
                    idx--;
                    putchar('\b');
                    putchar(' ');
                    putchar('\b');
                }
            }
            else if (idx < (maxLen - 1) && ch >= 32 && ch <= 126)
            {
                buffer[idx++] = ch;
                putchar(ch);
            }
        }
    }
}

static void Boot_CLI_Help(void)
{
    printf("\r\nAvailable Commands:\r\n");
    printf("  help                - Display this command list\r\n");
    printf("  info                - Print MCU, Flash, and System status\r\n");
    printf("  slots               - Display Dual-Slot status, sizes, and boot counts\r\n");
    printf("  ymodem [a|b]        - Download firmware via Ymodem into Slot A (default) or Slot B\r\n");
    printf("  backup              - Clone Slot A (Active) into Slot B (Backup)\r\n");
    printf("  restore             - Restore Slot B (Backup) into Slot A (Active)\r\n");
    printf("  verify [a|b]        - Verify vector table and integrity of specified slot\r\n");
    printf("  config [reset]      - View or reset NV-Config parameters\r\n");
    printf("  erase <off> <sec>   - Erase <sec> sectors starting from Flash offset <off>\r\n");
    printf("  read <offset> <len> - Read Hex dump from Flash (offset relative to 0x90000000)\r\n");
    printf("  boot [a|b]          - Launch Application from Slot A or Slot B immediately\r\n");
    printf("  reset               - Perform MCU System Soft Reset\r\n\r\n");
}

static void Boot_CLI_Info(void)
{
    uint8_t mf_id = 0, dev_id = 0;
    uint32_t jedec_id = 0;
    uint8_t sr1 = 0, sr2 = 0;

    BSP_W25Qx_ReadID(&mf_id, &dev_id);
    BSP_W25Qx_ReadJEDECID(&jedec_id);
    BSP_W25Qx_ReadStatusReg(1, &sr1);
    BSP_W25Qx_ReadStatusReg(2, &sr2);

    printf("\r\n================ System Information ================\r\n");
    printf(" Core Target    : ARM Cortex-M7 @ STM32H750VBT6\r\n");
    printf(" System Clock   : %lu MHz\r\n", (unsigned long)(SystemCoreClock / 1000000));
    printf(" Flash Chip     : Winbond W25Q64JVSIQ (8MB, Quad SPI)\r\n");
    printf(" Manufacturer ID: 0x%02X\r\n", mf_id);
    printf(" Device ID      : 0x%02X\r\n", dev_id);
    printf(" JEDEC ID       : 0x%06lX\r\n", (unsigned long)jedec_id);
    printf(" Status Reg 1/2 : SR1=0x%02X, SR2=0x%02X (QE=%s)\r\n",
           sr1, sr2, (sr2 & 0x02) ? "Enabled" : "Disabled");
    Bootloader_PrintPartitionTable();
}

static void Boot_CLI_Slots(void)
{
    BootConfig_t *pCfg = Bootloader_Config_Get();
    printf("\r\n================ Dual-Slot Status ================\r\n");
    printf(" Active Slot        : %s\r\n", (pCfg->active_slot == BOOT_SLOT_A) ? "Slot A" : "Slot B");
    printf(" Boot Attempt Count : %u / %u\r\n", pCfg->boot_count, pCfg->max_boot_count);
    printf(" Total Boot Cycles  : %lu\r\n", (unsigned long)pCfg->total_boot_cycles);
    printf(" Slot A Firmware    : %lu bytes | CRC32: 0x%08lX\r\n", (unsigned long)pCfg->slot_a_size, (unsigned long)pCfg->slot_a_crc);
    printf(" Slot B Firmware    : %lu bytes | CRC32: 0x%08lX\r\n", (unsigned long)pCfg->slot_b_size, (unsigned long)pCfg->slot_b_crc);

    uint32_t sp_a = 0, pc_a = 0, sp_b = 0, pc_b = 0;
    BootStatus_t st_a = Bootloader_VerifySlot(BOOT_SLOT_A, &sp_a, &pc_a);
    BootStatus_t st_b = Bootloader_VerifySlot(BOOT_SLOT_B, &sp_b, &pc_b);

    printf(" Slot A Integrity   : %s\r\n", (st_a == BOOT_OK) ? "[PASS]" : "[FAIL]");
    printf(" Slot B Integrity   : %s\r\n", (st_b == BOOT_OK) ? "[PASS]" : "[FAIL]");
    printf("==================================================\r\n\r\n");
}

static void Boot_CLI_Json(void)
{
    BootConfig_t *pCfg = Bootloader_Config_Get();
    uint8_t mf_id = 0, dev_id = 0;
    uint32_t jedec_id = 0;
    uint8_t sr1 = 0, sr2 = 0;

    BSP_W25Qx_ReadID(&mf_id, &dev_id);
    BSP_W25Qx_ReadJEDECID(&jedec_id);
    BSP_W25Qx_ReadStatusReg(1, &sr1);
    BSP_W25Qx_ReadStatusReg(2, &sr2);

    uint32_t sp_a = 0, pc_a = 0, sp_b = 0, pc_b = 0;
    BootStatus_t st_a = Bootloader_VerifySlot(BOOT_SLOT_A, &sp_a, &pc_a);
    BootStatus_t st_b = Bootloader_VerifySlot(BOOT_SLOT_B, &sp_b, &pc_b);

    printf("{\"type\":\"boot_status\",\"version\":\"%s\",\"active_slot\":%u,\"boot_count\":%u,\"max_count\":%u,\"cycles\":%lu,\"clock_mhz\":%lu,"
           "\"flash\":{\"mf_id\":\"0x%02X\",\"dev_id\":\"0x%02X\",\"jedec\":\"0x%06lX\",\"qe\":%d},"
           "\"slot_a\":{\"base\":\"0x%08lX\",\"size\":%lu,\"crc\":\"0x%08lX\",\"valid\":%d,\"msp\":\"0x%08lX\",\"pc\":\"0x%08lX\"},"
           "\"slot_b\":{\"base\":\"0x%08lX\",\"size\":%lu,\"crc\":\"0x%08lX\",\"valid\":%d,\"msp\":\"0x%08lX\",\"pc\":\"0x%08lX\"}}\r\n",
           BOOTLOADER_VERSION,
           pCfg->active_slot,
           pCfg->boot_count,
           pCfg->max_boot_count,
           (unsigned long)pCfg->total_boot_cycles,
           (unsigned long)(SystemCoreClock / 1000000),
           mf_id, dev_id, (unsigned long)jedec_id, (sr2 & 0x02) ? 1 : 0,
           (unsigned long)SLOT_A_ADDR, (unsigned long)pCfg->slot_a_size, (unsigned long)pCfg->slot_a_crc, (st_a == BOOT_OK) ? 1 : 0, (unsigned long)sp_a, (unsigned long)pc_a,
           (unsigned long)SLOT_B_ADDR, (unsigned long)pCfg->slot_b_size, (unsigned long)pCfg->slot_b_crc, (st_b == BOOT_OK) ? 1 : 0, (unsigned long)sp_b, (unsigned long)pc_b);
}

static void Boot_CLI_Read(uint32_t offset, uint32_t len)
{
    uint8_t buf[16];
    printf("\r\nFlash Hex Dump @ 0x%08lX (Offset: 0x%08lX, Length: %lu):\r\n",
           0x90000000UL + offset, (unsigned long)offset, (unsigned long)len);

    for (uint32_t i = 0; i < len; i += 16)
    {
        uint32_t chunk = ((len - i) < 16) ? (len - i) : 16;
        BSP_W25Qx_ReadBuffer(buf, offset + i, chunk);

        printf("  %08lX: ", (unsigned long)(0x90000000UL + offset + i));
        for (uint32_t j = 0; j < 16; j++)
        {
            if (j < chunk) printf("%02X ", buf[j]);
            else           printf("   ");
        }
        printf(" |");
        for (uint32_t j = 0; j < chunk; j++)
        {
            char c = (buf[j] >= 32 && buf[j] <= 126) ? (char)buf[j] : '.';
            printf("%c", c);
        }
        printf("|\r\n");
    }
    printf("\r\n");
}

void Boot_CLI_Run(void)
{
    char line[CLI_BUFFER_SIZE];
    BSP_RGB_SetColor(RGB_COLOR_CYAN);

    printf("\r\n");
    printf("============================================================\r\n");
    printf("   STM32H750 Bootloader Interactive Console (CLI)\r\n");
    printf("   Type 'help' for commands, 'boot' to run, 'reset' to reboot\r\n");
    printf("============================================================\r\n\r\n");

    while (1)
    {
        printf("bootloader> ");
        Boot_CLI_ReadLine(line, sizeof(line));

        char *cmd = strtok(line, " ");
        if (cmd == NULL) continue;

        if (strcmp(cmd, "help") == 0)
        {
            Boot_CLI_Help();
        }
        else if (strcmp(cmd, "info") == 0)
        {
            Boot_CLI_Info();
        }
        else if (strcmp(cmd, "slots") == 0)
        {
            Boot_CLI_Slots();
        }
        else if (strcmp(cmd, "json") == 0 || strcmp(cmd, "status") == 0)
        {
            Boot_CLI_Json();
        }
        else if (strcmp(cmd, "boot") == 0 || strcmp(cmd, "run") == 0 || strcmp(cmd, "go") == 0)
        {
            char *target = strtok(NULL, " ");
            BootSlot_t slot = BOOT_SLOT_A;
            if (target && (target[0] == 'b' || target[0] == 'B' || target[0] == '1'))
            {
                slot = BOOT_SLOT_B;
            }

            BootConfig_t *pCfg = Bootloader_Config_Get();
            if (pCfg->boot_count != 0)
            {
                pCfg->boot_count = 0;
                Bootloader_Config_Save();
            }

            uint32_t targetAddr = (slot == BOOT_SLOT_A) ? SLOT_A_ADDR : SLOT_B_ADDR;
            printf(" [INFO] Preparing to launch application from %s (0x%08lX)...\r\n",
                   (slot == BOOT_SLOT_A) ? "Slot A" : "Slot B", (unsigned long)targetAddr);

            QUADSPI->CCR = 0;
            QUADSPI->CR &= ~QUADSPI_CR_EN;
            MX_QUADSPI_Init();
            BSP_W25Qx_Reset();
            BSP_W25Qx_EnableMemoryMappedMode();
            Bootloader_JumpToApp(targetAddr);
            return;
        }
        else if (strcmp(cmd, "backup") == 0)
        {
            BootConfig_t *pCfg = Bootloader_Config_Get();
            uint32_t size = pCfg->slot_a_size ? pCfg->slot_a_size : 512 * 1024;
            printf(" [INFO] Initiating backup clone from Slot A -> Slot B (%lu KB)...\r\n", (unsigned long)(size / 1024));
            if (Bootloader_CloneSlot(BOOT_SLOT_A, BOOT_SLOT_B, size) == 0)
            {
                pCfg->slot_b_size = size;
                pCfg->slot_b_crc  = pCfg->slot_a_crc;
                Bootloader_Config_Save();
                printf(" [OK] Backup snapshot created successfully.\r\n");
                Boot_CLI_Json();
            }
            else
            {
                printf(" [ERR] Backup snapshot failed!\r\n");
            }
        }
        else if (strcmp(cmd, "restore") == 0)
        {
            BootConfig_t *pCfg = Bootloader_Config_Get();
            uint32_t size = pCfg->slot_b_size ? pCfg->slot_b_size : 512 * 1024;
            printf(" [INFO] Initiating restore rollback from Slot B -> Slot A (%lu KB)...\r\n", (unsigned long)(size / 1024));
            if (Bootloader_CloneSlot(BOOT_SLOT_B, BOOT_SLOT_A, size) == 0)
            {
                pCfg->slot_a_size = size;
                pCfg->slot_a_crc  = pCfg->slot_b_crc;
                pCfg->boot_count  = 0;
                Bootloader_Config_Save();
                printf(" [OK] Rollback restore completed successfully.\r\n");
                Boot_CLI_Json();
            }
            else
            {
                printf(" [ERR] Rollback restore failed!\r\n");
            }
        }
        else if (strcmp(cmd, "verify") == 0)
        {
            char *target = strtok(NULL, " ");
            BootSlot_t slot = BOOT_SLOT_A;
            if (target && (target[0] == 'b' || target[0] == 'B'))
            {
                slot = BOOT_SLOT_B;
            }
            uint32_t sp = 0, pc = 0;
            BootStatus_t st = Bootloader_VerifySlot(slot, &sp, &pc);
            printf(" Verification Result: %s\r\n", (st == BOOT_OK) ? "[OK]" : "[FAILED]");
        }
        else if (strcmp(cmd, "config") == 0)
        {
            char *arg = strtok(NULL, " ");
            if (arg && strcmp(arg, "reset") == 0)
            {
                Bootloader_Config_ResetToDefaults();
                Bootloader_Config_Save();
                printf(" [OK] NV-Config reset to factory defaults.\r\n");
            }
            else
            {
                Boot_CLI_Slots();
            }
        }
        else if (strcmp(cmd, "reset") == 0)
        {
            printf(" [INFO] Resetting system...\r\n");
            HAL_Delay(100);
            NVIC_SystemReset();
        }
        else if (strcmp(cmd, "ymodem") == 0)
        {
            char *target = strtok(NULL, " ");
            uint32_t destAddr = SLOT_A_ADDR;
            if (target && (target[0] == 'b' || target[0] == 'B'))
            {
                destAddr = SLOT_B_ADDR;
            }

            uint32_t received_size = 0;
            char file_name[64] = {0};
            int res = Ymodem_ReceiveToFlash(destAddr, &received_size, file_name);
            if (res == YMODEM_OK)
            {
                BootConfig_t *pCfg = Bootloader_Config_Get();
                if (destAddr == SLOT_A_ADDR)
                {
                    pCfg->slot_a_size = received_size;
                    pCfg->boot_count = 0;
                    pCfg->upgrade_state = UPGRADE_STATE_NORMAL;
                }
                else
                {
                    pCfg->slot_b_size = received_size;
                }
                Bootloader_Config_Save();

                printf(" [OK] Firmware updated successfully! File: %s (%lu bytes)\r\n", file_name, (unsigned long)received_size);
                Boot_CLI_Json();
            }
            else if (res == YMODEM_TIMEOUT)
            {
                printf(" [INFO] Ymodem transmission aborted due to timeout.\r\n");
            }
            else
            {
                printf(" [ERR] Firmware update failed!\r\n");
            }
        }
        else if (strcmp(cmd, "erase") == 0)
        {
            char *pOffset  = strtok(NULL, " ");
            char *pSectors = strtok(NULL, " ");
            uint32_t offset      = pOffset  ? (uint32_t)strtoul(pOffset, NULL, 0) : 0;
            uint32_t num_sectors = pSectors ? (uint32_t)strtoul(pSectors, NULL, 0) : 16;
            printf(" [INFO] Erasing %lu sectors starting at Flash offset 0x%08lX...\r\n",
                   (unsigned long)num_sectors, (unsigned long)offset);
            for (uint32_t s = 0; s < num_sectors; s++)
            {
                BSP_W25Qx_SectorErase(offset + (s * W25Q_SECTOR_SIZE));
            }
            printf(" [OK] Erase completed.\r\n");
        }
        else if (strcmp(cmd, "read") == 0)
        {
            char *pOffset = strtok(NULL, " ");
            char *pLen = strtok(NULL, " ");
            uint32_t offset = pOffset ? (uint32_t)strtoul(pOffset, NULL, 0) : 0;
            uint32_t len    = pLen ? (uint32_t)strtoul(pLen, NULL, 0) : 256;
            if (offset >= QSPI_BASE_ADDR) offset -= QSPI_BASE_ADDR;
            if (offset >= W25Q_FLASH_SIZE)
            {
                printf(" [ERR] Offset 0x%08lX exceeds 8MB Flash! (Usage: read <offset> [length])\r\n", (unsigned long)offset);
                printf("       Note: Make sure to separate offset and length with space!\r\n");
            }
            else
            {
                if (len > 4096) len = 4096;
                Boot_CLI_Read(offset, len);
            }
        }
        else
        {
            printf(" [ERR] Unknown command: '%s'. Type 'help' for command list.\r\n", cmd);
        }
    }
}
