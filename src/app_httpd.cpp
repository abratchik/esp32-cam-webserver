#include "app_httpd.h"

CLAppHttpd::CLAppHttpd() {
    // Gather static values used when dumping status; these are slow functions, so just do them once during startup
    _sketchSize = ESP.getSketchSize();;
    _sketchSpace = ESP.getFreeSketchSpace();
    _sketchMD5 = ESP.getSketchMD5();
    setTag("httpd");
#ifdef CAMERA_MODEL_AI_THINKER
    setPrefix("aithinker");
#endif
}

void IRAM_ATTR onSnapTimer(TimerHandle_t pxTimer){
    AppHttpd.snapFrame();
}

int CLAppHttpd::start() {
    
    loadPrefs();

    server = new AsyncWebServer(AppConn.getPort());
    ws = new AsyncWebSocket("/ws");
    
    server->on("/", HTTP_GET, [](AsyncWebServerRequest *request){
        if(!request->authenticate(AppConn.getUser(), AppConn.getPwd()))
            return request->requestAuthentication();
        if(AppConn.isConfigured())
            request->send(Storage.getFS(), "/www/camera.html", "", false, processor);
        else
            request->send(Storage.getFS(), "/www/setup.html", "", false, processor);
    });

    server->on("/camera", HTTP_GET, [](AsyncWebServerRequest *request){
        if(!request->authenticate(AppConn.getUser(), AppConn.getPwd()))
            return request->requestAuthentication();
        request->send(Storage.getFS(), "/www/camera.html", "", false, processor);
    });  

    server->on("/setup", HTTP_GET, [](AsyncWebServerRequest *request){
        if(!request->authenticate(AppConn.getUser(), AppConn.getPwd()))
            return request->requestAuthentication();
        request->send(Storage.getFS(), "/www/setup.html", "", false, processor);
    });    

    server->on("/dump", HTTP_GET, [](AsyncWebServerRequest *request){
        if(!request->authenticate(AppConn.getUser(), AppConn.getPwd()))
            return request->requestAuthentication();
        request->send(Storage.getFS(), "/www/dump.html", "", false, processor);
    });    

    server->on("/view", HTTP_GET, [](AsyncWebServerRequest *request){
        if(!request->authenticate(AppConn.getUser(), AppConn.getPwd()))
            return request->requestAuthentication();
        if(request->arg("mode") == "stream" || 
            request->arg("mode") == "still") {
            if(!AppCam.getLastErr()) {
                request->send(Storage.getFS(), "/www/view.html", "", false, processor);
            }
            else {
                request->send(Storage.getFS(), "/www/error.html", "", false, processor);
            }
        }
        else
            request->send(400);
    });

    // adding fixed mappigs
    for(int i=0; i<_mappingCount; i++) {
        server->serveStatic(mappingList[i]->uri, Storage.getFS(), mappingList[i]->path).setAuthentication(AppConn.getUser(), AppConn.getPwd());
    }

    server->on("/control", HTTP_GET, onControl).setAuthentication(AppConn.getUser(), AppConn.getPwd());
    server->on("/status", HTTP_GET, onStatus).setAuthentication(AppConn.getUser(), AppConn.getPwd());
    server->on("/system", HTTP_GET, onSystemStatus).setAuthentication(AppConn.getUser(), AppConn.getPwd());
    server->on("/info", HTTP_GET, onInfo).setAuthentication(AppConn.getUser(), AppConn.getPwd());

    
    // adding WebSocket handler
    ws->onEvent(onWsEvent);
    server->addHandler(ws);  

    _stream_timer = xTimerCreate("SnapTimer", 1000/AppCam.getFrameRate()/portTICK_PERIOD_MS, pdTRUE, 0, onSnapTimer);

    DefaultHeaders::Instance().addHeader("Access-Control-Allow-Origin", "*");
    server->begin();


    ESP_LOGD(tag,"Use '%s' to connect", AppConn.getHTTPUrl());
    ESP_LOGD(tag, "Stream viewer available at '%sview?mode=stream'", AppConn.getHTTPUrl());
    
    ESP_LOGI(tag, "HTTP server started");
    return OK;
}

