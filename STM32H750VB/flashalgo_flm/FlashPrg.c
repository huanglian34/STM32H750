/* -----------------------------------------------------------------------------
 * Copyright (c) 2014 ARM Ltd.
 *
 * This software is provided 'as-is', without any express or implied warranty.
 * In no event will the authors be held liable for any damages arising from
 * the use of this software. Permission is granted to anyone to use this
 * software for any purpose, including commercial applications, and to alter
 * it and redistribute it freely, subject to the following restrictions:
 *
 * 1. The origin of this software must not be misrepresented; you must not
 *    claim that you wrote the original software. If you use this software in
 *    a product, an acknowledgment in the product documentation would be
 *    appreciated but is not required.
 *
 * 2. Altered source versions must be plainly marked as such, and must not be
 *    misrepresented as being the original software.
 *
 * 3. This notice may not be removed or altered from any source distribution.
 *
 *
 * $Date:        15. April 2014
 * $Revision:    V1.00
 *
 * Project:      Flash Programming Functions for STMicroelectronics STM32F4xx Flash
 * --------------------------------------------------------------------------- */

/* History:
 *  Version 1.00
 *    Initial release
 */

#include "FlashOS.H" // FlashOS Structures

#include <stdint.h>
#include <string.h>

#include "main.h"
#include "quadspi.h"
#include "gpio.h"
#include "w25q64jv.h"

extern void SystemClock_Config(void);

HAL_StatusTypeDef HAL_InitTick(uint32_t TickPriority)
{
    return HAL_OK;
}

void HAL_Delay(uint32_t Delay)
{
    volatile int i = 0;
    for (i = 0; i < 4096; i++)
        ;
}

uint32_t HAL_GetTick(void)
{
    volatile int i;
    for (i = 0; i < 512; i++)
        ;
    uwTick++;
    return uwTick;
}

/*
 *  Initialize Flash Programming Functions
 *    Parameter:      adr:  Device Base Address
 *                    clk:  Clock Frequency (Hz)
 *                    fnc:  Function Code (1 - Erase, 2 - Program, 3 - Verify)
 *    Return Value:   0 - OK,  1 - Failed
 */
uint32_t base_adr = 0;
int Init(unsigned long adr, unsigned long clk, unsigned long fnc)
{
    base_adr = adr;

    SystemInit();
    HAL_Init();

    SystemClock_Config();
    // MX_GPIO_Init();

    QUADSPI->CCR = QUADSPI->CCR & 0xf7ffffff; // Indirect read mode
    QUADSPI->CR = QUADSPI->CR & 0xfffffffe;   // Disable the QUADSPI.
    MX_QUADSPI_Init();

    QSPI_W25Q64JV_Reset();
    QSPI_W25Q64JV_EnableMemoryMappedMode();
    return (0);
}

/*
 *  De-Initialize Flash Programming Functions
 *    Parameter:      fnc:  Function Code (1 - Erase, 2 - Program, 3 - Verify)
 *    Return Value:   0 - OK,  1 - Failed
 */

int UnInit(unsigned long fnc)
{
    return (0);
}

/*
 *  Erase complete Flash Memory
 *    Return Value:   0 - OK,  1 - Failed
 */

int EraseChip(void)
{
    HAL_QSPI_DeInit(&hqspi);
    MX_QUADSPI_Init();
    QSPI_W25Q64JV_Reset();

    if (QSPI_W25Q64JV_EraseChip() != QSPI_OK)
    {
        return 0;
    }
    for (volatile uint32_t i = 0; i < 999; i++)
    {
        ;
    }
    QSPI_W25Q64JV_EnableMemoryMappedMode();
    return (0);
}

/*
 *  Erase Sector in Flash Memory
 *    Parameter:      adr:  Sector Address
 *    Return Value:   0 - OK,  1 - Failed
 */

int EraseSector(unsigned long adr)
{
    HAL_QSPI_DeInit(&hqspi);
    MX_QUADSPI_Init();
    QSPI_W25Q64JV_Reset();

    if (QSPI_W25Q64JV_EraseBlock_64K(adr & 0x007FFFFF) != QSPI_OK)
    {
        return (1);
    }
    for (volatile uint32_t i = 0; i < 999; i++)
    {
        ;
    }
    QSPI_W25Q64JV_EnableMemoryMappedMode();
    return (0);
}

/*
 *  Program Page in Flash Memory
 *    Parameter:      adr:  Page Start Address
 *                    sz:   Page Size
 *                    buf:  Page Data
 *    Return Value:   0 - OK,  1 - Failed
 */

int ProgramPage(unsigned long adr, unsigned long sz, unsigned char *buf)
{
    HAL_QSPI_DeInit(&hqspi);
    MX_QUADSPI_Init();
    QSPI_W25Q64JV_Reset();

    if (QSPI_W25Q64JV_Write(buf, adr & 0x007FFFFF, sz) != QSPI_OK)
    {
        return (1);
    }
    for (volatile uint32_t i = 0; i < 999; i++)
    {
        ;
    }
    QSPI_W25Q64JV_EnableMemoryMappedMode();
    return (0);
}


unsigned long Verify(unsigned long adr, unsigned long sz, unsigned char *buf)
{
    uint8_t *p = (uint8_t *)adr;

    for (int i = 0; i < sz; i++)
    {
        if (*p != buf[i])
        {
            return ((unsigned long)p);
        }
        p++;
    }

    return (adr + sz);
}
