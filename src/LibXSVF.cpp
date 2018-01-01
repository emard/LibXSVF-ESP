#include "LibXSVF.h"
#include "trunk/libxsvf.h"

// bytes buffer for SPIFFS efficiency
// 256-8192
#define BUFFER_SIZE 8192

extern "C" int xsvftool_esp8266_scan(void);
extern "C" uint32_t xsvftool_esp8266_id(void);
extern "C" int xsvftool_esp8266_program(int (*file_getbyte)(), int x);
extern "C" int xsvftool_esp8266_svf_packet(int (*packet_getbyte)(), int index, int final, char *report);

struct libxsvf_file_buf
{
  uint8_t *buffer;
  FS* fs; // filesystem
  int count; // how many bytes in buffer
  int ptr; // current reading pointer
  uint8_t blink; // for the LED
  File file; // open file from SPIFFS
};
struct libxsvf_file_buf rd;

void LibXSVF::begin(FS* fs)
{
  rd.fs = fs;
}

// scan should return some struct
int LibXSVF::scan()
{
  return xsvftool_esp8266_scan();
}

uint32_t LibXSVF::id()
{
  return xsvftool_esp8266_id();
}

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

// warning: Filesystem SPIFFS may not be thread safe
// accesing it from several places at the same time
// sometimes may lead to data corruption
int LibXSVF::program(String filename, int x)
{
  int retval = -1;
  printf("Programming \"%s\"\n", filename.c_str());
  rd.file = rd.fs->open(filename.c_str(), "r");
  if(rd.file)
  {
    pinMode(LED_BUILTIN, OUTPUT);
    rd.buffer = (uint8_t *)malloc(BUFFER_SIZE * sizeof(uint8_t));
    rd.count = 0;
    rd.ptr = 0;
    retval = xsvftool_esp8266_program(libxsvf_file_getbyte, x);
    rd.file.close();
    pinMode(LED_BUILTIN, INPUT);
    free(rd.buffer);
    printf("Done\n", filename.c_str());
  }
  else
  {
    printf("File not found\n", filename.c_str());
  }
  return retval;
}

struct libxsvf_stream_buf
{
  uint8_t *buffer;
  int count; // how many bytes in packet buffer
  int ptr; // current reading pointer
};

struct libxsvf_stream_buf rs;

int libxsvf_stream_getbyte()
{
  if(rs.ptr >= rs.count) // end of buffer, try again later
    return -2;
  // one byte at a time from the buffer
  // printf("%c", rs.buffer[rs.ptr]);
  return rs.buffer[rs.ptr++];
}

/*
index:  absolute byte offset of the packet in entire stream
        currently only tested with 0 to detect stream start
        packets are supposed to come in sequential order
buffer: pointer to packet data payload
len:    length of data in bytes
final:  nonzero when last packet
*/
int LibXSVF::play_svf_packet(int index, uint8_t *buffer, int len, bool final, char *report)
{
  rs.buffer = buffer;
  rs.count = len;
  rs.ptr = 0;
  return xsvftool_esp8266_svf_packet(libxsvf_stream_getbyte, index, final ? 1 : 0, report);
}