void onWsEvent(AsyncWebSocket * server, AsyncWebSocketClient * client, AwsEventType type, void * arg, uint8_t *data, size_t len){
    

    if(type == WS_EVT_CONNECT){
        ESP_LOGI(AppHttpd.getTag(), "ws[%s][%u] connect", server->url(), client->id());
    }
    else if(type == WS_EVT_DISCONNECT){
        ESP_LOGI(AppHttpd.getTag(),"ws[%s][%u] disconnect", server->url(), client->id());
        AppHttpd.stopStream(client->id());        
        if(AppHttpd.getControlClient() == client->id()) {
            AppHttpd.setControlClient(0);
            AppHttpd.resetPWM(RESET_ALL_PWM);
            AppHttpd.serialSendCommand("Disconnected");
        }
    }
    else if(type == WS_EVT_ERROR){
        ESP_LOGE(AppHttpd.getTag(),"ws[%s][%u] error(%u): %s", server->url(), client->id(), *((uint16_t*)arg), (char*)data);
    }
    else if(type == WS_EVT_PONG){
        ESP_LOGE(AppHttpd.getTag(),"ws[%s][%u] pong[%u]: %s", server->url(), client->id(), len, (len)?(char*)data:"");
    }
    else if(type == WS_EVT_DATA){
        AwsFrameInfo * info = (AwsFrameInfo*)arg;
        uint8_t* msg = (uint8_t*) data;

        switch(*msg) {
            case (uint8_t)'s':
                if(AppHttpd.startStream(client->id(), CAPTURE_STREAM) != STREAM_SUCCESS)
                    client->close();
                break;
            case (uint8_t)'p':  
                AppHttpd.startStream(client->id(), CAPTURE_STILL);
                break;
            case (uint8_t)'c':
                if(AppHttpd.getControlClient()==0) {
                    AppHttpd.setControlClient(client->id());
                    AppHttpd.serialSendCommand("Connected");
                }
                break;
            case (uint8_t)'w':  // write PWM value
                if(AppHttpd.getControlClient())
                    if(len > 4) {
                        uint8_t pin = *(msg+1);
                        int nparams = *(msg+2);
                        int vlen = *(msg+3);
                        int value = 0;

                        if(vlen == 2)
                            value = *(msg+4) + *(msg+5)*256;
                        else
                            value = *(msg+4);


                        ESP_LOGD(AppHttpd.getTag(),"vlen %d nparams %d value %d", vlen, nparams, value);

                        if(nparams == 1)
                            AppHttpd.writePWM(pin, value); // write to servo
                        else
                            AppHttpd.writePWM(pin, value, 0); // write to raw PWM
                    }
                break;
            case (uint8_t)'t':  // terminate stream
                AppHttpd.stopStream(client->id());
                break;
            default:
            #if (CONFIG_LOG_DEFAULT_LEVEL >= CORE_DEBUG_LEVEL )
                Serial.printf("ws[%s] client[%u] frame[%u] %u %s[%llu - %llu]: ", server->url(), client->id(), info->num,
                    info->message_opcode, (info->message_opcode == WS_TEXT)?"text":"binary", info->index, info->index + len);
                for(int i=0; i< len; i++) {
                    Serial.printf("%d,", *msg);
                    msg++;
                }
                Serial.println();
            #endif
                break;
        }


        
    }

}    


String processor(const String& var) {
  if(var == "CAMNAME")
    return String(AppHttpd.getName());
  else if(var == "ERRORTEXT")
    return AppCam.getErr();
  else if(var == "APPURL")
    return String(AppConn.getHTTPUrl());
//   else if(var == "STREAMURL")
//     return String(AppConn.getStreamUrl());
  else
    return String();
}


int IRAM_ATTR CLAppHttpd::snapFrame() {
    int res = AppCam.snapToBuffer();

    if(!res) {

        if(AppCam.isJPEGinBuffer()){
            ws->binaryAll( AppCam.getBuffer(), AppCam.getBufferSize());

        } else {
            res = OS_FAIL;
        }
    }

    AppCam.releaseBuffer();
    return res;
}

StreamResponseEnum CLAppHttpd::startStream(uint32_t id, CaptureModeEnum streammode) {
    
    // if video stream requested, check if we can add extra
    if(streammode == CAPTURE_STREAM) {
        if(_streamCount+1 > _max_streams) return STREAM_NUM_EXCEEDED;
        if(addStreamClient(id) != OS_SUCCESS) return STREAM_CLIENT_REGISTER_FAILED;
    }

    if(!_stream_timer) return STREAM_TIMER_NOT_INITIALIZED;

    if(streammode == CAPTURE_STREAM) {


        ESP_LOGI(AppHttpd.getTag(),"Stream start, frame period = %d", xTimerGetPeriod(_stream_timer));
        
        // if stream is not started, start 
        if(xTimerIsTimerActive(_stream_timer) == pdFALSE) {
            vTimerSetReloadMode(_stream_timer, pdTRUE);
            if(xTimerStart(_stream_timer, 0) == pdPASS)
                ESP_LOGI(AppHttpd.getTag(),"Stream timer started");
            else
                ESP_LOGW(AppHttpd.getTag(),"Failed to start the Stream timer!");
        }

        _streamCount++;

    }
    else if(streammode == CAPTURE_STILL) {
        ESP_LOGI(tag,"Still image requested");
        // if video stream is not active, take the picture as usual
        if(xTimerIsTimerActive(_stream_timer) == pdFALSE) {
            if(_lampVal>=0 && _autoLamp){
                setLamp(_flashLamp);
                delay(150); // coupled with the status led flash this gives ~150ms for lamp to settle.
            }

            int64_t fr_start = esp_timer_get_time();
        
            if (snapFrame() != OS_SUCCESS) {
                if(_autoLamp) setLamp(0);
                return STREAM_IMAGE_CAPTURE_FAILED;
            }
        
        #if (CONFIG_LOG_DEFAULT_LEVEL >= CORE_DEBUG_LEVEL )
            int64_t fr_end = esp_timer_get_time();
            ESP_LOGD(AppHttpd.getTag(),"B %ums", (uint32_t)((fr_end - fr_start)/1000));
        #endif

            if(_autoLamp) setLamp(0);
            _imagesServed++;
            
        }
        else {
            ESP_LOGI(tag, "Image to be taken from the parallel video stream");
        }
        
    }
    else
        return STREAM_MODE_NOT_SUPPORTED;

    return STREAM_SUCCESS;
}

