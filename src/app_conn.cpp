#include "app_conn.h"

CLAppConn::CLAppConn() {
    setTag("conn");
}

int CLAppConn::start() {

    stationList = new Station[MAX_KNOWN_STATIONS];

    if(loadPrefs() != OK) {
        return WiFi.status();
    }
    
    Serial.println("Starting WiFi");

    WiFi.setHostname(this->mdnsName.c_str());
    
    WiFi.mode(WIFI_STA); 

    // Disable power saving on WiFi to improve responsiveness
    // (https://github.com/espressif/arduino-esp32/issues/1484)
    WiFi.setSleep(false);
    
    byte mac[6] = {0,0,0,0,0,0};
    WiFi.macAddress(mac);
    Serial.printf("MAC address: %02X:%02X:%02X:%02X:%02X:%02X\r\n", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

    int bestStation = -1;
    long bestRSSI = -1024;
    char bestSSID[65] = "";
    uint8_t bestBSSID[6];

    accesspoint = load_as_ap;

    if (!accesspoint) {
        if(stationCount > 0) {
            // We have a list to scan
            Serial.println("Scanning local Wifi Networks");
            int stationsFound = WiFi.scanNetworks();
            Serial.printf("%i networks found\r\n", stationsFound);
            if (stationsFound > 0) {
                for (int i = 0; i < stationsFound; ++i) {
                    // Print SSID and RSSI for each network found
                    String thisSSID = WiFi.SSID(i);
                    int thisRSSI = WiFi.RSSI(i);
                    String thisBSSID = WiFi.BSSIDstr(i);
                    Serial.printf("%3i : [%s] %s (%i)", i + 1, thisBSSID.c_str(), thisSSID.c_str(), thisRSSI);
                    // Scan our list of known external stations
                    for (int sta = 0; sta < stationCount; sta++) {
                        if (stationList[sta].ssid == thisSSID ||
                        stationList[sta].ssid == thisBSSID) {
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
            Serial.println("No known networks found, entering AccessPoint fallback mode");
            accesspoint = true;
        } 
        else {
            Serial.printf("Connecting to Wifi Network %d: [%02X:%02X:%02X:%02X:%02X:%02X] %s \r\n",
                        bestStation, bestBSSID[0], bestBSSID[1], bestBSSID[2], bestBSSID[3],
                        bestBSSID[4], bestBSSID[5], bestSSID);
            // Apply static settings if necesscary
            if (dhcp == false) {

                if(staticIP.ip && staticIP.gateway  && staticIP.netmask) {
                    Serial.println("Applying static IP settings");
                    dhcp = false;
                    WiFi.config(*staticIP.ip, *staticIP.gateway, *staticIP.netmask, *staticIP.dns1, *staticIP.dns2);
                }
                else {
                    dhcp = true;
                    Serial.println("Static IP settings requested but not defined properly in config, falling back to dhcp");
                }    
            }

            // WiFi.setHostname(mdnsName);

            // Initiate network connection request (3rd argument, channel = 0 is 'auto')
            //WiFi.begin(bestBSSID, stationList[bestStation]->password.c_str(), 0, bestBSSID);
            WiFi.begin(stationList[bestStation].ssid.c_str(), stationList[bestStation].password.c_str());

            // Wait to connect, or timeout
            unsigned long start = millis();
            while ((millis() - start <= WIFI_WATCHDOG) && (WiFi.status() != WL_CONNECTED)) {
                delay(500);
                Serial.print('.');
            }
            // If we have connected, inform user
            if (WiFi.status() == WL_CONNECTED) {
                setSSID(WiFi.SSID().c_str());
                setPassword(stationList[bestStation].password);
                Serial.println();
                // Print IP details
                Serial.printf("IP address: %s\r\n",WiFi.localIP().toString());
                Serial.printf("Netmask   : %s\r\n",WiFi.subnetMask().toString());
                Serial.printf("Gateway   : %s\r\n",WiFi.gatewayIP().toString());

            } else {
                Serial.println("WiFi connection failed");
                WiFi.disconnect();   // (resets the WiFi scan)
                return wifiStatus();
            }
        }
    }

    if (accesspoint && (WiFi.status() != WL_CONNECTED)) {
        // The accesspoint has been enabled, and we have not connected to any existing networks
        WiFi.softAPsetHostname(this->mdnsName.c_str());
        
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
            Serial.printf("IP address: %s\r\n",WiFi.softAPIP().toString());
        }
        else {
            Serial.println("softAPConfig failed");
            ap_status = WL_CONNECT_FAILED;
            return wifiStatus();
        }

        // WiFi.softAPsetHostname(mdnsName);

        if(!WiFi.softAP(this->apName.c_str(), this->apPass.c_str(), this->ap_channel)) {
            Serial.println("Access Point init failed!");
            ap_status = WL_CONNECT_FAILED;
            return wifiStatus();
        }       
        
        ap_status = WL_CONNECTED;
        Serial.println("Access Point init successful");

        // Start the DNS captive portal if requested
        if (ap_dhcp) {
            Serial.println("Starting Captive Portal");
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
    char s[100] = {0}; 
    memset(s, 0, sizeof(s));

    // Set the URL's

    // if host name is not defined or access point mode is activated, use local IP for url
    if(this->hostName != "") {
        if(accesspoint)
            this-> hostName = WiFi.softAPIP().toString();
        else
            this-> hostName = WiFi.localIP().toString();
    }
    if (httpPort != 80) {
        snprintf(s, sizeof(s), "http://%s:%d/", hostName, httpPort);
        this->httpURL = s;
        snprintf(s, sizeof(s), "http://%s:%d/view?mode=stream", hostName, httpPort);
        this->streamURL = s;

    } else {
        snprintf(s, sizeof(s), "http://%s/", hostName);
        this->httpURL = s;
        snprintf(s, sizeof(s), "http://%s/view?mode=stream", hostName);
        this->streamURL = s;
    }
    

}

int CLAppConn::loadPrefs() {
    
    JsonDocument json;
    int ret  = parsePrefs(json);
    if(ret != OS_SUCCESS) {
        return ret;
    }

    this->mdnsName = json["mdns_name"].as<String>();

    if(this->mdnsName) {
        this->hostName  = json["host_name"].as<String>();
        this->httpPort  = json["http_port"].as<int>();
        this->dhcp      = json["dhcp"].as<bool>();
    } else {
        Serial.println("MDNS Name is not defined!");
    }

    char sbuf[64];
    char dbuf[192];
    int count;
    stationCount = 0;

    if (this->mdnsName && json["stations"]) {
        Serial.print("Known external SSIDs: ");
        count = json["stations"].as<JsonArray>().size();
        if(count > 0)
            for(int i=0; i < count && i < MAX_KNOWN_STATIONS; i++) {

                if(json["stations"].as<JsonArray>()[i]) {
                    if(json["stations"].as<JsonArray>()[i]["ssid"] &&
                       json["stations"].as<JsonArray>()[i]["pass"] ) {
                        Station s;
                        s.ssid = json["stations"].as<JsonArray>()[i]["ssid"].as<String>();
                        this->urlDecode(s.password, json["stations"].as<JsonArray>()[i]["pass"].as<String>().c_str());
                        Serial.println(s.ssid);
                        stationList[i] = s;
                        stationCount++;
                    } 
                }
                
            }
        else
            Serial.println("None");
    }
        
    // read static IP
    if(this->mdnsName && json["static_ip"]) {
        staticIP.ip->fromString(json["static_ip"]["ip"].as<String>());
        staticIP.netmask->fromString(json["static_ip"]["netmask"].as<String>());
        staticIP.gateway->fromString(json["static_ip"]["gateway"].as<String>());
        staticIP.dns1->fromString(json["static_ip"]["dns1"].as<String>());
        staticIP.dns2->fromString(json["static_ip"]["dns2"].as<String>());
    }

    load_as_ap = json["accesspoint"].as<bool>();
    this->apName = json["ap_ssid"].as<String>().c_str();

    String apPassStr = json["ap_pass"].as<String>();
    this->urlDecode(this->apPass, json["ap_pass"].as<String>().c_str());

    if(json["ap_channel"]) { this->ap_channel = json["ap_channel"].as<int>(); } else { ap_channel = 1; }
    if(json["ap_dhcp"]) { this->ap_dhcp = json["ap_dhcp"].as<bool>(); } else { ap_dhcp = true; }
    
    // read AP IP
    if(this->mdnsName && json["ap_ip"]) {
        
        IPAddress ip;
        ip.fromString(json["ap_ip"]["ip"].as<String>());
        apIP.ip = &ip;
        
        IPAddress nm;
        nm.fromString(json["ap_ip"]["netmask"].as<String>());
        apIP.netmask = &nm;
    }
    
    // User name and password
    this->user = json["user"].as<String>();
    this->pwd = json["pwd"].as<String>();

    // OTA
    this->otaEnabled = json["ota_enabled"].as<bool>();
    this->urlDecode(this->otaPassword, json["ota_password"].as<String>().c_str());

    // NTP
    this->ntpServer = json["ntp_server"].as<String>(); 
    this->gmtOffset_sec = json["gmt_offset"].as<int64_t>();
    this->daylightOffset_sec = json["dst_offset"].as<int>();
    
    this->setDebugMode(json["debug_mode"].as<bool>());
    
    return ret;
}

void CLAppConn::setStaticIP (IPAddress ** ip_address, const char * strval) {
    if(!*ip_address) *ip_address = new IPAddress();
    if(!(*ip_address)->fromString(strval)) {
            Serial.print(strval); Serial.println(" is invalid IP address");
    }
}

void CLAppConn::readIPFromJSON (jparse_ctx_t * context, IPAddress ** ip_address, char * token) {
    char buf[16];
    if(json_obj_get_string(context, token, buf, sizeof(buf)) == OS_SUCCESS) {
        setStaticIP(ip_address, buf);
    }
}

int CLAppConn::savePrefs() {
  JsonDocument json;
  char ebuf[254]; 

  char * prefs_file = getPrefsFileName(true); 

  if (Storage.exists(prefs_file)) {
    Serial.printf("Updating %s\r\n", prefs_file);
  } else {
    Serial.printf("Creating %s\r\n", prefs_file);
  }

  json["mdns_name"] = this->mdnsName;

  int count = stationCount;
  int index = getSSIDIndex();
  if(index < 0 && count == MAX_KNOWN_STATIONS) {
    count--;
  }

  if(index < 0 || count > 0) {
    json["stations"].to<JsonArray>();
    uint8_t i=0;
    if(index < 0 && this->ssid != "") {
      json["stations"][i]["ssid"] = this->ssid;
      this->urlEncode(this->password, json["stations"][i]["pass"].as<String>().c_str());
      i++;
    }

    for(int i=0; i < count; i++) {
      json["stations"][i]["ssid"] = stationList[i].ssid;
      
      if(index >= 0 && i == index) {
        String encString("");
        this->urlEncode(encString, this->password.c_str());
        json["stations"][i]["pass"] = encString;
      }
      else {
        String encString("");
        this->urlEncode(encString, stationList[i].password.c_str());
        json["stations"][i]["pass"] = encString;
      }
    }
  }

  json["dhcp"] = this->dhcp;
  json["static_ip"].to<JsonObject>();
  if(staticIP.ip) json["static_ip"]["ip"] = staticIP.ip->toString();
  if (staticIP.netmask) json["static_ip"]["netmask"] = staticIP.netmask->toString();
  if (staticIP.gateway) json["static_ip"]["gateway"] = staticIP.gateway->toString();
  if (staticIP.dns1) json["static_ip"]["dns1"] = staticIP.dns1->toString();
  if (staticIP.dns2) json["static_ip"]["dns2"] = staticIP.dns2->toString();

  json["http_port"] = this->httpPort;
  json["user"] = this->user;
  json["pwd"] = this->pwd;
  json["ota_enabled"] = this->otaEnabled;
  
  String t("");
  this->urlEncode(t, this->otaPassword.c_str());
  json["ota_password"] = t;
    
  json["accesspoint"] = this->load_as_ap;
  json["ap_ssid"] = this-> apName;

  t = "";
  this->urlEncode(t, this->otaPassword.c_str());
  json["ap_pass"] = t;
  
  json["ap_dhcp"] = this->ap_dhcp;
  if(apIP.ip)       json["ap_ip"]["ip"] = apIP.ip->toString();
  if(apIP.netmask)  json["ap_ip"]["netmask"] = apIP.netmask->toString();
    
  json["ntp_server"] = this->ntpServer;
  json["gmt_offset"] = this->gmtOffset_sec;
  json["dst_offset"] = this->daylightOffset_sec;
  json["debug_mode"] = this->isDebugMode();

  File file = Storage.open(prefs_file, FILE_WRITE);
  if(file) {
    serializeJson(json, file);
    file.close();
    Serial.printf("File %s updated\r\n", prefs_file);
    serializeJsonPretty(json, Serial);
    Serial.println();
    return OK;
  }
  else {
    Serial.printf("Failed to save connection preferences to file %s\r\n", prefs_file);
    return FAIL;
  }
  return OS_SUCCESS;
} 

void CLAppConn::startOTA() {
    // Set up OTA

    if(otaEnabled) {
        Serial.println("Setting up OTA");
        // Port defaults to 3232
        // ArduinoOTA.setPort(3232);
        // Hostname defaults to esp3232-[MAC]
        ArduinoOTA.setHostname(this->mdnsName.c_str());

        if (otaPassword.length() > 0) {
            ArduinoOTA.setPassword(this->otaPassword.c_str());
            Serial.printf("OTA Password: %s\n\r", this->otaPassword.c_str());
        } 
        else {
            Serial.printf("\r\nNo OTA password has been set! (insecure)\r\n\r\n");
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
        Serial.println("OTA is enabled");
    }
    else {
        ArduinoOTA.end();
        Serial.println("OTA is disabled");
    }

}

void CLAppConn::configMDNS() {

    // if(!otaEnabled) {
    if (!MDNS.begin(this->mdnsName.c_str())) {
        Serial.println("Error setting up MDNS responder!");
    }
    else
        Serial.println("mDNS responder started");
    // }
    //MDNS Config -- note that if OTA is NOT enabled this needs prior steps!
    MDNS.addService("http", "tcp", this->httpPort);
    Serial.println("Added HTTP service to MDNS server");

}

void CLAppConn::configNTP() {
    configTime(gmtOffset_sec, daylightOffset_sec, ntpServer.c_str());
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
       if(getLocalTime(&timeinfo)) {
            char buffer[80];
            strftime(buffer, sizeof(buffer), "%H:%M:%S, %A, %B %d %Y", &timeinfo);
            this->localTimeString = (String)buffer;
       }
    }
    else {
        this->localTimeString = "N/A";
    }
    int64_t sec = esp_timer_get_time() / 1000000;
    int64_t upDays = int64_t(floor(sec/86400));
    int upHours = int64_t(floor(sec/3600)) % 24;
    int upMin = int64_t(floor(sec/60)) % 60;
    int upSec = sec % 60;

    char buffer[80];
    snprintf(buffer, sizeof(buffer),  "%" PRId64 ":%02i:%02i:%02i (d:h:m:s)", upDays, upHours, upMin, upSec);
    this->upTimeString = (String)buffer;
}

int CLAppConn::getSSIDIndex() {
  for(int i=0; i < stationCount; i++) {
    if(ssid == stationList[i].ssid) {
      return i;
    }
  }
  return -1;
}

CLAppConn AppConn;