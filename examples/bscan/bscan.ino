#include <LibXSVF.h>

LibXSVF jtag = LibXSVF(1,2,3,4);

void setup()
{
  Serial.begin(115200);
}

void loop()
{
  jtag.test();

  delay(1000);
}
