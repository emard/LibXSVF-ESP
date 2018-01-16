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
#define __CS_TFT       15
#define __DC_TFT       16
#define __RES_TFT      25
#define __MOSI_TFT     13
#define __MISO_TFT     12
#define __SCL_TFT      14
#endif
// pcb v1.8
#define __CS_SD        13
#define __CS_TFT       17
#define __DC_TFT       16
#define __RES_TFT      25
#define __MOSI_TFT     15
#define __MISO_TFT      2
#define __SCL_TFT      14
SSD_13XX tft = SSD_13XX(__CS_TFT, __DC_TFT);
#endif
#include <ArduinoOTA.h>
#include <ESPAsyncWebServer.h>
#include <SPIFFSEditor.h>
#include <LibXSVF.h>

// At boot it will attempt to connect as client.
// If this attempt fails, it will become AP.
// Same ssid/password apply for client and AP.
const char *ssid = "websvf";
const char *password = "12345678";
const char *hostName = "websvf"; // request local name when connected as client
const char *http_username = "admin";
const char *http_password = "admin";

// verbose print on serial
#define DEBUG 0

// SKETCH BEGIN
AsyncWebServer server(80);
AsyncWebSocket ws("/ws");
AsyncEventSource events("/events");

#define LED_WIFI 5

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

String sd_file_name_svf = "";
int sd_program_activate = 0;
int sd_detach = 0;

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

// string compare function for directory soring, ignore case
int DirNameCmp(const void *a, const void *b)
{
  const struct S_DirEntries *sa = (const struct S_DirEntries *)a;
  const struct S_DirEntries *sb = (const struct S_DirEntries *)b;
  return strcasecmp(sa->name, sb->name);
}

int sd_mount()
{
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
  return 0; // success
}

void sd_unmount()
{
  SD.end();
}

// read current directory path, don't recurse child dirs
// pass1: read to determine number of entries and sum all string lengths
// realloc() size
// pass2: read to fill the content to realloced area
void read_directory(fs::FS &storage)
{
  // counters
  int ndirs = 0, nfiles = 0, all_names_length = 0;
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
  if(reset || Ifb.hold[BTN_PWR] == 1)
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
      mount_read_directory();
      // Serial.print(DirPath);
    }
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
    else // it's afile
    {
      sd_file_name_svf = full_path;
      sd_program_activate = 1;
    }
  }
  if(Ifb.hold[BTN_DOWN] == 1 || Ifb.hold[BTN_DOWN] > 4)
    if(Ifb.cursor < DirN-1)
      Ifb.cursor++;
  if(Ifb.hold[BTN_UP] == 1 || Ifb.hold[BTN_UP] > 4)
    if(Ifb.cursor > 0)
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
void program_file(fs::FS &storage, String filename)
{
  tft.clearScreen();
  tft.setTextColor(WHITE, BLACK);
  tft.setTextWrap(true);
  if(sd_mount() < 0)
    return;
  tft.println(filename);
  SVF_file = storage.open(filename);
  // progress bar geometry
  const int pb_x = 0, pb_y = 40, pb_w = 95, pb_h = 8;
  const int pb_color_frame = WHITE, pb_color_empty = BLACK, pb_color_full = BLUE;
  static char report[256];
  if (!SVF_file)
  {
     tft.println("cannot open");
     return;
  }
  int file_len = SVF_file.size();
  tft.print("length ");
  tft.println(file_len, DEC);
  if(file_len <= 0)
    return;
  int index = 0;
  digitalWrite(LED_WIFI, HIGH);
  tft.print("SVF");
  tft.drawRect(pb_x,pb_y,pb_w,pb_h,pb_color_frame,pb_color_empty,true);
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
     if( (index & 0xFFF) == 0 || final != 0)
       if(pb_w_anim > 0 && pb_w_anim <= pb_w_full)
         tft.fillRect(pb_x+1,pb_y+1,pb_w_anim,pb_h-2,pb_color_full);
     if( (index & 0xFFFF) == 0)
       tft.print(".");
     if(final)
       tft.println("ok");
  }
  SVF_file.close();
  sd_unmount();
  tft.println(report);
  digitalWrite(LED_WIFI, LOW);
  // after bitstream, OLED may be reset,
  // so reinitialize
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
  SPI.end();
  pinMode(__CS_SD, INPUT);
  pinMode(__CS_TFT, INPUT);
  pinMode(__DC_TFT, INPUT);
  pinMode(__RES_TFT, INPUT);
  pinMode(__MOSI_TFT, INPUT);
  pinMode(__MISO_TFT, INPUT);
  pinMode(__SCL_TFT, INPUT);
  sd_detach = 1;
}

