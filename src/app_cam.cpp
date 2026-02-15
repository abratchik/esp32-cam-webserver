#include "app_cam.h"

CLAppCam::CLAppCam() {
    setTag("cam");
}


int CLAppCam::start() {
    // Populate camera config structure with hardware and other defaults
    config.ledc_channel = LEDC_CHANNEL_0;
    config.ledc_timer = LEDC_TIMER_0;
    config.pin_d0 = Y2_GPIO_NUM;
    config.pin_d1 = Y3_GPIO_NUM;
    config.pin_d2 = Y4_GPIO_NUM;
    config.pin_d3 = Y5_GPIO_NUM;
    config.pin_d4 = Y6_GPIO_NUM;
    config.pin_d5 = Y7_GPIO_NUM;
    config.pin_d6 = Y8_GPIO_NUM;
    config.pin_d7 = Y9_GPIO_NUM;
    config.pin_xclk = XCLK_GPIO_NUM;
    config.pin_pclk = PCLK_GPIO_NUM;
    config.pin_vsync = VSYNC_GPIO_NUM;
    config.pin_href = HREF_GPIO_NUM;
    config.pin_sscb_sda = SIOD_GPIO_NUM;
    config.pin_sscb_scl = SIOC_GPIO_NUM;
    config.pin_pwdn = PWDN_GPIO_NUM;
    config.pin_reset = RESET_GPIO_NUM;
    config.xclk_freq_hz = xclk * 1000000;
    config.pixel_format = PIXFORMAT_JPEG;
    // Low(ish) default framesize and quality
    config.frame_size = FRAMESIZE_SVGA;
    config.jpeg_quality = 12;
    config.fb_location = CAMERA_FB_IN_PSRAM;
    config.fb_count = 2;
    config.grab_mode = CAMERA_GRAB_LATEST;

    #if defined(CAMERA_MODEL_ESP_EYE)
        pinMode(13, INPUT_PULLUP);
        pinMode(14, INPUT_PULLUP);
    #endif

    // camera init
    setErr(esp_camera_init(&config));
    
    if (getLastErr()) {
        critERR = "Camera sensor failed to initialise";
        return getLastErr();
    } else {

        // Get a reference to the sensor
        sensor = esp_camera_sensor_get();

        // Dump camera module, warn for unsupported modules.
        switch (sensor->id.PID) {
            case OV9650_PID: 
                Serial.print("OV9650");  
                break;
            case OV7725_PID: 
                Serial.print("OV7725"); 
                break;
            case OV2640_PID: 
                Serial.print("OV2640"); 
                break;
            case OV3660_PID: 
                Serial.print("OV3660"); 
                break;
            default: 
                Serial.print("UNKNOWN");
                break;
        }
        Serial.println(" camera module detected");

    }

    return OK;
}

int CLAppCam::stop() {
    ESP_LOGI(tag,"Stopping Camera");
    return esp_camera_deinit();
}

