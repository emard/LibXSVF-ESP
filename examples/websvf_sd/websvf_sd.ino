#if ESP8266
#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <ESPAsyncTCP.h>
#include <Hash.h>
#include <FS.h>
#endif
#if ESP32
#include <WiFi.h>
#include <ESPmDNS.h>
#include <AsyncTCP.h>
#include <SPIFFS.h>
// Edit ESP32 SD.cpp line 33
// spi.begin() -> spi.begin(14, 12, 13, -1); // v1.7
// spi.begin() -> spi.begin(14, 2, 15, -1); // v1.8 CS=13
// SD chipselect is pin 26
#include <SD.h>
// Edit SSD_13XX.cpp line 266
// SPI.begin() -> SPI.begin(14, 12, 13, -1); // v1.7
// OLED chipselect is pin 15
// Add NOP command which will read buttons over OLED SPI
#if 0
  uint8_t SSD_13XX::nop()
  {
    startTransaction();
    enableCommandStream();
    uint8_t retval = spiwrite(CMD_NOP); disableCS();
    closeTransaction();
    return retval;
  }
  // also change spiwrite() to return value:
  // uint8_t retval = SPI.transfer(bla);
  // return retval;
#endif
#include <SSD_13XX.h>
// pcb v1.7
#if 0
#define __CS_SD        26
#define __CS_TFT       15
#define __DC_TFT       16
#define __RES_TFT      25
#define __MOSI_TFT     13
#define __MISO_TFT     12
#define __SCL_TFT      14
#endif
// pcb v1.8
#if 1
#define __CS_SD        13
#define __CS_TFT       17
#define __DC_TFT       16
#define __RES_TFT      25
#define __MOSI_TFT     15
#define __MISO_TFT      2
#define __SCL_TFT      14
#endif
SSD_13XX tft = SSD_13XX(__CS_TFT, __DC_TFT);
#endif
#include <ArduinoOTA.h>
#include <ESPAsyncWebServer.h>
#include <SPIFFSEditor.h>
#include <LibXSVF.h>
#include <ArduinoJson.h>

#include "f32cup.h"

// At boot it will attempt to connect as client.
// If this attempt fails, it will become AP.
// Same ssid/password apply for client and AP.
char host_name[32] = "websvf"; // request local name when connected as client, same as ssid
char ssid[32] = "websvf";
char password[32] = "12345678"; // >=8 chars or "" for open access
char http_username[32] = "admin";
char http_password[32] = "admin";

// verbose print on serial
#define DEBUG 0

// SKETCH BEGIN
AsyncWebServer server(80);
AsyncWebSocket ws("/ws");
AsyncEventSource events("/events");

#define LED_WIFI 5
#define BTN0_PIN 0

/*
> To change JTAG pinout, edit file
> ~/Arduino/libraries/LibXSVF-ESP/src/trunk/xsvftool-esp.c
> FPGA  wire     ESP32
> ---   ----     -------
> VCC   brown
> GND   black    GND
> TCK   yellow   18
> TDO   green    19
> TDI   violett  23
> TMS   blue     21
*/
LibXSVF jtag = LibXSVF();

File SVF_file; // SVF file opened on SD card during programming

String save_dirname = "/"; // where to save file uploaded file
String sd_file_name_svf = "";
String sd_file_name_bin = "";
String sd_file_name_f32c_svf = "/ULX3S/f32c-45k-vector/f32c-ulx3s-45k-vector-sram.svf";
int sd_program_activate = 0;
int spiffs_program_activate = 0;
int sd_detach = 1; // start with SPI bus detached (SD and OLED initially not in use)
int sd_binary_file_activate = 0; // any filename, passed to sd_file_name_svf
int sd_cs_counter = 0; // interrupt detects external SD access
int sd_mounted = 0; // track SD_mount() state

// directory read to malloced struct
String DirPath = "/"; // current directory path
int DirN; // number of entries in current dir path
struct S_DirEntries
{
  char *name; // item name: pointer to 0-terminated string
  size_t length; // file length (bytes)
  uint8_t type; // item type 1-directory, 0-file
};
struct S_DirEntries *DirEntries = NULL; // realloced N such entries
char *DirNames = NULL; // realloced size for all names in current directory

void IRAM_ATTR sd_cs_interrupt()
{
  sd_cs_counter++;
}

// string compare function for directory soring, ignore case
int DirNameCmp(const void *a, const void *b)
{
  const struct S_DirEntries *sa = (const struct S_DirEntries *)a;
  const struct S_DirEntries *sb = (const struct S_DirEntries *)b;
  return strcasecmp(sa->name, sb->name);
}

int sd_mount()
{
  if(sd_mounted)
    return 0;
  // initialize the SD card
  if(!SD.begin(__CS_SD, SPI, 20000000, "/sd")){
        #if DEBUG
        Serial.println("Card Mount Failed");
        #endif
        tft.println("Card Mount Failed");
        return -1;
  }
  uint8_t cardType = SD.cardType();
  if(cardType == CARD_NONE){
        SD.end();
        #if DEBUG
        Serial.println("No SD card attached");
        #endif
        tft.println("No SD card attached");
        return -2;
  }
  sd_mounted = 1;
  return 0; // success
}

void sd_unmount()
{
  if(!sd_mounted)
    return;
  sd_mounted = 0;
  SD.end();
}

