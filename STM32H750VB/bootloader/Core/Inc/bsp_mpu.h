/**
  ******************************************************************************
  * @file    bsp_mpu.h
  * @brief   Board Support Package for Cortex-M7 MPU & Cache Acceleration
  ******************************************************************************
  */

#ifndef __BSP_MPU_H__
#define __BSP_MPU_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"

/* Exported Functions */
void BSP_MPU_Config(void);
void BSP_Cache_Enable(void);
void BSP_Cache_Disable(void);

#ifdef __cplusplus
}
#endif

#endif /* __BSP_MPU_H__ */
