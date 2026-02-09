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
    JsonDocument jdoc;

    int ret  = parsePrefs(&jdoc);
    if(ret != OK) {
        return ret;
    }

    // process local settings    
    frameRate = jdoc["frame_rate"];
    xclk = jdoc["xclk"];
    myRotation = jdoc["rotate"];

    // get sensor reference
    sensor_t * s = esp_camera_sensor_get();
    JsonObject jctx = jdoc.as<JsonObject>();
    // process camera settings
    if(s) {
        s->set_framesize(s, (framesize_t)jdoc["framesize"].as<int>());
        s->set_quality(s, jdoc["quality"].as<int>());
        s->set_xclk(s, LEDC_TIMER_0, xclk);
        s->set_brightness(s, jdoc["brightness"].as<int>());
        s->set_contrast(s, jdoc["contrast"].as<int>());
        s->set_saturation(s, jdoc["saturation"].as<int>());
        s->set_sharpness(s, jdoc[ "sharpness"].as<int>());
        s->set_denoise(s, jdoc["denoise"].as<int>());
        s->set_special_effect(s, jdoc["special_effect"].as<int>());
        s->set_wb_mode(s, jdoc["wb_mode"].as<int>());
        s->set_whitebal(s, jdoc["awb"].as<int>());
        s->set_awb_gain(s, jdoc["awb_gain"].as<int>());
        s->set_exposure_ctrl(s,jdoc["aec"].as<int>());
        s->set_aec2(s, jdoc["aec2"].as<int>());
        s->set_ae_level(s, jdoc["ae_level"].as<int>());
        s->set_aec_value(s, jdoc["aec_value"].as<int>());
        s->set_gain_ctrl(s, jdoc["agc"].as<int>());
        s->set_agc_gain(s, jdoc["agc_gain"].as<int>());
        s->set_gainceiling(s, (gainceiling_t)jdoc["gainceiling"].as<int>());
        s->set_bpc(s, jdoc["bpc"].as<int>());
        s->set_wpc(s, jdoc["wpc"].as<int>());
        s->set_raw_gma(s, jdoc["raw_gma"].as<int>());
        s->set_lenc(s, jdoc["lenc"].as<int>());
        s->set_vflip(s, jdoc["vflip"].as<int>());
        s->set_hmirror(s, jdoc["hmirror"].as<int>());
        s->set_dcw(s, jdoc["dcw"].as<int>());
        s->set_colorbar(s, jdoc["colorbar"].as<int>());
        
    }
    else {
        ESP_LOGW(tag,"Failed to get camera handle. Camera settings skipped");
    }
  
    // close the file
    return ret;
}

int CLAppCam::savePrefs(){
    char * prefs_file = getPrefsFileName(true); 

    ESP_LOGI(tag,"%s %s",Storage.exists(prefs_file)?"Updating":"Creating", prefs_file); 

    JsonDocument jdoc;
    JsonObject jstr = jdoc.to<JsonObject>();

    dumpStatusToJson(jstr);

    File file = Storage.open(prefs_file, FILE_WRITE);
    if(file) {
        serializeJson(jdoc, file);
        file.close();
        return OK;
    }
    else {
        ESP_LOGW(tag,"Failed to save camera preferences to file %s", prefs_file);
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

void CLAppCam::dumpStatusToJson(JsonObject jstr, bool full_status) {
 
    jstr["rotate"] = myRotation;
    
    if(getLastErr()) return;

    sensor_t * s = esp_camera_sensor_get(); 

    jstr["cam_pid"] = s->id.PID;
    jstr["cam_ver"] = s->id.VER;
    jstr["framesize"] = s->status.framesize;
    jstr["frame_rate"] = frameRate;
    
    if(!full_status) return;

    jstr["quality"] = s->status.quality;
    jstr["brightness"] = s->status.brightness;
    jstr["contrast"] = s->status.contrast;
    jstr["saturation"] = s->status.saturation;
    jstr["sharpness"] = s->status.sharpness;
    jstr["denoise"] = s->status.denoise;
    jstr["special_effect"] = s->status.special_effect;
    jstr["wb_mode"] = s->status.wb_mode;
    jstr["awb"] = s->status.awb;
    jstr["awb_gain"] = s->status.awb_gain;
    jstr["aec"] = s->status.aec;
    jstr["aec2"] = s->status.aec2;
    jstr["ae_level"] = s->status.ae_level;
    jstr["aec_value"] = s->status.aec_value;
    jstr["agc"] = s->status.agc;
    jstr["agc_gain"] = s->status.agc_gain;
    jstr["gainceiling"] = s->status.gainceiling;
    jstr["bpc"] = s->status.bpc;
    jstr["wpc"] = s->status.wpc;
    jstr["raw_gma"] = s->status.raw_gma;
    jstr["lenc"] = s->status.lenc;
    jstr["vflip"] = s->status.vflip;
    jstr["hmirror"] = s->status.hmirror;
    jstr["dcw"] = s->status.dcw;
    jstr["colorbar"] = s->status.colorbar; 

    jstr["xclk"] = xclk;

}



CLAppCam AppCam;