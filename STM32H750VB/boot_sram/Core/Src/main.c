/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2023 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */
/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include "quadspi.h"
#include "usart.h"
#include "gpio.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include <stdio.h>
#include <string.h>

/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/

/* USER CODE BEGIN PV */

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
/* USER CODE BEGIN PFP */

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */
#ifndef __W25Q64JV_H__
#define __W25Q64JV_H__

#define W25Q64JV_FLASH_SIZE (8 * 1024 * 1024)
#define W25Q64JV_BLOCK_SIZE (64 * 1024)
#define W25Q64JV_SECTOR_SIZE (4096)
#define W25Q64JV_PAGE_SIZE (256)

#define QSPI_ERROR (-1)
#define QSPI_OK (0)

#define W25Q64JV_WRITE_ENABLE (0x06)
#define W25Q64JV_INPUT_FAST_READ (0xeb)
#define W25Q64JV_PAGE_PROGRAM (0x02)
#define W25Q64JV_STATUS_REG1 (0x05)
#define W25Q64JV_ENABLE_RESET (0x66)
#define W25Q64JV_RESET_DEVICE (0x99)
#define W25Q64JV_DEVICE_ID (0x90)
#define W25Q64JV_ID_NUMBER (0x4b)
#define W25Q64JV_ERASE_SECTOR (0x20)
#define W25Q64JV_ERASE_Block32K (0x52)
#define W25Q64JV_ERASE_Block64K (0xD8)
#define W25Q64JV_ERASE_CHIP (0xc7)

extern int QSPI_W25Q64JV_WriteEnable(void);
extern int QSPI_W25Q64JV_Reset(void);
extern int QSPI_W25Q64JV_DeviceID(uint8_t *v);
extern int QSPI_W25Q64JV_IDNumber(uint8_t *v);
extern int QSPI_W25Q64JV_EraseBlock_64K(uint32_t BlockAddress);
extern int QSPI_W25Q64JV_EraseChip(void);
extern int QSPI_W25Q64JV_Read(uint8_t *pData, uint32_t ReadAddr, uint32_t Size);
extern int QSPI_W25Q64JV_PageProgram(uint8_t *pData, uint32_t ReadAddr, uint32_t Size);
extern int QSPI_W25Q64JV_Write(uint8_t *pData, uint32_t WriteAddr, uint32_t Size);
extern int QSPI_W25Q64JV_EnableMemoryMappedMode(void);
extern int QSPI_W25Q64JV_EnableMemoryMappedMode_Quad(void);

#endif

static int QSPI_W25Q64JV_AutoPollingMemReady(uint32_t timeout)
{
  QSPI_CommandTypeDef sCmd;
  QSPI_AutoPollingTypeDef sConf;
  memset(&sCmd, 0, sizeof(sCmd));
  memset(&sConf, 0, sizeof(sConf));

  sCmd.InstructionMode = QSPI_INSTRUCTION_1_LINE;
  sCmd.Instruction = W25Q64JV_STATUS_REG1;

  sCmd.DataMode = QSPI_DATA_1_LINE;
  sCmd.DummyCycles = 0;

  sConf.Match = 0x00;
  sConf.Mask = 0x01;
  sConf.MatchMode = QSPI_MATCH_MODE_AND;
  sConf.StatusBytesSize = 1;
  sConf.Interval = 0x10;
  sConf.AutomaticStop = QSPI_AUTOMATIC_STOP_ENABLE;

  if (HAL_QSPI_AutoPolling(&hqspi, &sCmd, &sConf, timeout) != HAL_OK) return QSPI_ERROR;

  return QSPI_OK;
}

int QSPI_W25Q64JV_DeviceID(uint8_t *v)
{
  QSPI_CommandTypeDef sCmd;
  memset(&sCmd, 0, sizeof(sCmd));

  sCmd.InstructionMode = QSPI_INSTRUCTION_1_LINE;
  sCmd.Instruction = W25Q64JV_DEVICE_ID;

  sCmd.AddressMode = QSPI_ADDRESS_1_LINE;
  sCmd.AddressSize = QSPI_ADDRESS_24_BITS;
  sCmd.Address = 0;

  sCmd.DataMode = QSPI_DATA_1_LINE;
  sCmd.DummyCycles = 0;
  sCmd.NbData = 4;

  if (HAL_QSPI_Command(&hqspi, &sCmd, HAL_QPSI_TIMEOUT_DEFAULT_VALUE) != HAL_OK) return QSPI_ERROR;
  if (HAL_QSPI_Receive(&hqspi, (uint8_t *)v, HAL_QPSI_TIMEOUT_DEFAULT_VALUE) != HAL_OK) return QSPI_ERROR;

  return QSPI_OK;
}