// read wifi and password config from
// SD card file "/ulx3s-wifi.conf"
// this file max 2K
#define MAX_CONF_LEN 2048
void read_config(fs::FS &storage)
{
  File conf_file = storage.open("/ulx3s-wifi.conf");
  if(!conf_file)
  {
    Serial.println("no config file /ulx3s-wifi.conf");
    return; // no config file
  }
  uint8_t data[MAX_CONF_LEN];
  if(conf_file.available())
  {
     int len = conf_file.read(data, MAX_CONF_LEN);
     static StaticJsonBuffer<MAX_CONF_LEN> jsonBuffer;
     static JsonObject& jroot = jsonBuffer.parseObject(data);
     // Test if parsing succeeds.
     if (jroot.success())
     {
       #if 1
       const char *a;
       a = jroot["ssid"];
       if(a)
         strncpy(ssid, a, 31);
       a = jroot["password"];
       if(a)
         strncpy(password, a, 31);
       a = jroot["host_name"];
       if(a)
         strncpy(host_name, a, 31);
       a = jroot["http_username"];
       if(a)
         strncpy(http_username, a, 31);
       a = jroot["http_password"];
       if(a)
         strncpy(http_password, a, 31);
       #endif
       #if 0
       Serial.println(ssid);
       Serial.println(password);
       Serial.println(host_name);
       Serial.println(http_username);
       Serial.println(http_password);
       #endif
     }
     else
     {
       Serial.println("wifi.conf parseObject() failed");
     }
  }

  conf_file.close();
}

// read current directory path, don't recurse child dirs
// pass1: read to determine number of entries and sum all string lengths
// realloc() size
// pass2: read to fill the content to realloced area
void read_directory(fs::FS &storage)
{
  // counters
  int ndirs = 0, nfiles = 0, all_names_length = 0;
  DirN = 0; // let dir be read empty initially
  // pass1: read to determine number of entries and sum all string lengths
  File root = storage.open(DirPath);
  if(!root)
  {
    #if DEBUG
    Serial.println("Failed to open directory");
    #endif
    return;
  }
  if(!root.isDirectory())
  {
    #if DEBUG
    Serial.println("Not a directory");
    #endif
    return;
  }
  // for this to work, every name returned by file.name() must start with "/"
  File file = root.openNextFile();
  while(file)
  {
      char *basename = strrchr(file.name(), '/');
      all_names_length += strlen(basename);
        if(file.isDirectory()){
            #if DEBUG
            Serial.print("  DIR : ");
            Serial.println(basename);
            #endif
            ndirs++;
            #if 0
            if(levels){
                listDir(fs, file.name(), levels -1);
            }
            #endif
        } else {
            nfiles++;
            #if DEBUG
            Serial.print("  FILE: ");
            Serial.print(basename);
            Serial.print("  SIZE: ");
            Serial.println(file.size());
            #endif
        }
        file = root.openNextFile();
  }
  DirN = ndirs + nfiles;
  // realloc() for required size
  DirEntries = (struct S_DirEntries *)realloc(DirEntries, 
    DirN*sizeof(struct S_DirEntries));
  DirNames = (char *)realloc(DirNames, (all_names_length+1)*sizeof(char));
  // pass2: again read to fill the content to realloced area
  // initialized pointers for stroring directory entries
  root.rewindDirectory();
  struct S_DirEntries *pdir = DirEntries, *pfile = DirEntries+ndirs;
  char *entryname = DirNames;
  int cdirs = 0, cfiles = 0, call_names_length = 0; // sanity check counters
  file = root.openNextFile();
  while(file)
  {
      char *basename = strrchr(file.name(), '/');
      int namelen = strlen(basename);
      strcpy(entryname, basename);
        if(file.isDirectory()){
          if(cdirs < ndirs) // prevent pointer runaway
          {
            pdir->name = entryname;
            pdir->type = 1; // directory
            pdir->length = 0; // reserved for future use
            pdir++; // advance the pointer
            cdirs++; // count for sanity check
          }
        } else {
          if(cfiles < nfiles) // prevent pointer runaway
          {
            pfile->name = entryname;
            pfile->type = 0; // file
            pfile->length = file.size();
            pfile++; // advance the pointer
            cfiles++; // count for sanity check
          }
        }
        if(call_names_length + namelen <= all_names_length) // prevent pointer runaway
        {
          strcpy(entryname, basename+1); // +1 to skip leading /
          entryname += namelen; // length is unchanged as it doesn't count leading / but includes trailing null
          call_names_length += namelen;
        }
        file = root.openNextFile();
  }
  root.close(); // reading finished
  // sd_unmount();
  if(ndirs > 0)
    qsort(DirEntries, ndirs, sizeof(struct S_DirEntries), DirNameCmp);
  if(nfiles > 0)
    qsort(DirEntries+ndirs, nfiles, sizeof(struct S_DirEntries), DirNameCmp);
  // print sorted result in allocated memory
  #if DEBUG
  for(int i = 0; i < DirN; i++)
  {
    Serial.print(DirEntries[i].name);
    Serial.print(" ");
    Serial.print(DirEntries[i].type ? "DIR" : "FILE");
    Serial.print(" ");
    Serial.println(DirEntries[i].length);
  }
  #endif
}

enum 
{
  BTN_PWR = 0,
  BTN_FIRE1 = 1,
  BTN_FIRE2 = 2,
  BTN_UP = 3,
  BTN_DOWN = 4,
  BTN_LEFT = 5,
  BTN_RIGHT = 6,
  BTN_MAX = 7
};

// struct for interactive file browser state
struct S_ifb
{
  uint8_t hold[BTN_MAX]; // hold counter for holding buttons
  int topitem; // number of file item displayed on top of the window
  int cursor; // cursor position on file item 
};

