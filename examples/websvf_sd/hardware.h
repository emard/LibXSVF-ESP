#ifndef HARDWARE_H
#define HARDWARE_H

#define LED_WIFI 5
#define BTN0_PIN 0

// pcb v1.7
#if 0
#define __CS_SD        26
#define __CS_TFT       15
#define __DC_TFT       16
#define __RES_TFT      25
#define __MOSI_TFT     13
#define __MISO_TFT     12
#define __SCL_TFT      14
#endif
// pcb v1.8
#if 1
#define __CS_SD        13
#define __CS_TFT       17
#define __DC_TFT       16
#define __RES_TFT      25
#define __MOSI_TFT     15
#define __MISO_TFT      2
#define __SCL_TFT      14
#endif

enum 
{
  BTN_PWR = 0,
  BTN_FIRE1 = 1,
  BTN_FIRE2 = 2,
  BTN_UP = 3,
  BTN_DOWN = 4,
  BTN_LEFT = 5,
  BTN_RIGHT = 6,
  BTN_MAX = 7
};

#endif