int QSPI_W25Q64JV_WriteEnable(void)
{
  QSPI_CommandTypeDef sCmd;
  QSPI_AutoPollingTypeDef sConf;
  memset(&sCmd, 0, sizeof(sCmd));
  memset(&sConf, 0, sizeof(sConf));

  sCmd.InstructionMode = QSPI_INSTRUCTION_1_LINE;
  sCmd.Instruction = W25Q64JV_WRITE_ENABLE;

  if (HAL_QSPI_Command(&hqspi, &sCmd, HAL_MAX_DELAY) != HAL_OK) return QSPI_ERROR;

  /* Configure automatic polling mode to wait for write enabling */
  sCmd.InstructionMode = QSPI_INSTRUCTION_1_LINE;
  sCmd.Instruction = W25Q64JV_STATUS_REG1;

  sCmd.DataMode = QSPI_DATA_1_LINE;
  sCmd.DummyCycles = 0;

  sConf.Match = 0x02;
  sConf.Mask = 0x02;
  sConf.MatchMode = QSPI_MATCH_MODE_AND;
  sConf.StatusBytesSize = 1;
  sConf.Interval = 0x10;
  sConf.AutomaticStop = QSPI_AUTOMATIC_STOP_ENABLE;

  if (HAL_QSPI_AutoPolling(&hqspi, &sCmd, &sConf, HAL_MAX_DELAY) != HAL_OK) return QSPI_ERROR;

  return QSPI_OK;
}

int QSPI_W25Q64JV_Reset(void)
{
  QSPI_CommandTypeDef sCmd;
  memset(&sCmd, 0, sizeof(sCmd));

  sCmd.InstructionMode = QSPI_INSTRUCTION_1_LINE;
  sCmd.Instruction = W25Q64JV_ENABLE_RESET;
  if (HAL_QSPI_Command(&hqspi, &sCmd, HAL_MAX_DELAY) != HAL_OK) return QSPI_ERROR;

  sCmd.InstructionMode = QSPI_INSTRUCTION_1_LINE;
  sCmd.Instruction = W25Q64JV_RESET_DEVICE;
  if (HAL_QSPI_Command(&hqspi, &sCmd, HAL_MAX_DELAY) != HAL_OK) return QSPI_ERROR;

  HAL_Delay(1);
  if (QSPI_W25Q64JV_AutoPollingMemReady(HAL_MAX_DELAY) != QSPI_OK) return QSPI_ERROR;

  return QSPI_OK;
}

int QSPI_W25Q64JV_IDNumber(uint8_t *v)
{
  QSPI_CommandTypeDef sCmd;
  memset(&sCmd, 0, sizeof(sCmd));

  sCmd.InstructionMode = QSPI_INSTRUCTION_1_LINE;
  sCmd.Instruction = W25Q64JV_ID_NUMBER;

  sCmd.DataMode = QSPI_DATA_1_LINE;
  sCmd.DummyCycles = 0;
  sCmd.NbData = 24;

  if (HAL_QSPI_Command(&hqspi, &sCmd, HAL_QPSI_TIMEOUT_DEFAULT_VALUE) != HAL_OK) return QSPI_ERROR;

  if (HAL_QSPI_Receive(&hqspi, (uint8_t *)v, HAL_QPSI_TIMEOUT_DEFAULT_VALUE) != HAL_OK) return QSPI_ERROR;

  return QSPI_OK;
}

int QSPI_W25Q64JV_Read(uint8_t *pData, uint32_t ReadAddr, uint32_t Size)
{
  QSPI_CommandTypeDef sCmd;
  memset(&sCmd, 0, sizeof(sCmd));

  sCmd.InstructionMode = QSPI_INSTRUCTION_1_LINE;
  sCmd.Instruction = 0x03; // Standard Read (1-line)

  sCmd.AddressMode = QSPI_ADDRESS_1_LINE;
  sCmd.AddressSize = QSPI_ADDRESS_24_BITS;
  sCmd.Address = ReadAddr;

  sCmd.DataMode = QSPI_DATA_1_LINE;
  sCmd.DummyCycles = 0;
  sCmd.NbData = Size;

  if (HAL_QSPI_Command(&hqspi, &sCmd, HAL_QPSI_TIMEOUT_DEFAULT_VALUE) != HAL_OK) return QSPI_ERROR;
  if (HAL_QSPI_Receive(&hqspi, pData, HAL_QPSI_TIMEOUT_DEFAULT_VALUE) != HAL_OK) return QSPI_ERROR;

  return QSPI_OK;
}

