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
    config.pin_sccb_sda = SIOD_GPIO_NUM;
    config.pin_sccb_scl = SIOC_GPIO_NUM;
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

    return OS_SUCCESS;
}

int CLAppCam::stop() {
    Serial.println("Stopping Camera");
    return esp_camera_deinit();
}

int CLAppCam::loadPrefs() {
    JsonDocument json;
    int ret  = parsePrefs(json);
    if(ret != OS_SUCCESS) {
        return ret;
    }

  // process local settings

    this->frameRate = json["frame_rate"];
    this->xclk = json["xclk"];
    this->myRotation = json["rotate"];

    // get sensor reference
    sensor_t * s = esp_camera_sensor_get();
    // process camera settings
    if(s) {
        s->set_framesize(s, (framesize_t)json["framesize"]);
        s->set_quality(s, json["quality"]);
        s->set_xclk(s, LEDC_TIMER_0, xclk);
        s->set_brightness(s, json["brightness"]);
        s->set_contrast(s, json["contrast"]);
        s->set_saturation(s, json["saturation"]);
        s->set_sharpness(s, json["sharpness"]);
        s->set_denoise(s, json["denoise"]);
        s->set_special_effect(s, json["special_effect"]);
        s->set_wb_mode(s, json["wb_mode"]);
        s->set_whitebal(s, json["awb"]);
        s->set_awb_gain(s, json["awb_gain"]);
        s->set_exposure_ctrl(s, json["aec"]);
        s->set_aec2(s, json["aec2"]);
        s->set_ae_level(s, json["ae_level"]);
        s->set_aec_value(s, json["aec_value"]);
        s->set_gain_ctrl(s, json["agc"]);
        s->set_agc_gain(s, json["agc_gain"]);
        s->set_gainceiling(s, (gainceiling_t)json["gainceiling"]);
        s->set_bpc(s, json["bpc"]);
        s->set_wpc(s, json["wpc"]);
        s->set_raw_gma(s, json["raw_gma"]);
        s->set_lenc(s, json["lenc"]);
        s->set_vflip(s, json["vflip"]);
        s->set_hmirror(s, json["hmirror"]);
        s->set_dcw(s, json["dcw"]);
        s->set_colorbar(s, json["colorbar"]);
        
        if(json["debug_mode"]) setDebugMode(json["debug_mode"]);   
        
    }
    else {
        Serial.println("Failed to get camera handle. Camera settings skipped");
    }

    return ret;
}

int CLAppCam::savePrefs(){
    JsonDocument json;
    char* prefs_file = getPrefsFileName(true); 

    if (Storage.exists(prefs_file)) {
        Serial.printf("Updating %s\r\n", prefs_file); 
    } else {
        Serial.printf("Creating %s\r\n", prefs_file);
    }

    dumpStatusToJson(json);

    File file = Storage.open(prefs_file, FILE_WRITE);
    if(file) {
        serializeJson(json, file);
        serializeJsonPretty(json, Serial);
        Serial.println();
        
        file.close();
        Serial.printf("File %s updated\r\n", prefs_file);
        
        return OK;
    }
    else {
        Serial.printf("Failed to save camery preferences to file %s\r\n", prefs_file);
        return FAIL;
    }

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

void CLAppCam::dumpStatusToJson(JsonDocument& json, bool full_status) {
    
    json["rotate"] = this->myRotation;
    
    if(getLastErr()) return;

    sensor_t * s = esp_camera_sensor_get();
    json["cam_pid"] = s->id.PID;
    json["cam_ver"] = s->id.VER;        
    json["framesize"] = s->status.framesize;
    json["frame_rate"] = this->frameRate;   
    
    if(!full_status) return;

    json["quality"]          = s->status.quality;
    json["brightness"]       = s->status.brightness;
    json["contrast"]         = s->status.contrast;
    json["saturation"]       = s->status.saturation;
    json["sharpness"]        = s->status.sharpness;
    json["denoise"]          = s->status.denoise;
    json["special_effect"]   = s->status.special_effect;
    json["wb_mode"]          = s->status.wb_mode;
    json["awb"]              = s->status.awb;
    json["awb_gain"]         = s->status.awb_gain;
    json["aec"]              = s->status.aec;
    json["aec2"]             = s->status.aec2;
    json["ae_level"]         = s->status.ae_level;
    json["aec_value"]        = s->status.aec_value;
    json["agc"]              = s->status.agc;
    json["agc_gain"]         = s->status.agc_gain;
    json["gainceiling"]      = s->status.gainceiling;
    json["bpc"]              = s->status.bpc;
    json["wpc"]              = s->status.wpc;
    json["raw_gma"]          = s->status.raw_gma;
    json["lenc"]             = s->status.lenc;
    json["vflip"]            = s->status.vflip;
    json["hmirror"]          = s->status.hmirror;
    json["dcw"]              = s->status.dcw;
    json["colorbar"]         = s->status.colorbar;
    json["debug_mode"]       = this->isDebugMode();
    json["xclk"]             = this->xclk; 
}



CLAppCam AppCam;