#ifndef app_cam_h
#define app_cam_h

#define CAM_DUMP_BUFFER_SIZE   1024

#include <esp_camera.h>
#include <esp_int_wdt.h>
#include <esp_task_wdt.h>
#include <ArduinoJson.h>

#include "app_component.h"
#include "camera_pins.h"

#include <esp_log.h>

const char CAM_PNAME[] PROGMEM = "cam_name";

const char CAM_LAMP[] PROGMEM = "lamp";
const char CAM_AUTOLAMP[] PROGMEM = "autolamp";
const char CAM_FLASHLAMP[] PROGMEM = "flashlamp";

const char CAM_ROTATE[] PROGMEM = "rotate";
const char CAM_FRAMESIZE[] PROGMEM = "framesize";
const char CAM_PID[] PROGMEM = "cam_pid";
const char CAM_VER[] PROGMEM = "cam_ver";
const char CAM_FRAME_RATE[] PROGMEM = "frame_rate";
const char CAM_QUALITY[] PROGMEM = "quality";
const char CAM_BRIGHTNESS[] PROGMEM = "brightness";
const char CAM_CONTRAST[] PROGMEM = "contrast"; 
const char CAM_SATURATION[] PROGMEM = "saturation";
const char CAM_SHARPNESS[] PROGMEM = "sharpness";
const char CAM_DENOISE[] PROGMEM = "denoise";
const char CAM_SPECIAL_EFFECT[] PROGMEM = "special_effect";
const char CAM_WB_MODE[] PROGMEM = "wb_mode";
const char CAM_AWB[] PROGMEM = "awb";
const char CAM_AWB_GAIN[] PROGMEM = "awb_gain";
const char CAM_AEC[] PROGMEM = "aec";
const char CAM_AEC2[] PROGMEM = "aec2";
const char CAM_AE_LEVEL[] PROGMEM = "ae_level";
const char CAM_AEC_VALUE[] PROGMEM = "aec_value";
const char CAM_AGC[] PROGMEM = "agc";
const char CAM_AGC_GAIN[] PROGMEM = "agc_gain";
const char CAM_GAINCEILING[] PROGMEM = "gainceiling";
const char CAM_BPC[] PROGMEM = "bpc";
const char CAM_WPC[] PROGMEM = "wpc";
const char CAM_RAW_GMA[] PROGMEM = "raw_gma";
const char CAM_LENC[] PROGMEM = "lenc";
const char CAM_VFLIP[] PROGMEM = "vflip";
const char CAM_HMIRROR[] PROGMEM = "hmirror";
const char CAM_DCW[] PROGMEM = "dcw";
const char CAM_COLORBAR[] PROGMEM = "colorbar";
const char CAM_XCLK[] PROGMEM = "xclk";

// Callback type for binary data transmission
typedef int (*ProcessFrameCallback)(uint8_t* buffer, size_t size);

/**
 * @brief Camera Manager
 * Manages all interactions with camera
 */
class CLAppCam : public CLAppComponent {
    public:

        CLAppCam();

        int start();
        int stop(); 
        int loadPrefs();
        int savePrefs();

        int getSensorPID() {return (sensor?sensor->id.PID:0);};
        sensor_t * getSensor() {return sensor;};
        String getErr() {return critERR;};

        int getFrameRate() {return frameRate;};
        void setFrameRate(int newFrameRate) {frameRate = newFrameRate;};

        void setXclk(int val) {xclk = val;};
        int getXclk() {return xclk;};

        void setRotation(int val) {myRotation = val;};
        int getRotation() {return myRotation;};

        int snapToBuffer();
        int IRAM_ATTR snapFrame(ProcessFrameCallback sendCallback);
        uint8_t * IRAM_ATTR getBuffer() {return (fb?fb->buf:nullptr);};
        size_t IRAM_ATTR getBufferSize() {return (fb?fb->len:0);};
        bool IRAM_ATTR isJPEGinBuffer() {return (fb?fb->format == PIXFORMAT_JPEG:false);};
        void releaseBuffer(); 

        void dumpStatusToJson(JsonObject jstr, bool full_status = true);

    private:
        // Camera config structure
        camera_config_t config;

        // Camera module bus communications frequency.
        // Originally: config.xclk_freq_mhz = 20000000, but this lead to visual artifacts on many modules.
        // See https://github.com/espressif/esp32-camera/issues/150#issuecomment-726473652 et al.
        // Initial setting is configured in /default_prefs.json
        int xclk = 8;

        // frame rate in FPS
        // default can be set in /default_prefs.json
        int frameRate = 25;


        int lampChannel = 7;           // a free PWM channel (some channels used by camera)
        const int pwmfreq = 50000;     // 50K pwm frequency
        const int pwmresolution = 9;   // duty cycle bit range
        const int pwmMax = pow(2,pwmresolution)-1;

        // Critical error string; if set during init (camera hardware failure) it
        // will be returned for stream and still image requests
        String critERR = "";

        // initial rotation
        // default can be set in /default_prefs.json
        int myRotation = 0;

        // camera buffer pointer
        camera_fb_t * fb = NULL;

        // camera sensor
        sensor_t * sensor;


};

extern CLAppCam AppCam;

#endif