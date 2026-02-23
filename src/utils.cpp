/*
 * From https://stackoverflow.com/a/35236734
*/

#include "utils.h"

void parseBytes(const char* str, char sep, byte* bytes, int maxBytes, int base) {
    for (int i = 0; i < maxBytes; i++) {
        bytes[i] = strtoul(str, NULL, base);  // Convert byte
        str = strchr(str, sep);               // Find next separator
        if (str == NULL || *str == '\0') {
            break;                            // No more separators, exit
        }
        str++;                                // Point to next character after separator
    }
}

void hibernate(uint32_t seconds_till_wakeup) {
    esp_sleep_enable_timer_wakeup(uS_TO_S_FACTOR * seconds_till_wakeup);
    esp_deep_sleep_disable_rom_logging();
    esp_deep_sleep_start();
}

void scheduleReboot(uint32_t timeout) {
    esp_task_wdt_init(timeout,true);
    esp_task_wdt_add(NULL);
    Serial.print("Restarting ");
    while(true) {
        delay(200);
        Serial.print('.');
    }
}

// Reset the I2C bus.. may help when rebooting.
void resetI2CBus() {
    periph_module_disable(PERIPH_I2C0_MODULE); // try to shut I2C down properly in case that is the problem
    periph_module_disable(PERIPH_I2C1_MODULE);
    periph_module_reset(PERIPH_I2C0_MODULE);
    periph_module_reset(PERIPH_I2C1_MODULE);
}

// Flash LED if LED pin defined
void flashLED(uint8_t flashtime, uint8_t pause, uint8_t count) {
#ifdef LED_PIN
    while(count) {
        digitalWrite(LED_PIN, LED_ON);
        delay(flashtime);
        digitalWrite(LED_PIN, LED_OFF);
        delay(pause);
        count--;
    }
#endif
}