StreamResponseEnum CLAppHttpd::stopStream(uint32_t id) {

    if(removeStreamClient(id) != OS_SUCCESS) return STREAM_CLIENT_NOT_FOUND;

    if(!_stream_timer) return STREAM_TIMER_NOT_INITIALIZED;
    
    // if the stream is the last one active, stop the timer
    if(xTimerIsTimerActive(_stream_timer) != pdFALSE && _streamCount == 1) {
        vTimerSetReloadMode(_stream_timer, pdFALSE);
        if(xTimerStop(_stream_timer, 0) == pdPASS)
            ESP_LOGI(AppHttpd.getTag(),"Stop sent to Stream timer");
        else
            ESP_LOGW(AppHttpd.getTag(),"Failed to post the stop command to the Stream timer!");

        if(_lampVal>0 and _autoLamp) setLamp(0);     
    }
    
    _streamsServed++;
    _streamCount--;
    
    ESP_LOGI(AppHttpd.getTag(),"Stream stopped");
    return STREAM_SUCCESS;
}

void onControl(AsyncWebServerRequest *request) {
    
    if (AppCam.getLastErr()) {
        request->send(500);
        return;
    }

    if (request->args() == 0) {
        request->send(400);
        return;
    }

    String variable = request->arg("var");
    String value = request->arg("val");

#if (CONFIG_LOG_DEFAULT_LEVEL >= CORE_DEBUG_LEVEL )
    Serial.print("Command: var="); Serial.print(variable); 
    Serial.print(", val="); Serial.println(value);
#endif

    int res = 0;
    long val = value.toInt();
    sensor_t * s = AppCam.getSensor();

    if(variable == "cmdout") {
    #if (CONFIG_LOG_DEFAULT_LEVEL >= CORE_DEBUG_LEVEL )
        Serial.print("cmdout=");
        Serial.println(value.c_str());
    #endif
        AppHttpd.serialSendCommand(value.c_str());
        request->send(200);
        return;
    }    
    else if(variable ==  "save_prefs") {
        if(value == "conn") 
            res = AppConn.savePrefs();
        else if(value == "cam") 
            res = AppCam.savePrefs() + AppHttpd.savePrefs(); 
        else {
            request->send(400);
            return;
        }
        if(res == OS_SUCCESS)
            request->send(200);
        else
            request->send(500);
        return;
    }
    else if(variable ==  "remove_prefs") {
        if(value == "conn")
            res = AppConn.removePrefs(); 
        else if(value == "cam")
            res = AppCam.removePrefs();
        else {
            request->send(400);
            return;
        }

        if(res == OS_SUCCESS)
            request->send(200);
        else
            request->send(500);
        return;
    }
    else if(variable == "reboot") {
        request->send(200);
        if (AppHttpd.getLamp() != -1) AppHttpd.setLamp(0); // kill the lamp; otherwise it can remain on during the soft-reboot
        Storage.getFS().end();      // close file storage
        esp_task_wdt_init(3,true);  // schedule a a watchdog panic event for 3 seconds in the future
        esp_task_wdt_add(NULL);
        periph_module_disable(PERIPH_I2C0_MODULE); // try to shut I2C down properly
        periph_module_disable(PERIPH_I2C1_MODULE);
        periph_module_reset(PERIPH_I2C0_MODULE);
        periph_module_reset(PERIPH_I2C1_MODULE);
        ESP_LOGI(AppHttpd.getTag(),"REBOOT requested");
        while(true) {
          delay(200);
          Serial.print('.');
        }
    }
    else if(variable == "ssid") {AppConn.setSSID(value.c_str());AppConn.setPassword("");}
    else if(variable == "password") AppConn.setPassword(value.c_str());
    else if(variable == "st_ip") AppConn.setStaticIP(&(AppConn.getStaticIP()->ip), value.c_str());
    else if(variable == "st_subnet") AppConn.setStaticIP(&(AppConn.getStaticIP()->netmask), value.c_str());
    else if(variable == "st_gateway") AppConn.setStaticIP(&(AppConn.getStaticIP()->gateway), value.c_str());
    else if(variable == "dns1") AppConn.setStaticIP(&(AppConn.getStaticIP()->dns1), value.c_str());
    else if(variable == "dns2") AppConn.setStaticIP(&(AppConn.getStaticIP()->dns2), value.c_str());
    else if(variable == "ap_ip") AppConn.setStaticIP(&(AppConn.getAPIP()->ip), value.c_str());
    else if(variable == "ap_subnet") AppConn.setStaticIP(&(AppConn.getAPIP()->netmask), value.c_str());
    else if(variable == "ap_name") AppConn.setApName(value.c_str());
    else if(variable == "ap_pass") AppConn.setApPass(value.c_str());
    else if(variable == "mdns_name") AppConn.setMDNSName(value.c_str());
    else if(variable == "ntp_server") AppConn.setNTPServer(value.c_str());
    else if(variable == "user") AppConn.setUser(value.c_str());
    else if(variable == "pwd") AppConn.setPwd(value.c_str());
    else if(variable == "ota_password") AppConn.setOTAPassword(value.c_str());
    else if(variable == "framesize") {
        if(s->pixformat == PIXFORMAT_JPEG) res = s->set_framesize(s, (framesize_t)val);
    }
    else if(variable == "quality") res = s->set_quality(s, val);
    else if(variable == "xclk") { AppCam.setXclk(val); res = s->set_xclk(s, LEDC_TIMER_0, AppCam.getXclk()); }
    else if(variable == "contrast") res = s->set_contrast(s, val);
    else if(variable == "brightness") res = s->set_brightness(s, val);
    else if(variable ==  "saturation") res = s->set_saturation(s, val);
    else if(variable ==  "sharpness") res = s->set_sharpness(s, val);
    else if(variable ==  "denoise") res = s->set_denoise(s, val);
    else if(variable ==  "gainceiling") res = s->set_gainceiling(s, (gainceiling_t)val);
    else if(variable ==  "colorbar") res = s->set_colorbar(s, val);
    else if(variable ==  "awb") res = s->set_whitebal(s, val);
    else if(variable ==  "agc") res = s->set_gain_ctrl(s, val);
    else if(variable ==  "aec") res = s->set_exposure_ctrl(s, val);
    else if(variable ==  "hmirror") res = s->set_hmirror(s, val);
    else if(variable ==  "vflip") res = s->set_vflip(s, val);
    else if(variable ==  "awb_gain") res = s->set_awb_gain(s, val);
    else if(variable ==  "agc_gain") res = s->set_agc_gain(s, val);
    else if(variable ==  "aec_value") res = s->set_aec_value(s, val);
    else if(variable ==  "aec2") res = s->set_aec2(s, val);
    else if(variable ==  "dcw") res = s->set_dcw(s, val);
    else if(variable ==  "bpc") res = s->set_bpc(s, val);
    else if(variable ==  "wpc") res = s->set_wpc(s, val);
    else if(variable ==  "raw_gma") res = s->set_raw_gma(s, val);
    else if(variable ==  "lenc") res = s->set_lenc(s, val);
    else if(variable ==  "special_effect") res = s->set_special_effect(s, val);
    else if(variable ==  "wb_mode") res = s->set_wb_mode(s, val);
    else if(variable ==  "ae_level") res = s->set_ae_level(s, val);
    else if(variable ==  "rotate") AppCam.setRotation(val);
    else if(variable ==  "frame_rate") {
        AppCam.setFrameRate(val);
        AppHttpd.setFrameRate(val);
    }
    else if(variable ==  "autolamp" && AppHttpd.getLamp() != -1) {
        AppHttpd.setAutoLamp(val);
    }
    else if(variable ==  "lamp" && AppHttpd.getLamp() != -1) {
        AppHttpd.setLamp(constrain(val,0,100));
    }
    else if(variable ==  "flashlamp" && AppHttpd.getLamp() != -1) {
        AppHttpd.setFlashLamp(constrain(val,0,100));
    }
    else if(variable == "accesspoint") AppConn.setLoadAsAP(val);
    else if(variable == "ap_channel") AppConn.setAPChannel(val);
    else if(variable == "ap_dhcp") AppConn.setAPDHCP(val);
    else if(variable == "dhcp") AppConn.setDHCPEnabled(val);
    else if(variable == "port") AppConn.setPort(val);
    else if(variable == "ota_enabled") AppConn.setOTAEnabled(val);
    else if(variable == "gmt_offset") AppConn.setGmtOffset_sec(val);
    else if(variable == "dst_offset") AppConn.setDaylightOffset_sec(val);
    else {
        res = -1;
    }
    if(res){
        request->send(400);
        return;
    }
    request->send(200);
}