int QSPI_W25Q64JV_EraseBlock_32Kx2(uint32_t BlockAddress)
{
  QSPI_CommandTypeDef sCmd;
  memset(&sCmd, 0, sizeof(sCmd));

  if (QSPI_W25Q64JV_WriteEnable() != QSPI_OK) return QSPI_ERROR;

  sCmd.InstructionMode = QSPI_INSTRUCTION_1_LINE;
  sCmd.Instruction = W25Q64JV_ERASE_Block32K;

  sCmd.AddressMode = QSPI_ADDRESS_1_LINE;
  sCmd.AddressSize = QSPI_ADDRESS_24_BITS;
  sCmd.Address = BlockAddress;

  if (HAL_QSPI_Command(&hqspi, &sCmd, HAL_QPSI_TIMEOUT_DEFAULT_VALUE) != HAL_OK) return QSPI_ERROR;
  if (QSPI_W25Q64JV_AutoPollingMemReady(HAL_QPSI_TIMEOUT_DEFAULT_VALUE) != QSPI_OK) return QSPI_ERROR;

  return QSPI_OK;
}

int QSPI_W25Q64JV_EraseChip(void)
{
  QSPI_CommandTypeDef sCmd;
  memset(&sCmd, 0, sizeof(sCmd));

  if (QSPI_W25Q64JV_WriteEnable() != QSPI_OK) return QSPI_ERROR;

  sCmd.InstructionMode = QSPI_INSTRUCTION_1_LINE;
  sCmd.Instruction = W25Q64JV_ERASE_CHIP;

  if (HAL_QSPI_Command(&hqspi, &sCmd, HAL_QPSI_TIMEOUT_DEFAULT_VALUE) != HAL_OK) return QSPI_ERROR;
  if (QSPI_W25Q64JV_AutoPollingMemReady(HAL_QPSI_TIMEOUT_DEFAULT_VALUE) != QSPI_OK) return QSPI_ERROR;

  return QSPI_OK;
}

int QSPI_W25Q64JV_PageProgram(uint8_t *pData, uint32_t ReadAddr, uint32_t Size)
{
  QSPI_CommandTypeDef sCmd;
  memset(&sCmd, 0, sizeof(sCmd));

  if (QSPI_W25Q64JV_WriteEnable() != QSPI_OK) return QSPI_ERROR;

  sCmd.InstructionMode = QSPI_INSTRUCTION_1_LINE;
  sCmd.Instruction = W25Q64JV_PAGE_PROGRAM;

  sCmd.AddressMode = QSPI_ADDRESS_1_LINE;
  sCmd.AddressSize = QSPI_ADDRESS_24_BITS;
  sCmd.Address = ReadAddr;

  sCmd.DataMode = QSPI_DATA_1_LINE;
  sCmd.DummyCycles = 0;
  sCmd.NbData = Size;

  if (HAL_QSPI_Command(&hqspi, &sCmd, HAL_QPSI_TIMEOUT_DEFAULT_VALUE) != HAL_OK) return QSPI_ERROR;
  if (HAL_QSPI_Transmit(&hqspi, pData, HAL_QPSI_TIMEOUT_DEFAULT_VALUE) != HAL_OK) return QSPI_ERROR;
  if (QSPI_W25Q64JV_AutoPollingMemReady(HAL_QPSI_TIMEOUT_DEFAULT_VALUE) != QSPI_OK) return QSPI_ERROR;

  return QSPI_OK;
}

int QSPI_W25Q64JV_Write(uint8_t *pData, uint32_t WriteAddr, uint32_t Size)
{
  int ret = QSPI_OK;
  uint32_t end_addr, current_size, current_addr;
  uint8_t *write_data;

  /* Calculation of the size between the write address and the end of the page */
  current_size = W25Q64JV_PAGE_SIZE - (WriteAddr % W25Q64JV_PAGE_SIZE);

  /* Check if the size of the data is less than the remaining place in the page */
  if (current_size > Size)
  {
    current_size = Size;
  }

  /* Initialize the address variables */
  current_addr = WriteAddr;
  end_addr = WriteAddr + Size;
  write_data = pData;

  /* Perform the write page by page */
  do
  {
    /* Issue page program command */
    if (QSPI_W25Q64JV_PageProgram(write_data, current_addr, current_size) != QSPI_OK)
    {
        ret = QSPI_ERROR;
    }
    else
    {
      /* Update the address and size variables for next page programming */
      current_addr += current_size;
      write_data += current_size;
      current_size = ((current_addr + W25Q64JV_PAGE_SIZE) > end_addr) ? (end_addr - current_addr) : W25Q64JV_PAGE_SIZE;
    }
  } while ((current_addr < end_addr) && (ret == QSPI_OK));

  /* Return BSP status */
  return ret;
}

