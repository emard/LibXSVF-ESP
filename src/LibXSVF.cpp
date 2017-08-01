#include <FS.h>
#include "LibXSVF.h"
#include "trunk/libxsvf.h"

extern "C" int xsvftool_esp8266_scan(void);
extern "C" int xsvftool_esp8266_program(int (*file_getbyte)(), int x);

int LibXSVF::scan()
{
  return xsvftool_esp8266_scan();
}


File libxsvf_file;

int libxsvf_file_getbyte()
{
  uint8_t buf;
  if(libxsvf_file)
  {
    int bytes = libxsvf_file.read(&buf, 1);
    if(bytes == 1)
    {
      // printf("%c", buf); // some dumping of content
      return buf;
    }
  }
  return EOF;
}


int LibXSVF::program(String filename, int x)
{
  int retval = -1;
  libxsvf_file = SPIFFS.open(filename.c_str(), "r");
  if(libxsvf_file)
  {
    printf("Programming \"%s\"\n", filename.c_str());
    retval = xsvftool_esp8266_program(libxsvf_file_getbyte, x);
    libxsvf_file.close();
  }
  return retval;
}