struct S_ifb Ifb; // instance of interactive file browser state
const uint8_t LineH = 8, LineN = 8; // text line height in pixels, number of lines

// show first N entries of the directory on OLED screen
void show_directory(int cursor, int topitem)
{
  tft.clearScreen();
  tft.setTextWrap(false);
  tft.setTextScale(1); // smallest possible font
  int offset = topitem;
  int screen_line = cursor - topitem;
  for(int i = 0; i < LineN && i + offset < DirN; i++)
  {
    int index = i+offset;
    tft.setCursor(0, i*LineH);
    uint16_t foregnd = DirEntries[index].type ? YELLOW : WHITE;
    uint16_t backgnd = i == screen_line ? RED : BLACK;
    tft.setTextColor(foregnd, backgnd); // dirs yellow, files white
    tft.print(DirEntries[index].name);
  }
}

void refresh_dir_line(int cursor, int highlight, int topitem)
{
    int screen_line = cursor - topitem;
    if(screen_line < 0 || screen_line >= LineN)
      return; // no update: cursor is off screen
    uint16_t foregnd = DirEntries[cursor].type ? YELLOW : WHITE;
    uint16_t backgnd = highlight ? RED : BLACK;
    tft.setCursor(0, screen_line * LineH);
    tft.setTextColor(foregnd, backgnd);
    tft.print(DirEntries[cursor].name);
}

// keyboard interactive
// OLED is write-only device, allowing free use of SPI MISO return channel
// Sending any OLED command, e.g. NOP commands (BCh, BDh, E3h)
// on SPI bus provides 8-bit readout of pushbutton status,
// NOP can be manually added to SSD_13XX.cpp
uint8_t keyboard()
{
  return tft.nop() ^ (1<<BTN_PWR); // BTN_PWR has inverted logic
}

void scan_keyboard()
{
  uint8_t key = keyboard();
  // update hold count
  for(uint8_t i = 0; i < BTN_MAX; i++)
  {
    uint8_t keystate = key & (1<<i);
    if(keystate)
    {
      if(Ifb.hold[i] < 255)
        Ifb.hold[i]++;
    }
    else
      Ifb.hold[i] = 0;
  }
}

void init_oled_show_ip()
{
  static int oled_begin_done = 0;
  if(oled_begin_done == 0)
  {
    tft.begin();
    oled_begin_done = 1;
  }
  tft.setRotation(2);
  tft.defineScrollArea(0,0,0, 63, 0);
  tft.scroll(false);
  tft.clearScreen();
  tft.setCursor(0, 0);
  tft.setTextColor(WHITE);
  tft.setTextScale(1);
  tft.setTextWrap(false);
  IPAddress ip;
  if(WiFi.status() == WL_CONNECTED)
    ip = WiFi.localIP();
  else
    ip = WiFi.softAPIP();
  tft.println(ip);
}

void mount_read_directory()
{
    if(sd_mount() >= 0)
    {
      read_directory(SD);
      sd_unmount();
      Ifb.cursor = 0;
      Ifb.topitem = 0;
      show_directory(Ifb.cursor, Ifb.topitem);
    }
}


// ********* FILE BROWSER **************
// reset: directory changed, redraw screen
// if track keyboard changes, update display
void file_browser(uint8_t reset)
{
  scan_keyboard();
  if(reset /* || Ifb.hold[BTN_PWR] == 1 */ )
  { // reset directory path
    DirPath = "/";
    mount_read_directory();
    return;
  }
  int oldcursor = Ifb.cursor;
  if(Ifb.hold[BTN_LEFT] == 1)
  {
    // exit directory (up one level)
    if(DirPath != "/")
    {
      DirPath = DirPath.substring(0, DirPath.lastIndexOf('/'));
      if(DirPath == "")
        DirPath = "/";
    }
    mount_read_directory();
      // Serial.print(DirPath);
    return;
  }
  if(Ifb.hold[BTN_RIGHT] == 1 && Ifb.cursor < DirN)
  {
    // enter directory or load bitstream
    String full_path =
      DirPath == "/" ? 
      String("/") + DirEntries[Ifb.cursor].name :
      DirPath + "/" + DirEntries[Ifb.cursor].name;      
    if(DirEntries[Ifb.cursor].type == 1)
    {
      // it's directory, enter it
      DirPath = full_path;
      mount_read_directory();
      return;
    }
    else // it's a file
    {
      if(full_path.lastIndexOf(".bin") > 0)
      { // f32c binary
        sd_file_name_svf = sd_file_name_f32c_svf;
        sd_file_name_bin = full_path;
        sd_binary_file_activate = 1;
      }
      else
      { // probably SVF file
        sd_file_name_svf = full_path;
        sd_program_activate = 1;
      }
    }
  }
  if(Ifb.hold[BTN_DOWN] == 1 || Ifb.hold[BTN_DOWN] > 40)
  {
    if(Ifb.cursor < DirN-1 && (Ifb.hold[BTN_DOWN] & 7) == 1) // slowdown every 8
      Ifb.cursor++;
  }
  if(Ifb.hold[BTN_UP] == 1 || Ifb.hold[BTN_UP] > 40)
    if(Ifb.cursor > 0 && (Ifb.hold[BTN_UP] & 7) == 1) // slowdown every 8
      Ifb.cursor--;
  if(oldcursor != Ifb.cursor)
  {
    tft.setTextWrap(false);
    tft.setTextScale(1); // smallest possible font
    int screen_line = Ifb.cursor - Ifb.topitem;
    if(screen_line >= 0 && screen_line < LineN)
    { // move inside of screen, no scrolling
      refresh_dir_line(oldcursor, 0, Ifb.topitem);
      refresh_dir_line(Ifb.cursor, 1, Ifb.topitem);
    }
    else
    {
      // scroll
      // screen_line = screen_line < 0 ? 0 : LineN-1; // snap screen line
      if(screen_line < 0)
      {
        // cursor going up
        screen_line = 0;
        if(Ifb.topitem > 0)
        {
          Ifb.topitem--;
          show_directory(Ifb.cursor, Ifb.topitem);
        }
      }
      else
      {
        // cursor going down
        screen_line = LineN-1;
        if(Ifb.topitem < DirN-1)
        {
          Ifb.topitem++;
          show_directory(Ifb.cursor, Ifb.topitem);
        }
      }
    }
  }
}


