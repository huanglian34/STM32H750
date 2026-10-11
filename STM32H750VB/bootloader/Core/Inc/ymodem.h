/**
  ******************************************************************************
  * @file    ymodem.h
  * @brief   Ymodem Protocol Implementation for Firmware Update
  ******************************************************************************
  */

#ifndef __YMODEM_H__
#define __YMODEM_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"
#include <stdint.h>

#define YMODEM_OK             (0)
#define YMODEM_ERROR          (-1)
#define YMODEM_ABORT          (-2)
#define YMODEM_TIMEOUT        (-3)

/* Exported Functions */
int Ymodem_ReceiveToFlash(uint32_t flashBaseAddress, uint32_t *pReceivedSize, char *fileName);

#ifdef __cplusplus
}
#endif

#endif /* __YMODEM_H__ */