int CLAppCam::loadPrefs() {
    JsonDocument doc;

    int ret  = parsePrefs(&doc);
    if(ret != OK) {
        return ret;
    }

    // process local settings    
    frameRate = doc[FPSTR(CAM_FRAME_RATE)];
    xclk = doc[FPSTR(CAM_XCLK)];
    myRotation = doc["rotate"];

    // get sensor reference
    sensor_t * s = esp_camera_sensor_get();
    JsonObject jctx = doc.as<JsonObject>();
    // process camera settings
    if(s) {
        s->set_framesize(s, (framesize_t)doc[FPSTR(CAM_FRAMESIZE)].as<int>());
        s->set_quality(s, doc[FPSTR(CAM_QUALITY)].as<int>());
        s->set_xclk(s, LEDC_TIMER_0, xclk);
        s->set_brightness(s, doc[FPSTR(CAM_BRIGHTNESS)].as<int>());
        s->set_contrast(s, doc[FPSTR(CAM_CONTRAST)].as<int>());
        s->set_saturation(s, doc[FPSTR(CAM_SATURATION)].as<int>());
        s->set_sharpness(s, doc[ FPSTR(CAM_SHARPNESS)].as<int>());
        s->set_denoise(s, doc[FPSTR(CAM_DENOISE)].as<int>());
        s->set_special_effect(s, doc[FPSTR(CAM_SPECIAL_EFFECT)].as<int>());
        s->set_wb_mode(s, doc[FPSTR(CAM_WB_MODE)].as<int>());
        s->set_whitebal(s, doc[FPSTR(CAM_AWB)].as<int>());
        s->set_awb_gain(s, doc[FPSTR(CAM_AWB_GAIN)].as<int>());
        s->set_exposure_ctrl(s,doc[FPSTR(CAM_AEC)].as<int>());
        s->set_aec2(s, doc[FPSTR(CAM_AEC2)].as<int>());
        s->set_ae_level(s, doc[FPSTR(CAM_AE_LEVEL)].as<int>());
        s->set_aec_value(s, doc[FPSTR(CAM_AEC_VALUE)].as<int>());
        s->set_gain_ctrl(s, doc[FPSTR(CAM_AGC)].as<int>());
        s->set_agc_gain(s, doc[FPSTR(CAM_AGC_GAIN)].as<int>());
        s->set_gainceiling(s, (gainceiling_t)doc[FPSTR(CAM_GAINCEILING)].as<int>());
        s->set_bpc(s, doc[FPSTR(CAM_BPC)].as<int>());
        s->set_wpc(s, doc[FPSTR(CAM_WPC)].as<int>());
        s->set_raw_gma(s, doc[FPSTR(CAM_RAW_GMA)].as<int>());
        s->set_lenc(s, doc[FPSTR(CAM_LENC)].as<int>());
        s->set_vflip(s, doc[FPSTR(CAM_VFLIP)].as<int>());
        s->set_hmirror(s, doc[FPSTR(CAM_HMIRROR)].as<int>());
        s->set_dcw(s, doc[FPSTR(CAM_DCW)].as<int>());
        s->set_colorbar(s, doc[FPSTR(CAM_COLORBAR)].as<int>());
        
    }
    else {
        ESP_LOGW(tag,"Failed to get camera handle. Camera settings skipped");
    }

    _lampVal = doc[FPSTR(CAM_LAMP)] | -1;
    _autoLamp = doc[FPSTR(CAM_AUTOLAMP)] | false;
    _flashLamp = doc[FPSTR(CAM_FLASHLAMP)] | 0;

    AppPwm.loadPrefsFromJson(doc.as<JsonObject>());

    // First PWM shoudl be reserved for the lamp, if defined.
    ESP32PWM* lampPWM = AppPwm.get(0);
    if( lampPWM != nullptr && _lampVal >= 0) 
    { 
        _lamppin = lampPWM->getPin(); 
        _pwmMax = pow(2, lampPWM->getResolutionBits())-1; 
        ESP_LOGI(tag,"Flash lamp activated on pin %d", _lamppin); 
    } 
    else 
    { 
        ESP_LOGW(tag,"No PWM configured for flash lamp"); 
    }  

    _imagesServed = 0;
  
    // close the file
    return ret;
}

int CLAppCam::savePrefs(){

    JsonDocument doc;
    JsonObject jstr = doc.to<JsonObject>();

    dumpStatusToJson(jstr);

    return savePrefsToFile(&doc);

}

int IRAM_ATTR CLAppCam::snapToBuffer() {
    fb = esp_camera_fb_get();

    return (fb?ESP_OK:ESP_FAIL);
}

void IRAM_ATTR CLAppCam::releaseBuffer() {
    if(fb) {
        esp_camera_fb_return(fb);
        fb = NULL;
    }
}

int IRAM_ATTR CLAppCam::snapFrame(ProcessFrameCallback sendCallback) {

    int res = AppCam.snapToBuffer();

    if(!res) {

        if(isJPEGinBuffer()){
            res = sendCallback?sendCallback(getBuffer(), getBufferSize()):FAIL;
        } else {
            res = FAIL;
        }
    }

    releaseBuffer();
    return res;
}

