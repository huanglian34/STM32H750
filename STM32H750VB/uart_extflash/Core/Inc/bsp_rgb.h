/**
  ******************************************************************************
  * @file    bsp_rgb.h
  * @brief   Board Support Package for Onboard RGB LED (STM32H750VBT6)
  *          Hardware Pinout from Schematic:
  *            - Red   (R): PA1 (LRGB-R)
  *            - Green (G): PC3 (LRGB-G)
  *            - Blue  (B): PC1 (LRGB-B)
  ******************************************************************************
  */

#ifndef __BSP_RGB_H__
#define __BSP_RGB_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"

/* RGB LED GPIO Pin Definitions */
#define RGB_R_PORT            GPIOA
#define RGB_R_PIN             GPIO_PIN_1

#define RGB_G_PORT            GPIOC
#define RGB_G_PIN             GPIO_PIN_3

#define RGB_B_PORT            GPIOC
#define RGB_B_PIN             GPIO_PIN_1

/* 
 * Active Level Configuration:
 * 0: Active Low  (Common Anode / 共阳极, 0点亮, 1熄灭) [默认推荐]
 * 1: Active High (Common Cathode / 共阴极, 1点亮, 0熄灭)
 */
#define RGB_ACTIVE_LEVEL      0

#if (RGB_ACTIVE_LEVEL == 0)
  #define RGB_LED_ON          GPIO_PIN_RESET
  #define RGB_LED_OFF         GPIO_PIN_SET
#else
  #define RGB_LED_ON          GPIO_PIN_SET
  #define RGB_LED_OFF         GPIO_PIN_RESET
#endif

/* RGB Color Enumeration */
typedef enum {
    RGB_COLOR_OFF = 0,
    RGB_COLOR_RED,
    RGB_COLOR_GREEN,
    RGB_COLOR_BLUE,
    RGB_COLOR_YELLOW,    /* Red + Green */
    RGB_COLOR_PURPLE,    /* Red + Blue */
    RGB_COLOR_CYAN,      /* Green + Blue */
    RGB_COLOR_WHITE      /* Red + Green + Blue */
} RGB_Color_t;

/* Exported Functions */
void BSP_RGB_Init(void);
void BSP_RGB_SetColor(RGB_Color_t color);
void BSP_RGB_SetRaw(uint8_t r, uint8_t g, uint8_t b);
void BSP_RGB_ToggleR(void);
void BSP_RGB_ToggleG(void);
void BSP_RGB_ToggleB(void);
const char* BSP_RGB_GetColorName(RGB_Color_t color);

#ifdef __cplusplus
}
#endif

#endif /* __BSP_RGB_H__ */
