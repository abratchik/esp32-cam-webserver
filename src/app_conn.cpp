#include "app_conn.h"

CLAppConn::CLAppConn() {
    setTag("conn");
}

int CLAppConn::start() {

    if(loadPrefs() != OK) {
        return WiFi.status();
    }
    
    ESP_LOGI(tag,"Starting WiFi");

    WiFi.setHostname(mdnsName);
    
    WiFi.mode(WIFI_STA); 

    // Disable power saving on WiFi to improve responsiveness
    // (https://github.com/espressif/arduino-esp32/issues/1484)
    WiFi.setSleep(false);
    
    byte mac[6] = {0,0,0,0,0,0};
    WiFi.macAddress(mac);
    ESP_LOGI(tag,"MAC address: %02X:%02X:%02X:%02X:%02X:%02X", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

    int bestStation = -1;
    long bestRSSI = -1024;
    char bestSSID[65] = "";
    uint8_t bestBSSID[6];

    accesspoint = load_as_ap;

    if (!accesspoint) {
        if(stationCount > 0) {
            // We have a list to scan
            ESP_LOGI(tag,"Scanning local Wifi Networks");
            int stationsFound = WiFi.scanNetworks();
            ESP_LOGI(tag,"%i networks found", stationsFound);
            if (stationsFound > 0) {
                for (int i = 0; i < stationsFound; ++i) {
                    // Print SSID and RSSI for each network found
                    String thisSSID = WiFi.SSID(i);
                    int thisRSSI = WiFi.RSSI(i);
                    String thisBSSID = WiFi.BSSIDstr(i);
                    Serial.printf("%3i : [%s] %s (%i)", i + 1, thisBSSID.c_str(), thisSSID.c_str(), thisRSSI);
                    // Scan our list of known external stations
                    for (int sta = 0; sta < stationCount; sta++) {
                        if ((strcmp(stationList[sta]->ssid, thisSSID.c_str()) == 0) ||
                        (strcmp(stationList[sta]->ssid, thisBSSID.c_str()) == 0)) {
                            Serial.print("  -  Known!");
                            // Chose the strongest RSSI seen
                            if (thisRSSI > bestRSSI) {
                                bestStation = sta;
                                strncpy(bestSSID, thisSSID.c_str(), 64);
                                // Convert char bssid[] to a byte array
                                parseBytes(thisBSSID.c_str(), ':', bestBSSID, 6, 16);
                                bestRSSI = thisRSSI;
                            }
                        }
                    }
                    Serial.println();
                }
            }
        } 

        if (bestStation == -1 ) {
            ESP_LOGW(tag,"No known networks found, entering AccessPoint fallback mode");
            accesspoint = true;
        } 
        else {
            ESP_LOGI(tag,"Connecting to Wifi Network %d: [%02X:%02X:%02X:%02X:%02X:%02X] %s \r\n",
                        bestStation, bestBSSID[0], bestBSSID[1], bestBSSID[2], bestBSSID[3],
                        bestBSSID[4], bestBSSID[5], bestSSID);
            // Apply static settings if necesscary
            if (dhcp == false) {

                if(staticIP.ip && staticIP.gateway  && staticIP.netmask) {
                    ESP_LOGI(tag,"Applying static IP settings");
                    dhcp = false;
                    WiFi.config(*staticIP.ip, *staticIP.gateway, *staticIP.netmask, *staticIP.dns1, *staticIP.dns2);
                }
                else {
                    dhcp = true;
                    ESP_LOGW(tag,"Static IP settings requested but not defined properly in config, falling back to dhcp");
                }    
            }

            // WiFi.setHostname(mdnsName);

            // Initiate network connection request (3rd argument, channel = 0 is 'auto')
            WiFi.begin(bestSSID, stationList[bestStation]->password, 0, bestBSSID);

            // Wait to connect, or timeout
            unsigned long start = millis();
            while ((millis() - start <= WIFI_WATCHDOG) && (WiFi.status() != WL_CONNECTED)) {
                delay(500);
                Serial.print('.');
            }
            // If we have connected, inform user
            if (WiFi.status() == WL_CONNECTED) {
                setSSID(WiFi.SSID().c_str());
                setPassword(stationList[bestStation]->password);
                Serial.println();
                // Print IP details
                Serial.printf("IP address: %s\r\n",WiFi.localIP().toString());
                Serial.printf("Netmask   : %s\r\n",WiFi.subnetMask().toString());
                Serial.printf("Gateway   : %s\r\n",WiFi.gatewayIP().toString());

            } else {
                ESP_LOGW(tag,"WiFi connection failed");
                WiFi.disconnect();   // (resets the WiFi scan)
                return wifiStatus();
            }
        }
    }

    if (accesspoint && (WiFi.status() != WL_CONNECTED)) {
        // The accesspoint has been enabled, and we have not connected to any existing networks
        WiFi.softAPsetHostname(mdnsName);
        
        WiFi.mode(WIFI_AP);
        // reset ap_status
        ap_status = WL_DISCONNECTED;

        Serial.printf("Setting up Access Point (channel=%d)\r\n", ap_channel);
        Serial.print("  SSID     : ");
        Serial.println(apName);
        Serial.print("  Password : ");
        Serial.println(apPass);

        // User has specified the AP details; apply them after a short delay
        // (https://github.com/espressif/arduino-esp32/issues/985#issuecomment-359157428)
        if(WiFi.softAPConfig(*apIP.ip, *apIP.ip, *apIP.netmask)) {
            ESP_LOGI(tag,"IP address: %s",WiFi.softAPIP().toString());
        }
        else {
            ESP_LOGW(tag,"softAPConfig failed");
            ap_status = WL_CONNECT_FAILED;
            return wifiStatus();
        }

        // WiFi.softAPsetHostname(mdnsName);

        if(!WiFi.softAP(apName, apPass, ap_channel)) {
            ESP_LOGE(tag,"Access Point init failed!");
            ap_status = WL_CONNECT_FAILED;
            return wifiStatus();
        }       
        
        ap_status = WL_CONNECTED;
        ESP_LOGI(tag,"Access Point init successful");

        // Start the DNS captive portal if requested
        if (ap_dhcp) {
            ESP_LOGI(tag,"Starting Captive Portal");
            dnsServer.start(DNS_PORT, "*", *apIP.ip);
            captivePortal = true;
        }
    }

    calcURLs();

    startOTA();
    // http service attached to port
    configMDNS();

    return wifiStatus();
}

void CLAppConn::calcURLs() {
    // Set the URL's

    // if host name is not defined or access point mode is activated, use local IP for url
    if(!strcmp(hostName, "")) {
        if(accesspoint)
            strcpy(hostName, WiFi.softAPIP().toString().c_str());
        else
            strcpy(hostName, WiFi.localIP().toString().c_str());
    }
    if (httpPort != 80) {
        snprintf(httpURL, sizeof(httpURL), "http://%s:%d/", hostName, httpPort);
        snprintf(streamURL, sizeof(streamURL), "http://%s:%d/view?mode=stream", hostName, httpPort);

    } else {
        snprintf(httpURL, sizeof(httpURL), "http://%s/", hostName);
        snprintf(streamURL, sizeof(streamURL), "http://%s/view?mode=stream", hostName);
    }
    

}

int CLAppConn::loadFromJson(JsonObject jctx, bool full_set) {
   if(snprintf(mdnsName, sizeof(mdnsName), "%s", jctx[FPSTR(CONN_MDNS_NAME)] | "") == 0)
        ESP_LOGW(tag,"MDNS Name is not defined!");

    snprintf(hostName, sizeof(hostName), "%s", jctx[FPSTR(CONN_HOST_NAME)] | "");
    httpPort = jctx[FPSTR(CONN_HTTP_PORT)] | 80;
    dhcp = jctx[FPSTR(CONN_DHCP)] | true;

    JsonArray stations = jctx[FPSTR(CONN_SSID_LIST)].as<JsonArray>();
    if(stations.size() > MAX_KNOWN_STATIONS) {
        ESP_LOGW(tag,"Too many known stations defined in config, only first %d will be used", MAX_KNOWN_STATIONS);
    }
     char dbuf[192];
    if(stations.size() > 0) {
        ESP_LOGI(tag,"Known external SSIDs: ");
        for(JsonObject station : stations) {
            Station *s = (Station*) malloc(sizeof(Station));
            snprintf(s->ssid, sizeof(s->ssid), station[FPSTR(CONN_SSID)] | "");
            snprintf(dbuf, sizeof(dbuf), station[FPSTR(CONN_PASSWORD)] | "");
            urlDecode(s->password, dbuf, sizeof(dbuf));
            Serial.println(s->ssid);
            stationList[stationCount] = s;
            stationCount++;
        }
    }
    else {
        ESP_LOGI(tag,"No known external SSIDs defined in config");
    }
        
    // read static IP

    if(jctx[FPSTR(CONN_STATIC_IP)].is<JsonObject>()) {
        JsonObject static_ip = jctx[FPSTR(CONN_STATIC_IP)].as<JsonObject>();
        readIPFromJSON(static_ip, &staticIP.ip, FPSTR(CONN_IP));
        readIPFromJSON(static_ip, &staticIP.netmask, FPSTR(CONN_NETMASK));
        readIPFromJSON(static_ip, &staticIP.gateway, FPSTR(CONN_GATEWAY));
        readIPFromJSON(static_ip, &staticIP.dns1, FPSTR(CONN_DNS1));
        readIPFromJSON(static_ip, &staticIP.dns2, FPSTR(CONN_DNS2));
    }
    
    load_as_ap = jctx[FPSTR(CONN_LOAD_AS_AP)] | false;
    ap_timeout = jctx[FPSTR(CONN_AP_TIMEOUT)] | 0;
    snprintf(apName, sizeof(apName), "%s", jctx[FPSTR(CONN_AP_SSID)] | "ESP32-CAM-AP");

    sniprintf(dbuf, sizeof(dbuf), "%s", jctx[FPSTR(CONN_AP_PASS)] | "");
    urlDecode(apPass, dbuf, sizeof(dbuf));

    ap_channel = jctx[FPSTR(CONN_AP_CHANNEL)] | 1;
    ap_dhcp = jctx[FPSTR(CONN_AP_DHCP)] | true;
    
    // read AP IP
    if(jctx[FPSTR(CONN_AP_IP)].is<JsonObject>()) {
        JsonObject ap_ip = jctx[FPSTR(CONN_AP_IP)].as<JsonObject>();
        readIPFromJSON(ap_ip, &apIP.ip, FPSTR(CONN_IP));
        readIPFromJSON(ap_ip, &apIP.netmask, FPSTR(CONN_NETMASK));
    }
    
    // User name and password
    sniprintf(user, sizeof(user), "%s", jctx[FPSTR(CONN_USER)] | "admin");
    sniprintf(dbuf, sizeof(dbuf), "%s", jctx[FPSTR(CONN_PWD)] | "");
    urlDecode(pwd, dbuf, sizeof(dbuf));

    // OTA
    otaEnabled = jctx[FPSTR(CONN_OTA_ENABLED)] | false;
    sniprintf(dbuf, sizeof(dbuf), "%s", jctx[FPSTR(CONN_OTA_PASSWORD)] | "");
    urlDecode(otaPassword, dbuf, sizeof(dbuf));

    // NTP
    snprintf(ntpServer, sizeof(ntpServer), "%s", jctx[FPSTR(CONN_NTP_SERVER)] | "ru.pool.ntp.org");
    gmtOffset_sec = jctx[FPSTR(CONN_GMT_OFFSET)] | 0;

    daylightOffset_sec = daylightOffset_sec ? daylightOffset_sec : 0;

    return OK;
}

void CLAppConn::setStaticIP (IPAddress ** ip_address, const char * strval) {
    if(!*ip_address) *ip_address = new IPAddress();
    if(!(*ip_address)->fromString(strval)) {
            ESP_LOGW(tag,"%s is invalid IP address", strval);
    }
}

void CLAppConn::readIPFromJSON (JsonObject context, IPAddress ** ip_address, const __FlashStringHelper* token) {
    char buf[16];
    if(snprintf(buf, sizeof(buf), "%s", context[token] | "") != 0) {
        setStaticIP(ip_address, buf);
    }
}

int CLAppConn::saveToJson(JsonObject jctx, bool full_set) {
    jctx[FPSTR(CONN_MDNS_NAME)] = mdnsName;

    int count = stationCount;
    int index = getSSIDIndex();
    if(index < 0 && count == MAX_KNOWN_STATIONS) {
        count--;
    }

    char ebuf[254]; 

    if(index < 0 || count > 0) {
        JsonArray stations = jctx[FPSTR(CONN_SSID_LIST)].to<JsonArray>();

        // if(index < 0 && strcmp(ssid, "") != 0) {
        //     JsonObject station = stations.createNestedObject();
        //     station[FPSTR(CONN_SSID)] = ssid;
        //     urlEncode(ebuf, password, sizeof(password));
        //     station[FPSTR(CONN_PASSWORD)] = ebuf;
        //     stations.add(station);
        // }
 
        for(int i=0; i < count && stationList[i]; i++) {
            JsonObject station = stations.add<JsonObject>();
            station[FPSTR(CONN_SSID)] = stationList[i]->ssid;
            if(index >= 0 && i == index) {
                urlEncode(ebuf, password, sizeof(password));
                station[FPSTR(CONN_PASSWORD)] = ebuf;
            }
            else {
                urlEncode(ebuf, stationList[i]->password, sizeof(stationList[i]->password));
                station[FPSTR(CONN_PASSWORD)] = ebuf;
            }
        }
    }

    jctx[FPSTR(CONN_DHCP)] = dhcp;
    if(staticIP.ip) jctx[FPSTR(CONN_STATIC_IP)][FPSTR(CONN_IP)] = staticIP.ip->toString(); 
    if(staticIP.netmask) jctx[FPSTR(CONN_STATIC_IP)][FPSTR(CONN_NETMASK)] = staticIP.netmask->toString();
    if(staticIP.gateway) jctx[FPSTR(CONN_STATIC_IP)][FPSTR(CONN_GATEWAY)] = staticIP.gateway->toString();
    if(staticIP.dns1) jctx[FPSTR(CONN_STATIC_IP)][FPSTR(CONN_DNS1)] = staticIP.dns1->toString();
    if(staticIP.dns2) jctx[FPSTR(CONN_STATIC_IP)][FPSTR(CONN_DNS2)] = staticIP.dns2->toString();

    jctx[FPSTR(CONN_HTTP_PORT)] = httpPort;
    jctx[FPSTR(CONN_USER)] = user;
    jctx[FPSTR(CONN_PWD)] = pwd;
    jctx[FPSTR(CONN_OTA_ENABLED)] = otaEnabled;
    urlEncode(ebuf, otaPassword, sizeof(otaPassword));
    jctx[FPSTR(CONN_OTA_PASSWORD)] = ebuf;

    jctx[FPSTR(CONN_LOAD_AS_AP)] = load_as_ap;
    jctx[FPSTR(CONN_AP_TIMEOUT)] = ap_timeout;
    jctx[FPSTR(CONN_AP_SSID)] = apName;
    urlEncode(ebuf, apPass, sizeof(apPass));
    jctx[FPSTR(CONN_AP_PASS)] = ebuf;
    jctx[FPSTR(CONN_AP_DHCP)] = ap_dhcp;
    if(apIP.ip) jctx[FPSTR(CONN_AP_IP)][FPSTR(CONN_IP)] = apIP.ip->toString(); 
    if(apIP.netmask) jctx[FPSTR(CONN_AP_IP)][FPSTR(CONN_NETMASK)] = apIP.netmask->toString();

    jctx[FPSTR(CONN_NTP_SERVER)] = ntpServer;
    jctx[FPSTR(CONN_GMT_OFFSET)] = gmtOffset_sec;
    jctx[FPSTR(CONN_DST_OFFSET)] = daylightOffset_sec;
    return OK;
}

void CLAppConn::startOTA() {
    // Set up OTA

    if(otaEnabled) {
        ESP_LOGI(tag,"Setting up OTA");
        // Port defaults to 3232
        // ArduinoOTA.setPort(3232);
        // Hostname defaults to esp3232-[MAC]
        ArduinoOTA.setHostname(mdnsName);

        if (strlen(otaPassword) != 0) {
            ArduinoOTA.setPassword(otaPassword);
            ESP_LOGI(tag,"OTA Password: %s", otaPassword);
        } 
        else {
            ESP_LOGW(tag,"No OTA password has been set! (insecure)");
        }
        
        ArduinoOTA
            .onStart([]() {
                String type;
                if (ArduinoOTA.getCommand() == U_FLASH)
                    type = "sketch";
                else // U_SPIFFS
                    // NOTE: if updating SPIFFS this would be the place to unmount SPIFFS using SPIFFS.end()
                    type = "filesystem";
                Serial.println("Start updating " + type);

                // Stop the camera since OTA will crash the module if it is running.
                // the unit will need rebooting to restart it, either by OTA on success, or manually by the user
                AppCam.stop();

                // critERR = "<h1>OTA Has been started</h1><hr><p>Camera has Halted!</p>";
                // critERR += "<p>Wait for OTA to finish and reboot, or <a href=\"control?var=reboot&val=0\" title=\"Reboot Now (may interrupt OTA)\">reboot manually</a> to recover</p>";
            })
            .onEnd([]() {
                Serial.println("End");
            })
            .onProgress([](unsigned int progress, unsigned int total) {
                Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
            })
            .onError([](ota_error_t error) {
                Serial.printf("Error[%u]: ", error);
                if (error == OTA_AUTH_ERROR) Serial.println("Auth Failed");
                else if (error == OTA_BEGIN_ERROR) Serial.println("Begin Failed");
                else if (error == OTA_CONNECT_ERROR) Serial.println("Connect Failed");
                else if (error == OTA_RECEIVE_ERROR) Serial.println("Receive Failed");
                else if (error == OTA_END_ERROR) Serial.println("End Failed");
            });

        ArduinoOTA.begin();
        ESP_LOGI(tag,"OTA is enabled");
    }
    else {
        ArduinoOTA.end();
        ESP_LOGI(tag,"OTA is disabled");
    }

}

void CLAppConn::configMDNS() {

    // if(!otaEnabled) {
    if (!MDNS.begin(mdnsName)) {
        ESP_LOGW(tag,"Error setting up MDNS responder!");
    }
    else
        ESP_LOGI(tag,"mDNS responder started");
    // }
    //MDNS Config -- note that if OTA is NOT enabled this needs prior steps!
    MDNS.addService("http", "tcp", httpPort);
    ESP_LOGI(tag,"Added HTTP service to MDNS server");

}

void CLAppConn::configNTP() {
    configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
    ntp_in_sync = true;
}

void CLAppConn::printLocalTime(bool extraData) {
    updateTimeStr();
    Serial.println(localTimeString);
    if (extraData) {
        Serial.printf("NTP Server: %s, GMT Offset: %li(s), DST Offset: %i(s)\r\n", 
                      ntpServer, gmtOffset_sec , daylightOffset_sec);
    }
}



void CLAppConn::updateTimeStr() {
    if(!accesspoint) {
       tm timeinfo; 
       if(getLocalTime(&timeinfo)) 
            strftime(localTimeString, sizeof(localTimeString), "%H:%M:%S, %A, %B %d %Y", &timeinfo);
    }
    else {
        snprintf(localTimeString, sizeof(localTimeString), "N/A");
    }
    int64_t sec = esp_timer_get_time() / 1000000;
    int64_t upDays = int64_t(floor(sec/86400));
    int upHours = int64_t(floor(sec/3600)) % 24;
    int upMin = int64_t(floor(sec/60)) % 60;
    int upSec = sec % 60;
    snprintf(upTimeString, sizeof(upTimeString),  "%" PRId64 ":%02i:%02i:%02i (d:h:m:s)", upDays, upHours, upMin, upSec);
}

int CLAppConn::getSSIDIndex() {
    for(int i=0; i < stationCount; i++) {
        if(!stationList[i]) break;
        if(strcmp(ssid, stationList[i]->ssid) == 0) {
            return i;
        }
    }
    return -1;
}

CLAppConn AppConn;