int QSPI_W25Q64JV_EnableQE(void)
{
  QSPI_CommandTypeDef sCmd;
  uint8_t write_buf[2] = {0x00, 0x02}; // SR1=0x00 (No protect), SR2=0x02 (QE=1)
  memset(&sCmd, 0, sizeof(sCmd));

  printf("Configuring QE bit via instruction 0x01 (SR1=0x00, SR2=0x02)...\r\n");

  /* Write Enable (0x06) */
  if (QSPI_W25Q64JV_WriteEnable() != QSPI_OK)
  {
    printf("Error: WriteEnable failed!\r\n");
    return QSPI_ERROR;
  }

  sCmd.InstructionMode = QSPI_INSTRUCTION_1_LINE;
  sCmd.Instruction = 0x01; // Write Status Register
  sCmd.AddressMode = QSPI_ADDRESS_NONE;
  sCmd.DataMode = QSPI_DATA_1_LINE;
  sCmd.DummyCycles = 0;
  sCmd.NbData = 2;

  if (HAL_QSPI_Command(&hqspi, &sCmd, HAL_QPSI_TIMEOUT_DEFAULT_VALUE) != HAL_OK)
  {
    printf("Error: Command 0x01 failed!\r\n");
    return QSPI_ERROR;
  }

  if (HAL_QSPI_Transmit(&hqspi, write_buf, HAL_QPSI_TIMEOUT_DEFAULT_VALUE) != HAL_OK)
  {
    printf("Error: Transmit failed!\r\n");
    return QSPI_ERROR;
  }

  /* Wait for completion (Status Register Write Cycle takes up to 15ms) */
  if (QSPI_W25Q64JV_AutoPollingMemReady(HAL_QPSI_TIMEOUT_DEFAULT_VALUE) != QSPI_OK)
  {
    printf("Error: AutoPolling after Write Status Register failed!\r\n");
    return QSPI_ERROR;
  }
  HAL_Delay(15);

  printf("QE bit successfully enabled and programmed!\r\n");
  return QSPI_OK;
}

int QSPI_W25Q64JV_EnableMemoryMappedMode_Quad(void)
{
  QSPI_CommandTypeDef sCmd;
  QSPI_MemoryMappedTypeDef sMapCfg;
  memset(&sCmd, 0, sizeof(sCmd));
  memset(&sMapCfg, 0, sizeof(sMapCfg));

  sCmd.InstructionMode = QSPI_INSTRUCTION_1_LINE;
  sCmd.Instruction = W25Q64JV_INPUT_FAST_READ; // 0xEB (Fast Read Quad I/O)

  sCmd.AddressMode = QSPI_ADDRESS_4_LINES;
  sCmd.AddressSize = QSPI_ADDRESS_24_BITS;
  sCmd.Address = 0;

  /* Essential: Send Alternate Bytes = 0xFF on 4 lines to prevent entering Continuous Read Mode */
  sCmd.AlternateByteMode = QSPI_ALTERNATE_BYTES_4_LINES;
  sCmd.AlternateBytesSize = QSPI_ALTERNATE_BYTES_8_BITS;
  sCmd.AlternateBytes = 0xFF;

  /* 4 dummy cycles following the 2 cycles of alternate bytes (total 6 wait cycles) */
  sCmd.DummyCycles = 4;

  sCmd.DataMode = QSPI_DATA_4_LINES;
  sCmd.NbData = 0;

  sMapCfg.TimeOutActivation = QSPI_TIMEOUT_COUNTER_DISABLE;
  if (HAL_QSPI_MemoryMapped(&hqspi, &sCmd, &sMapCfg) != HAL_OK) return QSPI_ERROR;

  return QSPI_OK;
}