// From SD to JTAG
// Read file by blocks, passing each to
// packetized SVF player
void program_file(fs::FS &storage, String filename, int detach)
{
  SVF_file = storage.open(filename);
  // progress bar geometry
  const int pb_x = 0, pb_y = 40, pb_w = 95, pb_h = 8;
  const int pb_color_frame = WHITE, pb_color_empty = BLACK, pb_color_full = BLUE;
  static char report[256];
  if(sd_detach == 0)
  {
    tft.clearScreen();
    tft.setTextColor(WHITE, BLACK);
    tft.setTextWrap(true);
    tft.println(filename);
  }
  if (!SVF_file)
  {
     if(sd_detach == 0)
       tft.println("cannot open");
     return;
  }
  int file_len = SVF_file.size();
  if(sd_detach == 0)
  {
    tft.print("length ");
    tft.println(file_len, DEC);
  }
  if(file_len <= 0)
    return;
  int index = 0;
  digitalWrite(LED_WIFI, HIGH);
  if(sd_detach == 0)
  {
    // tft.print("SVF");
    tft.drawRect(pb_x,pb_y,pb_w,pb_h,pb_color_frame,pb_color_empty,true);
  }
  int pb_w_full = pb_w-2; // full progress bar width
  int pb_w_anim; // progress width during animated progess
  const int buflen = 512;
  uint8_t data[buflen];
  while(SVF_file.available())
  {
     int len = SVF_file.read(data, buflen);
     int final = index + len >= file_len ? 1 : 0;
     jtag.play_svf_packet(index, data, len, final, report);
     index += len;
     pb_w_anim = pb_w_full * index / file_len;
     if(sd_detach == 0)
     {
       if( (index & 0xFFF) == 0 || final != 0)
         if(pb_w_anim > 0 && pb_w_anim <= pb_w_full)
           tft.fillRect(pb_x+1,pb_y+1,pb_w_anim,pb_h-2,pb_color_full);
       #if 0
       if( (index & 0xFFFF) == 0)
         tft.print(".");
       if(final)
         tft.println("ok");
       #endif
     }
  }
  SVF_file.close();
  if(sd_detach == 0)
    tft.println(report);
  digitalWrite(LED_WIFI, LOW);
  // is SPI detach required?
  if(detach)
  {
    sd_unmount();
    SPI.end();
    pinMode(__CS_SD, INPUT);
    pinMode(__CS_TFT, INPUT);
    pinMode(__DC_TFT, INPUT);
    pinMode(__RES_TFT, INPUT);
    pinMode(__MOSI_TFT, INPUT);
    pinMode(__MISO_TFT, INPUT);
    pinMode(__SCL_TFT, INPUT);
    sd_detach = 1;
    return; // after detach, OLED will be inaccessible
  }
  // after bitstream, if passthru still works
  // OLED may need to be reset, so reinitialize
  // and print final message
  if(sd_detach == 0)
  {
    delay(500);
    tft.begin();
    tft.setRotation(2);
    tft.defineScrollArea(0,0,0, 63, 0);
    tft.scroll(false);
    tft.clearScreen();
    tft.setCursor(0, 0);
    tft.setTextColor(WHITE);
    tft.setTextScale(1);
    tft.println(report);
  }
  // sd_detach = 0;
}


// command response to user typing
String CommandLine(String user)
{
  char jtag_id[100];
  jtag.scan();
  sprintf(jtag_id, "0x%08X", jtag.id());
  if(user == "p")
  {
    spiffs_program_activate = 1; // fixed filename
  }
  else
  if(user.length() > 4)
  {
    sd_program_activate = 1;
    sd_file_name_svf = user;
  }
  return String(jtag_id) + " CommandLine typed: " + user;
}

