# Somfy_MQTT

Controlling Somfy/Simu RTS 433MHz radio shutters via ESP8266 and CC1101 Transceiver Module.
Experimental version.
Use at your own risk. 


This version is a fork of Somfy_MQTT in order to make Somfy & Simu RTS protocol work with Message Queuing Telemetry Transport (MQTT) procotol.
MQTT enables communication with several different Smart Home systems like OpenHab, FHEM or Home Assistant.
It is for use with a ECP8266 with a CC1101 module with the library made by LSatan.
All the credits come to madmartin for the excelent User Interface and Nickduino & EinfachArne for Somfy Remote Class.

### Cédits
https://github.com/madmartin/Somfy_MQTT
https://github.com/Nickduino/Somfy_Remote
https://github.com/EinfachArne/Somfy_Remote
https://github.com/LSatan/RCSwitch-CC1101-Driver-Lib

### Necessary Hardware:

* a NodeMCU board or similar, based on ESP8266 microcontroller
* a CC1101 Transmitter board

### My build

I desing my own PCB, see /pcb
You can also use EasyEda with my project : https://easyeda.com/n.morreale/somfy-esp8266

BOM :
- WEMOS D1 Mini Pro : https://www.aliexpress.com/item/32803725174.html?spm=a2g0s.9042311.0.0.53954c4dPuQzfj
- CC1101-433MHz wireless module : https://www.aliexpress.com/item/32260182854.html?spm=a2g0s.9042311.0.0.53954c4dPuQzfj
- RF Coaxial Connectors : https://www.aliexpress.com/item/32836695692.html?spm=a2g0s.9042311.0.0.53954c4dPuQzfj
- 433MHz Antenna : https://www.aliexpress.com/item/32981671438.html?spm=a2g0s.9042311.0.0.53954c4dPuQzfj
- din rial case : https://www.aliexpress.com/item/32917979071.html?spm=a2g0s.9042311.0.0.53954c4dPuQzfj

### Compile

Open the Arduino IDE and open the project .ino file (`Somfy_MQTT.ino`)
Upload to your Hardware

### Uploading files to SPIFFS (ESP8266 internal filesystem)

*ESP8266FS* is a tool which integrates into the Arduino IDE. It adds a
menu item to *Tools* menu for uploading the contents of sketch data
directory into ESP8266 flash file system.

-  Download the [SPIFFS tool](https://github.com/esp8266/arduino-esp8266fs-plugin/releases/download/0.3.0/ESP8266FS-0.3.0.zip)
-  In your Arduino sketchbook directory, create `tools` directory if
   it doesn't exist yet
-  Unpack the tool into `tools` directory (the path will look like
   `$HOME/Arduino/tools/ESP8266FS/tool/esp8266fs.jar`)
-  Restart Arduino IDE
-  Open the Somfy_MQTT sketch
-  Make sure you have selected a board, port, and closed Serial Monitor
-  Select Tools > ESP8266 Sketch Data Upload. This should start
   uploading the files into ESP8266 flash file system. When done, IDE
   status bar will display `SPIFFS Image Uploaded` message.

### already Compiled firmware
in /bin
to flash it if connected to COM3 : 
python esptool.py -b 115200 --port COM3 write_flash --flash_freq 80m 0x000000 flash_4M.bin

#### Tested on Wemos D1 miniPro
* Board: "NodeMCU 1.0 (ESP-12E Module)"
* Flash Size: "4M (1M SPIFFS)"
* lwIP variant: "v2 Lower Memory"
* CPU Frequency: "80 MHz"
* Upload speed: "115200"

### Setup instructions

The configuration of the Somfy Dongle is stored in the EEPROM memory of the ESP8266. On first initialisation, when no configuration is found, it is initialized with some default values and the Dongle turns on the Admin-Mode.

In Admin-Mode, the blue LED on the ESP submodule is turned on and the Dongle creates an WLAN-Access-Point with the SSID `Somfy-Dongle`, protectet with the WPA-Passwort `12345678`. Now you have 180 seconds (3 minutes) time to connect to the WLAN Accesspoint and visit the configuration webserver on

http://192.168.4.1

Admin-Mode quits after the 180 second timeout or when you restart the Dongle from the configuration webserver.

If you need the Admin-Mode later, just press the "Reset" button on the NodeMCU module two times within 10 seconds. This double-reset will be detected and the Dongle enters Admin-Mode again, showing this with the blue LED turned on.

The running Somfy Dongle does some debug output on the serial console.
Console Speed is 115200 Bit/s

To program a new shutter :
- Long-press the program button of your actual remote until your blind goes up and down slightly
- Send 'PROGRAM' to the simulated remote
The rolling code value is stored in the EEPROM, so that you don't loose count of your rolling code after a reset.

### Known issues

I'm sure many to come...

### Contribute

You can contribute as much as you can.
