# ESP32 and ESP8266 Arduino as (X)SVF JTAG programmer

Works as standalone SVF or XSVF JTAG programmer
for FPGA devices. This one should be better than my 
[wifi_jtag](https://github.com/emard/wifi_jtag). 
ESP8266 reads file from onboard SPI flash chip at ESP8266
(it can hold 3MB) to program the target JTAG device.
I tested it on my Lattice XP2.
ESP32 accepts SVF file upload from web browser.
I tested it on Lattice ECP5.

It is small fork of Clifford Wolf's [Lib(X)SVF](http://www.clifford.at/libxsvf/),
with minimal adaptation to get it working with ESP8266.
Arduino ESP8266 Library wrapper is added with proper SPIFFS
buffering and a small bugfix to original "svf.c" is done
to accept decimal floats, e.g. it accepts now numbers
like 1.00E-02 while original library would throw syntax error or
accept only 1E-02

# Install

Close arduino

    cd ~/Arduino/libraries
    git clone https://github.com/emard/LibXSVF

Some additional dependencies are required, this list may not 
be complete or some apply for ESP32 or ESP8266. Here are
git checkouts known to work on ESP32. espressif/arduino-esp32
from 2018 doesn't work in this combination - ESPAsyncWebServer
itself works but can't open any pages stored in SPIFFS.

    *** ESP32 ***
    arduino-esp32 Tue Dec 19 11:07:22 2017 -0200
      https://github.com/espressif/arduino-esp32
      git checkout 70d0d4648766cea047613062ed50a9ecfc9de31f
    AsyncTCP Sat Nov 25 23:51:36 2017 +0100
      https://github.com/me-no-dev/AsyncTCP
      git checkout 4dbbf1060923fd3940c6478bc2ac887ec389397f
    ESPAsyncWebServer Sun Nov 26 02:24:07 2017 +0200
      https://github.com/me-no-dev/ESPAsyncWebServer
      git checkout 232b87aeb12dd212af21b62a9f68e5e2c89a3a9c
    ArduinoJson Fri Jan 19 15:35:19 2018 +0100
      https://github.com/bblanchon/ArduinoJson
      git checkout cf5396aaed6d16d1fb4e73b82ce6606938591043
    arduino-esp32fs-plugin 
    https://github.com/me-no-dev/arduino-esp32fs-plugin
      release ESP32FS-v0.1.zip

    *** ESP8266 ***
    ESPAsyncTCP
        https://github.com/me-no-dev/ESPAsyncTCP
    FSBrowserNG
        https://github.com/gmag11/FSBrowserNG
    ESPAsyncWebServer
        https://github.com/me-no-dev/ESPAsyncWebServer
    NtpClient
        https://github.com/gmag11/NtpClient
    Time

    *** OLED ***
    SSD1331
    https://github.com/emard/SSD_13XX

Start arduino, open examples->LibXSVF

# Upload bitstream file to SPI flash

ESP8266 (examples/prog)

In the sketch example, you can place file in 
"/data/bitstream.svf" and click 
Tools->[ESP8266 Sketch Data Upload](https://github.com/esp8266/arduino-esp8266fs-plugin),
or get some cool Web server like [FSBrowserNG](https://github.com/gmag11/FSBrowserNG) which can already
upload any file to "/bitstream.svf" using web browser.

ESP32 (examples/websvf)

Process packetized SVF stream from web file upload.
Packets need to come in sequential order.
Open page esp32.lan, enter any string and press
ENTER, it will show JTAG device ID.
Open page esp32.lan/upload.htm
navigate to SVF file and click upload. 2MB SVF file
uploads in 12 seoonds.
ESP32 needs to buffer each command, remember to
limit command size to 128 kbit or less at SVF file generation.
Lattice example that limits to 8 kbit (works up to 128):

    ddtcmd  -oft -svfsingle -revd -maxdata 8 -if bitstream.xcf -of bitstream.svf

# Connect to WiFi

On power up it will shortly try to connect to the access point
with default ssid/password:

    ssid: websvf
    pass: 12345678

If it can't connect as wifi client, it will become wifi access point
with same ssid and passowrd, IP address of server will be 192.168.4.1 
and it will give UP addresses to clients like 192.168.4.2, 3, etc...

To change wifi settings, insert SD card with
file "ulx3s-wifi.conf" in SD root directory:

    {
       "host_name": "ulx3s",
       "ssid": "ulx3s",
       "password": "testpass",
       "http_username": "user",
       "http_password": "pass"
    }

Max file length 2047 bytes and json syntax.
For "host_name" and "ssid" it's 
recommended to use the same value.

# JTAG PINOUT

It's currently "hidden" in xsvftool-esp8266.c
You can edit this file and recompile sketch 
to change pinout.

    #if ESP9266
    #define TCK 14 // NodeMCU D5
    #define TMS  5 // NodeMCU D1
    #define TDI 13 // NodeMCU D7
    #define TDO 12 // NodeMCU D6
    #endif

    #if ESP32
    #define TCK 18
    #define TMS 21
    #define TDI 23
    #define TDO 19
    #endif

# TODO

    [x] fix memory leaks, if WEB TCP connection is broken,
        it won't free() what it had malloc()'d
    [x] make upload page be the home page
    [ ] SD card support
    [ ] OLED and buttons support
    [ ] websvf AP mode
    [x] progress bar
    [x] after upload show status, success or error
    [ ] after upload show elapsed time
    [ ] test websvf on ESP8266
    [ ] SPI or DMA (remote control on ESP32) acceleration
    [x] report IP to usb-serial (works only if passthru image is loaded)
    [ ] Clicking on "upload" button during already running upload
        will corrupt the transfer.
        Use upload-running flag to prevent concurrent uploads.
    [ ] Timeout (cca 10 minutes) to clear upload-running flag
        as some help in case of broken connections
    [ ] report error if out-of-order packet is received
    [ ] make it work from text client "lynx" currently it just opens
        empty page with some "HTTP/1.0 200 OK" response.
        Text client "elinks" works on "minimal.htm" page, though.

