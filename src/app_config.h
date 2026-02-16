#ifndef app_config_h
#define app_config_h

/* Give the camera a name for the web interface */
#define CAM_NAME "ESP32 CAM Web Server"

/* Base application version */
#define BASE_VERSION "6.0"

#include <esp_log.h>

/* Extended WiFi Settings */

/*
 * Wifi Watchdog defines how long we spend waiting for a connection before retrying,
 * and how often we check to see if we are still connected, milliseconds
 * You may wish to increase this if your WiFi is slow at connecting.
 */
#define WIFI_WATCHDOG 15000

/*
 * Additional Features
 *
 */

/*
* Filesystem choice
* These settings allow to choose between the different file systems. If neither of these is 
* defined then the filesystem will default to SD flash card.
*/ 

// #define ARDUINO_SPIFFS
// #define ARDUINO_LITTLEFS /* currently not supported for ESP32-DEV and Platformio */ 

/*
 * Camera Hardware Selection
 *
 * You must uncomment one, and only one, of the lines below to select your board model.
 * Remember to also select the board in the Boards Manager
 * This is not optional
 */

// #define CAMERA_MODEL_AI_THINKER       // default
// #define CAMERA_MODEL_WROVER_KIT
// #define CAMERA_MODEL_ESP_EYE
// #define CAMERA_MODEL_M5STACK_PSRAM
// #define CAMERA_MODEL_M5STACK_V2_PSRAM
// #define CAMERA_MODEL_M5STACK_WIDE
// #define CAMERA_MODEL_M5STACK_ESP32CAM   // Originally: CAMERA_MODEL_M5STACK_NO_PSRAM
// #define CAMERA_MODEL_TTGO_T_JOURNAL
// #define CAMERA_MODEL_ARDUCAM_ESP32S_UNO
// #define CAMERA_MODEL_LILYGO_T_SIMCAM

/*
 * Mail image feature
 *
 * Uncomment the lines below to enabe snap & mail functionality
 * This feature requires additional configuration.
 */

// #define ENABLE_MAIL_FEATURE 


/*
* Commands to an external device over UART
*/
// #define ENABLE_SERIAL_COMMANDS


#endif
