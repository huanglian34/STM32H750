/**
  ******************************************************************************
  * @file    boot_cli.h
  * @brief   Interactive Command Line Interface (CLI) for Bootloader
  ******************************************************************************
  */

#ifndef __BOOT_CLI_H__
#define __BOOT_CLI_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"
#include <stdbool.h>

/* Exported Functions */
bool Boot_CLI_CheckIntercept(uint32_t timeoutSeconds);
void Boot_CLI_Run(void);

#ifdef __cplusplus
}
#endif

#endif /* __BOOT_CLI_H__ */
