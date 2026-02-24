#ifndef app_cam_h
#define app_cam_h

#define DEFAULT_FLASH                   0xFF

#include <esp_camera.h>
#include <esp_int_wdt.h>
#include <esp_task_wdt.h>
#include <ArduinoJson.h>

#include "app_component.h"
#include "camera_pins.h"
#include "app_pwm.h"

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

        int loadFromJson(JsonObject jstr, bool full_set = true);
        int saveToJson(JsonObject jstr, bool full_set = true);

        int getSensorPID() {return (sensor?sensor->id.PID:0);};
        sensor_t * getSensor() {return sensor;};
        String getErr() {return critERR;};

        int getFrameRate() {return frameRate;};
        void setFrameRate(int newFrameRate) {frameRate = newFrameRate;};

        void setXclk(int val) {xclk = val;};
        int getXclk() {return xclk;};

        void setRotation(int val) {myRotation = val;};
        int getRotation() {return myRotation;};

        int IRAM_ATTR snapFrame(ProcessFrameCallback sendCallback);

        int snapStillImage(ProcessFrameCallback sendCallback);

        void setAutoLamp(bool val) {_autoLamp = val;};
        bool isAutoLamp() { return _autoLamp;};   
        int getFlashLamp() {return _flashLamp;}; 
        void setFlashLamp(int newVal) {_flashLamp = newVal;};

        void setLamp(int newVal = DEFAULT_FLASH);
        int getLamp() {return _lampVal;};   
        
        long getImagesServed() {return _imagesServed;};
    
    protected:
        int IRAM_ATTR snapToBuffer();
        void IRAM_ATTR releaseBuffer(); 
        bool IRAM_ATTR isJPEGinBuffer() {return (fb?fb->format == PIXFORMAT_JPEG:false);};
        uint8_t * IRAM_ATTR getBuffer() {return (fb?fb->buf:nullptr);};
        size_t IRAM_ATTR getBufferSize() {return (fb?fb->len:0);};

    private:
        // Camera config structure
        camera_config_t config;

        // Camera module bus communications frequency.
        // Originally: config.xclk_freq_mhz = 20000000, but this lead to visual artifacts on many modules.
        // See https://github.com/espressif/esp32-camera/issues/150#issuecomment-726473652 et al.
        // Initial setting is configured in /default_prefs.json
        int xclk = 8;

        // frame rate in FPS
        int frameRate = 25;

        // Flash LED lamp parameters.
        // should be defined in the 1st line of the pwm collection in the cam prefs (cam.json)
        bool _autoLamp = false;         // Automatic lamp (auto on while camera running)
        int _lampVal = -1;              // Lamp brightness
        int _flashLamp = 80;            // Flash brightness when taking still images or capturing streams
        uint8_t _lamppin = 0;           // Lamp pin, not defined by default
        int _pwmMax = 1;                // _pwmMax = pow(2,pwmresolution)-1;

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

        long _imagesServed;

};

extern CLAppCam AppCam;

#endif