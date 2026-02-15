#ifndef APP_DEFINES_H
#define APP_DEFINES_H

#include <Arduino.h>

const char APP_CODE_VERSION_PARAM[] PROGMEM = "code_ver";
const char APP_BASE_VERSION_PARAM[] PROGMEM = "base_version";
const char APP_SKETCH_SIZE_PARAM[] PROGMEM = "sketch_size";
const char APP_SKETCH_SPACE_PARAM[] PROGMEM = "sketch_space";
const char APP_SKETCH_MD5_PARAM[] PROGMEM = "sketch_md5";

const char APP_DATETIME_FORMAT[] = "%Y-%m-%d %H:%M:%S";

const char APP_CONFIGURED_PARAM[] PROGMEM = "configured";

const char ESP_SDK_VERSION_PARAM[] PROGMEM = "esp_sdk";
const char ESP_TEMP_PARAM[] PROGMEM = "esp_temp";
const char ESP_CPU_FREQ_PARAM[] PROGMEM = "cpu_freq";
const char ESP_NUM_CORES_PARAM[] PROGMEM = "num_cores";
const char ESP_HEAP_AVAIL_PARAM[] PROGMEM = "heap_avail";
const char ESP_HEAP_FREE_PARAM[] PROGMEM = "heap_free";
const char ESP_HEAP_MAX_BLOC_PARAM[] PROGMEM = "heap_max_bloc";
const char ESP_HEAP_MIN_FREE_PARAM[] PROGMEM = "heap_min_free";

const char ESP_PSRAM_FOUND_PARAM[] PROGMEM = "psram_found";
const char ESP_PSRAM_SIZE_PARAM[] PROGMEM = "psram_size";
const char ESP_PSRAM_FREE_PARAM[] PROGMEM = "psram_free";
const char ESP_PSRAM_MIN_FREE_PARAM[] PROGMEM = "psram_min_free";
const char ESP_PSRAM_MAX_BLOC_PARAM[] PROGMEM = "psram_max_bloc";



#endif