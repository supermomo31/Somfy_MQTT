/*  Controlling Somfy RTS Stores via ESP8266 and CC1101.
     All the code is from Jarolift_MQTT
     And
     Somfy_Remote
*/
/*  Controlling Jarolift TDEF 433MHZ radio shutters via ESP8266 and CC1101 Transceiver Module in asynchronous mode.
    Copyright (C) 2017-2018 Steffen Hille et al.

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include <ESP8266WiFi.h>
#include <WiFiClient.h>
#include <EEPROM.h>
#include <ESP8266WebServer.h>
#include <SPI.h>
#include <FS.h>
#include <PubSubClient.h>
#include <Ticker.h>
#include <DoubleResetDetector.h>
#include <simpleDSTadjust.h>
#include <coredecls.h>              // settimeofday_cb()

#include "helpers.h"
#include "global.h"
#include "html_api.h"

extern "C" {
#include "user_interface.h"
#include "Arduino.h"
#include "Somfy_Remote.h"
}

// Number of seconds after reset during which a
// subseqent reset will be considered a double reset.
#define DRD_TIMEOUT 10

// RTC Memory Address for the DoubleResetDetector to use
#define DRD_ADDRESS 0

// User configuration
uint32_t remoteCode[] = {0x131100, 0x131110, 0x131120, 0x131130, 0x131140, 0x131150, 0x131160, 0x131170, 0x131180, 0x131190, 0x131200, 0x131210, 0x131220, 0x131230, 0x131240, 0x131250}; // Defineremote code to use for each remote
byte adresses[]       = {5, 11, 17, 23, 29, 35, 41, 47, 53, 59, 65, 71, 77, 85, 91, 97 }; // Defines start addresses of channel rollingcode stored in EEPROM 4bytes.
SomfyRemote *remote[16];

boolean time_is_set_first = true;
DoubleResetDetector drd(DRD_TIMEOUT, DRD_ADDRESS);

//####################################################################
// sketch initialization routine
//####################################################################
void setup()
{
  InitLog();
  EEPROM.begin(4096);
  Serial.begin(115200);
  settimeofday_cb(time_is_set);
  updateNTP(); // Init the NTP time
  WriteLog("[INFO] - starting Somfy Dongle " + (String)PROGRAM_VERSION, true);
  WriteLog("[INFO] - ESP-ID " + (String)ESP.getChipId() + " // ESP-Core  " + ESP.getCoreVersion() + " // SDK Version " + ESP.getSdkVersion(), true);

  // callback functions for WiFi connect and disconnect
  // placed as early as possible in the setup() function to get the connect
  // message catched when the WiFi connect is really fast
  gotIpEventHandler = WiFi.onStationModeGotIP([](const WiFiEventStationModeGotIP & event)
  {
    WriteLog("[INFO] - WiFi station connected - IP: " + WiFi.localIP().toString(), true);
    wifi_disconnect_log = true;
  });

  disconnectedEventHandler = WiFi.onStationModeDisconnected([](const WiFiEventStationModeDisconnected & event)
  {
    if (wifi_disconnect_log) {
      WriteLog("[INFO] - WiFi station disconnected", true);
      // turn off logging disconnect events after first occurrence, otherwise the log is filled up
      wifi_disconnect_log = false;
    }
  });

  InitializeConfigData();

  // init SomefyRemotes
  for (int channelNum = 0; channelNum <= 15; channelNum++) {
    WriteLog((String)"[INFO] - Init SomfyRemote 1 : 0x" + String(remoteCode[channelNum],HEX) + " / " + config.channel_name[channelNum], true);
    remote[channelNum] = new SomfyRemote(config.channel_name[channelNum], remoteCode[channelNum], adresses[channelNum]);
  }
  
  pinMode(led_pin, OUTPUT);   // prepare LED on ESP-Chip

  // test if the WLAN SSID is on default
  // or DoubleReset detected
  if ((drd.detectDoubleReset()) || (config.ssid == "MYSSID")) {
    digitalWrite(led_pin, LOW);  // turn LED on                    // if yes then turn on LED
    AdminEnabled = true;                                           // and go to Admin-Mode
  } else {
    digitalWrite(led_pin, HIGH); // turn LED off                   // turn LED off
  }

  // enable access point mode if Admin-Mode is enabled
  if (AdminEnabled)
  {
    WriteLog("[WARN] - Admin-Mode enabled!", true);
    WriteLog("[WARN] - starting soft-AP ... ", false);
    wifi_disconnect_log = false;
    WiFi.mode(WIFI_AP);
    WriteLog(WiFi.softAP(ACCESS_POINT_NAME, ACCESS_POINT_PASSWORD) ? "Ready" : "Failed!", true);
    WriteLog("[WARN] - Access Point <" + (String)ACCESS_POINT_NAME + "> activated. WPA password is " + ACCESS_POINT_PASSWORD, true);
    WriteLog("[WARN] - you have " + (String)AdminTimeOut + " seconds time to connect and configure!", true);
    WriteLog("[WARN] - configuration webserver is http://" + WiFi.softAPIP().toString(), true);
  }
  else
  {
    // establish Wifi connection in station mode
    ConfigureWifi();
  }

  // configure webserver and start it
  server.on ( "/api", html_api );                       // command api
  SPIFFS.begin();                                       // Start the SPI flash filesystem
  server.onNotFound([]() {                              // If the client requests any URI
    if (!handleFileRead(server.uri())) {                // send it if it exists
      server.send(404, "text/plain", "404: Not Found"); // otherwise, respond with a 404 (Not Found) error
      Serial.println(" File not found: did you upload the data directory?");
    }
  });

  server.begin();
  WriteLog("[INFO] - HTTP server started", true);
  tkHeartBeat.attach(1, HeartBeat);

  // configure MQTT client
  mqtt_client.setServer(IPAddress(config.mqtt_broker_addr[0], config.mqtt_broker_addr[1],
                                  config.mqtt_broker_addr[2], config.mqtt_broker_addr[3]),
                        config.mqtt_broker_port.toInt());
  mqtt_client.setCallback(mqtt_callback);   // define Handler for incoming messages
  mqttLastConnectAttempt = 0;

} // void setup

//####################################################################
// main loop
//####################################################################
void loop()
{

  // Call the double reset detector loop method every so often,
  // so that it can recognise when the timeout expires.
  // You can also call drd.stop() when you wish to no longer
  // consider the next reset as a double reset.
  drd.loop();

  // disable Admin-Mode after AdminTimeOut
  if (AdminEnabled)
  {
    if (AdminTimeOutCounter > AdminTimeOut / HEART_BEAT_CYCLE)
    {
      AdminEnabled = false;
      digitalWrite(led_pin, HIGH);   // turn LED off
      WriteLog("[WARN] - Admin-Mode disabled, soft-AP terminate ...", false);
      WriteLog(WiFi.softAPdisconnect(true) ? "success" : "fail!", true);
      ConfigureWifi();
    }
  }
  server.handleClient();

  // If you do not use a MQTT broker so configure the address 0.0.0.0
  if (config.mqtt_broker_addr[0] + config.mqtt_broker_addr[1] + config.mqtt_broker_addr[2] + config.mqtt_broker_addr[3]) {
    // establish connection to MQTT broker
    if (WiFi.status() == WL_CONNECTED) {
      if (!mqtt_client.connected()) {
        // calculate time since last connection attempt
        long now = millis();
        // possible values of mqttLastReconnectAttempt:
        // 0  => never attempted to connect
        // >0 => at least one connect attempt was made
        if ((mqttLastConnectAttempt == 0) || (now - mqttLastConnectAttempt > MQTT_Reconnect_Interval)) {
          mqttLastConnectAttempt = now;
          // attempt to connect
          mqtt_connect();
        }
      } else {
        // client is connected, call the mqtt loop
        mqtt_client.loop();
      }
    }
  }

  // run a CMD whenever a web_cmd event has been triggered
  if (web_cmd != "") {
    if (web_cmd == "up") {
      cmd_up(web_cmd_channel);
    } else if (web_cmd == "down") {
      cmd_down(web_cmd_channel);
    }  else if (web_cmd == "prog") {
      cmd_prog(web_cmd_channel);
    } else if (web_cmd == "stop") {
      cmd_stop(web_cmd_channel);
    } else if (web_cmd == "save") {
      Serial.println("main loop: in web_cmd save");
      cmd_save_config();
    } else if (web_cmd == "restart") {
      Serial.println("main loop: in web_cmd restart");
      cmd_restart();
    } else {
      WriteLog("[ERR ] - received unknown command from web_cmd.", true);
    }
    web_cmd = "";
  }
} // void loop



//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
// Webserver functions group
//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++

// void html_api() -> see html_api.h

//####################################################################
// convert the file extension to the MIME type
//####################################################################
String getContentType(String filename) {
  if (filename.endsWith(".html")) return "text/html";
  else if (filename.endsWith(".css")) return "text/css";
  else if (filename.endsWith(".js")) return "application/javascript";
  else if (filename.endsWith(".ico")) return "image/x-icon";
  return "text/plain";
} // String getContentType

//####################################################################
// send the right file to the client (if it exists)
//####################################################################
bool handleFileRead(String path) {
  if (debug_webui) Serial.println("handleFileRead: " + path);
  if (path.endsWith("/")) path += "index.html";         // If a folder is requested, send the index file
  String contentType = getContentType(path);            // Get the MIME type
  if (SPIFFS.exists(path)) {                            // If the file exists
    File file = SPIFFS.open(path, "r");                 // Open it
    size_t sent = server.streamFile(file, contentType); // And send it to the client
    file.close();                                       // Then close the file again
    return true;
  }
  if (debug_webui) Serial.println("\tFile Not Found");
  return false;                                         // If the file doesn't exist, return false
} // bool handleFileRead


//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
// MQTT functions group
//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++

//####################################################################
// Callback for incoming MQTT messages
//####################################################################
void mqtt_callback(char* topic, byte* payload, unsigned int length) {

  if (debug_mqtt) {
    Serial.printf("mqtt in: %s - ", topic);
    for (int i = 0; i < length; i++) {
      Serial.print((char)payload[i]);
    }
    Serial.println();
  }

  // extract channel id from topic name
  int channel = 999;
  char * token = strtok(topic, "/");  // initialize token
  token = strtok(NULL, "/");          // now token = 2nd token
  token = strtok(NULL, "/");          // now token = 3rd token, "shutter" or so
  if (debug_mqtt) Serial.printf("command token: %s\n", token);
  if (strncmp(token, "shutter", 7) == 0) {
    token = strtok(NULL, "/");
    if (token != NULL) {
      channel = atoi(token);
    }
  } else if (strncmp(token, "sendconfig", 10) == 0) {
    WriteLog("[INFO] - incoming MQTT command: sendconfig", true);
    mqtt_send_config();
    return;
  } else {
    WriteLog("[ERR ] - incoming MQTT command unknown: " + (String)token, true);
    return;
  }

  // convert payload in string
  payload[length] = '\0';
  String cmd = String((char*)payload);

  // print serial message
  WriteLog("[INFO] - incoming MQTT command: channel " + (String) channel + ":", false);
  WriteLog(cmd, true);

  if (channel <= 15) {
    if (cmd == "UP" || cmd == "0") {
      cmd_up(channel);
    } else if (cmd == "DOWN"  || cmd == "100") {
      cmd_down(channel);
    } else if (cmd == "PROG") {
      cmd_prog(channel);
    } else if (cmd == "STOP") {
      cmd_stop(channel);
    } else {
      WriteLog("[ERR ] - incoming MQTT payload unknown.", true);
    }
  } else {
    WriteLog("[ERR ] - invalid channel, choose one of 0-15", true);
  }
} // void mqtt_callback

//####################################################################
// send status via mqtt
//####################################################################
void mqtt_send_percent_closed_state(int channelNum, int percent, String command) {
  if (percent > 100) percent = 100;
  if (percent < 0) percent = 0;
  if (mqtt_client.connected()) {
    char percentstr[4];
    itoa(percent, percentstr, 10);
    String Topic = "stat/" + config.mqtt_devicetopic + "/shutter/" + (String)channelNum;
    const char * msg = Topic.c_str();
    mqtt_client.publish(msg, percentstr);
  }
  WriteLog("[INFO] - command " + command + " for channel " + (String)channelNum + " (" + config.channel_name[channelNum] + ") sent.", true);
} // void mqtt_send_percent_closed_state

//####################################################################
// send config via mqtt
//####################################################################
void mqtt_send_config() {
  String Payload;
  int configCnt = 0, lineCnt = 0;
  char numBuffer[25];

  if (mqtt_client.connected()) {

    // send config of the shutter channels
    for (int channelNum = 0; channelNum <= 15; channelNum++) {
      if (config.channel_name[channelNum] != "") {
        if (lineCnt == 0) {
          Payload = "{\"channel\":[";
        } else {
          Payload += ", ";
        }

        Payload += "{\"id\":" + String(channelNum) + ", \"name\":\"" + config.channel_name[channelNum] + "\", "
                   + "\"serial\":\"" + numBuffer +  "\"}";
        lineCnt++;

        if (lineCnt >= 4) {
          Payload += "]}";
          mqtt_send_config_line(configCnt, Payload);
          lineCnt = 0;
        }
      } // if (config.channel_name[channelNum] != "")
    } // for

    // handle last item
    if (lineCnt > 0) {
      Payload += "]}";
      mqtt_send_config_line(configCnt, Payload);
    }

    // send most important other config info
    snprintf(numBuffer, 15, "%d", devcnt);
    Payload = "{\"mqtt-clientid\":\"" + config.mqtt_broker_client_id + "\", "
              + "\"mqtt-devicetopic\":\"" + config.mqtt_devicetopic + "\", "
              + "\"devicecounter\":" + (String)numBuffer + "}";
    mqtt_send_config_line(configCnt, Payload);
  } // if (mqtt_client.connected())
} // void mqtt_send_config

//####################################################################
// send one config telegram via mqtt
//####################################################################
void mqtt_send_config_line(int & counter, String Payload) {
  String Topic = "stat/" + config.mqtt_devicetopic + "/config/" + (String)counter;
  if (debug_mqtt) Serial.println("mqtt send: " + Topic + " - " + Payload);
  mqtt_client.publish(Topic.c_str(), Payload.c_str());
  counter++;
  yield();
} // void mqtt_send_config_line


//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
// execute cmd_ functions group
//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++

//####################################################################
// function to move the shutter up
//####################################################################
void cmd_up(int channel) {
  remote[channel]->move("UP");
  mqtt_send_percent_closed_state(channel, 0, "UP");
} // void cmd_up

//####################################################################
// function to move the shutter down
//####################################################################
void cmd_down(int channel) {
  remote[channel]->move("DOWN");
  mqtt_send_percent_closed_state(channel, 100, "DOWN");
} // void cmd_down

//####################################################################
// function to program the shutter
//####################################################################
void cmd_prog(int channel) {
  remote[channel]->move("PROGRAM");
  WriteLog("[INFO] - command PROG for channel " + (String)channel + " (" + config.channel_name[channel] + ") sent.", true);
} // void cmd_prog

//####################################################################
// function to stop the shutter
//####################################################################
void cmd_stop(int channel) {
  remote[channel]->move("MY");
  WriteLog("[INFO] - command STOP for channel " + (String)channel + " (" + config.channel_name[channel] + ") sent.", true);
} // void cmd_stop

//####################################################################
// webUI save config function
//####################################################################
void cmd_save_config() {
  WriteLog("[CFG ] - save config initiated from WebUI", true);
  // check if mqtt_devicetopic was changed
  if (config.mqtt_devicetopic_new != config.mqtt_devicetopic) {
    // in case the devicetopic has changed, the LWT state with the old devicetopic should go away
    WriteLog("[CFG ] - devicetopic changed, gracefully disconnect from mqtt server", true);
    // first we send an empty message that overwrites the retained "Online" message
    String topicOld = "tele/" + config.mqtt_devicetopic + "/LWT";
    mqtt_client.publish(topicOld.c_str(), "", true);
    // next: remove retained "devicecounter" message
    topicOld = "stat/" + config.mqtt_devicetopic + "/devicecounter";
    mqtt_client.publish(topicOld.c_str(), "", true);
    delay(200);
    // finally we disconnect gracefully from the mqtt broker so the stored LWT "Offline" message is discarded
    mqtt_client.disconnect();
    config.mqtt_devicetopic = config.mqtt_devicetopic_new;
    delay(200);
  }
  WriteConfig();
  server.send ( 200, "text/plain", "Configuration has been saved, system is restarting. Please refresh manually in about 30 seconds.." );
  cmd_restart();
} // void cmd_save_config

//####################################################################
// webUI restart function
//####################################################################
void cmd_restart() {
  server.send ( 200, "text/plain", "System is restarting. Please refresh manually in about 30 seconds." );
  delay(500);
  wifi_disconnect_log = false;
  ESP.restart();
} // void cmd_restart

//####################################################################
// connect function for MQTT broker
// called from the main loop
//####################################################################
boolean mqtt_connect() {
  const char* client_id = config.mqtt_broker_client_id.c_str();
  const char* username = config.mqtt_broker_username.c_str();
  const char* password = config.mqtt_broker_password.c_str();
  String willTopic = "tele/" + config.mqtt_devicetopic + "/LWT"; // connect with included "Last-Will-and-Testament" message
  uint8_t willQos = 0;
  boolean willRetain = true;
  const char* willMessage = "Offline";           // LWT message says "Offline"
  String subscribeString = "cmd/" + config.mqtt_devicetopic + "/#";

  WriteLog("[INFO] - trying to connect to MQTT broker", true);
  // try to connect to MQTT
  if (mqtt_client.connect(client_id, username, password, willTopic.c_str(), willQos, willRetain, willMessage )) {
    WriteLog("[INFO] - MQTT connect success", true);
    // subscribe the needed topics
    mqtt_client.subscribe(subscribeString.c_str());
    // publish telemetry message "we are online now"
    mqtt_client.publish(willTopic.c_str(), "Online", true);
  } else {
    WriteLog("[ERR ] - MQTT connect failed, rc =" + (String)mqtt_client.state(), true);
  }
  return mqtt_client.connected();
} // boolean mqtt_connect

//####################################################################
// NTP update
//####################################################################
void updateNTP() {
  configTime(TIMEZONE * 3600, 0, NTP_SERVERS);
} // void updateNTP

//####################################################################
// callback function when time is set via SNTP
//####################################################################
void time_is_set(void) {
  if (time_is_set_first) {    // call WriteLog only once for the initial time set
    time_is_set_first = false;
    WriteLog("[INFO] - time set from NTP server", true);
  }
} // void time_is_set
