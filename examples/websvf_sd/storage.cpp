#include <ArduinoJson.h>
#include "storage.h"
#include "hardware.h"
#include "wifi.h"
#include "web.h"
#include "disp.h"
#include "keyboard.h"

String sd_file_name_svf = "";
String sd_file_name_bin = "/ULX3S/f32c/autoexec/autoexec.bin";
String sd_file_name_f32c_svf = "/ULX3S/f32c/autoexec/f32c.svf";
int sd_program_activate = 0;
int sd_detach = 1; // start with SPI bus detached (SD and OLED initially not in use)
int sd_binary_file_activate = 0; // any filename, passed to sd_file_name_svf
int sd_mounted = 0; // track SD_mount() state

// directory read to malloced struct
String save_dirname = "/"; // where to save file uploaded file
String DirPath = "/"; // current directory path
int DirN; // number of entries in current dir path
struct S_DirEntries *DirEntries = NULL; // realloced N such entries
char *DirNames = NULL; // realloced size for all names in current directory

struct S_ifb Ifb; // instance of interactive file browser state

void sd_unmount()
{
  if(!sd_mounted)
    return;
  sd_mounted = 0;
  SD.end();
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
        // if OLED is not initialized ESP32 will crash
        // tft.println("Card Mount Failed");
        return -1;
  }
  uint8_t cardType = SD.cardType();
  if(cardType == CARD_NONE){
        SD.end();
        #if DEBUG
        Serial.println("No SD card attached");
        #endif
        // if OLED is not initialized ESP32 will crash
        // tft.println("No SD card attached");
        return -2;
  }
  sd_mounted = 1;
  return 0; // success
}

// string compare function for directory soring, ignore case
int DirNameCmp(const void *a, const void *b)
{
  const struct S_DirEntries *sa = (const struct S_DirEntries *)a;
  const struct S_DirEntries *sb = (const struct S_DirEntries *)b;
  return strcasecmp(sa->name, sb->name);
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
       #if 0 // unused, example
       /* support example for unused format
  "client":
  [
    { "ssid": "ssid1", "password": "passwd1"  },
    { "ssid": "ssid2", "password": "passwd2" }
  ],
        */
       int l = jroot["client"].size();
       Serial.printf("client array length %d\n", l);
       a = jroot["client"][0]["password"];
       if(a)
         Serial.println(a);
       #endif // unused, example
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

// SD should be already mounted prior to this
void try_to_autoexec(fs::FS &storage)
{
  File bin_file = storage.open(sd_file_name_bin);
  // does autoexec.bin file exist?
  if(bin_file)
  {
    bin_file.close();
    File svf_file = storage.open(sd_file_name_f32c_svf);
    // does f32c.svf file exist?
    if(svf_file)
    {
      svf_file.close();
      sd_file_name_svf = sd_file_name_f32c_svf;
      sd_binary_file_activate = 1;
    }
  }
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