void CLAppHttpd::setFrameRate(int tps) {
    if(_stream_timer)
        xTimerChangePeriod(_stream_timer, 1000/tps/portTICK_PERIOD_MS, 100);
}

void onInfo(AsyncWebServerRequest *request) {
    AsyncResponseStream *response = request->beginResponseStream("application/json");

    char buf[CAM_DUMP_BUFFER_SIZE];

    AppHttpd.dumpCameraStatusToJson(buf, sizeof(buf), false);

    response->print(buf);
    request->send(response); 
}

void onStatus(AsyncWebServerRequest *request) {
    AsyncResponseStream *response = request->beginResponseStream("application/json");
    // Do not get attempt to get sensor when in error; causes a panic..

    char buf[CAM_DUMP_BUFFER_SIZE];

    AppHttpd.dumpCameraStatusToJson(buf, sizeof(buf));

    response->print(buf);
    request->send(response);
}

void onSystemStatus(AsyncWebServerRequest *request) {
    AsyncResponseStream *response = request->beginResponseStream("application/json");

    char buf[1280];
    AppHttpd.dumpSystemStatusToJson(buf, sizeof(buf));
    
    response->print(buf);

#if (CONFIG_LOG_DEFAULT_LEVEL >= CORE_DEBUG_LEVEL )
    Serial.println();
    Serial.println("Dump requested through web");
    Serial.println(buf);
#endif

    request->send(response);
}

