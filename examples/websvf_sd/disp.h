#ifndef DISP_H
#define DISP_H
#include <SSD_13XX.h>

extern SSD_13XX tft;
const uint8_t LineH = 8, LineN = 8; // text line height in pixels, number of lines

void init_oled_show_ip();

#endif
