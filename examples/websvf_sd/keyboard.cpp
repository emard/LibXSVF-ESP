#include <Arduino.h>
#include "disp.h"
#include "hardware.h"
#include "storage.h"

// keyboard interactive
// OLED is write-only device, allowing free use of SPI MISO return channel
// Sending any OLED command, e.g. NOP commands (BCh, BDh, E3h)
// on SPI bus provides 8-bit readout of pushbutton status,
// NOP can be manually added to SSD_13XX.cpp
uint8_t keyboard()
{
  return tft.nop() ^ (1<<BTN_PWR); // BTN_PWR has inverted logic
}

void scan_keyboard()
{
  uint8_t key = keyboard();
  // update hold count
  for(uint8_t i = 0; i < BTN_MAX; i++)
  {
    uint8_t keystate = key & (1<<i);
    if(keystate)
    {
      if(Ifb.hold[i] < 255)
        Ifb.hold[i]++;
    }
    else
      Ifb.hold[i] = 0;
  }
}
