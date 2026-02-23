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

int IRAM_ATTR bcastBufImgCallback(uint8_t* buffer, size_t size) {
    return AppHttpd.bcastBufImg(buffer, size);
}

void IRAM_ATTR onSnapTimer(TimerHandle_t pxTimer){
    AppCam.snapFrame(bcastBufImgCallback);
}

int IRAM_ATTR CLAppHttpd::bcastBufImg(uint8_t* buffer, size_t size) {
    return ws->binaryAll(buffer, size) != 
           AsyncWebSocket::SendStatus::DISCARDED?OK:FAIL;;
}

int CLAppHttpd::start() {
    
    loadPrefs();

    server = new AsyncWebServer(AppConn.getHTTPPort());
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
            AppPwm.reset(RESET_ALL_PWM);
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
                            AppPwm.write(pin, value); // write to servo
                        else
                            AppPwm.write(pin, value, 0); // write to raw PWM
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

StreamResponseEnum CLAppHttpd::startStream(uint32_t id, CaptureModeEnum streammode) {
    
    // if video stream requested, check if we can add extra
    if(streammode == CAPTURE_STREAM) {
        if(_streamCount+1 > _max_streams) return STREAM_NUM_EXCEEDED;
        if(addStreamClient(id) != OK) return STREAM_CLIENT_REGISTER_FAILED;
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
        
            if (AppCam.snapStillImage(bcastBufImgCallback) != OK) {
                return STREAM_IMAGE_CAPTURE_FAILED;
            }
            
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

    if(removeStreamClient(id) != OK) return STREAM_CLIENT_NOT_FOUND;

    if(!_stream_timer) return STREAM_TIMER_NOT_INITIALIZED;
    
    // if the stream is the last one active, stop the timer
    if(xTimerIsTimerActive(_stream_timer) != pdFALSE && _streamCount == 1) {
        vTimerSetReloadMode(_stream_timer, pdFALSE);
        if(xTimerStop(_stream_timer, 0) == pdPASS)
            ESP_LOGI(tag,"Stop sent to Stream timer");
        else
            ESP_LOGW(tag,"Failed to post the stop command to the Stream timer!");

        if(AppCam.getLamp()>0 and AppCam.isAutoLamp()) AppCam.setLamp(0);     
    }
    
    _streamsServed++;
    _streamCount--;
    
    ESP_LOGI(tag,"Stream stopped");
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
        if(res == OK)
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

        if(res == OK)
            request->send(200);
        else
            request->send(500);
        return;
    }
    else if(variable == "reboot") {
        request->send(200);
        if (AppCam.getLamp() != -1) AppCam.setLamp(0); // kill the lamp; otherwise it can remain on during the soft-reboot
        Storage.getFS().end();      // close file storage
        resetI2CBus();
        scheduleReboot(3);
    }
    else if(variable == FPSTR(CONN_SSID)) {AppConn.setSSID(value.c_str());AppConn.setPassword("");}
    else if(variable == FPSTR(CONN_PASSWORD)) AppConn.setPassword(value.c_str());
    else if(variable == FPSTR(CONN_ST_IP)) AppConn.setStaticIP(&(AppConn.getStaticIP()->ip), value.c_str());
    else if(variable == FPSTR(CONN_ST_SUBNET)) AppConn.setStaticIP(&(AppConn.getStaticIP()->netmask), value.c_str());
    else if(variable == FPSTR(CONN_ST_GATEWAY)) AppConn.setStaticIP(&(AppConn.getStaticIP()->gateway), value.c_str());
    else if(variable == FPSTR(CONN_DNS1)) AppConn.setStaticIP(&(AppConn.getStaticIP()->dns1), value.c_str());
    else if(variable == FPSTR(CONN_DNS2)) AppConn.setStaticIP(&(AppConn.getStaticIP()->dns2), value.c_str());
    else if(variable == FPSTR(CONN_AP_IP)) AppConn.setStaticIP(&(AppConn.getAPIP()->ip), value.c_str());
    else if(variable == FPSTR(CONN_AP_SUBNET)) AppConn.setStaticIP(&(AppConn.getAPIP()->netmask), value.c_str());
    else if(variable == FPSTR(CONN_AP_SSID)) AppConn.setApName(value.c_str());
    else if(variable == FPSTR(CONN_AP_PASS)) AppConn.setApPass(value.c_str());
    else if(variable == FPSTR(CONN_MDNS_NAME)) AppConn.setMDNSName(value.c_str());
    else if(variable == FPSTR(CONN_NTP_SERVER)) AppConn.setNTPServer(value.c_str());
    else if(variable == FPSTR(CONN_USER)) AppConn.setUser(value.c_str());
    else if(variable == FPSTR(CONN_PWD)) AppConn.setPwd(value.c_str());
    else if(variable == FPSTR(CONN_OTA_PASSWORD)) AppConn.setOTAPassword(value.c_str());
    else if(variable == FPSTR(CAM_FRAMESIZE)) {
        if(s->pixformat == PIXFORMAT_JPEG) res = s->set_framesize(s, (framesize_t)val);
    }
    else if(variable == FPSTR(CAM_QUALITY)) res = s->set_quality(s, val);
    else if(variable == FPSTR(CAM_XCLK)) { AppCam.setXclk(val); res = s->set_xclk(s, LEDC_TIMER_0, AppCam.getXclk()); }
    else if(variable == FPSTR(CAM_CONTRAST)) res = s->set_contrast(s, val);
    else if(variable == FPSTR(CAM_BRIGHTNESS)) res = s->set_brightness(s, val);
    else if(variable == FPSTR(CAM_SATURATION)) res = s->set_saturation(s, val);
    else if(variable == FPSTR(CAM_SHARPNESS)) res = s->set_sharpness(s, val);
    else if(variable == FPSTR(CAM_DENOISE)) res = s->set_denoise(s, val);
    else if(variable == FPSTR(CAM_GAINCEILING)) res = s->set_gainceiling(s, (gainceiling_t)val);
    else if(variable == FPSTR(CAM_COLORBAR)) res = s->set_colorbar(s, val);
    else if(variable == FPSTR(CAM_AWB)) res = s->set_whitebal(s, val);
    else if(variable == FPSTR(CAM_AGC)) res = s->set_gain_ctrl(s, val);
    else if(variable == FPSTR(CAM_AEC)) res = s->set_exposure_ctrl(s, val);
    else if(variable == FPSTR(CAM_HMIRROR)) res = s->set_hmirror(s, val);
    else if(variable == FPSTR(CAM_VFLIP)) res = s->set_vflip(s, val);
    else if(variable == FPSTR(CAM_AWB_GAIN)) res = s->set_awb_gain(s, val);
    else if(variable == FPSTR(CAM_AGC_GAIN)) res = s->set_agc_gain(s, val);
    else if(variable == FPSTR(CAM_AEC_VALUE)) res = s->set_aec_value(s, val);
    else if(variable == FPSTR(CAM_AEC2)) res = s->set_aec2(s, val);
    else if(variable == FPSTR(CAM_DCW)) res = s->set_dcw(s, val);
    else if(variable == FPSTR(CAM_BPC)) res = s->set_bpc(s, val);
    else if(variable == FPSTR(CAM_WPC)) res = s->set_wpc(s, val);
    else if(variable == FPSTR(CAM_RAW_GMA)) res = s->set_raw_gma(s, val);
    else if(variable == FPSTR(CAM_LENC)) res = s->set_lenc(s, val);
    else if(variable == FPSTR(CAM_SPECIAL_EFFECT)) res = s->set_special_effect(s, val);
    else if(variable == FPSTR(CAM_WB_MODE)) res = s->set_wb_mode(s, val);
    else if(variable == FPSTR(CAM_AE_LEVEL)) res = s->set_ae_level(s, val);
    else if(variable == FPSTR(CAM_ROTATE)) AppCam.setRotation(val);
    else if(variable == FPSTR(CAM_FRAME_RATE)) {
        AppCam.setFrameRate(val);
        AppHttpd.setFrameRate(val);
    }
    else if(variable ==  FPSTR(CAM_AUTOLAMP) && AppCam.getLamp() != -1) {
        AppCam.setAutoLamp(val);
    }
    else if(variable ==  FPSTR(CAM_LAMP) && AppCam.getLamp() != -1) {
        AppCam.setLamp(constrain(val,0,100));
    }
    else if(variable ==  FPSTR(CAM_FLASHLAMP) && AppCam.getLamp() != -1) {
        AppCam.setFlashLamp(constrain(val,0,100));
    }
    else if(variable == FPSTR(CONN_LOAD_AS_AP)) AppConn.setLoadAsAP(val);
    else if(variable == FPSTR(CONN_AP_TIMEOUT)) AppConn.setAPTimeout(val);
    else if(variable == FPSTR(CONN_AP_CHANNEL)) AppConn.setAPChannel(val);
    else if(variable == FPSTR(CONN_AP_DHCP)) AppConn.setAPDHCP(val);
    else if(variable == FPSTR(CONN_DHCP)) AppConn.setDHCPEnabled(val);
    else if(variable == FPSTR(CONN_HTTP_PORT)) AppConn.setHTTPPort(val);
    else if(variable == FPSTR(CONN_OTA_ENABLED)) AppConn.setOTAEnabled(val);
    else if(variable == FPSTR(CONN_GMT_OFFSET)) AppConn.setGmtOffset_sec(val);
    else if(variable == FPSTR(CONN_DST_OFFSET)) AppConn.setDaylightOffset_sec(val);
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

    JsonDocument jdoc;
    JsonObject jstr = jdoc.to<JsonObject>();

    AppHttpd.dumpCameraStatusToJson(jstr, false);

    String output;
    serializeJson(jstr, output);

    response->print(output);
    request->send(response); 
}

void onStatus(AsyncWebServerRequest *request) {
    AsyncResponseStream *response = request->beginResponseStream("application/json");
    // Do not get attempt to get sensor when in error; causes a panic..

    JsonDocument jdoc;
    JsonObject jstr = jdoc.to<JsonObject>();

    AppHttpd.dumpCameraStatusToJson(jstr);
    
    String output;
    serializeJson(jstr, output);

    response->print(output);
    request->send(response);
}

void onSystemStatus(AsyncWebServerRequest *request) {
    AsyncResponseStream *response = request->beginResponseStream("application/json");

    JsonDocument jdoc;
    JsonObject jstr = jdoc.to<JsonObject>();

    AppHttpd.dumpSystemStatusToJson(jstr);

    String output;
    serializeJson(jstr, output);
    response->print(output);

#if (CONFIG_LOG_DEFAULT_LEVEL >= CORE_DEBUG_LEVEL )
    Serial.println();
    Serial.println("Dump requested through web");
    Serial.println(buf);
#endif

    request->send(response);
}

void CLAppHttpd::dumpCameraStatusToJson(JsonObject jstr, bool full_status) {

    jstr[FPSTR(CAM_PNAME)] = getName();
    // jstr["stream_url"] = AppConn.getStreamUrl();
    AppConn.updateTimeStr();
    jstr[FPSTR(CONN_LOCAL_TIME)] = AppConn.getLocalTimeStr();
    jstr[FPSTR(CONN_UP_TIME)] = AppConn.getUpTimeStr();
    jstr[FPSTR(CONN_RSSI)] = (!AppConn.isAccessPoint()?WiFi.RSSI():(uint8_t)0);
    jstr[FPSTR(ESP_TEMP_PARAM)] = getTemp();
    jstr[FPSTR(HTTPD_SERIAL_BUF)] = getSerialBuffer();  

    AppCam.dumpStatusToJson(jstr, full_status);

    if(full_status) {
        jstr[FPSTR(APP_CODE_VERSION_PARAM)] = getVersion();
    }

}

void CLAppHttpd::dumpSystemStatusToJson(JsonObject jstr) {

    jstr[FPSTR(CAM_PNAME)] = getName();
    jstr[FPSTR(APP_CODE_VERSION_PARAM)] = getVersion();
    jstr[FPSTR(APP_BASE_VERSION_PARAM)] = BASE_VERSION;
    jstr[FPSTR(APP_SKETCH_SIZE_PARAM)] = getSketchSize();
    jstr[FPSTR(APP_SKETCH_SPACE_PARAM)] = getSketchSpace();
    jstr[FPSTR(APP_SKETCH_MD5_PARAM)] = getSketchMD5();
    jstr[FPSTR(ESP_SDK_VERSION_PARAM)] = ESP.getSdkVersion();

    jstr[FPSTR(CONN_LOAD_AS_AP)] = AppConn.isAccessPoint();
    jstr[FPSTR(CONN_AP_TIMEOUT)] = AppConn.getAPTimeout();
    jstr[FPSTR(CONN_CAPTIVE_PORTAL)] = AppConn.isCaptivePortal();
    jstr[FPSTR(CONN_AP_NAME)] = AppConn.getApName();
    jstr[FPSTR(CONN_SSID)] = AppConn.getSSID();

    jstr[FPSTR(CONN_RSSI)] = (!AppConn.isAccessPoint()?WiFi.RSSI():(uint8_t)0);
    jstr[FPSTR(CONN_BSSID)] = (!AppConn.isAccessPoint()?WiFi.BSSIDstr().c_str():(char*)"");
    jstr[FPSTR(CONN_DHCP)] = AppConn.isDHCPEnabled();
    jstr[FPSTR(CONN_IP_ADDRESS)] = (AppConn.isAccessPoint()?WiFi.softAPIP().toString().c_str():WiFi.localIP().toString().c_str());
    jstr[FPSTR(CONN_SUBNET)] = (!AppConn.isAccessPoint()?WiFi.subnetMask().toString().c_str():(char*)"");
    jstr[FPSTR(CONN_GATEWAY)] = (!AppConn.isAccessPoint()?WiFi.gatewayIP().toString().c_str():(char*)"");

    jstr[FPSTR(CONN_ST_IP)] = (AppConn.getStaticIP()->ip?AppConn.getStaticIP()->ip->toString().c_str():(char*)"");
    jstr[FPSTR(CONN_ST_SUBNET)] = (AppConn.getStaticIP()->netmask?AppConn.getStaticIP()->netmask->toString().c_str():(char*)"");
    jstr[FPSTR(CONN_ST_GATEWAY)] = (AppConn.getStaticIP()->gateway?AppConn.getStaticIP()->gateway->toString().c_str():(char*)"");
    jstr[FPSTR(CONN_DNS1)] = (AppConn.getStaticIP()->dns1?AppConn.getStaticIP()->dns1->toString().c_str():(char*)"");
    jstr[FPSTR(CONN_DNS2)] = (AppConn.getStaticIP()->dns2?AppConn.getStaticIP()->dns2->toString().c_str():(char*)"");

    jstr[FPSTR(CONN_AP_IP)] = (AppConn.getAPIP()->ip?AppConn.getAPIP()->ip->toString().c_str():(char*)"");
    jstr[FPSTR(CONN_AP_SUBNET)] = (AppConn.getAPIP()->netmask?AppConn.getAPIP()->netmask->toString().c_str():(char*)"");

    jstr[FPSTR(CONN_AP_CHANNEL)] = AppConn.getAPChannel();
    jstr[FPSTR(CONN_AP_DHCP)] = AppConn.isAPDHCP();

    jstr[FPSTR(CONN_MDNS_NAME)] = AppConn.getMDNSname();
    jstr[FPSTR(CONN_HTTP_PORT)] = AppConn.getHTTPPort();

    jstr[FPSTR(CONN_USER)] = AppConn.getUser();

    byte mac[6];
    WiFi.macAddress(mac);
    char mac_buf[18];
    snprintf(mac_buf, sizeof(mac_buf), "%02X:%02X:%02X:%02X:%02X:%02X", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    jstr[FPSTR(CONN_MAC_ADDRESS)] = mac_buf;

    AppConn.updateTimeStr();

    jstr[FPSTR(CONN_LOCAL_TIME)] = AppConn.getLocalTimeStr();
    jstr[FPSTR(CONN_UP_TIME)] = AppConn.getUpTimeStr();
    jstr[FPSTR(CONN_NTP_SERVER)] = AppConn.getNTPServer();
    jstr[FPSTR(CONN_GMT_OFFSET)] = AppConn.getGmtOffset_sec();
    jstr[FPSTR(CONN_DST_OFFSET)] = AppConn.getDaylightOffset_sec();
    
    jstr[FPSTR(HTTPD_ACTIVE_STREAMS)] = AppHttpd.getStreamCount();
    jstr[FPSTR(HTTPD_STREAMS_SERVED)] = AppHttpd.getStreamsServed();
    jstr[FPSTR(HTTPD_IMAGES_SERVED)] = AppCam.getImagesServed();

    jstr[FPSTR(CONN_OTA_ENABLED)] = AppConn.isOTAEnabled();

    jstr[FPSTR(ESP_CPU_FREQ_PARAM)] = ESP.getCpuFreqMHz();
    jstr[FPSTR(ESP_NUM_CORES_PARAM)] = ESP.getChipCores();
    jstr[FPSTR(ESP_TEMP_PARAM)] = getTemp(); // Celsius
    jstr[FPSTR(ESP_HEAP_AVAIL_PARAM)] = ESP.getHeapSize();
    jstr[FPSTR(ESP_HEAP_FREE_PARAM)] = ESP.getFreeHeap();
    jstr[FPSTR(ESP_HEAP_MIN_FREE_PARAM)] = ESP.getMinFreeHeap();
    jstr[FPSTR(ESP_HEAP_MAX_BLOC_PARAM)] = ESP.getMaxAllocHeap();

    jstr[FPSTR(ESP_PSRAM_FOUND_PARAM)] = psramFound();
    jstr[FPSTR(ESP_PSRAM_SIZE_PARAM)] = (psramFound()?ESP.getPsramSize():0);
    jstr[FPSTR(ESP_PSRAM_FREE_PARAM)] = (psramFound()?ESP.getFreePsram():0);
    jstr[FPSTR(ESP_PSRAM_MIN_FREE_PARAM)] = (psramFound()?ESP.getMinFreePsram():0);
    jstr[FPSTR(ESP_PSRAM_MAX_BLOC_PARAM)] = (psramFound()?ESP.getMaxAllocPsram():0);

    jstr[FPSTR(CAM_XCLK)] = AppCam.getXclk();

    jstr[FPSTR(STORAGE_SIZE)] = Storage.getSize();
    jstr[FPSTR(STORAGE_USED)] = Storage.getUsed();
    jstr[FPSTR(STORAGE_UNITS_STR)] = (Storage.capacityUnits()==STORAGE_UNITS_MB?"MB":"");

    jstr[FPSTR(HTTPD_SERIAL_BUF)] = getSerialBuffer();

#ifdef ENABLE_MAIL_FEATURE
    jstr[FPSTR(MAIL_SMTP_SERVER)] = AppMailSender.getSMTPServer();
    jstr[FPSTR(MAIL_SMTP_PORT)] = AppMailSender.getSMTPPort();
    jstr[FPSTR(MAIL_FROM)] = AppMailSender.getFromEmail();
    jstr[FPSTR(MAIL_TO)] = AppMailSender.getToEmail();
    jstr[FPSTR(MAIL_SNAPONSTART)] = AppMailSender.isSnapOnStart()?"On":"Off";
    jstr[FPSTR(MAIL_SLEEPONCOMPLETE)] = AppMailSender.isSleepOnComplete()?"On":"Off";
    jstr[FPSTR(MAIL_PERIOD)] = AppMailSender.getSecondsTillFire();
#endif

}

void CLAppHttpd::serialSendCommand(const char *cmd) {
#ifdef ENABLE_SERIAL_COMMANDS
    Serial.print("^");
    Serial.println(cmd);
#endif
}

int CLAppHttpd::loadPrefs() {
    JsonDocument doc;
    int ret  = parsePrefs(&doc);
    if(ret != OK) {
        return ret;
    }
    
    _max_streams = doc[FPSTR(HTTPD_MAX_STREAMS)] | 2;

    JsonArray jaMapping = doc[FPSTR(HTTPD_MAPPING)].as<JsonArray>();

    for(JsonObject joMap : jaMapping) {
        UriMapping *um = (UriMapping*) malloc(sizeof(UriMapping));
        if(snprintf(um->uri, sizeof(um->uri), "%s", joMap[FPSTR(HTTPD_URI)] | "") > 0 &&
           snprintf(um->path, sizeof(um->path), "%s", joMap[FPSTR(HTTPD_PATH)] | "") > 0) {
            mappingList[_mappingCount] = um;
            ESP_LOGD(tag,"Added URI mapping: %s -> %s", um->uri, um->path);
            _mappingCount++;
            if(_mappingCount >= MAX_URI_MAPPINGS) break;
        } 
        else {
            ESP_LOGW(tag,"Failed to parse URI mapping for uri %s and path %s", joMap[FPSTR(HTTPD_URI)] | "", joMap[FPSTR(HTTPD_PATH)] | "");
            free(um);
        }
    }

    snprintf(myName, sizeof(myName), "%s", doc["my_name"] | "ESP32Cam");

    return ret;
}

int CLAppHttpd::savePrefs() {

    JsonDocument doc;
    JsonObject jstr = doc.to<JsonObject>();
    
    jstr["my_name"] = myName;

    jstr[FPSTR(HTTPD_MAX_STREAMS)] = _max_streams;

    if(_mappingCount > 0) {
        JsonArray jaMapping = jstr[FPSTR(HTTPD_MAPPING)].to<JsonArray>();

        for(int i=0; i < _mappingCount; i++) {
            JsonObject objMap = jaMapping.add<JsonObject>();
            objMap[FPSTR(HTTPD_URI)] = mappingList[i]->uri;
            objMap[FPSTR(HTTPD_PATH)] = mappingList[i]->path;
        }
    }

    return savePrefsToFile(&doc);
}


int CLAppHttpd::addStreamClient(uint32_t client_id) {
    for(int i=0; i < _max_streams; i++) {
        if(!stream_clients[i]) {
            stream_clients[i] = client_id;
            return OK;
        }
    }
    return FAIL;
}

int CLAppHttpd::removeStreamClient(uint32_t client_id) {
    for(int i=0; i < _max_streams; i++) {
        if(stream_clients[i] ==  client_id) {
            stream_clients[i] = 0;
            return OK;
        }    
    }
    return FAIL;
}

void CLAppHttpd::cleanupWsClients() {
    if(ws) ws->cleanupClients();
}

CLAppHttpd AppHttpd;
