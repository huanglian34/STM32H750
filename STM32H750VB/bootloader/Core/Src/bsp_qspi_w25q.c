/**
  ******************************************************************************
  * @file    bsp_qspi_w25q.c
  * @brief   Board Support Package for Winbond W25Qxx QSPI NOR Flash
  *          Target: W25Q64JVSIQ (8MB, Single Flash, Quad SPI)
  ******************************************************************************
  */

#include "bsp_qspi_w25q.h"
#include <stdio.h>
#include <string.h>

/**
  * @brief  Poll until Flash memory is not busy
  */
int BSP_W25Qx_AutoPollingMemReady(uint32_t timeout)
{
    QSPI_CommandTypeDef sCmd;
    QSPI_AutoPollingTypeDef sConf;

    memset(&sCmd, 0, sizeof(sCmd));
    memset(&sConf, 0, sizeof(sConf));

    sCmd.InstructionMode   = QSPI_INSTRUCTION_1_LINE;
    sCmd.Instruction       = W25Q_CMD_READ_STATUS_REG1;
    sCmd.DataMode          = QSPI_DATA_1_LINE;
    sCmd.DummyCycles       = 0;

    sConf.Match            = 0x00;
    sConf.Mask             = W25Q_SR1_BUSY_MASK;
    sConf.MatchMode        = QSPI_MATCH_MODE_AND;
    sConf.StatusBytesSize  = 1;
    sConf.Interval         = 0x10;
    sConf.AutomaticStop    = QSPI_AUTOMATIC_STOP_ENABLE;

    if (HAL_QSPI_AutoPolling(&hqspi, &sCmd, &sConf, timeout) != HAL_OK)
    {
        return W25Q_ERROR;
    }

    return W25Q_OK;
}

/**
  * @brief  Send Write Enable (0x06) instruction and wait for WEL latch
  */
int BSP_W25Qx_WriteEnable(void)
{
    QSPI_CommandTypeDef sCmd;
    QSPI_AutoPollingTypeDef sConf;

    memset(&sCmd, 0, sizeof(sCmd));
    memset(&sConf, 0, sizeof(sConf));

    sCmd.InstructionMode = QSPI_INSTRUCTION_1_LINE;
    sCmd.Instruction     = W25Q_CMD_WRITE_ENABLE;
    sCmd.AddressMode     = QSPI_ADDRESS_NONE;
    sCmd.DataMode        = QSPI_DATA_NONE;
    sCmd.DummyCycles     = 0;

    if (HAL_QSPI_Command(&hqspi, &sCmd, HAL_QPSI_TIMEOUT_DEFAULT_VALUE) != HAL_OK)
    {
        return W25Q_ERROR;
    }

    /* Verify WEL bit in Status Register 1 */
    sCmd.Instruction       = W25Q_CMD_READ_STATUS_REG1;
    sCmd.DataMode          = QSPI_DATA_1_LINE;

    sConf.Match            = W25Q_SR1_WEL_MASK;
    sConf.Mask             = W25Q_SR1_WEL_MASK;
    sConf.MatchMode        = QSPI_MATCH_MODE_AND;
    sConf.StatusBytesSize  = 1;
    sConf.Interval         = 0x10;
    sConf.AutomaticStop    = QSPI_AUTOMATIC_STOP_ENABLE;

    if (HAL_QSPI_AutoPolling(&hqspi, &sCmd, &sConf, HAL_QPSI_TIMEOUT_DEFAULT_VALUE) != HAL_OK)
    {
        return W25Q_ERROR;
    }

    return W25Q_OK;
}

/**
  * @brief  Perform software reset on Flash chip
  */
int BSP_W25Qx_Reset(void)
{
    QSPI_CommandTypeDef sCmd;
    memset(&sCmd, 0, sizeof(sCmd));

    sCmd.InstructionMode = QSPI_INSTRUCTION_1_LINE;
    sCmd.AddressMode     = QSPI_ADDRESS_NONE;
    sCmd.DataMode        = QSPI_DATA_NONE;
    sCmd.DummyCycles     = 0;

    /* Step 1: Enable Reset (0x66) */
    sCmd.Instruction = W25Q_CMD_ENABLE_RESET;
    if (HAL_QSPI_Command(&hqspi, &sCmd, HAL_QPSI_TIMEOUT_DEFAULT_VALUE) != HAL_OK)
    {
        return W25Q_ERROR;
    }

    /* Step 2: Reset Device (0x99) */
    sCmd.Instruction = W25Q_CMD_RESET_DEVICE;
    if (HAL_QSPI_Command(&hqspi, &sCmd, HAL_QPSI_TIMEOUT_DEFAULT_VALUE) != HAL_OK)
    {
        return W25Q_ERROR;
    }

    /* Recovery time after reset (typical 30us) */
    HAL_Delay(1);

    return W25Q_OK;
}