void onWsEvent(AsyncWebSocket * server, AsyncWebSocketClient * client, AwsEventType type, void * arg, uint8_t *data, size_t len){
  digitalWrite(LED_WIFI, HIGH);
  if(type == WS_EVT_CONNECT){
    Serial.printf("ws[%s][%u] connect\n", server->url(), client->id());
    client->printf("Hello Client %u :)", client->id());
    client->ping();
  } else if(type == WS_EVT_DISCONNECT){
    Serial.printf("ws[%s][%u] disconnect: %u\n", server->url(), client->id());
  } else if(type == WS_EVT_ERROR){
    Serial.printf("ws[%s][%u] error(%u): %s\n", server->url(), client->id(), *((uint16_t*)arg), (char*)data);
  } else if(type == WS_EVT_PONG){
    Serial.printf("ws[%s][%u] pong[%u]: %s\n", server->url(), client->id(), len, (len)?(char*)data:"");
  } else if(type == WS_EVT_DATA){
    AwsFrameInfo * info = (AwsFrameInfo*)arg;
    String msg = "";
    if(info->final && info->index == 0 && info->len == len){
      //the whole message is in a single frame and we got all of it's data
      Serial.printf("ws[%s][%u] %s-message[%llu]: ", server->url(), client->id(), (info->opcode == WS_TEXT)?"text":"binary", info->len);

      if(info->opcode == WS_TEXT){
        for(size_t i=0; i < info->len; i++) {
          msg += (char) data[i];
        }
      } else {
        char buff[3];
        for(size_t i=0; i < info->len; i++) {
          sprintf(buff, "%02x ", (uint8_t) data[i]);
          msg += buff ;
        }
      }
      Serial.printf("%s\n",msg.c_str());

      if(info->opcode == WS_TEXT)
        // client->text("I got your text message in single frame");
        client->text(CommandLine(msg));
      else
        client->binary("I got your binary message");
    } else {
      //message is comprised of multiple frames or the frame is split into multiple packets
      if(info->index == 0){
        if(info->num == 0)
          Serial.printf("ws[%s][%u] %s-message start\n", server->url(), client->id(), (info->message_opcode == WS_TEXT)?"text":"binary");
        Serial.printf("ws[%s][%u] frame[%u] start[%llu]\n", server->url(), client->id(), info->num, info->len);
      }

      Serial.printf("ws[%s][%u] frame[%u] %s[%llu - %llu]: ", server->url(), client->id(), info->num, (info->message_opcode == WS_TEXT)?"text":"binary", info->index, info->index + len);

      if(info->opcode == WS_TEXT){
        for(size_t i=0; i < info->len; i++) {
          msg += (char) data[i];
        }
      } else {
        char buff[3];
        for(size_t i=0; i < info->len; i++) {
          sprintf(buff, "%02x ", (uint8_t) data[i]);
          msg += buff ;
        }
      }
      Serial.printf("%s\n",msg.c_str());

      if((info->index + len) == info->len){
        Serial.printf("ws[%s][%u] frame[%u] end[%llu]\n", server->url(), client->id(), info->num, info->len);
        if(info->final){
          Serial.printf("ws[%s][%u] %s-message end\n", server->url(), client->id(), (info->message_opcode == WS_TEXT)?"text":"binary");
          if(info->message_opcode == WS_TEXT)
            // client->text("I got your text message in multiple frames");
            client->text(CommandLine(msg));
          else
            client->binary("I got your binary message");
        }
      }
    }
  }
  digitalWrite(LED_WIFI, LOW);
}

int sd_delete(String filename)
{
  int deleted = 0;
  // sd_detach = 0;
  if(sd_mount() >= 0)
  {
    if(SD.remove(filename))
    {
      read_directory(SD);
      sd_unmount();
      Ifb.cursor = 0;
      Ifb.topitem = 0;
      init_oled_show_ip();
      // OLED must be initialized before show_directory
      show_directory(Ifb.cursor, Ifb.topitem);
      deleted = 1;
    }
    else
    {
      sd_unmount();
      deleted = 0;
    }
  }
  return deleted;
}


