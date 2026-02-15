
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



/* 
 * This sketch is a extension/expansion/rework of the ESP32 Camera webserer example.
 * 
 */

const char* TAG = "ino";

void setup() {
    Serial.begin(115200);
    Serial.setDebugOutput(true);

    // Warn if no PSRAM is detected (typically user error with board selection in the IDE)
    if(!psramFound()){
        ESP_LOGE(TAG, "Fatal Error; Halting");
        while (true) {
            ESP_LOGE(TAG, "No PSRAM found; camera cannot be initialised: Please check the board config for your module.");
            delay(5000);
        }
    }

    #if defined(LED_PIN)  // If we have a notification LED, set it to output
        pinMode(LED_PIN, OUTPUT);
    #endif

    // Start the filesystem before we initialise the camera
    filesystemStart();
    delay(200); // a short delay to let spi bus settle after init

    // Start (init) the camera 
    if (AppCam.start() != OK) {
        delay(100);  // need a delay here or the next serial o/p gets missed
        ESP_LOGE(TAG,"CRITICAL FAILURE:%s", AppCam.getErr()); 
        ESP_LOGE(TAG,"A full (hard, power off/on) reboot will probably be needed to recover from this.");
        ESP_LOGE(TAG,"Meanwhile; this unit will reboot in 1 minute since these errors sometime clear automatically");
        resetI2CBus();
        scheduleReboot(60);
    }
    else
        ESP_LOGI(TAG,"Camera init succeeded");

    // Now load and apply preferences
    delay(200); // a short delay to let spi bus settle after camera init
    AppCam.loadPrefs();

    /*
    * Camera setup complete; initialise the rest of the hardware.
    */

    // Start Wifi and loop until we are connected or have started an AccessPoint
    while (AppConn.wifiStatus() != WL_CONNECTED)  {
        if(AppConn.start() != WL_CONNECTED) {
            ESP_LOGW(TAG,"Failed to initiate WiFi, retrying in 5 sec ... ");
            delay(5000);
        }
        else {
            // Flash the LED to show we are connected
            notifyConnect();
        }
    }

    #ifdef ENABLE_MAIL_FEATURE
    // Start the mail client if enabled
    AppMailSender.start();
    #endif

    // Start the web server
    AppHttpd.start();

}

void loop() {
    /*
     *  Just loop forever, reconnecting Wifi As necesscary in client mode
     * The stream and URI handler processes initiated by the startCameraServer() call at the
     * end of setup() will handle the camera and UI processing from now on.
    */
    if (AppConn.isAccessPoint()) {
        // Accespoint is permanently up, so just loop, servicing the captive portal as needed
        // Rather than loop forever, follow the watchdog, in case we later add auto re-scan.
        unsigned long pingwifi = millis();
        while (millis() - pingwifi < WIFI_WATCHDOG ) {
            AppConn.handleOTA();
            handleSerial();
            AppConn.handleDNSRequest();
        }
        AppHttpd.cleanupWsClients();
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
            unsigned long pingwifi = millis();
            while (millis() - pingwifi < WIFI_WATCHDOG ) {
                AppConn.handleOTA();
                handleSerial();
            #ifdef ENABLE_MAIL_FEATURE
                        // mail image if configured, ready for  
                if(AppMailSender.isPendingSnap() && AppCam.getLastErr() == 0 && AppConn.isNTPSyncDone()) {
                    AppMailSender.mailImage();
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


/// @brief tries to initialize the filesystem until success, otherwise loops indefinitely
void filesystemStart(){

  ESP_LOGI(TAG, "Starting filesystem");
  while ( !Storage.init() ) {
    // if we sit in this loop something is wrong;
    ESP_LOGE(TAG, "Filesystem mount failed");
    for (int i=0; i<10; i++) {
      flashLED(100); // Show filesystem failure
      delay(100);
    }
    delay(1000);
    ESP_LOGI(TAG,"Retrying...");
  }
  
  Storage.listDir("/", 0);
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
                    AppMailSender.mailImage();
                }
                else {
                    ESP_LOGE(TAG, "Camera is in error state, reboot required!");
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
    for (int i = 0; i < 5; i++) {
        flashLED(150);
        delay(50);
    }
    AppHttpd.serialSendCommand("Connected");
}

void notifyDisconnect() {
    AppHttpd.serialSendCommand("Disconnected");
}

// Flash LED if LED pin defined
void flashLED(int flashtime) {
#ifdef LED_PIN
    digitalWrite(LED_PIN, LED_ON);
    delay(flashtime);
    digitalWrite(LED_PIN, LED_OFF);
#endif
}

void scheduleReboot(int delay) {
    esp_task_wdt_init(delay,true);
    esp_task_wdt_add(NULL);
}

// Reset the I2C bus.. may help when rebooting.
void resetI2CBus() {
    periph_module_disable(PERIPH_I2C0_MODULE); // try to shut I2C down properly in case that is the problem
    periph_module_disable(PERIPH_I2C1_MODULE);
    periph_module_reset(PERIPH_I2C0_MODULE);
    periph_module_reset(PERIPH_I2C1_MODULE);
}