/**
  * @brief  Read Manufacturer ID and Device ID (Command 0x90)
  */
int BSP_W25Qx_ReadID(uint8_t *pManufacturerID, uint8_t *pDeviceID)
{
    QSPI_CommandTypeDef sCmd;
    uint8_t id_buf[4] = {0};

    memset(&sCmd, 0, sizeof(sCmd));

    sCmd.InstructionMode = QSPI_INSTRUCTION_1_LINE;
    sCmd.Instruction     = W25Q_CMD_DEVICE_ID;
    sCmd.AddressMode     = QSPI_ADDRESS_1_LINE;
    sCmd.AddressSize     = QSPI_ADDRESS_24_BITS;
    sCmd.Address         = 0x000000;
    sCmd.DataMode        = QSPI_DATA_1_LINE;
    sCmd.DummyCycles     = 0;
    sCmd.NbData          = 2;

    if (HAL_QSPI_Command(&hqspi, &sCmd, HAL_QPSI_TIMEOUT_DEFAULT_VALUE) != HAL_OK)
    {
        return W25Q_ERROR;
    }

    if (HAL_QSPI_Receive(&hqspi, id_buf, HAL_QPSI_TIMEOUT_DEFAULT_VALUE) != HAL_OK)
    {
        return W25Q_ERROR;
    }

    if (pManufacturerID) *pManufacturerID = id_buf[0];
    if (pDeviceID)       *pDeviceID       = id_buf[1];

    return W25Q_OK;
}

/**
  * @brief  Read JEDEC ID (Command 0x9F, 3 bytes: MF ID, Memory Type, Capacity)
  */
int BSP_W25Qx_ReadJEDECID(uint32_t *pJEDECID)
{
    QSPI_CommandTypeDef sCmd;
    uint8_t buf[3] = {0};

    memset(&sCmd, 0, sizeof(sCmd));

    sCmd.InstructionMode = QSPI_INSTRUCTION_1_LINE;
    sCmd.Instruction     = W25Q_CMD_JEDEC_ID;
    sCmd.AddressMode     = QSPI_ADDRESS_NONE;
    sCmd.DataMode        = QSPI_DATA_1_LINE;
    sCmd.DummyCycles     = 0;
    sCmd.NbData          = 3;

    if (HAL_QSPI_Command(&hqspi, &sCmd, HAL_QPSI_TIMEOUT_DEFAULT_VALUE) != HAL_OK)
    {
        return W25Q_ERROR;
    }

    if (HAL_QSPI_Receive(&hqspi, buf, HAL_QPSI_TIMEOUT_DEFAULT_VALUE) != HAL_OK)
    {
        return W25Q_ERROR;
    }

    if (pJEDECID)
    {
        *pJEDECID = ((uint32_t)buf[0] << 16) | ((uint32_t)buf[1] << 8) | buf[2];
    }

    return W25Q_OK;
}

/**
  * @brief  Read Status Register (1, 2, or 3)
  * @param  regNum: 1, 2, or 3
  * @param  pValue: Pointer to received byte
  */
int BSP_W25Qx_ReadStatusReg(uint8_t regNum, uint8_t *pValue)
{
    QSPI_CommandTypeDef sCmd;
    memset(&sCmd, 0, sizeof(sCmd));

    sCmd.InstructionMode = QSPI_INSTRUCTION_1_LINE;
    sCmd.AddressMode     = QSPI_ADDRESS_NONE;
    sCmd.DataMode        = QSPI_DATA_1_LINE;
    sCmd.DummyCycles     = 0;
    sCmd.NbData          = 1;

    switch (regNum)
    {
        case 1: sCmd.Instruction = W25Q_CMD_READ_STATUS_REG1; break;
        case 2: sCmd.Instruction = W25Q_CMD_READ_STATUS_REG2; break;
        case 3: sCmd.Instruction = W25Q_CMD_READ_STATUS_REG3; break;
        default: return W25Q_ERROR;
    }

    if (HAL_QSPI_Command(&hqspi, &sCmd, HAL_QPSI_TIMEOUT_DEFAULT_VALUE) != HAL_OK)
    {
        return W25Q_ERROR;
    }

    if (HAL_QSPI_Receive(&hqspi, pValue, HAL_QPSI_TIMEOUT_DEFAULT_VALUE) != HAL_OK)
    {
        return W25Q_ERROR;
    }

    return W25Q_OK;
}

