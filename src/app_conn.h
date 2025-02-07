#ifndef app_conn_h
#define app_conn_h

#include <ArduinoOTA.h>
#include <WiFi.h>
#include <WiFiUdp.h>
#include <DNSServer.h>
#include <ESPmDNS.h>
#include <time.h>
#include <ArduinoJson.h>

#include "parsebytes.h"
#include "app_component.h"
#include "app_cam.h"

#define MAX_KNOWN_STATIONS              5

#define CREDENTIALS_SIZE                32

/**
 * @brief WiFi connectivity details (SSID/password).
 * 
 */
struct Station { String ssid; String password; };

/**
 * @brief Static IP structure for configuring AP and WiFi parameters
 * 
 */
struct StaticIP { IPAddress *ip; IPAddress *netmask; IPAddress *gateway; IPAddress *dns1; IPAddress *dns2; };

enum StaticIPField {IP, NETMASK, GATEWAY, DNS1, DNS2};

/**
 * @brief Connection Manager
 * This class manages everything related to connectivity of the application: WiFi, OTA etc.
 * 
 */
class CLAppConn : public CLAppComponent {
    public:
        CLAppConn();

        int loadPrefs();
        int savePrefs();
        int start();
        bool stop() {return WiFi.disconnect();};

        void startOTA();
        void handleOTA() {if(otaEnabled) ArduinoOTA.handle();};
        bool isOTAEnabled() {return otaEnabled;};        
        void setOTAEnabled(bool val) {otaEnabled = val;};
        void setOTAPassword(const String val) {otaPassword = val;};

        void configMDNS();
        void handleDNSRequest(){if (captivePortal) dnsServer.processNextRequest();};
        const String& getMDNSname() {return mdnsName;};
        void setMDNSName(String val) {mdnsName = val;};

        void configNTP();
        const String& getNTPServer() { return ntpServer;};
        void setNTPServer(const String val) {ntpServer = val;};
        long getGmtOffset_sec() {return gmtOffset_sec;};
        void setGmtOffset_sec(long sec) {gmtOffset_sec = sec;};
        int getDaylightOffset_sec() {return daylightOffset_sec;};
        void setDaylightOffset_sec(int sec) {daylightOffset_sec = sec;};

        const String& getSSID() {return ssid;};
        void setSSID(const String val) {ssid = val;};
        void setPassword(const String val) {password = val;};;

        bool isDHCPEnabled() {return dhcp;};
        void setDHCPEnabled(bool val) {dhcp = val;};
        StaticIP* getStaticIP() {return &staticIP;};
        void setStaticIP(IPAddress ** address, const char * strval);

        wl_status_t wifiStatus() {return (accesspoint?ap_status:WiFi.status());};

        const String& getHTTPUrl(){ return httpURL;};
        // char * getStreamUrl(){ return streamURL;};
        int getPort() {return httpPort;};
        void setPort(int port) {httpPort = port;};

        const String& getApName() {return apName;};
        void setApName(const String val) {apName = val;};
        void setApPass(const String val) {apPass = val;};

        bool isAccessPoint() {return accesspoint;};
        void setAccessPoint(bool val) {accesspoint = val;};
        void setLoadAsAP(bool val) {load_as_ap = val;}
        bool getAPDHCP() {return ap_dhcp;};
        void setAPDHCP(bool val) {ap_dhcp = val;};
        StaticIP* getAPIP() {return &apIP;};
        int getAPChannel() {return ap_channel;};
        void setAPChannel(int channel) {ap_channel = channel;};

        bool isCaptivePortal() {return captivePortal;};

        const String& getLocalTimeStr() {return localTimeString;};
        const String& getUpTimeStr() {return upTimeString;};
        void updateTimeStr();

        void printLocalTime(bool extraData=false);

        const String& getUser() {return user;};
        const String& getPwd() {return pwd;};
        void setUser(const String val) {user = val;};
        void setPwd(const String val) {pwd = val;}

    private:
        int getSSIDIndex();
        void calcURLs();
        void readIPFromJSON(jparse_ctx_t * context, IPAddress ** ip_address, char * token);

        // Known networks structure. Max number of known stations limited for memory considerations
        Station* stationList; 
        // number of known stations
        int stationCount = 0;

        // Static IP structure
        StaticIP staticIP;

        bool dhcp=false;

        String ssid;
        String password;

        String mdnsName;

        bool accesspoint = false;
        bool load_as_ap = false;

        String apName;
        String apPass;
        int ap_channel=1;
        StaticIP apIP;
        bool ap_dhcp=true;
        wl_status_t ap_status = WL_DISCONNECTED;

        // DNS server
        const byte DNS_PORT = 53;
        DNSServer dnsServer;
        bool captivePortal = false;

        // HOST_NAME
        String hostName;

        // The app and stream URLs (initialized during WiFi setup)
        String httpURL;
        String streamURL;

        // HTTP Port. Can be overriden during IP setup
        uint16_t httpPort = 80;
        
        // user name and password
        String user = "admin";
        String pwd = "admin";

        // OTA parameters
        bool otaEnabled = false;
        String otaPassword = "";

        // NTP parameters
        String ntpServer = "";
        long  gmtOffset_sec;
        int  daylightOffset_sec;

        String localTimeString;
        String upTimeString;

};

extern CLAppConn AppConn;

#endif