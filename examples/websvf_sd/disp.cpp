#include "disp.h"
#include "wifi.h"
#include "hardware.h"

SSD_13XX tft = SSD_13XX(__CS_TFT, __DC_TFT);

void init_oled_show_ip()
{
  static int oled_begin_done = 0;
  if(oled_begin_done == 0)
  {
    tft.begin();
    oled_begin_done = 1;
  }
  tft.setRotation(2);
  tft.defineScrollArea(0,0,0, 63, 0);
  tft.scroll(false);
  tft.clearScreen();
  tft.setCursor(0, 0);
  tft.setTextColor(WHITE);
  tft.setTextScale(1);
  tft.setTextWrap(false);
  IPAddress ip;
  if(WiFi.status() == WL_CONNECTED)
    ip = WiFi.localIP();
  else
    ip = WiFi.softAPIP();
  tft.println(ip);
}