/**
  * @brief  Intelligently check and enable QE (Quad Enable) bit.
  *         If QE bit is already 1, skip writing to protect Flash endurance and save boot time.
  */
int BSP_W25Qx_CheckAndEnableQE(void)
{
    uint8_t sr2 = 0;
    uint8_t sr1 = 0;

    /* 1. Read Status Register 2 to inspect current QE status */
    if (BSP_W25Qx_ReadStatusReg(2, &sr2) != W25Q_OK)
    {
        printf(" [WARN] Failed to read Status Register 2!\r\n");
        return W25Q_ERROR;
    }

    /* 2. Check if QE bit is already active */
    if (sr2 & W25Q_SR2_QE_MASK)
    {
        printf(" [INFO] Quad Enable (QE) bit is already SET (SR2=0x%02X). Skipping write.\r\n", sr2);
        return W25Q_OK;
    }

    printf(" [INFO] QE bit is 0 (SR2=0x%02X). Programming Non-Volatile Status Register...\r\n", sr2);

    /* 3. Send Write Enable (0x06) */
    if (BSP_W25Qx_WriteEnable() != W25Q_OK)
    {
        printf(" [ERR]  Write Enable command failed before QE programming!\r\n");
        return W25Q_ERROR;
    }

    /* 4. Read SR1 to preserve non-volatile protection bits */
    if (BSP_W25Qx_ReadStatusReg(1, &sr1) != W25Q_OK)
    {
        sr1 = 0x00;
    }

    /* 5. Write Status Register: SR1 (no protect) + SR2 (QE=1) */
    QSPI_CommandTypeDef sCmd;
    uint8_t write_buf[2] = {sr1 & 0xFC, sr2 | W25Q_SR2_QE_MASK}; // Clear SEC/TB if needed, Set QE
    memset(&sCmd, 0, sizeof(sCmd));

    sCmd.InstructionMode = QSPI_INSTRUCTION_1_LINE;
    sCmd.Instruction     = W25Q_CMD_WRITE_STATUS_REG;
    sCmd.AddressMode     = QSPI_ADDRESS_NONE;
    sCmd.DataMode        = QSPI_DATA_1_LINE;
    sCmd.DummyCycles     = 0;
    sCmd.NbData          = 2;

    if (HAL_QSPI_Command(&hqspi, &sCmd, HAL_QPSI_TIMEOUT_DEFAULT_VALUE) != HAL_OK)
    {
        return W25Q_ERROR;
    }

    if (HAL_QSPI_Transmit(&hqspi, write_buf, HAL_QPSI_TIMEOUT_DEFAULT_VALUE) != HAL_OK)
    {
        return W25Q_ERROR;
    }

    /* 6. Wait for Status Register write cycle to finish (up to 15ms) */
    if (BSP_W25Qx_AutoPollingMemReady(HAL_QPSI_TIMEOUT_DEFAULT_VALUE) != W25Q_OK)
    {
        printf(" [ERR]  AutoPolling timeout after Status Register write!\r\n");
        return W25Q_ERROR;
    }

    HAL_Delay(10);

    /* 7. Verify that QE bit is successfully enabled */
    if (BSP_W25Qx_ReadStatusReg(2, &sr2) != W25Q_OK || !(sr2 & W25Q_SR2_QE_MASK))
    {
        printf(" [ERR]  Verification failed! SR2=0x%02X, QE is still not set.\r\n", sr2);
        return W25Q_ERROR;
    }

    printf(" [OK]   QE bit successfully programmed and verified (SR2=0x%02X)!\r\n", sr2);
    return W25Q_OK;
}

/**
  * @brief  Initialize QSPI peripheral state machine and reset Flash
  */
int BSP_W25Qx_Init(void)
{
    /* Force abort and clean reset QUADSPI peripheral state machine */
    QUADSPI->CR |= QUADSPI_CR_ABORT;
    for (volatile int i = 0; i < 5000; i++);
    QUADSPI->CCR = 0;
    QUADSPI->CR &= ~QUADSPI_CR_EN;

    HAL_QSPI_DeInit(&hqspi);
    MX_QUADSPI_Init();

    /* Software reset Flash chip */
    if (BSP_W25Qx_Reset() != W25Q_OK)
    {
        return W25Q_ERROR;
    }

    return W25Q_OK;
}

