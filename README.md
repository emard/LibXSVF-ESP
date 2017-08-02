# ESP8266 Arduino as (X)SVF JTAG programmer

Works as standalone SVF or XSVF JTAG programmer
for FPGA devices. This one should be better than my wifi_jtag. 
Reads file from onboard SPI flash chip at ESP8266
(it can hold 3MB) to program the target JTAG device.
I tested it on my Lattice XP2.

It is small fork of Clifford Wolf [Lib(X)SVF](http://www.clifford.at/libxsvf/),
minimal adaptation to get it working with ESP8266.
Arduino ESP8266 Library wrapper is added and a small bugfix is done
to accept decimal floats in "svf.c", e.g. it accepts now numbers
like 1.00E-02 while original library would throw syntax error or
accept only 1E-02

# Upload bitstream file to SPI flash

In the sketch example, you can place file in 
"/data/bitstream.svf" and click Tools->"ESP8266 Sketch Data Upload",
(google a bit howto install that tool) or get some cool 
Web server like FSBrowserNG which can already
upload any file to "/bitstream.svf" using web browser.

# JTAG PINOUT

It's currently "hidden" in xsvftool-esp8266.c
You can edit this file and recompile sketch 
to change pinout.

    #define TCK 14 // NodeMCU D5
    #define TMS  5 // NodeMCU D1
    #define TDI 13 // NodeMCU D7
    #define TDO 12 // NodeMCU D6
