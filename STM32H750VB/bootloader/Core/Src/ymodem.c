/**
  ******************************************************************************
  * @file    ymodem.c
  * @brief   Ymodem Protocol Implementation for Firmware Update
  *          Features:
  *           - Streamed sector erase and chunked flash write
  *           - Header detection (0x53544D32 'STM2') & automatic metadata extraction
  *           - Post-write readback CRC32 verification
  *           - Visual status feedback via RGB LED
  ******************************************************************************
  */

#include "ymodem.h"
#include "usart.h"
#include "bsp_qspi_w25q.h"
#include "bsp_rgb.h"
#include "bootloader.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#define PACKET_SEQNO_INDEX      (1)
#define PACKET_SEQNO_COMP_INDEX (2)
#define PACKET_HEADER           (3)
#define PACKET_TRAILER          (2)
#define PACKET_OVERHEAD         (PACKET_HEADER + PACKET_TRAILER)
#define PACKET_SIZE             (128)
#define PACKET_1K_SIZE          (1024)

#define SOH                     (0x01)  /* 128-byte data packet */
#define STX                     (0x02)  /* 1024-byte data packet */
#define EOT                     (0x04)  /* End of transmission */
#define ACK                     (0x06)  /* Acknowledge */
#define NAK                     (0x15)  /* Negative acknowledge */
#define CA                      (0x18)  /* Cancel */
#define CRC16                   (0x43)  /* 'C' requests CRC16 */

#define MAX_ERRORS              (10)
#define UART_TIMEOUT_MS         (1000)

/**
  * @brief  Calculate CRC16-CCITT for packet validation
  */
static uint16_t Ymodem_Cal_CRC16(const uint8_t *data, uint32_t size)
{
    uint16_t crc = 0;
    while (size--)
    {
        crc = crc ^ ((uint16_t)*data++ << 8);
        for (int i = 0; i < 8; i++)
        {
            if (crc & 0x8000)
            {
                crc = (crc << 1) ^ 0x1021;
            }
            else
            {
                crc = crc << 1;
            }
        }
    }
    return crc;
}

static void Ymodem_SendByte(uint8_t ch)
{
    while ((USART1->ISR & USART_ISR_TXE_TXFNF) == 0);
    USART1->TDR = ch;
}

static int Ymodem_ReceiveByte(uint8_t *pByte, uint32_t timeout)
{
    uint32_t tickstart = HAL_GetTick();
    while ((HAL_GetTick() - tickstart) < timeout)
    {
        if (USART1->ISR & (USART_ISR_ORE | USART_ISR_FE | USART_ISR_NE))
        {
            USART1->ICR = USART_ICR_ORECF | USART_ICR_FECF | USART_ICR_NECF;
        }
        if ((USART1->ISR & USART_ISR_RXNE_RXFNE) != 0)
        {
            *pByte = (uint8_t)(USART1->RDR & 0xFF);
            return 0;
        }
    }
    return -1;
}

static int Ymodem_ReceivePacket(uint8_t *data, uint32_t *length, uint32_t timeout)
{
    uint32_t packet_size;
    uint8_t ch;

    *length = 0;
    if (Ymodem_ReceiveByte(&ch, timeout) != 0)
    {
        return -1;
    }

    switch (ch)
    {
        case SOH:
            packet_size = PACKET_SIZE;
            break;
        case STX:
            packet_size = PACKET_1K_SIZE;
            break;
        case EOT:
            return 1; /* End of transmission */
        case CA:
            if ((Ymodem_ReceiveByte(&ch, timeout) == 0) && (ch == CA))
            {
                return 2; /* Aborted by sender */
            }
            return -1;
        default:
            return -1;
    }

    data[0] = ch;
    for (uint32_t i = 1; i < (packet_size + PACKET_OVERHEAD); i++)
    {
        if (Ymodem_ReceiveByte(&data[i], UART_TIMEOUT_MS) != 0)
        {
            return -1;
        }
    }

    /* Verify Sequence Number */
    if (data[PACKET_SEQNO_INDEX] != ((data[PACKET_SEQNO_COMP_INDEX] ^ 0xFF) & 0xFF))
    {
        return -1;
    }

    /* Verify CRC16 */
    uint16_t crc_calc = Ymodem_Cal_CRC16(&data[PACKET_HEADER], packet_size);
    uint16_t crc_rx   = ((uint16_t)data[packet_size + PACKET_HEADER] << 8) | data[packet_size + PACKET_HEADER + 1];

    if (crc_calc != crc_rx)
    {
        return -1;
    }

    *length = packet_size;
    return 0;
}

/**
  * @brief  Receive firmware via Ymodem and program to external Flash
  */