/**
  * @brief  Configure QUADSPI into 4-line Fast Read Memory-Mapped Mode (0xEB)
  */
int BSP_W25Qx_EnableMemoryMappedMode(void)
{
    QSPI_CommandTypeDef sCmd;
    QSPI_MemoryMappedTypeDef sMapCfg;

    memset(&sCmd, 0, sizeof(sCmd));
    memset(&sMapCfg, 0, sizeof(sMapCfg));

    /* 0xEB: Fast Read Quad I/O */
    sCmd.InstructionMode    = QSPI_INSTRUCTION_1_LINE;
    sCmd.Instruction        = W25Q_CMD_FAST_READ_QUAD_IO;

    /* Address on 4 lines, 24 bits */
    sCmd.AddressMode        = QSPI_ADDRESS_4_LINES;
    sCmd.AddressSize        = QSPI_ADDRESS_24_BITS;
    sCmd.Address            = 0x000000;

    /* Alternate Bytes = 0xFF on 4 lines to prevent entering Continuous Read Mode */
    sCmd.AlternateByteMode  = QSPI_ALTERNATE_BYTES_4_LINES;
    sCmd.AlternateBytesSize = QSPI_ALTERNATE_BYTES_8_BITS;
    sCmd.AlternateBytes     = 0xFF;

    /* 4 dummy cycles following 2 cycles of alternate bytes (total 6 wait states) */
    sCmd.DummyCycles        = 4;

    /* Data on 4 lines */
    sCmd.DataMode           = QSPI_DATA_4_LINES;
    sCmd.NbData             = 0;

    sMapCfg.TimeOutActivation = QSPI_TIMEOUT_COUNTER_DISABLE;

    if (HAL_QSPI_MemoryMapped(&hqspi, &sCmd, &sMapCfg) != HAL_OK)
    {
        return W25Q_ERROR;
    }

    return W25Q_OK;
}

/**
  * @brief  Erase a 4KB Sector
  */
int BSP_W25Qx_SectorErase(uint32_t sectorAddress)
{
    QSPI_CommandTypeDef sCmd;
    memset(&sCmd, 0, sizeof(sCmd));

    if (BSP_W25Qx_WriteEnable() != W25Q_OK)
    {
        return W25Q_ERROR;
    }

    sCmd.InstructionMode = QSPI_INSTRUCTION_1_LINE;
    sCmd.Instruction     = W25Q_CMD_SECTOR_ERASE;
    sCmd.AddressMode     = QSPI_ADDRESS_1_LINE;
    sCmd.AddressSize     = QSPI_ADDRESS_24_BITS;
    sCmd.Address         = sectorAddress;
    sCmd.DataMode        = QSPI_DATA_NONE;
    sCmd.DummyCycles     = 0;

    if (HAL_QSPI_Command(&hqspi, &sCmd, HAL_QPSI_TIMEOUT_DEFAULT_VALUE) != HAL_OK)
    {
        return W25Q_ERROR;
    }

    if (BSP_W25Qx_AutoPollingMemReady(HAL_QPSI_TIMEOUT_DEFAULT_VALUE) != W25Q_OK)
    {
        return W25Q_ERROR;
    }

    return W25Q_OK;
}

/**
  * @brief  Erase a 64KB Block
  */
int BSP_W25Qx_BlockErase64K(uint32_t blockAddress)
{
    QSPI_CommandTypeDef sCmd;
    memset(&sCmd, 0, sizeof(sCmd));

    if (BSP_W25Qx_WriteEnable() != W25Q_OK)
    {
        return W25Q_ERROR;
    }

    sCmd.InstructionMode = QSPI_INSTRUCTION_1_LINE;
    sCmd.Instruction     = W25Q_CMD_BLOCK_ERASE_64K;
    sCmd.AddressMode     = QSPI_ADDRESS_1_LINE;
    sCmd.AddressSize     = QSPI_ADDRESS_24_BITS;
    sCmd.Address         = blockAddress;
    sCmd.DataMode        = QSPI_DATA_NONE;
    sCmd.DummyCycles     = 0;

    if (HAL_QSPI_Command(&hqspi, &sCmd, HAL_QPSI_TIMEOUT_DEFAULT_VALUE) != HAL_OK)
    {
        return W25Q_ERROR;
    }

    if (BSP_W25Qx_AutoPollingMemReady(HAL_QPSI_TIMEOUT_DEFAULT_VALUE) != W25Q_OK)
    {
        return W25Q_ERROR;
    }

    return W25Q_OK;
}

