/*
  user_config_override.h - user configuration overrides my_user_config.h for Tasmota

  Copyright (C) 2021  Theo Arends

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

#ifndef _USER_CONFIG_OVERRIDE_H_
#define _USER_CONFIG_OVERRIDE_H_

/*****************************************************************************************************\
 * USAGE:
 *   To modify the stock configuration without changing the my_user_config.h file:
 *   (1) copy this file to "user_config_override.h" (It will be ignored by Git)
 *   (2) define your own settings below
 *
 ******************************************************************************************************
 * ATTENTION:
 *   - Changes to SECTION1 PARAMETER defines will only override flash settings if you change define CFG_HOLDER.
 *   - Expect compiler warnings when no ifdef/undef/endif sequence is used.
 *   - You still need to update my_user_config.h for major define USE_MQTT_TLS.
 *   - All parameters can be persistent changed online using commands via MQTT, WebConsole or Serial.
\*****************************************************************************************************/

// -- Project -------------------------------------

// If not selected the default will be Sonoff Mini Template
#undef MODULE
#define MODULE USER_MODULE // [Module] Select default module from tasmota_template.h

#undef FALLBACK_MODULE

#ifdef ESP8266
#define FALLBACK_MODULE USER_MODULE                                                                                // [Module2] Select default module on fast reboot where USER_MODULE is user template
#define USER_TEMPLATE "{\"NAME\":\"Sonoff Mini\",\"GPIO\":[17,0,0,0,9,0,0,0,21,56,0,0,255],\"FLAG\":0,\"BASE\":1}" // [Template] Set JSON template
#endif                                                                                                             // ESP8266

#ifdef ESP32
#define FALLBACK_MODULE WEMOS                                                                                                                                // [Module2] Select default module on fast reboot where USER_MODULE is user template
#define USER_TEMPLATE "{\"NAME\":\"ESP32-DevKit\",\"GPIO\":[1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,0,1,1,1,0,1,1,1,0,0,0,0,1,1,1,1,1,0,0,1],\"FLAG\":0,\"BASE\":1}" // [Template] Set JSON template
#endif                                                                                                                                                       // ESP32

// -- Wi-Fi ---------------------------------------
#undef STA_SSID1
#define STA_SSID1 "SMUDGE_SECRET_STA_SSID1" // [Ssid1] Wifi SSID

#undef STA_PASS1
#define STA_PASS1 "SMUDGE_SECRET_STA_PASS1" // [Password1] Wifi password

// -- Ota -----------------------------------------
#undef OTA_URL
#ifdef ESP8266
#define OTA_URL "http://SMUDGE_SECRET_FTP_SERVER/SRGDFiles/firmware/tasmota.bin.gz" // [OtaUrl]
#endif                                                                                  // ESP8266
#ifdef ESP32
#define OTA_URL "http://SMUDGE_SECRET_FTP_SERVER/SRGDFiles/firmware/tasmota32.bin" // [OtaUrl]
#endif                                                                                 // ESP32

// -- MQTT ----------------------------------------
#undef MQTT_HOST
#define MQTT_HOST "SMUDGE_SECRET_MQTT_HOST" // [MqttHost]

#undef MQTT_PORT
#define MQTT_PORT SMUDGE_SECRET_MQTT_PORT // [MqttPort] MQTT port (10123 on CloudMQTT)

#undef MQTT_USER
#define MQTT_USER "SMUDGE_SECRET_MQTT_USER" // [MqttUser] Optional user

#undef MQTT_PASS
#define MQTT_PASS "SMUDGE_SECRET_MQTT_PASS" // [MqttPassword] Optional password

// -- Time - Start Daylight Saving Time and timezone offset from UTC in minutes
#undef TIME_DST_HEMISPHERE
#define TIME_DST_HEMISPHERE North // [TimeDst] Hemisphere (0 or North, 1 or South)
#undef TIME_DST_WEEK
#define TIME_DST_WEEK Second // Week of month (0 or Last, 1 or First, 2 or Second, 3 or Third, 4 or Fourth)
#undef TIME_DST_DAY
#define TIME_DST_DAY Sun // Day of week (1 or Sun, 2 or Mon, 3 or Tue, 4 or Wed, 5 or Thu, 6 or Fri, 7 or Sat)
#undef TIME_DST_MONTH
#define TIME_DST_MONTH Mar // Month (1 or Jan, 2 or Feb, 3 or Mar, 4 or Apr, 5 or May, 6 or Jun, 7 or Jul, 8 or Aug, 9 or Sep, 10 or Oct, 11 or Nov, 12 or Dec)
#undef TIME_DST_HOUR
#define TIME_DST_HOUR 2 // Hour (0 to 23)
#undef TIME_DST_OFFSET
#define TIME_DST_OFFSET -240 // Offset from UTC in minutes (-780 to +780)