void setup()
{
  pinMode(__CS_SD, INPUT_PULLUP); // interrupt will monitor SD chip select
  Serial.begin(115200);
  Serial.setDebugOutput(true);
  pinMode(LED_WIFI, OUTPUT);
  digitalWrite(LED_WIFI, HIGH); // welcome blink
  pinMode(BTN0_PIN, INPUT_PULLUP); // holding GPIO0 LOW will signal ESP32 to load "passthru.svf" from SPIFFS to JTAG
  delay(30); // allow pullup line to stabilize
  int btn0_pressed_at_powerup = 0;
  if(digitalRead(BTN0_PIN) == LOW)
    btn0_pressed_at_powerup = 1;
  if(btn0_pressed_at_powerup == 0)
  {
  delay(1170); // 1.2s delay, allow FPGA to boot from config flash
  if(digitalRead(__CS_SD) == LOW)
    sd_cs_counter = 1; // SD card is accessed by FPGA
  else
  { // wait some extra time and monitor is FPGA accessing SD
    sd_cs_counter = 0; // reset counter, sd_cs_interrupt() will increment this
    attachInterrupt(digitalPinToInterrupt(BTN0_PIN), sd_cs_interrupt, FALLING);
    for(int i = 0; i < 5 && sd_cs_counter == 0; i++)
      delay(100); // during this delay (total 0.5 s) SD pin will be monitored
    detachInterrupt(digitalPinToInterrupt(BTN0_PIN));
    if(digitalRead(__CS_SD) == LOW) // if interrupt didn't catch the transition
      sd_cs_counter++; // increment just to be sure    
  }
  }
  digitalWrite(LED_WIFI, LOW); // turn off initial blink
  // global variable sd_cs_counter will contain number of sd cs activations

  if(sd_cs_counter == 0 && btn0_pressed_at_powerup == 0)
  // if sd card access is detected at powerup we cannot read SD card
  // with passowords -> we can only use compiled password
  // if btn0 is held during initial blink at powerup
  // we will default to open AP
  // in both cases we skip reading SD card
  {
    // warning - detection of SD accesss is not 100% reliable.
    // Hold BTN0 to be sure to avoid SD access.
    // mounting SD card at this time can interfere with FPGA access.
    // if amiga core is loaded to FPGA config flash
    // and powered up, amiga won't boot from SD
    // because both ESP32 and amiga are accessing SD card at the same time
    // some exclusion mechanism needs to be done.
    // if we skip this, compiled-in password will be used instead from SD
    sd_mount();
    read_config(SD);
    sd_unmount();
  }

  // Start with open AP
  if(btn0_pressed_at_powerup)
    password[0] = '\0'; // nul char terminator = 0-length password = open AP

  #if ESP8266
  WiFi.hostname(host_name);
  #endif
  WiFi.mode(WIFI_AP_STA); // combned AP and STA
  // WiFi.mode(WIFI_AP);
  WiFi.softAP(host_name);
  // WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  if (WiFi.waitForConnectResult() != WL_CONNECTED) {
    Serial.printf("STA: Failed!\n");
    WiFi.disconnect(false);
    delay(1000);
    WiFi.begin(ssid, password);
  }
  else // 2nd blink indicates connection to remote AP
    digitalWrite(LED_WIFI, HIGH); // conected blink
  Serial.print("host:");
  Serial.println(host_name);
  Serial.print("wifi ssid:");
  Serial.println(ssid);
  Serial.print("wifi pass:");
  Serial.println(password);
  Serial.print("web user:");
  Serial.println(http_username);
  Serial.print("web pass:");
  Serial.println(http_password);

  // Serial.println("done!");
  // file_browser(1); // reset
  //Send OTA events to the browser
  ArduinoOTA.onStart([]() { events.send("Update Start", "ota"); });
  ArduinoOTA.onEnd([]() { events.send("Update End", "ota"); });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    char p[32];
    sprintf(p, "Progress: %u%%\n", (progress/(total/100)));
    events.send(p, "ota");
  });
  ArduinoOTA.onError([](ota_error_t error) {
    if(error == OTA_AUTH_ERROR) events.send("Auth Failed", "ota");
    else if(error == OTA_BEGIN_ERROR) events.send("Begin Failed", "ota");
    else if(error == OTA_CONNECT_ERROR) events.send("Connect Failed", "ota");
    else if(error == OTA_RECEIVE_ERROR) events.send("Recieve Failed", "ota");
    else if(error == OTA_END_ERROR) events.send("End Failed", "ota");
  });
  ArduinoOTA.setHostname(host_name);
  ArduinoOTA.begin();

  MDNS.addService("http","tcp",80);

  #if ESP8266
  SPIFFS.begin();
  #endif
  #if ESP32
  SPIFFS.begin(true);
  #endif
  jtag.begin(&SPIFFS);
  ws.onEvent(onWsEvent);
  server.addHandler(&ws);

  events.onConnect([](AsyncEventSourceClient *client){
    client->send("hello!",NULL,millis(),1000);
  });
  server.addHandler(&events);

  #ifdef ESP8266
  server.addHandler(new SPIFFSEditor(http_username,http_password));
  #endif
  #ifdef ESP32
  server.addHandler(new SPIFFSEditor(SPIFFS,http_username,http_password));
  #endif

  server.on("/heap", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send(200, "text/plain", String(ESP.getFreeHeap()));
  });

  // http://192.168.4.1/dir?path=/path/to/directory
  server.on("/dir", HTTP_GET, [](AsyncWebServerRequest *request)
  {
    AsyncResponseStream *response = request->beginResponseStream("text/json");
    DynamicJsonBuffer jsonBuffer;
    JsonObject &root = jsonBuffer.createObject();
    if(request->hasParam("path"))
    {
      AsyncWebParameter* p = request->getParam("path");
      if(p)
        if(p->value())
          DirPath=p->value();
    }
    init_oled_show_ip();
    mount_read_directory();
    root["path"]=DirPath;
    int ndirs = 0;
    JsonArray& data = root.createNestedArray("files");
    for(int i = 0; i < DirN; i++)
    {
      if(DirEntries[i].type) ndirs++; // count how many are dirs before files
      data.add(DirEntries[i].name);
    }
    root["ndirs"]=String(ndirs);
    root.printTo(*response);
    request->send(response);
  });

  // http://192.168.4.1/svf?path=/path/to/sd_card/file.svf
  server.on("/svf", HTTP_GET, [](AsyncWebServerRequest *request)
  {
    int isuccess = 0;
    if(request->hasParam("path"))
    {
      AsyncWebParameter* p = request->getParam("path");
      if(p)
        if(p->value())
        {
          sd_file_name_svf = p->value();
          sd_program_activate = 1;
        }
    }
    // currently web interface doesn't know did it actually succeed
    request->send(200, "text/plain", "requested");
  });

  // http://192.168.4.1/bin?path=/path/to/sd_card/file.bin
  server.on("/bin", HTTP_GET, [](AsyncWebServerRequest *request)
  {
    int isuccess = 0;
    if(request->hasParam("path"))
    {
      AsyncWebParameter* p = request->getParam("path");
      if(p)
        if(p->value())
        {
          sd_file_name_svf = sd_file_name_f32c_svf;
          sd_file_name_bin = p->value();
          sd_binary_file_activate = 1;
        }
    }
    // currently web interface doesn't know did it actually succeed
    request->send(200, "text/plain", "requested");
  });


  // http://192.168.4.1/mkdir?path=/path/to/new_directory
  server.on("/mkdir", HTTP_GET, [](AsyncWebServerRequest *request)
  {
    int isuccess = 0;
    if(request->hasParam("path"))
    {
      AsyncWebParameter* p = request->getParam("path");
      if(p)
        if(p->value())
        {
          if(sd_mount()>=0)
          {
            if(SD.mkdir(p->value()))
            {
              isuccess = 1;
              // todo: switch to parent directory (path go up one level)
            }
            sd_unmount();
          }
        }
    }
    if(isuccess)
    {
      init_oled_show_ip();
      mount_read_directory();
    }
    String message = isuccess ? "success" : "fail";
    request->send(200, "text/plain", message);
  });

  // http://192.168.4.1/rmdir?path=/path/to/junk_directory
  // directory should be empty to be removed
  server.on("/rmdir", HTTP_GET, [](AsyncWebServerRequest *request)
  {
    int isuccess = 0;
    if(request->hasParam("path"))
    {
      AsyncWebParameter* p = request->getParam("path");
      if(p)
        if(p->value())
        {
          if(sd_mount()>=0)
          {
            if(SD.rmdir(p->value()))
            {
              isuccess = 1;
              // todo: switch to parent directory (path go up one level)
            }
            sd_unmount();
          }
        }
    }
    if(isuccess)
    {
      init_oled_show_ip();
      mount_read_directory();
    }
    String message = isuccess ? "success" : "fail";
    request->send(200, "text/plain", message);
  });

  // http://192.168.4.1/rm?path=/path/to/junk.file
  server.on("/rm", HTTP_GET, [](AsyncWebServerRequest *request)
  {
    int isuccess = 0;
    if(request->hasParam("path"))
    {
      AsyncWebParameter* p = request->getParam("path");
      if(p)
        if(p->value())
        {
          if(sd_mount()>=0)
          {
            if(SD.remove(p->value()))
            {
              isuccess = 1;
            }
            sd_unmount();
          }
        }
    }
    if(isuccess)
    {
      init_oled_show_ip();
      mount_read_directory();
    }
    String message = isuccess ? "success" : "fail";
    request->send(200, "text/plain", message);
  });

  // Download File from SD card
  // http://192.168.4.1/dl?path=/path/to/download.file
  server.on("/dl", HTTP_GET, [](AsyncWebServerRequest *request)
  {
    if(request->hasParam("path"))
    {
      AsyncWebParameter* p = request->getParam("path");
      if(p)
        if(p->value())
        {
          if(sd_mount() >= 0)
          {
            AsyncWebServerResponse *response = request->beginResponse(SD, p->value(), String(), true);
            request->send(response);
            // sd_unmount(); // can't immediately unmount - file will be sent later
          }
          else
          {
            request->send(200, "text/plain", "SD card mount failed");
          }
        }
    }
  });

  server.serveStatic("/", SPIFFS, "/").setDefaultFile("index.htm");

  server.onNotFound([](AsyncWebServerRequest *request){
    Serial.printf("NOT_FOUND: ");
    if(request->method() == HTTP_GET)
      Serial.printf("GET");
    else if(request->method() == HTTP_POST)
      Serial.printf("POST");
    else if(request->method() == HTTP_DELETE)
      Serial.printf("DELETE");
    else if(request->method() == HTTP_PUT)
      Serial.printf("PUT");
    else if(request->method() == HTTP_PATCH)
      Serial.printf("PATCH");
    else if(request->method() == HTTP_HEAD)
      Serial.printf("HEAD");
    else if(request->method() == HTTP_OPTIONS)
      Serial.printf("OPTIONS");
    else
      Serial.printf("UNKNOWN");
    Serial.printf(" http://%s%s\n", request->host().c_str(), request->url().c_str());

    if(request->contentLength()){
      Serial.printf("_CONTENT_TYPE: %s\n", request->contentType().c_str());
      Serial.printf("_CONTENT_LENGTH: %u\n", request->contentLength());
    }

    int headers = request->headers();
    int i;
    for(i=0;i<headers;i++){
      AsyncWebHeader* h = request->getHeader(i);
      Serial.printf("_HEADER[%s]: %s\n", h->name().c_str(), h->value().c_str());
    }

    int params = request->params();
    for(i=0;i<params;i++){
      AsyncWebParameter* p = request->getParam(i);
      if(p->isFile()){
        Serial.printf("_FILE[%s]: %s, size: %u\n", p->name().c_str(), p->value().c_str(), p->size());
      } else if(p->isPost()){
        Serial.printf("_POST[%s]: %s\n", p->name().c_str(), p->value().c_str());
      } else {
        Serial.printf("_GET[%s]: %s\n", p->name().c_str(), p->value().c_str());
      }
    }
    request->send(404);
  });
  server.onFileUpload([](AsyncWebServerRequest *request, const String& filename, size_t index, uint8_t *data, size_t len, bool final){
    static int packet_counter = 0;
    static int out_of_order = 0;
    static int write_error = 0;
    static size_t expect_index = 0;
    static char report[256];
    static File save_file;
    if(!index)
    {
      Serial.printf("UploadStart: %s\n", filename.c_str());
      packet_counter=0;
      expect_index = index; // for out-of-order detection
      out_of_order = 0;
      write_error = 0;
      report[0] = '\0';
      digitalWrite(LED_WIFI, HIGH);
#if 1
      int params = request->params();
      // Serial.printf("request params %d\n", params);
      String save_file_name = "";
      for(int i=0;i<params;i++)
      {
        AsyncWebParameter* p = request->getParam(i);
        if(p->isFile()){
          //Serial.printf("_FILE[%s]: %s, size: %u\n", p->name().c_str(), p->value().c_str(), p->size());
        } else if(p->isPost()){
          //Serial.printf("_POST[%s]: %s\n", p->name().c_str(), p->value().c_str());
        } else {
          //Serial.printf("_GET[%s]: %s\n", p->name().c_str(), p->value().c_str());
          if(p->name() == "path") // directory path specified -> no jtag upload but save to SD
          {
            if(p->value())
            {
              Serial.printf("save %s to directory %s\n", filename.c_str(), p->value().c_str());
              save_dirname = p->value();
              if(!save_dirname.startsWith("/"))
                save_dirname = "/"+save_dirname;
              if(!save_dirname.endsWith("/"))
                save_dirname = save_dirname+"/";
              save_file_name = save_dirname+filename;
              // we want later to cd to this directory
              // if not root, we must cut off trailing "/"
              if(save_dirname != "/")
                while(save_dirname.endsWith("/"))
                  save_dirname = save_dirname.substring(0,save_dirname.length()-1);
            }
          }
        }
      }
      if(save_file_name != "" && save_file_name != "/")
      {
        if(sd_mount() >= 0)
        {
          save_file = SD.open(save_file_name.c_str(), FILE_WRITE);
          if(save_file)
            Serial.printf("file writing to %s starts\n", save_file_name.c_str());
          else
          {
            Serial.printf("file writing to: %s failed\n", save_file_name.c_str());
            sd_unmount();
          }
        }
        else
          Serial.printf("SD card mount failed\n");
      }
#endif
    }
    #if 0
      Serial.printf("%s", (const char*)data);
      if((packet_counter % 100) == 0 || packet_counter < 4 || final != 0)
        Serial.printf("packet %d len=%d\n", packet_counter, len); // the content
      packet_counter++;
    #endif
    if(index != expect_index)
      out_of_order++;
    expect_index = index + len;
    if(out_of_order == 0)
    {
      if(save_file)
      {
        if(len != save_file.write(data, len))
          write_error++;
      }
      else
        jtag.play_svf_packet(index, data, len, final, report);
    }
    if(final)
    {
      if(save_file) // FIXME: track also write error (disk full)
      {
        if(write_error == 0)
          strcpy(report,"success");
        else
          strcpy(report,"fail"); // disk full probably
      }
      if(out_of_order != 0)
        request->send(200, "text/plain", "received" + String(out_of_order) + " out-of-order packets");
      else
        request->send(200, "text/plain", report);
      if(save_file)
      {
        save_file.close();
        #if 0
        sd_unmount();
        // disabled because the same here will be done
        // when web page reloads
        // then it will init OLED to read directory content
        init_oled_show_ip();
        // cd to the uploaded file's directory
        DirPath=save_dirname;
        mount_read_directory();
        #endif
      }
      Serial.printf("UploadEnd: %s (%u)\n", filename.c_str(), index+len);
      digitalWrite(LED_WIFI, LOW);
    }
  });
  server.onRequestBody([](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total){
    if(!index)
      Serial.printf("BodyStart: %u\n", total);
    Serial.printf("%s", (const char*)data);
    if(index + len == total)
      Serial.printf("BodyEnd: %u\n", total);
  });
  server.begin();
  digitalWrite(LED_WIFI, LOW); // remove conected blink
}