int CLAppCam::snapStillImage(ProcessFrameCallback sendCallback) {

    if(_lampVal>=0 && _autoLamp){
        setLamp(_flashLamp);
        delay(150); // coupled with the status led flash this gives ~150ms for lamp to settle.
    }

#if (CONFIG_LOG_DEFAULT_LEVEL >= CORE_DEBUG_LEVEL )
    int64_t fr_start = esp_timer_get_time();
#endif
        
    if (snapFrame(sendCallback) != OK) {
        if(_autoLamp) setLamp(0);
        return FAIL;
    }
        
#if (CONFIG_LOG_DEFAULT_LEVEL >= CORE_DEBUG_LEVEL )
    int64_t fr_end = esp_timer_get_time();
    ESP_LOGD(tag,"B %ums", (uint32_t)((fr_end - fr_start)/1000));
#endif

    if(_autoLamp) setLamp(0);

    _imagesServed++;

    return OK;
}

// Lamp Control
void CLAppCam::setLamp(int newVal) {

    if(newVal == DEFAULT_FLASH) {
        newVal = _flashLamp;
    }
    _lampVal = newVal;
    
    // Apply a logarithmic function to the scale.
    if(_lamppin) {
        int brightness = round(_lampVal * _pwmMax/100.00);
        AppPwm.write(_lamppin, brightness,0);
    }

}

void CLAppCam::dumpStatusToJson(JsonObject jstr, bool full_status) {
 
    jstr["rotate"] = myRotation;
    
    if(getLastErr()) return;

    sensor_t * s = esp_camera_sensor_get(); 

    jstr[FPSTR(CAM_PID)] = s->id.PID;
    jstr[FPSTR(CAM_VER)] = s->id.VER;
    jstr[FPSTR(CAM_FRAMESIZE)] = s->status.framesize;
    jstr[FPSTR(CAM_FRAME_RATE)] = frameRate;
    
    if(!full_status) return;

    jstr[FPSTR(CAM_QUALITY)] = s->status.quality;
    jstr[FPSTR(CAM_BRIGHTNESS)] = s->status.brightness;
    jstr[FPSTR(CAM_CONTRAST)] = s->status.contrast;
    jstr[FPSTR(CAM_SATURATION)] = s->status.saturation;
    jstr[FPSTR(CAM_SHARPNESS)] = s->status.sharpness;
    jstr[FPSTR(CAM_DENOISE)] = s->status.denoise;
    jstr[FPSTR(CAM_SPECIAL_EFFECT)] = s->status.special_effect;
    jstr[FPSTR(CAM_WB_MODE)] = s->status.wb_mode;
    jstr[FPSTR(CAM_AWB)] = s->status.awb;
    jstr[FPSTR(CAM_AWB_GAIN)] = s->status.awb_gain;
    jstr[FPSTR(CAM_AEC)] = s->status.aec;
    jstr[FPSTR(CAM_AEC2)] = s->status.aec2;
    jstr[FPSTR(CAM_AE_LEVEL)] = s->status.ae_level;
    jstr[FPSTR(CAM_AEC_VALUE)] = s->status.aec_value;
    jstr[FPSTR(CAM_AGC)] = s->status.agc;
    jstr[FPSTR(CAM_AGC_GAIN)] = s->status.agc_gain;
    jstr[FPSTR(CAM_GAINCEILING)] = s->status.gainceiling;
    jstr[FPSTR(CAM_BPC)] = s->status.bpc;
    jstr[FPSTR(CAM_WPC)] = s->status.wpc;
    jstr[FPSTR(CAM_RAW_GMA)] = s->status.raw_gma;
    jstr[FPSTR(CAM_LENC)] = s->status.lenc;
    jstr[FPSTR(CAM_VFLIP)] = s->status.vflip;
    jstr[FPSTR(CAM_HMIRROR)] = s->status.hmirror;
    jstr[FPSTR(CAM_DCW)] = s->status.dcw;
    jstr[FPSTR(CAM_COLORBAR)] = s->status.colorbar; 

    jstr[FPSTR(CAM_XCLK)] = xclk;

    jstr[FPSTR(CAM_LAMP)] = getLamp();
    jstr[FPSTR(CAM_AUTOLAMP)] = isAutoLamp();
    jstr[FPSTR(CAM_FLASHLAMP)] = getFlashLamp(); 

    AppPwm.savePrefsToJson(jstr);

}


CLAppCam AppCam;