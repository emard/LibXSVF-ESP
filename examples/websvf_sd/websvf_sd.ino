#if ESP8266
#include <Hash.h>
#include <FS.h>
#endif // ESP8266

#if ESP32
#include <SPIFFS.h>
// SD card not working:
// Edit ESP32 SD.cpp line 33
// spi.begin() -> spi.begin(14, 12, 13, -1); // v1.7
// spi.begin() -> spi.begin(14, 2, 15, -1); // v1.8 CS=13
// SD chipselect is pin 26
#include <SD.h>

int sd_cs_counter = 0; // interrupt detects external SD access

// Edit SSD_13XX.cpp line 266
// SPI.begin() -> SPI.begin(14, 12, 13, -1); // v1.7
// SPI.begin() -> SPI.begin(14, 2, 15, -1); // v1.8 CS=13
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


// #include <ArduinoOTA.h>
// Errors compiling AsyncTCP on ESP32?
// cd Arduino/libraries
// git clone https://github.com/me-no-dev/AsyncTCP
// cd AsyncTCP
// git checkout idf-update
#endif // ESP32

#include "hardware.h"
#include "f32cup.h"
#include "web.h"
#include "storage.h"
#include "jtag.h"
#include "disp.h"
#include "wifi.h"
#include "keyboard.h"


// verbose print on serial
#define DEBUG 0

// SKETCH BEGIN
#if 0
AsyncWebServer server(80);
AsyncWebSocket ws("/ws");
AsyncEventSource events("/events");
#endif


void IRAM_ATTR sd_cs_interrupt()
{
  sd_cs_counter++;
}


void setup()
{
  pinMode(__CS_SD, INPUT_PULLUP); // interrupt will monitor SD chip select
  pinMode(__MOSI_TFT, INPUT_PULLUP); // pullup SPI shared with SD
  pinMode(__MISO_TFT, INPUT_PULLUP); // pullup SPI shared with SD
  pinMode(__SCL_TFT, INPUT_PULLUP); // pullup SPI shared with SD
  pinMode(__DC_TFT, INPUT_PULLUP); // pullup SPI
  pinMode(__CS_TFT, INPUT_PULLUP); // pullup SPI
  pinMode(__RES_TFT, INPUT_PULLUP); // pullup SPI
  Serial.begin(115200);
  Serial.setDebugOutput(true);
  pinMode(LED_WIFI, OUTPUT);
  digitalWrite(LED_WIFI, HIGH); // welcome blink
  pinMode(BTN0_PIN, INPUT_PULLUP); // holding GPIO0 LOW will signal ESP32 to load "passthru.svf" from SPIFFS to JTAG
  delay(30); // allow pullup line to stabilize 30 ms is sufficient
  int btn0_pressed_at_powerup = 0;
  if(digitalRead(BTN0_PIN) == LOW)
    btn0_pressed_at_powerup = 1;
  if(btn0_pressed_at_powerup == 0)
  {
  // delay(1170); // 1.2s delay, allow FPGA to boot from config flash
  #if 1
  if(digitalRead(__MOSI_TFT) == LOW)
    sd_cs_counter = 1; // SD card is accessed by FPGA
  else
  #endif
  { // wait some extra time and monitor is FPGA accessing SD
    sd_cs_counter = 0; // reset counter, sd_cs_interrupt() will increment this
    attachInterrupt(digitalPinToInterrupt(__MOSI_TFT), sd_cs_interrupt, FALLING);
    for(int i = 0; i < 10 && sd_cs_counter == 0; i++)
      delay(100); // during this delay (total 0.5 s) SD pin will be monitored
    detachInterrupt(digitalPinToInterrupt(__MOSI_TFT));
    #if 1
    if(digitalRead(__MOSI_TFT) == LOW) // if interrupt didn't catch the transition
      sd_cs_counter++; // increment just to be sure    
    #endif
  }
  }
  digitalWrite(LED_WIFI, LOW); // turn off initial blink
  // global variable sd_cs_counter will contain number of sd cs activations
  Serial.printf("\nsd_cs_counter=%d btn0_pressed=%d\n", sd_cs_counter, btn0_pressed_at_powerup);
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
    if(sd_mount() >= 0)
    {
      Serial.println("reading sd config");
      read_config(SD);
      try_to_autoexec(SD);
      sd_unmount();
    }
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
  #if 0
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
  #endif

  #if ESP8266
  SPIFFS.begin();
  #endif
  #if ESP32
  SPIFFS.begin(true);
  #endif
  jtag.begin(&SPIFFS);

  web_server_init();

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
  #if 0
  ArduinoOTA.handle();
  #endif
}
