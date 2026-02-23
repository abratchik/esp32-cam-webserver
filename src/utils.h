#ifndef APP_UTILS
#define APP_UTILS

#include <Arduino.h>
#include <esp_task_wdt.h>
#include <esp_int_wdt.h>
#include <soc/periph_defs.h>
#include <driver/periph_ctrl.h>
#include "camera_pins.h"


#define uS_TO_S_FACTOR 1000000ULL /* Conversion factor for micro seconds to seconds */
#define LED_ON_mS 100
#define LED_OFF_mS 100


enum TimePeriod : uint8_t {
    NONE=0,
    MINUTE=1,
    HOUR=2,
    DAY=3,
    WEEK=4
};

const uint32_t TIME_PERIODS[] = {0, 60, 3600, 86400, 604800};

extern void parseBytes(const char* str, char sep, byte* bytes, int maxBytes, int base);

extern void hibernate(uint32_t seconds_till_wakeup = TIME_PERIODS[WEEK]);

extern void scheduleReboot(uint32_t delay);

extern void resetI2CBus();

extern void flashLED(uint8_t flashtime = LED_ON_mS, uint8_t pause = LED_OFF_mS, uint8_t count = 1); 

#endif