#define APPLICATION_ADDRESS 0x90000000UL
typedef void (*appFun)(void);
appFun App;
static void GoToApp(void)
{
    volatile uint32_t *vector_table = (volatile uint32_t *)APPLICATION_ADDRESS;
    uint32_t stack_point = vector_table[0];
    uint32_t entry_point = vector_table[1];

    printf("Vector Table @ 0x%08lX:\r\n", APPLICATION_ADDRESS);
    printf("  MSP: 0x%08lX\r\n", (unsigned long)stack_point);
    printf("  PC : 0x%08lX\r\n", (unsigned long)entry_point);

    if ((entry_point & 0xf0000000) == APPLICATION_ADDRESS)
    {
      printf("Jumping to Application...\r\n\r\n");
      while (__HAL_UART_GET_FLAG(&huart1, UART_FLAG_TC) == RESET);

      __disable_irq();

      SysTick->CTRL = 0;
      SysTick->LOAD = 0;
      SysTick->VAL = 0;

      for (int i = 0; i < 8; i++)
      {
          NVIC->ICER[i] = 0xFFFFFFFF;
          NVIC->ICPR[i] = 0xFFFFFFFF;
      }

      SCB->VTOR = APPLICATION_ADDRESS;

      __set_MSP(stack_point);
      __set_CONTROL(0);

      __DSB();
      __ISB();

      App = (appFun)entry_point;
      App();
    }
    else
    {
      printf("Error: Invalid Application Vector! PC = 0x%08lX\r\n", (unsigned long)entry_point);
      QUADSPI->CCR = QUADSPI->CCR & 0xf7ffffff; // Indirect read mode
      QUADSPI->CR = QUADSPI->CR & 0xfffffffe;   // Disable the QUADSPI.
    }
}

uint8_t pagebuf[256];
/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{
  /* USER CODE BEGIN 1 */

  /* USER CODE END 1 */

  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  HAL_Init();

  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* Configure the system clock */
  SystemClock_Config();

  /* USER CODE BEGIN SysInit */

  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_QUADSPI_Init();
  MX_USART1_UART_Init();
  /* USER CODE BEGIN 2 */
  printf("STM32H750VB Bootloader Starting...\r\n");

  /* Force abort and clean reset QUADSPI peripheral state machine */
  QUADSPI->CR |= QUADSPI_CR_ABORT;
  for (volatile int i = 0; i < 5000; i++);
  QUADSPI->CCR = 0;
  QUADSPI->CR &= ~QUADSPI_CR_EN;
  HAL_QSPI_DeInit(&hqspi);
  MX_QUADSPI_Init();

  uint8_t id[4] = {0};
  QSPI_W25Q64JV_Reset();
  if (QSPI_W25Q64JV_DeviceID(id) == QSPI_OK)
  {
    printf("W25Q64 Manufacturer ID: 0x%02X, Device ID: 0x%02X\r\n", id[0], id[1]);
  }

  /* 1. Ensure Quad Enable (QE) bit is permanently set */
  if (QSPI_W25Q64JV_EnableQE() != QSPI_OK)
  {
    printf("Warning: Failed to configure QE bit!\r\n");
  }

  /* 2. Reset QSPI hardware state machine */
  QUADSPI->CCR = 0;
  QUADSPI->CR &= ~QUADSPI_CR_EN;
  MX_QUADSPI_Init();

  /* 3. Software Reset Flash chip */
  QSPI_W25Q64JV_Reset();

  /* 4. Enable Quad (4-line) Memory Mapped Mode (0xEB Fast Read Quad I/O) */
  printf("Entering Quad (4-line) Memory Mapped Mode (0xEB)...\r\n");
  if (QSPI_W25Q64JV_EnableMemoryMappedMode_Quad() == QSPI_OK)
  {
    printf("Quad Memory Mapped Mode Enabled.\r\n");
    GoToApp();
  }
  else
  {
    printf("Error: Failed to Enable Quad Memory Mapped Mode!\r\n");
  }
  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
  }
  /* USER CODE END 3 */
}

/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  /** Supply configuration update enable
  */
  HAL_PWREx_ConfigSupply(PWR_LDO_SUPPLY);

  /** Configure the main internal regulator output voltage
  */
  __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE3);

  while(!__HAL_PWR_GET_FLAG(PWR_FLAG_VOSRDY)) {}

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI;
  RCC_OscInitStruct.HSIState = RCC_HSI_DIV1;
  RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_NONE;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2
                              |RCC_CLOCKTYPE_D3PCLK1|RCC_CLOCKTYPE_D1PCLK1;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_HSI;
  RCC_ClkInitStruct.SYSCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_HCLK_DIV1;
  RCC_ClkInitStruct.APB3CLKDivider = RCC_APB3_DIV2;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_APB1_DIV2;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_APB2_DIV2;
  RCC_ClkInitStruct.APB4CLKDivider = RCC_APB4_DIV2;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_1) != HAL_OK)
  {
    Error_Handler();
  }
}

/* USER CODE BEGIN 4 */

/* USER CODE END 4 */

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  /* User can add his own implementation to report the HAL error return state */
  __disable_irq();
  while (1)
  {
  }
  /* USER CODE END Error_Handler_Debug */
}

#ifdef  USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line source number
  * @retval None
  */
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  /* User can add his own implementation to report the file name and line number,
     ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