// Report IP address to serial every 15 seconds.
// On ULX3S board it works only if USB-serial only
// if passthru bitstream is loaded.
// Otherwise try 192.168.4.1, "websvf" hostname
// or tools like arp, nmap, tcpdump, ...
void periodic()
{
  const int32_t report_interval = 10; // ms
  static int32_t before_ms;
  static uint32_t btn0_hold_counter = 0; // must hold btn0 1 second to take effect
  int32_t now_ms = millis();
  int32_t diff_ms = now_ms - before_ms;
  if(abs(diff_ms) > report_interval)
  {
    before_ms = now_ms;
    if(sd_detach == 0)
      file_browser(0);
    if(digitalRead(BTN0_PIN) == LOW)
    { // btn0 pressed
      if(btn0_hold_counter == 50) // short hold, enable OLED and SD access
      {
        sd_detach = 0; // allow SD and OLED access
        init_oled_show_ip();
      }
      if(btn0_hold_counter == 200) // hold button 2 seconds to take control over FPGA
        spiffs_program_activate = 1;
      btn0_hold_counter++;
    }
    else
    { // btn0 released
       btn0_hold_counter = 0;
    }
  }
}

void loop()
{
  if(sd_program_activate > 0)
  {
    if(sd_mount() >= 0)
    {
      program_file(SD, sd_file_name_svf, 1);
      sd_unmount(); // ?? if we unmount after SPI detach hope it won't reattach
    }
    sd_program_activate = 0;
  }
  else
  {
    if(spiffs_program_activate > 0)
    {
      program_file(SPIFFS, "/passthru.svf", 0);
      spiffs_program_activate = 0;
      mount_read_directory();
    }
    else
      if(sd_binary_file_activate > 0)
      {
        if(sd_mount() >= 0)
        { // upload f32c bitstream
          program_file(SD, sd_file_name_svf, 1);
          sd_unmount();
        }
        if(sd_mount() >= 0)
        { // upload and execute binary
          f32c_exec_binary(SD, sd_file_name_bin, 0x80000000);
          sd_unmount();
        }
        sd_binary_file_activate = 0;
      }
      else
        periodic();
  }
  ArduinoOTA.handle();
}

/* TODO
 * [x] faster scanning for SPI keys (improve user click experience)
 * [ ] download file to web browser
 */
