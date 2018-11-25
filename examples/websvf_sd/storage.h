#ifndef STORAGE_H
#define STORAGE_H

#if ESP8266
#include <FS.h>
#endif // ESP8266

#if ESP32
#include <SPIFFS.h>
#endif // ESP32

#include <SD.h>

#include "hardware.h" // for BTN_MAX

extern String DirPath;
extern int DirN;

struct S_DirEntries
{
  char *name; // item name: pointer to 0-terminated string
  size_t length; // file length (bytes)
  uint8_t type; // item type 1-directory, 0-file
};
extern struct S_DirEntries *DirEntries;

// struct for interactive file browser state
struct S_ifb
{
  uint8_t hold[BTN_MAX]; // hold counter for holding buttons
  int topitem; // number of file item displayed on top of the window
  int cursor; // cursor position on file item 
};
extern struct S_ifb Ifb;


extern char *DirNames;

extern String sd_file_name_svf;
extern String sd_file_name_bin;
extern String sd_file_name_f32c_svf;
extern int sd_program_activate;
extern int spiffs_program_activate;
extern int sd_detach;
extern int sd_binary_file_activate;
extern int sd_cs_counter;
extern int sd_mounted;

extern String save_dirname;
extern String DirPath;
extern int DirN;
extern struct S_DirEntries *DirEntries;
extern char *DirNames;

void init_oled_show_ip();
void mount_read_directory();
void show_directory(int cursor, int topitem);
void sd_unmount();
int sd_mount();
int sd_delete(String filename);
void read_config(fs::FS &storage);
void try_to_autoexec(fs::FS &storage);
void read_directory(fs::FS &storage);
void show_directory(int cursor, int topitem);
void refresh_dir_line(int cursor, int highlight, int topitem);
void file_browser(uint8_t reset);

#endif
