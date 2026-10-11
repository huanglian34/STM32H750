/**
  ******************************************************************************
  * @file    bsp_qspi_w25q.h
  * @brief   Board Support Package for Winbond W25Qxx QSPI NOR Flash
  *          Target: W25Q64JVSIQ (8MB, Single Flash, Quad SPI)
  ******************************************************************************
  */

#ifndef __BSP_QSPI_W25Q_H__
#define __BSP_QSPI_W25Q_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"
#include "quadspi.h"

/* Flash Memory Specifications */
#define W25Q_FLASH_SIZE               (8 * 1024 * 1024)   /* 8 MBytes */
#define W25Q_BLOCK_SIZE               (64 * 1024)         /* 64 KBytes */
#define W25Q_SECTOR_SIZE              (4096)              /* 4 KBytes */
#define W25Q_PAGE_SIZE                (256)               /* 256 Bytes */

/* QSPI Return Status */
#define W25Q_OK                       (0)
#define W25Q_ERROR                    (-1)
#define W25Q_TIMEOUT                  (-2)

/* Winbond Standard Command Instructions */
#define W25Q_CMD_WRITE_ENABLE         0x06
#define W25Q_CMD_WRITE_DISABLE        0x04
#define W25Q_CMD_READ_STATUS_REG1     0x05
#define W25Q_CMD_READ_STATUS_REG2     0x35
#define W25Q_CMD_READ_STATUS_REG3     0x15
#define W25Q_CMD_WRITE_STATUS_REG     0x01
#define W25Q_CMD_PAGE_PROGRAM         0x02
#define W25Q_CMD_SECTOR_ERASE         0x20
#define W25Q_CMD_BLOCK_ERASE_32K      0x52
#define W25Q_CMD_BLOCK_ERASE_64K      0xD8
#define W25Q_CMD_CHIP_ERASE           0xC7
#define W25Q_CMD_ENABLE_RESET         0x66
#define W25Q_CMD_RESET_DEVICE         0x99
#define W25Q_CMD_DEVICE_ID            0x90
#define W25Q_CMD_JEDEC_ID             0x9F
#define W25Q_CMD_FAST_READ            0x0B
#define W25Q_CMD_FAST_READ_QUAD_IO    0xEB

/* Status Register Bit Definitions */
#define W25Q_SR1_BUSY_MASK            0x01
#define W25Q_SR1_WEL_MASK             0x02
#define W25Q_SR2_QE_MASK              0x02   /* Quad Enable (QE) bit in Status Register 2 */

/* Exported Functions */
int  BSP_W25Qx_Init(void);
int  BSP_W25Qx_Reset(void);
int  BSP_W25Qx_ReadID(uint8_t *pManufacturerID, uint8_t *pDeviceID);
int  BSP_W25Qx_ReadJEDECID(uint32_t *pJEDECID);
int  BSP_W25Qx_ReadStatusReg(uint8_t regNum, uint8_t *pValue);
int  BSP_W25Qx_WriteEnable(void);
int  BSP_W25Qx_AutoPollingMemReady(uint32_t timeout);
int  BSP_W25Qx_CheckAndEnableQE(void);
int  BSP_W25Qx_EnableMemoryMappedMode(void);
int  BSP_W25Qx_SectorErase(uint32_t sectorAddress);
int  BSP_W25Qx_BlockErase64K(uint32_t blockAddress);
int  BSP_W25Qx_PageProgram(const uint8_t *pData, uint32_t writeAddr, uint32_t size);
int  BSP_W25Qx_WriteBuffer(const uint8_t *pData, uint32_t writeAddr, uint32_t size);
int  BSP_W25Qx_ReadBuffer(uint8_t *pData, uint32_t readAddr, uint32_t size);

#ifdef __cplusplus
}
#endif

#endif /* __BSP_QSPI_W25Q_H__ */
