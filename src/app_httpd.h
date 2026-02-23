#ifndef app_httpd_h
#define app_httpd_h

#include <esp_task_wdt.h>
#include <ESPAsyncWebServer.h>
#include <ArduinoJson.h>

#include "app_defines.h"

#include "storage.h"
#include "app_conn.h"
#include "app_cam.h"
#include "app_pwm.h"
#include "utils.h"

#ifdef ENABLE_MAIL_FEATURE
#include "app_mail.h"      // Mail client
#endif

#include <esp_log.h>

#define MAX_URI_MAPPINGS                32

#define SERIAL_BUFFER_SIZE              64

#define MAX_VIDEO_STREAMS               5

const char HTTPD_SERIAL_BUF[] PROGMEM = "serial_buf";
const char HTTPD_ACTIVE_STREAMS[] PROGMEM = "active_streams";
const char HTTPD_STREAMS_SERVED[] PROGMEM = "prev_streams";
const char HTTPD_IMAGES_SERVED[] PROGMEM = "img_captured";
const char HTTPD_MAX_STREAMS[] PROGMEM = "max_streams";

const char HTTPD_MAPPING[] PROGMEM = "mapping";
const char HTTPD_URI[] PROGMEM = "uri";
const char HTTPD_PATH[] PROGMEM = "path";

enum CaptureModeEnum {CAPTURE_STILL, CAPTURE_STREAM};
enum StreamResponseEnum {STREAM_SUCCESS, 
                         STREAM_NUM_EXCEEDED, 
                         STREAM_CLIENT_REGISTER_FAILED,
                         STREAM_TIMER_NOT_INITIALIZED,
                         STREAM_MODE_NOT_SUPPORTED, 
                         STREAM_IMAGE_CAPTURE_FAILED,
                         STREAM_CLIENT_NOT_FOUND};


String processor(const String& var);
void onSystemStatus(AsyncWebServerRequest *request);
void onStatus(AsyncWebServerRequest *request);
void onInfo(AsyncWebServerRequest *request);
void onControl(AsyncWebServerRequest *request);
void onWsEvent(AsyncWebSocket * server, AsyncWebSocketClient * client, AwsEventType type, void * arg, uint8_t *data, size_t len);
void onSnapTimer(TimerHandle_t pxTimer);



/**
 * @brief Static URI to path mapping
 * 
 */
struct UriMapping { char uri[32]; char path[32];};


/** 
 * @brief WebServer Manager
 * Class for handling web server requests. The web pages are assumed to be stored in the file system (can be SD card or LittleFS).  
 * 
 */
class CLAppHttpd : public CLAppComponent {
    public:
        CLAppHttpd();

        int start();
        int loadPrefs();
        int savePrefs();

        void cleanupWsClients();

        // register a client streaming video
        int addStreamClient(uint32_t client_id);
        int removeStreamClient(uint32_t client_id);

        uint32_t getControlClient() {return _control_client;};
        void setControlClient(uint32_t id) {_control_client = id;};

        int8_t getStreamCount() {return _streamCount;};
        long getStreamsServed() {return _streamsServed;};

        // start stream
        StreamResponseEnum startStream(uint32_t id, CaptureModeEnum stream_mode);
        //terminate stream
        StreamResponseEnum stopStream(uint32_t id);

        int bcastBufImg(uint8_t* buffer, size_t size);

        void setFrameRate(int frameRate);

        void serialSendCommand(const char * cmd);

        int getSketchSize(){ return _sketchSize;};
        int getSketchSpace() {return _sketchSpace;};
        
        const char * getSketchMD5() {return _sketchMD5.c_str();};

        const char * getVersion() {return _version.c_str();};

        char * getName() {return myName;};

        char * getSerialBuffer() {return serialBuffer;};

        void dumpSystemStatusToJson(JsonObject jstr);
        void dumpCameraStatusToJson(JsonObject jstr, bool full = true);

        uint8_t getTemp() {return temperatureRead();};
        
    private:

        UriMapping *mappingList[MAX_URI_MAPPINGS]; 
        int _mappingCount=0;


        // Name of the application used in web interface
        // Can be re-defined in the httpd.json file
        char myName[32] = CAM_NAME;

        char serialBuffer[SERIAL_BUFFER_SIZE]="";

        AsyncWebServer *server;
        AsyncWebSocket *ws; 
        
        // array of clients currently streaming video 
        uint32_t stream_clients[MAX_VIDEO_STREAMS];

        uint32_t _control_client;
        
        TimerHandle_t _stream_timer = NULL;

        int8_t _streamCount=0;

        long _streamsServed=0;

        // maximum number of parallel video streams supported. This number can range from 1 to MAX_VIDEO_STREAMS
        int _max_streams=2;
        
        // Sketch Info
        int _sketchSize ;
        int _sketchSpace ;
        String _sketchMD5;

        const String _version = __DATE__ " @ " __TIME__;

};


extern CLAppHttpd AppHttpd;

#endif