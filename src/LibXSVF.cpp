#include "LibXSVF.h"
#include "trunk/libxsvf.h"

extern "C" int xsvftool_esp8266_scan(void);

void LibXSVF::scan()
{
  xsvftool_esp8266_scan();
}
