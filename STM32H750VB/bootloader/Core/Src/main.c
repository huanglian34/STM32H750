/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body for STM32H750VB QSPI Dual-Slot Bootloader
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2023 STMicroelectronics.
  * All rights reserved.
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
#include "bsp_rgb.h"
#include "bsp_qspi_w25q.h"
#include "bsp_mpu.h"
#include "bootloader.h"
#include "boot_cli.h"
#include "ymodem.h"
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

  /* 1. Initialize Onboard RGB LED indicator */
  BSP_RGB_Init();
  BSP_RGB_SetColor(RGB_COLOR_BLUE); // Blue: Bootloader running / Self-test

  /* 2. Output Professional Startup Banner */
  Bootloader_PrintBanner();

  /* 3. Configure MPU & L1 Instruction Cache Acceleration */
  BSP_MPU_Config();
  BSP_Cache_Enable();

  /* 4. Step 1: Clean reset QSPI peripheral state machine and software-reset Flash */
  printf(" [STEP 1/5] Initializing QSPI Controller & Flash Software Reset...\r\n");
  if (BSP_W25Qx_Init() != W25Q_OK)
  {
    Bootloader_Panic("Failed to initialize QSPI / Reset Flash!");
  }
  printf(" [OK]       QSPI Peripheral & Flash Reset Succeeded.\r\n\r\n");

  /* 5. Step 2: Probe Flash ID and JEDEC Information */
  printf(" [STEP 2/5] Probing Flash Hardware Identity...\r\n");
  uint8_t mf_id = 0;
  uint8_t dev_id = 0;
  uint32_t jedec_id = 0;

  if (BSP_W25Qx_ReadID(&mf_id, &dev_id) != W25Q_OK)
  {
    Bootloader_Panic("Failed to read Flash Device ID (Command 0x90)!");
  }
  BSP_W25Qx_ReadJEDECID(&jedec_id);

  printf(" [OK]       Manufacturer ID : 0x%02X (Winbond)\r\n", mf_id);
  printf("            Device ID       : 0x%02X (W25Q64 64M-bit / 8M-Byte)\r\n", dev_id);
  printf("            JEDEC ID        : 0x%06lX\r\n\r\n", (unsigned long)jedec_id);

  /* 6. Step 3: Check and Ensure Quad Enable (QE) Bit */
  printf(" [STEP 3/5] Checking Quad Enable (QE) Status in Flash...\r\n");
  if (BSP_W25Qx_CheckAndEnableQE() != W25Q_OK)
  {
    Bootloader_Panic("Quad Enable (QE) bit configuration failed!");
  }
  printf("\r\n");

  /* 7. Step 4: Load NV-Config Parameters and Check Integrity */
  printf(" [STEP 4/5] Loading Persistent NV-Config Parameters...\r\n");
  Bootloader_Config_Init();
  BootConfig_t *pCfg = Bootloader_Config_Get();

  /* Check health of current active slot */
  uint32_t sp = 0, pc = 0;
  BootStatus_t status = Bootloader_VerifySlot((BootSlot_t)pCfg->active_slot, &sp, &pc);

  if (status != BOOT_OK || (pCfg->upgrade_state == UPGRADE_STATE_TESTING && pCfg->boot_count >= pCfg->max_boot_count))
  {
    if (status != BOOT_OK)
    {
      printf(" [WARN] Active slot image corrupted!\r\n");
    }
    else
    {
      printf(" [WARN] Tentative OTA boot attempts limit reached (%u >= %u). Initiating Rollback...\r\n",
             pCfg->boot_count, pCfg->max_boot_count);
    }

    /* Try automated rollback from backup slot */
    if (Bootloader_Rollback() == 0)
    {
      /* Re-verify after rollback */
      status = Bootloader_VerifySlot((BootSlot_t)pCfg->active_slot, &sp, &pc);
    }
  }
  else
  {
    /* 仅在待确认的 OTA 升级模式下累加试运行计数，日常开发保持 0 避免误触发回滚 */
    if (pCfg->upgrade_state == UPGRADE_STATE_TESTING)
    {
      pCfg->boot_count++;
      Bootloader_Config_Save();
    }
    else if (pCfg->boot_count != 0)
    {
      pCfg->boot_count = 0;
      Bootloader_Config_Save();
    }
  }
  printf("\r\n");

  /* 8. Intercept Check: Allow User to Enter Interactive Console (CLI) or Ymodem IAP */
  if (Boot_CLI_CheckIntercept(2))
  {
    Boot_CLI_Run();
  }

  /* 9. Step 5: Activate 4-Line Memory Mapped Mode (0xEB Fast Read Quad I/O) */
  printf(" [STEP 5/5] Activating 4-Line Memory Mapped Mode (0xEB)...\r\n");

  /* Reset QSPI hardware state machine and soft-reset Flash chip to exit indirect command mode cleanly */
  QUADSPI->CCR = 0;
  QUADSPI->CR &= ~QUADSPI_CR_EN;
  MX_QUADSPI_Init();
  BSP_W25Qx_Reset();

  if (BSP_W25Qx_EnableMemoryMappedMode() != W25Q_OK)
  {
    Bootloader_Panic("Failed to activate QSPI Memory Mapped Mode!");
  }
  printf(" [OK]       Memory Mapped Mode Enabled (Mapped @ 0x%08lX).\r\n\r\n", QSPI_BASE_ADDR);

  /* 10. Verify Application Vector Table and Safely Jump to Active Slot */
  uint32_t targetAddr = (pCfg->active_slot == BOOT_SLOT_A) ? SLOT_A_ADDR : SLOT_B_ADDR;
  Bootloader_JumpToApp(targetAddr);

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

  /** Supply configuration update enable */
  HAL_PWREx_ConfigSupply(PWR_LDO_SUPPLY);

  /** Configure the main internal regulator output voltage: Scale 1 for 400MHz */
  __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE1);

  while(!__HAL_PWR_GET_FLAG(PWR_FLAG_VOSRDY)) {}

  /** Initializes the RCC Oscillators: HSI (64MHz) with PLL1 enabled
    * VCO = (64MHz / 4) * 50 = 800MHz
    * SYSCLK = 800MHz / 2 = 400MHz
    */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI;
  RCC_OscInitStruct.HSIState = RCC_HSI_DIV1;
  RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSI;
  RCC_OscInitStruct.PLL.PLLM = 4;
  RCC_OscInitStruct.PLL.PLLN = 50;
  RCC_OscInitStruct.PLL.PLLP = 2;
  RCC_OscInitStruct.PLL.PLLQ = 4;
  RCC_OscInitStruct.PLL.PLLR = 2;
  RCC_OscInitStruct.PLL.PLLRGE = RCC_PLL1VCIRANGE_3;
  RCC_OscInitStruct.PLL.PLLVCOSEL = RCC_PLL1VCOWIDE;
  RCC_OscInitStruct.PLL.PLLFRACN = 0;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks:
    * SYSCLK = 400MHz, HCLK = 200MHz, APB1/2/3/4 = 100MHz
    */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2
                              |RCC_CLOCKTYPE_D3PCLK1|RCC_CLOCKTYPE_D1PCLK1;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.SYSCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_HCLK_DIV2;
  RCC_ClkInitStruct.APB3CLKDivider = RCC_APB3_DIV2;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_APB1_DIV2;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_APB2_DIV2;
  RCC_ClkInitStruct.APB4CLKDivider = RCC_APB4_DIV2;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_4) != HAL_OK)
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