void CLAppHttpd::dumpCameraStatusToJson(char * buf, size_t size, bool full_status) {
    
    json_gen_str_t jstr;
    json_gen_str_start(&jstr, buf, size, NULL, NULL);
    json_gen_start_object(&jstr);

    json_gen_obj_set_string(&jstr, (char*)"cam_name", getName());
    // json_gen_obj_set_string(&jstr, (char*)"stream_url", AppConn.getStreamUrl());
    AppConn.updateTimeStr();
    json_gen_obj_set_string(&jstr, (char*)"local_time", AppConn.getLocalTimeStr());
    json_gen_obj_set_string(&jstr, (char*)"up_time", AppConn.getUpTimeStr());   
    json_gen_obj_set_int(&jstr, (char*)"rssi", (!AppConn.isAccessPoint()?WiFi.RSSI():(uint8_t)0));
    json_gen_obj_set_int(&jstr, (char*)"esp_temp", getTemp());
    json_gen_obj_set_string(&jstr, (char*)"serial_buf", getSerialBuffer());    

    AppCam.dumpStatusToJson(&jstr, full_status);

    if(full_status) {
        json_gen_obj_set_int(&jstr, (char*)"lamp", getLamp());
        json_gen_obj_set_bool(&jstr, (char*)"autolamp", isAutoLamp());
        json_gen_obj_set_int(&jstr, (char*)"lamp", getLamp());
        json_gen_obj_set_int(&jstr, (char*)"flashlamp", getFlashLamp());

        json_gen_obj_set_string(&jstr, (char*)"code_ver", getVersion());  
    }
    json_gen_end_object(&jstr);
    json_gen_str_end(&jstr);
}