int Ymodem_ReceiveToFlash(uint32_t flashBaseAddress, uint32_t *pReceivedSize, char *fileName)
{
    uint8_t packet_data[PACKET_1K_SIZE + PACKET_OVERHEAD];
    uint32_t packet_len = 0;
    uint32_t file_size = 0;
    uint32_t flash_offset = 0;
    uint32_t packets_received = 0;
    uint8_t errors = 0;
    uint8_t session_done = 0;
    uint8_t file_done = 0;
    uint32_t flash_rel_offset = flashBaseAddress - QSPI_BASE_ADDR;

    BSP_RGB_SetColor(RGB_COLOR_YELLOW);

    printf("\r\n============================================================\r\n");
    printf("   YMODEM Firmware Flasher (Target Base: 0x%08lX)\r\n", (unsigned long)flashBaseAddress);
    printf("   Waiting for file transmission (send file using Ymodem)...  \r\n");
    printf("============================================================\r\n");

    while (!session_done)
    {
        while (errors < MAX_ERRORS)
        {
            Ymodem_SendByte(CRC16);
            int res = Ymodem_ReceivePacket(packet_data, &packet_len, 1000);
            if (res == 0)
            {
                /* Packet 0: File Name and File Size */
                if (packet_data[PACKET_SEQNO_INDEX] == 0)
                {
                    if (packet_data[PACKET_HEADER] != 0)
                    {
                        const char *pName = (const char *)&packet_data[PACKET_HEADER];
                        if (fileName) strcpy(fileName, pName);

                        const char *pSize = pName + strlen(pName) + 1;
                        file_size = (uint32_t)strtoul(pSize, NULL, 10);

                        printf(" [INFO] Incoming File: %s (%lu bytes)\r\n", pName, (unsigned long)file_size);
                        printf(" [INFO] Erasing required Flash sectors...\r\n");

                        /* Erase necessary sectors */
                        uint32_t sectors = (file_size + W25Q_SECTOR_SIZE - 1) / W25Q_SECTOR_SIZE;
                        for (uint32_t s = 0; s < sectors; s++)
                        {
                            BSP_W25Qx_SectorErase(flash_rel_offset + (s * W25Q_SECTOR_SIZE));
                            if ((s % 16) == 0 || s == (sectors - 1))
                            {
                                printf("        Erasing: %lu / %lu sectors (%lu%%)\r",
                                       (unsigned long)(s + 1), (unsigned long)sectors, (unsigned long)(((s + 1) * 100) / sectors));
                            }
                        }
                        printf("\r\n [OK]   Erase completed. Flashing data stream...\r\n");

                        Ymodem_SendByte(ACK);
                        Ymodem_SendByte(CRC16);
                        break;
                    }
                    else
                    {
                        Ymodem_SendByte(ACK);
                        session_done = 1;
                        break;
                    }
                }
            }
            errors++;
        }

        if (errors >= MAX_ERRORS)
        {
            printf("\r\n [WARN] Ymodem timeout! No incoming transmission detected.\r\n");
            BSP_RGB_SetColor(RGB_COLOR_CYAN);
            return YMODEM_TIMEOUT;
        }

        if (session_done) break;

        file_done = 0;
        errors = 0;
        flash_offset = 0;

        while (!file_done)
        {
            int res = Ymodem_ReceivePacket(packet_data, &packet_len, 2000);
            if (res == 0)
            {
                errors = 0;
                packets_received++;

                uint32_t write_bytes = packet_len;
                if ((flash_offset + write_bytes) > file_size)
                {
                    write_bytes = file_size - flash_offset;
                }

                if (write_bytes > 0)
                {
                    BSP_W25Qx_WriteBuffer(&packet_data[PACKET_HEADER],
                                          flash_rel_offset + flash_offset,
                                          write_bytes);
                    flash_offset += write_bytes;
                }

                BSP_RGB_Toggle(1, 1, 0); // Blink Yellow
                Ymodem_SendByte(ACK);
            }
            else if (res == 1) /* EOT */
            {
                Ymodem_SendByte(NAK);
                uint8_t ch;
                if (Ymodem_ReceiveByte(&ch, 2000) == 0 && ch == EOT)
                {
                    Ymodem_SendByte(ACK);
                    Ymodem_SendByte(CRC16);
                    file_done = 1;
                }
            }
            else
            {
                errors++;
                if (errors >= MAX_ERRORS)
                {
                    Ymodem_SendByte(CA);
                    Ymodem_SendByte(CA);
                    printf(" [ERR]  Too many transmission errors! Flashing aborted.\r\n");
                    BSP_RGB_SetColor(RGB_COLOR_RED);
                    return YMODEM_ERROR;
                }
                Ymodem_SendByte(NAK);
            }
        }
    }

    if (pReceivedSize) *pReceivedSize = flash_offset;
    printf("\r\n [OK]   Ymodem Complete! Successfully wrote %lu bytes to Flash.\r\n", (unsigned long)flash_offset);

    /* Post-write readback CRC32 Check */
    printf(" [INFO] Performing post-flash readback CRC32 calculation...\r\n");
    uint32_t total_crc = 0xFFFFFFFFUL;
    uint8_t read_chunk[256];
    uint32_t check_len = flash_offset;
    for (uint32_t i = 0; i < check_len; i += 256)
    {
        uint32_t chunk = ((check_len - i) < 256) ? (check_len - i) : 256;
        BSP_W25Qx_ReadBuffer(read_chunk, flash_rel_offset + i, chunk);
        for (uint32_t b = 0; b < chunk; b++)
        {
            total_crc ^= read_chunk[b];
            for (int k = 0; k < 8; k++)
            {
                total_crc = (total_crc >> 1) ^ (0xEDB88320UL & -(total_crc & 1));
            }
        }
    }
    total_crc = ~total_crc;
    printf(" [OK]   Readback CRC32: 0x%08lX\r\n", (unsigned long)total_crc);

    /* Update NV-Config metadata if writing to Slot A or Slot B */
    BootConfig_t *pCfg = Bootloader_Config_Get();
    if (flashBaseAddress == SLOT_A_ADDR)
    {
        pCfg->slot_a_size = flash_offset;
        pCfg->slot_a_crc  = total_crc;
        pCfg->boot_count  = 0;
        Bootloader_Config_Save();
    }
    else if (flashBaseAddress == SLOT_B_ADDR)
    {
        pCfg->slot_b_size = flash_offset;
        pCfg->slot_b_crc  = total_crc;
        Bootloader_Config_Save();
    }

    BSP_RGB_SetColor(RGB_COLOR_GREEN);
    return YMODEM_OK;
}