// command response to user typing
String CommandLine(String user)
{
  char jtag_id[100];
  jtag.scan();
  sprintf(jtag_id, "0x%08X", jtag.id());
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
#if 0
void listDir(fs::FS &fs, const char * dirname, uint8_t levels){
    Serial.printf("Listing directory: %s\n", dirname);

    File root = fs.open(dirname);
    if(!root){
        Serial.println("Failed to open directory");
        return;
    }
    if(!root.isDirectory()){
        Serial.println("Not a directory");
        return;
    }

    File file = root.openNextFile();
    while(file){
        if(file.isDirectory()){
            Serial.print("  DIR : ");
            Serial.println(file.name());
            if(levels){
                listDir(fs, file.name(), levels -1);
            }
        } else {
            Serial.print("  FILE: ");
            Serial.print(file.name());
            Serial.print("  SIZE: ");
            Serial.println(file.size());
        }
        file = root.openNextFile();
    }
}
#endif
void setup(){
  pinMode(LED_WIFI, OUTPUT);
  Serial.begin(115200);
  Serial.setDebugOutput(true);

  tft.begin();
  tft.setRotation(2);
  tft.defineScrollArea(0,0,0, 63, 0);
  tft.scroll(false);
  tft.clearScreen();
  tft.setCursor(0, 0);
  tft.setTextColor(WHITE);
  tft.setTextScale(1);
  tft.setTextWrap(false);
  tft.println("Connecting to WiFi");

  #if ESP8266
  WiFi.hostname(hostName);
  #endif
  WiFi.mode(WIFI_AP_STA);
  WiFi.softAP(hostName);
  WiFi.begin(ssid, password);
  if (WiFi.waitForConnectResult() != WL_CONNECTED) {
    Serial.printf("STA: Failed!\n");
    WiFi.disconnect(false);
    delay(1000);
    WiFi.begin(ssid, password);
  }
  IPAddress ip = WiFi.localIP();
  tft.clearScreen();
  tft.setCursor(0, 0);
  tft.println(ip);

#if 0
  tft.println("Initializing SD card...");
  if(!SD.begin(13, SPI, 20000000, "/sd")){
        Serial.println("Card Mount Failed");
        tft.println("Card Mount Failed");
        // return;
  }
  uint8_t cardType = SD.cardType();
  if(cardType == CARD_NONE){
        Serial.println("No SD card attached");
        tft.println("No SD card attached");
        // return;
  }
#endif
  read_directory(SD);
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
  ArduinoOTA.setHostname(hostName);
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
    static size_t expect_index = 0;
    static char report[256];
    if(!index)
    {
      Serial.printf("UploadStart: %s\n", filename.c_str());
      packet_counter=0;
      expect_index = index; // for out-of-order detection
      out_of_order = 0;
      digitalWrite(LED_WIFI, HIGH);
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
      jtag.play_svf_packet(index, data, len, final, report);
    if(final)
    {
      if(out_of_order != 0)
        request->send(200, "text/plain", "received" + String(out_of_order) + " out-of-order packets");
      else
        request->send(200, "text/plain", report);
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
}

// Report IP address to serial every 15 seconds.
// On ULX3S board it works only if USB-serial only
// if passthru bitstream is loaded.
// Otherwise try 192.168.4.1, "websvf" hostname
// or tools like arp, nmap, tcpdump, ...
void report_ip()
{
  const int32_t report_interval = 100; // ms
  static int32_t before_ms;
  int32_t now_ms = millis();
  int32_t diff_ms = now_ms - before_ms;
  if(abs(diff_ms) > report_interval)
  {
    IPAddress ip = WiFi.localIP();
    before_ms = now_ms;
    // Serial.println(ip);
    if(sd_detach == 0)
      file_browser(0);
  }
}

void loop()
{
  if(sd_program_activate)
  {
    if(sd_mount() >= 0)
    {
      program_file(SD, sd_file_name_svf);
      sd_unmount();
    }
    sd_program_activate = 0;
  }
  else
    report_ip();
  ArduinoOTA.handle();
}

