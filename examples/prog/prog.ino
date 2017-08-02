#include <FS.h>
#include <LibXSVF.h>

/*
> Currently hardcoded pinout
> ULX2S wire     NodeMCU Arduino
> ---   ----     --      -------
> VCC   brown
> GND   black    GND
> TCK   white    D5      14
> TDO   gray     D6      12
> TDI   violett  D7      13
> TMS   blue     D1      5
*/

LibXSVF jtag = LibXSVF();

void setup()
{
  SPIFFS.begin();
  Serial.begin(115200);
}

void loop()
{
  jtag.scan();
  jtag.program("/bitstream.svf", 0);
  delay(1000);
}
