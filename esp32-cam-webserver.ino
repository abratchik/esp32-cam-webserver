
#include "src/app_config.h"     // global definitions
#include "src/storage.h"        // Filesystem
#include "src/app_conn.h"       // Conectivity 
#include "src/app_cam.h"        // Camera 
#include "src/app_httpd.h"      // Web server
#include "src/camera_pins.h"    // Pin Mappings

#ifdef ENABLE_MAIL_FEATURE
#include "src/app_mail.h"      // Mail client
#endif

#include <esp_log.h>
#include <esp_timer.h>
#include <Preferences.h>


/* 
 * This sketch is a extension/expansion/rework of the ESP32 Camera webserer example.
 * 
 */

const char* TAG = "ino";

Preferences preferences;
uint8_t boot_error = BootError::NO_ERROR;
uint8_t reboot_attempts = 0;

unsigned long connect_ms = 0;
unsigned long connect_timeout = 0;

void loadNVS(){
    preferences.begin(SYSTEM_PREF_NS, true);
    boot_error = preferences.getUChar(SYSTEM_BOOT_ERROR, 0);
    reboot_attempts = preferences.getUChar(SYSTEM_REBOOT_ATTEMPTS, 0);
    preferences.end();
}

void saveNVS() {
    preferences.begin(SYSTEM_PREF_NS, false);
    preferences.putUChar(SYSTEM_BOOT_ERROR, boot_error);
    preferences.putUChar(SYSTEM_REBOOT_ATTEMPTS, reboot_attempts);
    preferences.end();
}

void recordError(uint8_t error) {
    if(boot_error != error || error == 0)
        reboot_attempts == 0;
    else
        reboot_attempts++;
    boot_error = error;
    flashLED(LED_ON_mS, LED_OFF_mS, error);
    saveNVS();
}

void onNetworkFailure() {
    ESP_LOGE(TAG,"Failed to initiate WiFi.");
    recordError(NETWORK_FAILURE);
    if(reboot_attempts < 3) {
        ESP_LOGI(TAG, "Snooze and retry in 1 hour, attempt %d", reboot_attempts);
        hibernate(TIME_PERIODS[HOUR]);
    }
    else {
        ESP_LOGI(TAG, "Hibernate as no WiFi connection");
        hibernate();
    }
}

/// @brief tries to initialize the filesystem. Makes 30 attempts, 1 second 
/// @return true if success or false otherwise
bool filesystemStart(){

  ESP_LOGI(TAG, "Starting filesystem");
  uint8_t attempts = 30; 
  while ( !Storage.init()) {
    // if we sit in this loop something is wrong;
    ESP_LOGE(TAG, "Filesystem mount failed");
    flashLED(LED_ON_mS, LED_OFF_mS, FILESYSTEM_FAILURE);
    delay(1000);
    attempts--;
    if(attempts == 0) return false;
    ESP_LOGI(TAG,"Retrying...");
  }
#if (CONFIG_LOG_DEFAULT_LEVEL >= CORE_DEBUG_LEVEL )
  Storage.listDir("/", 0);
#endif
  return true;
}

/// @brief tries to initialize the wifi. Makes 20 attempts to init, 5 seconds interval.
/// @return true if success or false otherwise
bool networkStart() {
    uint8_t attempts = 2;
    while (AppConn.wifiStatus() != WL_CONNECTED)  {
        if(AppConn.start() == WL_CONNECTED) {
            break;
        }
        AppConn.stop();
        attempts--;
        ESP_LOGI(TAG, "%d attempts to connect remaining", attempts);
        if(!attempts) return false;
    }
    connect_ms = millis();
    connect_timeout = AppConn.getAPTimeout() * 1000UL;
    notifyConnect();
    return true;
}

