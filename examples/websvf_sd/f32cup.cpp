#include <SD.h>

// String Filename = "/ULX3S/f32c-bin/autoexec.bin";
// uint32_t Start_addr = 0x80000000; // the adress to load the binary

uint8_t Retry_block = 5, Retry_crc = 4;
uint32_t serial_baud_default = 115200; // don't touch
uint32_t serial_baud_upload = 115200;
uint32_t serial_break_duration = 150; // ms (serial break currenty doesn't work)
uint32_t serial_baud_current = serial_baud_default;

int File_len = 0;

#define __CS_SD        13
#define __CS_TFT       17
#define __DC_TFT       16
#define __RES_TFT      25
#define __MOSI_TFT     15
#define __MISO_TFT      2
#define __SCL_TFT      14
 
#define TXD_PIN 1

int SD_mounted = -999;

void serial_port_send_break(int duration)
{
  #if 1
  Serial.write((char)0xFF);
  delay(duration);
  return;
  #endif
  // following code code does not work
  // TODO: how to make serial break on ESP32 ?
  #if 0
  //Serial.end();
  delay(100);
  pinMode(TXD_PIN, OUTPUT);
  digitalWrite(TXD_PIN, HIGH);
  delay(100);
  //Serial.write(0);
  digitalWrite(TXD_PIN, LOW);
  delay(duration);
  digitalWrite(TXD_PIN, HIGH);
  //Serial.begin(serial_baud_default);
  #endif
}

void serial_port_reset_input_buffer()
{
}

void serial_port_reset_output_buffer()
{
}

uint32_t crc_block(uint8_t *data, uint32_t len)
{
  uint32_t crc = 0;
  for(uint32_t i = 0; i < len; i++)
  {
    crc = ((crc >> 31) | (crc << 1));
    crc += *data;
    data++;
  }
  return crc;
}

int try_to_get_prompt(int retries)
{
  uint8_t reply[255];
  if(serial_baud_current != serial_baud_default)
  {
    Serial.begin(serial_baud_default);
    serial_baud_current = serial_baud_default;
  }
  serial_port_send_break(serial_break_duration);
  while(retries > 0)
  {
    serial_port_reset_input_buffer();
    serial_port_reset_output_buffer();
    Serial.write('\r');
    delay(20);
    Serial.readBytes(reply, 20);
    String str_reply = (char *)reply;
    if(str_reply.indexOf("m32l> ") >= 0)
      return 1; // MIPS little-endian prompt is found
    if(str_reply.indexOf("rv32> ") >= 0)
      return 5; // RISC-V prompt is found
    retries--;
  }
  return 0; // failure
}

uint32_t read32(uint8_t *addr)
{
  uint32_t value = 0;
  for(int i = 0; i < 4; i++)
    value = (value << 8) | addr[i];
  return value;
}

uint32_t receive_crc(uint8_t retry_crc, int sleep_ms)
{
  uint32_t crc;
  uint8_t read_crc[4];
  size_t read_crc_len;
  while(retry_crc > 0)
  {
    if( (retry_crc & 0) == 0 )
    {
      serial_port_reset_input_buffer();
      serial_port_reset_output_buffer();
      Serial.write((char)0x81); // request f32c to send checksum
    }
    read_crc_len = Serial.readBytes(read_crc, sizeof(read_crc)); // read 4 bytes of checksum
    crc = read32(read_crc);
    if(read_crc_len == sizeof(read_crc))
      return crc;
    crc++; // hopfully should destroy previous crc
    delay(sleep_ms);
    retry_crc--;
  }
  return crc;
}


void write32(uint8_t *addr, uint32_t value)
{
  for(int i = 3; i >= 0; i--)
  {
    addr[i] = (uint8_t) value;
    value >>= 8;
  }
}

int upload_block(uint32_t addr, uint32_t len, uint8_t *chunk, uint8_t first)
{
  uint32_t expected_crc = crc_block(chunk, len);
  uint32_t received_crc = ~expected_crc;
  int8_t retry_block = Retry_block;
  uint32_t serial_port_baudrate;
  static uint8_t cmd_baud[6] = {0x80, 0,0,0,0, 0xB0};
  static uint8_t cmd_block[12] = {0x80, 0,0,0,0, 0x90, 0x80, 0,0,0,0, 0xA0};
  while(retry_block > 0)
  {
    if(first)
    {
      serial_port_baudrate = serial_baud_default;
      if(try_to_get_prompt(3) > 0)
      {
        Serial.write((char)0xFF);
        if(serial_baud_upload != serial_baud_default)
        {
          write32(cmd_baud+1, serial_baud_upload);
          Serial.write(cmd_baud, sizeof(cmd_baud));
          Serial.flush();
          //Serial.end();
          //delay(100);
          Serial.begin(serial_baud_upload);
          serial_baud_current = serial_baud_upload;
        }
      }
    }
    serial_port_reset_input_buffer();
    serial_port_reset_output_buffer();
    write32(cmd_block+1, len);
    write32(cmd_block+7, addr);
    Serial.write(cmd_block, sizeof(cmd_block));
    Serial.write(chunk, len);
    received_crc = receive_crc(Retry_crc, 20);
    if(received_crc == expected_crc)
      return 1; // success
    retry_block--;
  }
  return 0; // failure after retries
}

void jump(uint32_t addr)
{
  static uint8_t cmd_jump[6] = {0x80, 0,0,0,0, 0xB1};
  write32(cmd_jump+1, addr);
  Serial.write(cmd_jump, sizeof(cmd_jump));
}

int f32c_exec_binary(fs::FS &storage, String filename, uint32_t start_addr)
{
  File binary_file = storage.open(filename);
  File_len = binary_file.size();
  if(File_len <= 0)
    return 0;
  int block_retval = 1;
  uint32_t index = 0;
  const int buflen = 512;
  uint8_t data[buflen];
  while(binary_file.available() > 0 && block_retval == 1)
  {
     int len = binary_file.read(data, buflen);
     int first = index == 0 ? 1 : 0;
     int last = index + len >= File_len ? 1 : 0;
     block_retval = upload_block(start_addr + index, len, data, first);
     if(last == 1 && block_retval == 1)
       jump(start_addr);
     index += len;
  }
  return block_retval;
}
