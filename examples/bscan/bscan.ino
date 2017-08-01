#include <LibXSVF.h>

LibXSVF jtag = LibXSVF(1,2,3,4);

void setup()
{
  Serial.begin(115200);
  pinMode(12, INPUT); // NodeMCU D6
  pinMode(13, OUTPUT); // NodeMCU D7
  pinMode(14, OUTPUT); // NodeMCU D5
  pinMode(5, OUTPUT); // NodeMCU D1
}

void loop()
{
  jtag.test();

  delay(1000);
}