/**
  * @brief  Program up to 256 bytes in a single page
  */
int BSP_W25Qx_PageProgram(const uint8_t *pData, uint32_t writeAddr, uint32_t size)
{
    QSPI_CommandTypeDef sCmd;
    memset(&sCmd, 0, sizeof(sCmd));

    if (pData == NULL || size == 0 || size > W25Q_PAGE_SIZE)
    {
        return W25Q_ERROR;
    }

    if (BSP_W25Qx_WriteEnable() != W25Q_OK)
    {
        return W25Q_ERROR;
    }

    sCmd.InstructionMode = QSPI_INSTRUCTION_1_LINE;
    sCmd.Instruction     = W25Q_CMD_PAGE_PROGRAM;
    sCmd.AddressMode     = QSPI_ADDRESS_1_LINE;
    sCmd.AddressSize     = QSPI_ADDRESS_24_BITS;
    sCmd.Address         = writeAddr;
    sCmd.DataMode        = QSPI_DATA_1_LINE;
    sCmd.DummyCycles     = 0;
    sCmd.NbData          = size;

    if (HAL_QSPI_Command(&hqspi, &sCmd, HAL_QPSI_TIMEOUT_DEFAULT_VALUE) != HAL_OK)
    {
        return W25Q_ERROR;
    }

    if (HAL_QSPI_Transmit(&hqspi, (uint8_t *)pData, HAL_QPSI_TIMEOUT_DEFAULT_VALUE) != HAL_OK)
    {
        return W25Q_ERROR;
    }

    if (BSP_W25Qx_AutoPollingMemReady(HAL_QPSI_TIMEOUT_DEFAULT_VALUE) != W25Q_OK)
    {
        return W25Q_ERROR;
    }

    return W25Q_OK;
}

/**
  * @brief  Write arbitrary length buffer to Flash across page boundaries
  */
int BSP_W25Qx_WriteBuffer(const uint8_t *pData, uint32_t writeAddr, uint32_t size)
{
    uint32_t current_addr = writeAddr;
    uint32_t end_addr = writeAddr + size;
    const uint8_t *current_data = pData;
    uint32_t current_size = W25Q_PAGE_SIZE - (writeAddr % W25Q_PAGE_SIZE);

    if (current_size > size)
    {
        current_size = size;
    }

    while (current_addr < end_addr)
    {
        if (BSP_W25Qx_PageProgram(current_data, current_addr, current_size) != W25Q_OK)
        {
            return W25Q_ERROR;
        }

        current_addr += current_size;
        current_data += current_size;
        current_size = ((current_addr + W25Q_PAGE_SIZE) > end_addr) ? (end_addr - current_addr) : W25Q_PAGE_SIZE;
    }

    return W25Q_OK;
}

/**
  * @brief  Read data from Flash in 1-line Fast Read mode (Command 0x0B)
  */
int BSP_W25Qx_ReadBuffer(uint8_t *pData, uint32_t readAddr, uint32_t size)
{
    QSPI_CommandTypeDef sCmd;
    memset(&sCmd, 0, sizeof(sCmd));

    if (pData == NULL || size == 0)
    {
        return W25Q_ERROR;
    }

    sCmd.InstructionMode = QSPI_INSTRUCTION_1_LINE;
    sCmd.Instruction     = W25Q_CMD_FAST_READ;
    sCmd.AddressMode     = QSPI_ADDRESS_1_LINE;
    sCmd.AddressSize     = QSPI_ADDRESS_24_BITS;
    sCmd.Address         = readAddr;
    sCmd.DataMode        = QSPI_DATA_1_LINE;
    sCmd.DummyCycles     = 8;
    sCmd.NbData          = size;

    if (HAL_QSPI_Command(&hqspi, &sCmd, HAL_QPSI_TIMEOUT_DEFAULT_VALUE) != HAL_OK)
    {
        return W25Q_ERROR;
    }

    if (HAL_QSPI_Receive(&hqspi, pData, HAL_QPSI_TIMEOUT_DEFAULT_VALUE) != HAL_OK)
    {
        return W25Q_ERROR;
    }

    return W25Q_OK;
}