void CLAppHttpd::dumpSystemStatusToJson(char * buf, size_t size) {

    json_gen_str_t jstr;
    json_gen_str_start(&jstr, buf, size, NULL, NULL);
    json_gen_start_object(&jstr);

    json_gen_obj_set_string(&jstr, (char*)"cam_name", getName());
    json_gen_obj_set_string(&jstr, (char*)"code_ver", getVersion());
    json_gen_obj_set_string(&jstr, (char*)"base_version", BASE_VERSION);
    json_gen_obj_set_int(&jstr, (char*)"sketch_size", getSketchSize());
    json_gen_obj_set_int(&jstr, (char*)"sketch_space", getSketchSpace());
    json_gen_obj_set_string(&jstr, (char*)"sketch_md5", getSketchMD5());
    json_gen_obj_set_string(&jstr, (char*)"esp_sdk", ESP.getSdkVersion());
    
    json_gen_obj_set_bool(&jstr,(char*)"accesspoint", AppConn.isAccessPoint());
    json_gen_obj_set_bool(&jstr,(char*)"captiveportal", AppConn.isCaptivePortal());
    json_gen_obj_set_string(&jstr, (char*)"ap_name", AppConn.getApName());
    json_gen_obj_set_string(&jstr, (char*)"ssid", AppConn.getSSID());

    json_gen_obj_set_int(&jstr, (char*)"rssi", (!AppConn.isAccessPoint()?WiFi.RSSI():(uint8_t)0));
    json_gen_obj_set_string(&jstr, (char*)"bssid", (!AppConn.isAccessPoint()?WiFi.BSSIDstr().c_str():(char*)""));
    json_gen_obj_set_int(&jstr, (char*)"dhcp", AppConn.isDHCPEnabled());
    
    json_gen_obj_set_string(&jstr, (char*)"ip_address", (AppConn.isAccessPoint()?WiFi.softAPIP().toString().c_str():WiFi.localIP().toString().c_str()));
    json_gen_obj_set_string(&jstr, (char*)"subnet", (!AppConn.isAccessPoint()?WiFi.subnetMask().toString().c_str():(char*)""));
    json_gen_obj_set_string(&jstr, (char*)"gateway", (!AppConn.isAccessPoint()?WiFi.gatewayIP().toString().c_str():(char*)""));

    json_gen_obj_set_string(&jstr, (char*)"st_ip", (AppConn.getStaticIP()->ip?AppConn.getStaticIP()->ip->toString().c_str():(char*)""));
    json_gen_obj_set_string(&jstr, (char*)"st_subnet", (AppConn.getStaticIP()->netmask?AppConn.getStaticIP()->netmask->toString().c_str():(char*)""));
    json_gen_obj_set_string(&jstr, (char*)"st_gateway", (AppConn.getStaticIP()->gateway?AppConn.getStaticIP()->gateway->toString().c_str():(char*)""));
    json_gen_obj_set_string(&jstr, (char*)"dns1", (AppConn.getStaticIP()->dns1?AppConn.getStaticIP()->dns1->toString().c_str():(char*)""));
    json_gen_obj_set_string(&jstr, (char*)"dns2", (AppConn.getStaticIP()->dns2?AppConn.getStaticIP()->dns2->toString().c_str():(char*)""));

    json_gen_obj_set_string(&jstr, (char*)"ap_ip", (AppConn.getAPIP()->ip?AppConn.getAPIP()->ip->toString().c_str():(char*)""));
    json_gen_obj_set_string(&jstr, (char*)"ap_subnet", (AppConn.getAPIP()->netmask?AppConn.getAPIP()->netmask->toString().c_str():(char*)""));

    json_gen_obj_set_int(&jstr, (char*)"ap_channel", AppConn.getAPChannel());
    json_gen_obj_set_int(&jstr, (char*)"ap_dhcp", AppConn.getAPDHCP());

    json_gen_obj_set_string(&jstr, (char*)"mdns_name", AppConn.getMDNSname());
    json_gen_obj_set_int(&jstr, (char*)"port", AppConn.getPort());

    json_gen_obj_set_string(&jstr, (char*)"user", AppConn.getUser());

    byte mac[6];
    WiFi.macAddress(mac);
    char mac_buf[18];
    snprintf(mac_buf, sizeof(mac_buf), "%02X:%02X:%02X:%02X:%02X:%02X", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    json_gen_obj_set_string(&jstr, (char*)"mac_address", mac_buf);

    AppConn.updateTimeStr();
    json_gen_obj_set_string(&jstr, (char*)"local_time", AppConn.getLocalTimeStr());
    json_gen_obj_set_string(&jstr, (char*)"up_time", AppConn.getUpTimeStr());
    json_gen_obj_set_string(&jstr, (char*)"ntp_server", AppConn.getNTPServer());
    json_gen_obj_set_int(&jstr, (char*)"gmt_offset", AppConn.getGmtOffset_sec());
    json_gen_obj_set_int(&jstr, (char*)"dst_offset", AppConn.getDaylightOffset_sec());
    
    json_gen_obj_set_int(&jstr, (char*)"active_streams", AppHttpd.getStreamCount());
    json_gen_obj_set_int(&jstr, (char*)"prev_streams", AppHttpd.getStreamsServed());
    json_gen_obj_set_int(&jstr, (char*)"img_captured", AppHttpd.getImagesServed());

    json_gen_obj_set_int(&jstr, (char*)"ota_enabled", AppConn.isOTAEnabled());

    json_gen_obj_set_int(&jstr, (char*)"cpu_freq", ESP.getCpuFreqMHz());
    json_gen_obj_set_int(&jstr, (char*)"num_cores", ESP.getChipCores());
    json_gen_obj_set_int(&jstr, (char*)"esp_temp", getTemp()); // Celsius
    json_gen_obj_set_int(&jstr, (char*)"heap_avail", ESP.getHeapSize());
    json_gen_obj_set_int(&jstr, (char*)"heap_free", ESP.getFreeHeap());
    json_gen_obj_set_int(&jstr, (char*)"heap_min_free", ESP.getMinFreeHeap());
    json_gen_obj_set_int(&jstr, (char*)"heap_max_bloc", ESP.getMaxAllocHeap());

    json_gen_obj_set_bool(&jstr, (char*)"psram_found", psramFound());
    json_gen_obj_set_int(&jstr, (char*)"psram_size", (psramFound()?ESP.getPsramSize():0));
    json_gen_obj_set_int(&jstr, (char*)"psram_free", (psramFound()?ESP.getFreePsram():0));
    json_gen_obj_set_int(&jstr, (char*)"psram_min_free", (psramFound()?ESP.getMinFreePsram():0));
    json_gen_obj_set_int(&jstr, (char*)"psram_max_bloc", (psramFound()?ESP.getMaxAllocPsram():0));

    json_gen_obj_set_int(&jstr, (char*)"xclk", AppCam.getXclk());

    json_gen_obj_set_int(&jstr, (char*)"storage_size", Storage.getSize());
    json_gen_obj_set_int(&jstr, (char*)"storage_used", Storage.getUsed());
    json_gen_obj_set_string(&jstr, (char*)"storage_units", (Storage.capacityUnits()==STORAGE_UNITS_MB?(char*)"MB":(char*)""));

    json_gen_obj_set_string(&jstr, (char*)"serial_buf", getSerialBuffer());

    json_gen_end_object(&jstr);
    json_gen_str_end(&jstr);
}

void CLAppHttpd::serialSendCommand(const char *cmd) {
    Serial.print("^");
    Serial.println(cmd);
}

int CLAppHttpd::loadPrefs() {
    jparse_ctx_t jctx;
    int ret  = parsePrefs(&jctx);
    if(ret != OS_SUCCESS) {
        return ret;
    }
    
    json_obj_get_int(&jctx, (char*)"lamp", &_lampVal);
    json_obj_get_bool(&jctx, (char*)"autolamp", &_autoLamp);
    json_obj_get_int(&jctx, (char*)"flashlamp", &_flashLamp);
    json_obj_get_int(&jctx, (char*)"max_streams", &_max_streams);

    int count = 0, pin = 0, freq = 0, resolution = 0, def_val = 0;

    if(json_obj_get_array(&jctx, (char*)"pwm", &count) == OS_SUCCESS) {

        for(int i=0; i < count && i < NUM_PWM; i++) 
            if(json_arr_get_object(&jctx, i) == OS_SUCCESS) {
                if(json_obj_get_int(&jctx, (char*)"pin", &pin) == OS_SUCCESS &&
                    json_obj_get_int(&jctx, (char*)"frequency", &freq) == OS_SUCCESS &&
                    json_obj_get_int(&jctx, (char*)"resolution", &resolution) == OS_SUCCESS) {
                    int index = attachPWM(pin, freq, resolution);
                    delay(75); // let the PWM settle
                    if(index >= 0) {
                        if(_lampVal >= 0 && i == 0) {
                            _lamppin = pin;
                            _pwmMax = pow(2, resolution)-1;
                            ESP_LOGI(tag,"Flash lamp activated on pin %d", _lamppin);
                        }

                        if(json_obj_get_int(&jctx, (char*)"default", &def_val) == OS_SUCCESS)  {
                            pwm[index]->setDefaultDuty(def_val);
                            pwm[index]->reset();
                        }
                    }
                    else
                        ESP_LOGW(tag,"Failed to attach PWM to pin %d", pin);
                }
                json_arr_leave_object(&jctx);
            }

        json_obj_leave_array(&jctx);
    }
    

    if (json_obj_get_array(&jctx, (char*)"mapping", &_mappingCount) == OS_SUCCESS) {

        for(int i=0; i < _mappingCount && i < MAX_URI_MAPPINGS; i++) {
            if(json_arr_get_object(&jctx, i) == OS_SUCCESS) {
                UriMapping *um = (UriMapping*) malloc(sizeof(UriMapping));
                if(json_obj_get_string(&jctx, (char*)"uri", um->uri, sizeof(um->uri)) == OS_SUCCESS &&
                    json_obj_get_string(&jctx, (char*)"path", um->path, sizeof(um->path)) == OS_SUCCESS ) {
                    mappingList[i] = um;
                } 
                else {
                    free(um);
                }
                json_arr_leave_object(&jctx);
            }    
        }    
        json_obj_leave_array(&jctx);
    }

    json_obj_get_string(&jctx, (char*)"my_name", myName, sizeof(myName));

    return ret;
}

int CLAppHttpd::savePrefs() {
    char * prefs_file = getPrefsFileName(true); 
    char buf[1024];
    json_gen_str_t jstr;
    json_gen_str_start(&jstr, buf, sizeof(buf), NULL, NULL);
    json_gen_start_object(&jstr);
    
    json_gen_obj_set_string(&jstr, (char*)"my_name", myName);

    json_gen_obj_set_int(&jstr, (char*)"lamp", _lampVal);
    json_gen_obj_set_bool(&jstr, (char*)"autolamp", _autoLamp);
    json_gen_obj_set_int(&jstr, (char*)"flashlamp", _flashLamp);
    json_gen_obj_set_int(&jstr, (char*)"max_streams", _max_streams);

    if(_pwmCount > 0) {
        json_gen_push_array(&jstr, (char*)"pwm");
        for(int i=0; i < _pwmCount; i++) 
            if(pwm[i]) {
                json_gen_start_object(&jstr);
                json_gen_obj_set_int(&jstr, (char*)"pin", pwm[i]->getPin());
                json_gen_obj_set_int(&jstr, (char*)"frequency", pwm[i]->getFreq());
                json_gen_obj_set_int(&jstr, (char*)"resolution", pwm[i]->getResolutionBits());
                if(pwm[i]->getDefaultDuty())
                    json_gen_obj_set_int(&jstr, (char*)"default", pwm[i]->getDefaultDuty());
                json_gen_end_object(&jstr); 
            }
        
        json_gen_pop_array(&jstr);
    }

    if(_mappingCount > 0) {
        json_gen_push_array(&jstr, (char*)"mapping");
        for(int i=0; i < _mappingCount; i++) {
            json_gen_start_object(&jstr);
            json_gen_obj_set_string(&jstr, (char*)"uri", mappingList[i]->uri);
            json_gen_obj_set_string(&jstr, (char*)"path", mappingList[i]->path);
            json_gen_end_object(&jstr); 
        }
        json_gen_pop_array(&jstr);
    }

    json_gen_end_object(&jstr);
    json_gen_str_end(&jstr);

    File file = Storage.open(prefs_file, FILE_WRITE);
    if(file) {
        file.print(buf);
        file.close();
        return OK;
    }
    else {
        ESP_LOGW(tag,"Failed to save web server preferences to file %s", prefs_file);
        return FAIL;
    }
}

int CLAppHttpd::attachPWM(uint8_t pin, double freq, uint8_t resolution_bits) {

    if(_pwmCount >= NUM_PWM) {
        ESP_LOGW(tag,"Number of available PWM channels exceeded");
        return OS_FAIL;
    }

    for(int i=0; i<_pwmCount; i++) 
        if(pwm[i]->getPin() == pin) {
            ESP_LOGW(tag,"Pin %d already utilized");
            return OS_FAIL; // pin already used
        }

    ESP32PWM * newpwm = new ESP32PWM();
    if(!newpwm) {
        ESP_LOGW(tag,"Failed to create PWM"); 
        delete newpwm;
        return OS_FAIL;
    }
    
    newpwm->attachPin(pin, freq, resolution_bits);

    if(!newpwm->attached()) {
        ESP_LOGW(tag,"Failed to attach PWM on pin %d", pin);
        delete newpwm;
        return OS_FAIL;
    }

    ESP_LOGI(tag,"Created a new PWM channel %d on pin %d (freq=%.2f, bits=%d)", 
        newpwm->getChannel(), pin, freq, resolution_bits);

    pwm[_pwmCount] = newpwm;

    _pwmCount++;

    return _pwmCount - 1; 
}

int CLAppHttpd::writePWM(uint8_t pin, int value, int min_v, int max_v) {
    for(int i=0; i<_pwmCount; i++) {
        if(pwm[i] && pwm[i]->getPin() == pin) {
            if(pwm[i]->attached()) {
                if(min_v > 0) {
                    // treat values less than MIN_PULSE_WIDTH (500) as angles in degrees 
                    // (valid values in microseconds are handled as microseconds)
                    if (value < MIN_PULSE_WIDTH)
                    {
                        if (value < 0)
                            value = 0;
                        else if (value > 180)
                            value = 180;

                        value = map(value, 0, 180, min_v, max_v);

                    }
                    if (value < min_v)          // ensure pulse width is valid
                        value = min_v;
                    else if (value > max_v)
                        value = max_v;

                    value = pwm[i]->usToTicks(value);  // convert to ticks

                }
                // do the actual write
                ESP_LOGD(tag,"Write %d to PWM channel %d pin %d min %d max %d", 
                         value, pwm[i]->getChannel(), pwm[i]->getPin(), min_v, max_v);
                pwm[i]->write(value);
                return OS_SUCCESS;
            }
            else {
                ESP_LOGW(tag,"PWM write failed: pin %d is not attached", pin);
                return OS_FAIL;    
            }
        }
    }
    
    ESP_LOGW(tag,"PWM write failed: pin %d is not found", pin);
    return OS_FAIL;
}

void CLAppHttpd::resetPWM(uint8_t pin) {
    for(int i=0; i<_pwmCount; i++) {
        if(pwm[i]->getPin() == pin || pin == RESET_ALL_PWM)
            pwm[i]->reset();
    }
}


// Lamp Control
void CLAppHttpd::setLamp(int newVal) {

    if(newVal == DEFAULT_FLASH) {
        newVal = _flashLamp;
    }
    _lampVal = newVal;
    
    // Apply a logarithmic function to the scale.
    if(_lamppin) {
        int brightness = round(_lampVal * _pwmMax/100.00);
        writePWM(_lamppin, brightness,0);
    }

}

int CLAppHttpd::addStreamClient(uint32_t client_id) {
    for(int i=0; i < _max_streams; i++) {
        if(!stream_clients[i]) {
            stream_clients[i] = client_id;
            return OS_SUCCESS;
        }
    }
    return OS_FAIL;
}

int CLAppHttpd::removeStreamClient(uint32_t client_id) {
    for(int i=0; i < _max_streams; i++) {
        if(stream_clients[i] ==  client_id) {
            stream_clients[i] = 0;
            return OS_SUCCESS;
        }    
    }
    return OS_FAIL;
}

void CLAppHttpd::cleanupWsClients() {
    if(ws) ws->cleanupClients();
}

CLAppHttpd AppHttpd;