void setup() {
    Serial.begin(115200);
    Serial.setDebugOutput(true);

    #if defined(LED_PIN)  // If we have a notification LED, set it to output
    pinMode(LED_PIN, OUTPUT);
    #endif

    loadNVS();

    ESP_LOGI(TAG, "Last boot error=%d, attempt=%d", boot_error, reboot_attempts);

    // Warn if no PSRAM is detected (typically user error with board selection in the IDE)
    if(!psramFound()){
        ESP_LOGE(TAG, "No PSRAM found, please check the board config. Fatal error, halting ... ");
        recordError(PSRAM_INIT_FAILURE);
        hibernate();
    }

    // Start the filesystem 
    if(!filesystemStart()) {
        ESP_LOGE(TAG, "Unable to mount the filesystem. Fatal error, halting ... ");
        recordError(FILESYSTEM_FAILURE);
        hibernate();
    }

    delay(200); // a short delay to let spi bus settle after init

    // Start (init) the camera 
    if (AppCam.start() != OK) {
        delay(100);  // need a delay here or the next serial o/p gets missed
        ESP_LOGE(TAG,"Critical camera failure: %s", AppCam.getErr()); 
        recordError(CAMERA_FAILURE);
        resetI2CBus();
        if(reboot_attempts < 3) {
            ESP_LOGI(TAG, "Snooze and retry in 20 seconds, attempt %d", reboot_attempts);
            hibernate(20);
        }
        else {
            hibernate();
        }
    }
    else {
        ESP_LOGI(TAG,"Camera init succeeded");
    }

    // Now load and apply preferences
    delay(200); // a short delay to let spi bus settle after camera init
    AppCam.loadPrefs();

    /*
    * Camera setup complete; initialise the rest of the hardware.
    */

    // Start Wifi and loop until we are connected or have started an AccessPoint
    if(!networkStart()) {
        onNetworkFailure();
    }

    #ifdef ENABLE_MAIL_FEATURE
    // Start the mail client if enabled
    AppMailSender.start();
    #endif

    // Start the web server
    AppHttpd.start();

    recordError(NO_ERROR);

}

void loop() {
    
    if (AppConn.isAccessPoint()) {
        unsigned long pingwifi = millis();
        while (millis() - pingwifi < WIFI_WATCHDOG ) {
            AppConn.handleOTA();
            handleSerial();
            AppConn.handleDNSRequest();
        }
        AppHttpd.cleanupWsClients();
        // check AP timeout in case if not configured as AP (network fallback)
        if(!AppConn.isLoadAsAp() && connect_timeout && (millis() - connect_ms > connect_timeout) ) {
            onNetworkFailure();
        }
    } else {
        // client mode can fail; so reconnect as appropriate

        if (AppConn.wifiStatus() == WL_CONNECTED) {
            // We are connected to WiFi

            if(!AppConn.isNTPSyncDone()) {
                AppConn.configNTP();
                Serial.print("Time: ");
                AppConn.printLocalTime(true);
            }

            // loop here to process events
            connect_ms = millis();
            while (millis() - connect_ms < WIFI_WATCHDOG ) {
                AppConn.handleOTA();
                handleSerial();
            #ifdef ENABLE_MAIL_FEATURE
                // snap image if camera is ready and mail it if configured  
                if(AppMailSender.isPendingSnap() && AppCam.getLastErr() == 0 && AppConn.isNTPSyncDone()) {
                    if(AppMailSender.mailImage() != OK) {
                        // if mailImage fails it means something wrong with the camera, need reboot
                        recordError(CAMERA_FAILURE);
                        scheduleReboot(3);
                    }
                }
                AppMailSender.process();
            #endif
            }
            AppHttpd.cleanupWsClients();

        } else {
            // disconnected; notify 
            notifyDisconnect();

            // ensures disconnect is complete, wifi scan cleared
            AppConn.stop();  

            //attempt to reconnect
            if(AppConn.start() == WL_CONNECTED) {
                notifyConnect();
            }
            
        }
    }
}

// Serial input 
void handleSerial() {
    if(Serial.available()) {
        char cmd = Serial.read();

        // Receiving commands and data from serial. Any input, which doesnt start from '#' is ignored.
        if (cmd == '#' ) {
            String rsp = Serial.readStringUntil('\n');
            rsp.trim();
            if(rsp == "M") {
             #ifdef ENABLE_MAIL_FEATURE
                if(!AppCam.getLastErr() && 
                    AppConn.wifiStatus() == WL_CONNECTED && 
                   !AppConn.isAccessPoint() && 
                    AppConn.isNTPSyncDone()) {

                    if(AppMailSender.mailImage() != OK) {
                        ESP_LOGE(TAG, "Failure to make a snapshot, check camera");
                    }

                }
                else {
                    ESP_LOGE(TAG, "Unable to snap2 & mail, check camera, WiFi or NTP settings");
                }
            #else
                ESP_LOGW(TAG, "Mail component is not enabled.");
            #endif
            }
            else {
                snprintf(AppHttpd.getSerialBuffer(), SERIAL_BUFFER_SIZE, rsp.c_str());
            }
        }
    }
}

void notifyConnect() {
    AppHttpd.serialSendCommand("Connected");
}

void notifyDisconnect() {
    AppHttpd.serialSendCommand("Disconnected");
}




