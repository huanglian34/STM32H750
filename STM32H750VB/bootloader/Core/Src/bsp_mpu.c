/**
  ******************************************************************************
  * @file    bsp_mpu.c
  * @brief   Board Support Package for Cortex-M7 MPU & Cache Acceleration
  ******************************************************************************
  */

#include "bsp_mpu.h"
#include <stdio.h>

/**
  * @brief  Configure MPU attributes for QSPI NOR Flash and AXI SRAM
  */
void BSP_MPU_Config(void)
{
    MPU_Region_InitTypeDef MPU_InitStruct = {0};

    /* Disable MPU before configuration */
    HAL_MPU_Disable();

    /* -----------------------------------------------------------------------
     * Region 0: QSPI NOR Flash (0x90000000 - 0x907FFFFF, 8MB)
     * Attribute: Normal, Cacheable, Executable (Read-Only / Full Access)
     * ----------------------------------------------------------------------- */
    MPU_InitStruct.Enable           = MPU_REGION_ENABLE;
    MPU_InitStruct.Number           = MPU_REGION_NUMBER0;
    MPU_InitStruct.BaseAddress      = 0x90000000UL;
    MPU_InitStruct.Size             = MPU_REGION_SIZE_8MB;
    MPU_InitStruct.SubRegionDisable = 0x00;
    MPU_InitStruct.TypeExtField     = MPU_TEX_LEVEL0;
    MPU_InitStruct.AccessPermission = MPU_REGION_FULL_ACCESS;
    MPU_InitStruct.DisableExec      = MPU_INSTRUCTION_ACCESS_ENABLE;
    MPU_InitStruct.IsShareable      = MPU_ACCESS_NOT_SHAREABLE;
    MPU_InitStruct.IsCacheable      = MPU_ACCESS_CACHEABLE;
    MPU_InitStruct.IsBufferable     = MPU_ACCESS_NOT_BUFFERABLE;
    HAL_MPU_ConfigRegion(&MPU_InitStruct);

    /* -----------------------------------------------------------------------
     * Region 1: AXI SRAM (0x24000000 - 0x2407FFFF, 512KB)
     * Attribute: Normal, Cacheable, Bufferable (Write-Through/Write-Back)
     * ----------------------------------------------------------------------- */
    MPU_InitStruct.Enable           = MPU_REGION_ENABLE;
    MPU_InitStruct.Number           = MPU_REGION_NUMBER1;
    MPU_InitStruct.BaseAddress      = 0x24000000UL;
    MPU_InitStruct.Size             = MPU_REGION_SIZE_512KB;
    MPU_InitStruct.SubRegionDisable = 0x00;
    MPU_InitStruct.TypeExtField     = MPU_TEX_LEVEL0;
    MPU_InitStruct.AccessPermission = MPU_REGION_FULL_ACCESS;
    MPU_InitStruct.DisableExec      = MPU_INSTRUCTION_ACCESS_ENABLE;
    MPU_InitStruct.IsShareable      = MPU_ACCESS_NOT_SHAREABLE;
    MPU_InitStruct.IsCacheable      = MPU_ACCESS_CACHEABLE;
    MPU_InitStruct.IsBufferable     = MPU_ACCESS_BUFFERABLE;
    HAL_MPU_ConfigRegion(&MPU_InitStruct);

    /* Enable MPU with default memory map for background regions */
    HAL_MPU_Enable(MPU_PRIVILEGED_DEFAULT);

    printf(" [OK]       MPU Configured: QSPI 0x90000000 [Normal Cacheable Executable]\r\n");
}

/**
  * @brief  Enable Cortex-M7 L1 Instruction Cache
  */
void BSP_Cache_Enable(void)
{
    /* Enable I-Cache */
    SCB_EnableICache();
    printf(" [OK]       L1 Instruction Cache (I-Cache) Enabled.\r\n");
}

/**
  * @brief  Disable Cache before Handover if needed
  */
void BSP_Cache_Disable(void)
{
    SCB_DisableICache();
    __DSB();
    __ISB();
}
