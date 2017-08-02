# Lib(X)SVF player for ESP8266 JTAG

Reads file from SPIFFS and plays (or uploads) it to
target JTAG device. In the sketch example, you can
place file in "/data/bitstream.svf" 
using "ESP8266 Sketch Data Upload" or from Web
server like FSBrowserNG to "/bitstream.svf".

Arduino ESP8266 wrapper is added and a small bugfix
to accept decimal floats, e.g. it accepts now numbers
like 1.00E-02 while before only 1E-02 would be accepted.

Something important still doesn't work well.
SVF file starts uploading with some acceptable 
speed and after 600-2000 commands, it becomes
too slow to be of any practical use. Typical 
file has over 10000 commands. It seems that
filesystem read (byte at a time) is becoming
slow.
