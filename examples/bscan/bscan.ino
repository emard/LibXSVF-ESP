#include <LibXSVF.h>

LibXSVF jtag = LibXSVF(1,2,3,4);

void setup()
{
  Serial.begin(115200);
  pinMode(12, INPUT);
  pinMode(13, OUTPUT);
  pinMode(14, OUTPUT);
  pinMode(16, OUTPUT);
}

void loop()
{
  jtag.test();

  delay(1000);
}
