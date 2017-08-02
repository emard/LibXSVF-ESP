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

struct libxsvf_file_buf
{
  uint8_t *buffer;
  int count; // how many bytes in buffer
  int ptr; // current reading pointer
  uint8_t blink; // for the LED
  File file; // open file from SPIFFS
};
struct libxsvf_file_buf rd;

int libxsvf_file_getbyte()
{
  if(rd.ptr >= rd.count) // refill the buffer and update content
  {
    rd.ptr = 0;
    rd.count = rd.file.read(rd.buffer, BUFFER_SIZE);
    if(rd.count <= 0 || rd.count > BUFFER_SIZE)
      return EOF; // should return EOF
    digitalWrite(LED_BUILTIN, (rd.blink++) & 1);
  }
  // one byte at a time from the buffer
  // printf("%c", libxsvf_file_buffer[BUFFER_SIZE-1-libxsvf_file_buffer_content]);
  return rd.buffer[rd.ptr++];
}


int LibXSVF::program(String filename, int x)
{
  int retval = -1;
  rd.file = SPIFFS.open(filename.c_str(), "r");
  if(rd.file)
  {
    pinMode(LED_BUILTIN, OUTPUT);
    rd.buffer = (uint8_t *)malloc(BUFFER_SIZE * sizeof(uint8_t));
    rd.count = 0;
    rd.ptr = 0;
    printf("Programming \"%s\"\n", filename.c_str());
    retval = xsvftool_esp8266_program(libxsvf_file_getbyte, x);
    rd.file.close();
    pinMode(LED_BUILTIN, INPUT);
    free(rd.buffer);
  }
  return retval;
}
