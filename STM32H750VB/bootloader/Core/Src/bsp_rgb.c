/**
  ******************************************************************************
  * @file    bsp_rgb.c
  * @brief   Board Support Package for Onboard RGB LED (STM32H750VBT6)
  ******************************************************************************
  */

#include "bsp_rgb.h"

/**
  * @brief  Initialize RGB LED GPIO pins
  */
void BSP_RGB_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    /* Enable GPIO Clocks */
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOC_CLK_ENABLE();

    /* Initial state: All LEDs OFF */
    HAL_GPIO_WritePin(RGB_R_PORT, RGB_R_PIN, RGB_LED_OFF);
    HAL_GPIO_WritePin(RGB_G_PORT, RGB_G_PIN, RGB_LED_OFF);
    HAL_GPIO_WritePin(RGB_B_PORT, RGB_B_PIN, RGB_LED_OFF);

    /* Configure PA1 (Red) */
    GPIO_InitStruct.Pin = RGB_R_PIN;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(RGB_R_PORT, &GPIO_InitStruct);

    /* Configure PC1 (Blue) and PC3 (Green) */
    GPIO_InitStruct.Pin = RGB_G_PIN | RGB_B_PIN;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GPIOC, &GPIO_InitStruct);
}

/**
  * @brief  Set raw states for R, G, B channels
  * @param  r: 1 to turn on, 0 to turn off
  * @param  g: 1 to turn on, 0 to turn off
  * @param  b: 1 to turn on, 0 to turn off
  */
void BSP_RGB_SetRaw(uint8_t r, uint8_t g, uint8_t b)
{
    HAL_GPIO_WritePin(RGB_R_PORT, RGB_R_PIN, r ? RGB_LED_ON : RGB_LED_OFF);
    HAL_GPIO_WritePin(RGB_G_PORT, RGB_G_PIN, g ? RGB_LED_ON : RGB_LED_OFF);
    HAL_GPIO_WritePin(RGB_B_PORT, RGB_B_PIN, b ? RGB_LED_ON : RGB_LED_OFF);
}

/**
  * @brief  Set preset RGB color
  */
void BSP_RGB_SetColor(RGB_Color_t color)
{
    switch (color)
    {
        case RGB_COLOR_RED:
            BSP_RGB_SetRaw(1, 0, 0);
            break;
        case RGB_COLOR_GREEN:
            BSP_RGB_SetRaw(0, 1, 0);
            break;
        case RGB_COLOR_BLUE:
            BSP_RGB_SetRaw(0, 0, 1);
            break;
        case RGB_COLOR_YELLOW:
            BSP_RGB_SetRaw(1, 1, 0);
            break;
        case RGB_COLOR_PURPLE:
            BSP_RGB_SetRaw(1, 0, 1);
            break;
        case RGB_COLOR_CYAN:
            BSP_RGB_SetRaw(0, 1, 1);
            break;
        case RGB_COLOR_WHITE:
            BSP_RGB_SetRaw(1, 1, 1);
            break;
        case RGB_COLOR_OFF:
        default:
            BSP_RGB_SetRaw(0, 0, 0);
            break;
    }
}

/**
  * @brief  Toggle individual RGB channels
  */
void BSP_RGB_Toggle(uint8_t r, uint8_t g, uint8_t b)
{
    if (r) HAL_GPIO_TogglePin(RGB_R_PORT, RGB_R_PIN);
    if (g) HAL_GPIO_TogglePin(RGB_G_PORT, RGB_G_PIN);
    if (b) HAL_GPIO_TogglePin(RGB_B_PORT, RGB_B_PIN);
}
