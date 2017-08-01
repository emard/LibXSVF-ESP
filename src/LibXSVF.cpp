#include "LibXSVF.h"
#include "trunk/libxsvf.h"

extern "C" int xsvftool_esp8266_scan(void);

void LibXSVF::test()
{
  Serial.print("SCAN: ");
  Serial.println(xsvftool_esp8266_scan());
}