// -- Time - Start Standard Time and timezone offset from UTC in minutes
#undef TIME_STD_HEMISPHERE
#define TIME_STD_HEMISPHERE North // [TimeStd] Hemisphere (0 or North, 1 or South)
#undef TIME_STD_WEEK
#define TIME_STD_WEEK First // Week of month (0 or Last, 1 or First, 2 or Second, 3 or Third, 4 or Fourth)
#undef TIME_STD_DAY
#define TIME_STD_DAY Sun // Day of week (1 or Sun, 2 or Mon, 3 or Tue, 4 or Wed, 5 or Thu, 6 or Fri, 7 or Sat)
#undef TIME_STD_MONTH
#define TIME_STD_MONTH Nov // Month (1 or Jan, 2 or Feb, 3 or Mar, 4 or Apr, 5 or May, 6 or Jun, 7 or Jul, 8 or Aug, 9 or Sep, 10 or Oct, 11 or Nov, 12 or Dec)
#undef TIME_STD_HOUR
#define TIME_STD_HOUR 3 // Hour (0 to 23)
#undef TIME_STD_OFFSET
#define TIME_STD_OFFSET -300 // Offset from UTC in minutes (-780 to +780)

// -- Location ------------------------------------
#undef LATITUDE
#define LATITUDE SMUDGE_SECRET_LATITUDE // [Latitude] Your location to be used with sunrise and sunset
#undef LONGITUDE
#define LONGITUDE SMUDGE_SECRET_LONGITUDE // [Longitude] Your location to be used with sunrise and sunset

#undef APP_TIMEZONE
#define APP_TIMEZONE 99 // [Timezone] +1 hour (Amsterdam) (-13 .. 14 = hours from UTC, 99 = use TIME_DST/TIME_STD)

// -- Application ---------------------------------

#undef KEY_DEBOUNCE_TIME
#define KEY_DEBOUNCE_TIME 50 // [ButtonDebounce] Number of mSeconds button press debounce time
#undef KEY_HOLD_TIME
#define KEY_HOLD_TIME 6 // [SetOption32] Number of 0.1 seconds to hold Button or external Pushbutton before sending HOLD message
#undef KEY_DISABLE_MULTIPRESS
#define KEY_DISABLE_MULTIPRESS true // [SetOption1]  Disable button multipress (SRGD: disable reset!)
#undef TEMP_CONVERSION
#define TEMP_CONVERSION true // [SetOption8] Return temperature in (false = Celsius or true = Fahrenheit)

// -- MQTT - Options ------------------------------
#undef MQTT_GRPTOPIC

#ifdef ESP8266
#define MQTT_GRPTOPIC "tasmotas" // [GroupTopic] MQTT Group topic
// #define MQTT_GRPTOPIC "tasmesh" // [GroupTopic] MQTT Group topic
#endif                           // ESP8266

#ifdef ESP32
#define MQTT_GRPTOPIC "tasmotas" // [GroupTopic] MQTT Group topic
// #define MQTT_GRPTOPIC "tasmotas32" // [GroupTopic] MQTT Group topic
#endif                             // ESP32

