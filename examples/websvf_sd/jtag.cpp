#include "jtag.h"
#include "hardware.h"
#include "disp.h"

/*
> To change JTAG pinout, edit file
> ~/Arduino/libraries/LibXSVF-ESP/src/trunk/xsvftool-esp.c
> FPGA  wire     ESP32
> ---   ----     -------
> VCC   brown
> GND   black    GND
> TCK   yellow   18
> TDO   green    19
> TDI   violett  23
> TMS   blue     21
*/
LibXSVF jtag = LibXSVF();

File SVF_file; // SVF file opened on SD card during programming

int spiffs_program_activate = 0;

// From SD to JTAG
// Read file by blocks, passing each to
// packetized SVF player
void program_file(fs::FS &storage, String filename, int detach)
{
  SVF_file = storage.open(filename);
  // progress bar geometry
  const int pb_x = 0, pb_y = 40, pb_w = 95, pb_h = 8;
  const int pb_color_frame = WHITE, pb_color_empty = BLACK, pb_color_full = BLUE;
  static char report[256];
  if(sd_detach == 0)
  {
    tft.clearScreen();
    tft.setTextColor(WHITE, BLACK);
    tft.setTextWrap(true);
    tft.println(filename);
  }
  if (!SVF_file)
  {
     if(sd_detach == 0)
       tft.println("cannot open");
     return;
  }
  int file_len = SVF_file.size();
  if(sd_detach == 0)
  {
    tft.print("length ");
    tft.println(file_len, DEC);
  }
  if(file_len <= 0)
    return;
  int index = 0;
  digitalWrite(LED_WIFI, HIGH);
  if(sd_detach == 0)
  {
    // tft.print("SVF");
    tft.drawRect(pb_x,pb_y,pb_w,pb_h,pb_color_frame,pb_color_empty,true);
  }
  int pb_w_full = pb_w-2; // full progress bar width
  int pb_w_anim; // progress width during animated progess
  const int buflen = 512;
  uint8_t data[buflen];
  while(SVF_file.available())
  {
     int len = SVF_file.read(data, buflen);
     int final = index + len >= file_len ? 1 : 0;
     jtag.play_svf_packet(index, data, len, final, report);
     index += len;
     pb_w_anim = pb_w_full * index / file_len;
     if(sd_detach == 0)
     {
       if( (index & 0xFFF) == 0 || final != 0)
         if(pb_w_anim > 0 && pb_w_anim <= pb_w_full)
           tft.fillRect(pb_x+1,pb_y+1,pb_w_anim,pb_h-2,pb_color_full);
       #if 0
       if( (index & 0xFFFF) == 0)
         tft.print(".");
       if(final)
         tft.println("ok");
       #endif
     }
  }
  SVF_file.close();
  if(sd_detach == 0)
    tft.println(report);
  digitalWrite(LED_WIFI, LOW);
  // is SPI detach required?
  if(detach)
  {
    sd_unmount();
    SPI.end();
    pinMode(__CS_SD, INPUT);
    pinMode(__CS_TFT, INPUT);
    pinMode(__DC_TFT, INPUT);
    pinMode(__RES_TFT, INPUT);
    pinMode(__MOSI_TFT, INPUT);
    pinMode(__MISO_TFT, INPUT);
    pinMode(__SCL_TFT, INPUT);
    sd_detach = 1;
    return; // after detach, OLED will be inaccessible
  }
  // after bitstream, if passthru still works
  // OLED may need to be reset, so reinitialize
  // and print final message
  if(sd_detach == 0)
  {
    delay(500);
    tft.begin();
    tft.setRotation(2);
    tft.defineScrollArea(0,0,0, 63, 0);
    tft.scroll(false);
    tft.clearScreen();
    tft.setCursor(0, 0);
    tft.setTextColor(WHITE);
    tft.setTextScale(1);
    tft.println(report);
  }
  // sd_detach = 0;
}
