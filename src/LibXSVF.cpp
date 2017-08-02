#include <FS.h>
#include "LibXSVF.h"
#include "trunk/libxsvf.h"

// bytes buffer for SPIFFS efficiency
// 256-8192
#define BUFFER_SIZE 1024

extern "C" int xsvftool_esp8266_scan(void);
extern "C" int xsvftool_esp8266_program(int (*file_getbyte)(), int x);

int LibXSVF::scan()
{
  return xsvftool_esp8266_scan();
}

uint8_t *libxsvf_file_buffer = NULL;
int libxsvf_file_buffer_content = 0;
int libxsvf_file_buffer_read_ptr = 0;
File libxsvf_file;

int libxsvf_file_getbyte()
{
  if(libxsvf_file_buffer_content == 0) // refill the buffer and update content
  {
    libxsvf_file_buffer_content = libxsvf_file.read(libxsvf_file_buffer, BUFFER_SIZE);
    libxsvf_file_buffer_read_ptr = 0;
  }
  
  if(libxsvf_file_buffer_content <= 0 || libxsvf_file_buffer_content > BUFFER_SIZE)
    return EOF; // should return EOF
  libxsvf_file_buffer_content--; // one byte less in the content
  // one byte at a time from the buffer
  // printf("%c", libxsvf_file_buffer[BUFFER_SIZE-1-libxsvf_file_buffer_content]);
  return libxsvf_file_buffer[libxsvf_file_buffer_read_ptr++];
}


int LibXSVF::program(String filename, int x)
{
  int retval = -1;
  libxsvf_file = SPIFFS.open(filename.c_str(), "r");
  if(libxsvf_file)
  {
    libxsvf_file_buffer = (uint8_t *)malloc(4*BUFFER_SIZE * sizeof(uint8_t));
    libxsvf_file_buffer_content = 0;
    printf("Programming \"%s\"\n", filename.c_str());
    retval = xsvftool_esp8266_program(libxsvf_file_getbyte, x);
    libxsvf_file.close();
    free(libxsvf_file_buffer);
  }
  return retval;
}