#undef MQTT_POWER_FORMAT
#define MQTT_POWER_FORMAT true // [SetOption26] Switch between POWER or POWER1 for single power devices (SRGD: Use POWER1)
#undef MQTT_BUTTON_TOPIC
#define MQTT_BUTTON_TOPIC "0" // [ButtonTopic] MQTT button topic, "0" = same as MQTT_TOPIC, set to 'PROJECT "_BTN_%06X"' for unique topic including device MAC address
#undef MQTT_SWITCH_TOPIC
#define MQTT_SWITCH_TOPIC "0" // [SwitchTopic] MQTT button topic, "0" = same as MQTT_TOPIC, set to 'PROJECT "_SW_%06X"' for unique topic including device MAC address
#undef TELE_ON_POWER
#define TELE_ON_POWER true // [SetOption59] send tele/STATE together with stat/RESULT (false = Disable, true = Enable)
#undef MQTT_BUTTON_RETAIN
#define MQTT_BUTTON_RETAIN false // [ButtonRetain] Button may send retain flag (false = off, true = on)
#undef MQTT_POWER_RETAIN
#define MQTT_POWER_RETAIN true // [PowerRetain] Power status message may send retain flag (false = off, true = on)
#undef MQTT_SWITCH_RETAIN
#define MQTT_SWITCH_RETAIN false // [SwitchRetain] Switch may send retain flag (false = off, true = on)
#undef MQTT_SENSOR_RETAIN
#define MQTT_SENSOR_RETAIN false // [SensorRetain] Sensor may send retain flag (false = off, true = on)
#undef MQTT_INFO_RETAIN
#define MQTT_INFO_RETAIN true // [InfoRetain] Info may send retain flag (false = off, true = on)
#undef MQTT_STATE_RETAIN
#define MQTT_STATE_RETAIN true // [StateRetain] State may send retain flag (false = off, true = on)
#undef MQTT_NO_HOLD_RETAIN
#define MQTT_NO_HOLD_RETAIN false // [SetOption62] Disable retain flag on HOLD messages
#undef MQTT_NO_RETAIN
#define MQTT_NO_RETAIN false // [SetOption104] No Retain - disable all MQTT retained messages, some brokers don't support it: AWS IoT, Losant

// -- ESP-NOW -------------------------------------
#ifndef FIRMWARE_MINIMAL
#define USE_TASMESH // Enable Tasmota Mesh using ESP-NOW (+11k code)
#endif

// -- MQTT - Domoticz -----------------------------
#ifdef USE_DOMOTICZ
#undef USE_DOMOTICZ // Disable Domoticz (+6k code, +0.3k mem)
#endif

// -- MQTT - Home Assistant Discovery -------------
#ifdef USE_HOME_ASSISTANT
#undef USE_HOME_ASSISTANT // Disable Home Assistant Discovery Support (+12k code, +6 bytes mem)
#endif

// -- MQTT - Tasmota Discovery ---------------------
#ifndef FIRMWARE_MINIMAL
#define USE_TASMOTA_DISCOVERY // Enable Tasmota Discovery support (+2k code)
#endif

// -- Ping ----------------------------------------
// #ifndef FIRMWARE_MINIMAL
// #define USE_PING // Enable Ping command (+2k code)
// #endif

// -- Rules or Script  ----------------------------
#ifndef FIRMWARE_MINIMAL
// Select none or only one of the below defines USE_RULES or USE_SCRIPT
#define USE_RULES            // Add support for rules (+8k code)
// #define USE_EXPRESSION       // Add support for expression evaluation in rules (+3k2 code, +64 bytes mem)
// #define SUPPORT_IF_STATEMENT // Add support for IF statement in rules (+4k2 code, -332 bytes mem)

//  #define USER_RULE1 "<Any rule1 data>"          // Add rule1 data saved at initial firmware load or when command reset is executed
//  #define USER_RULE2 "<Any rule2 data>"          // Add rule2 data saved at initial firmware load or when command reset is executed
//  #define USER_RULE3 "<Any rule3 data>"          // Add rule3 data saved at initial firmware load or when command reset is executed

// #define USE_WEBGETCONFIG // Enable restoring config from external webserver (+0k6)

// #define USER_BACKLOG "WebGetConfig http://SMUDGE_SECRET_FTP_SERVER/SRGDFiles/configuration/config_%id%.dmp" // Add commands executed at firmware load or when command reset is executed
#endif

// -- Emulation for Alexa ---------------------
#ifdef USE_EMULATION
#undef USE_EMULATION // Disable Wemo or Hue emulation
#endif
#ifdef USE_EMULATION_HUE
#undef USE_EMULATION_HUE // Disable Hue Bridge emulation for Alexa (+14k code, +2k mem common)
#endif
#ifdef USE_EMULATION_WEMO
#undef USE_EMULATION_WEMO // Disable Belkin WeMo emulation for Alexa (+6k code, +2k mem common)
#endif

// -- KNX IP Protocol -----------------------------
#ifdef USE_KNX
#undef USE_KNX // Enable KNX IP Protocol Support (+9.4k code, +3k7 mem)
#endif
#ifdef USE_KNX_WEB_MENU
#undef USE_KNX_WEB_MENU // Enable KNX WEB MENU (+8.3k code, +144 mem)
#endif

#endif // _USER_CONFIG_OVERRIDE_H_
