#ifndef app_component_h
#define app_component_h

#include <ArduinoJson.h>

#if __has_include("../myconfig.h")
#include "../myconfig.h"
#else
#include "app_config.h"
#endif

#include "storage.h"

#include <esp_log.h>

#define TAG_LENGTH 32

/**
 * @brief Abstract root class for the application components.
 * 
 */
class CLAppComponent {
    public:
    // Sketch Info
    
        virtual int start(){return OK;};
        virtual int loadPrefs(){return OK;};
        virtual int savePrefs(){return OK;};
        
        virtual void dumpPrefs();
        virtual int removePrefs();
        
        char * getPrefsFileName(bool forsave = false);

        int getLastErr() {return last_err;};

        bool isConfigured() {return configured;};

        const char* getTag() {return tag;};

    protected:
        // prefix for forming preference file name of this class
        const char * tag;   
        const char * prefix;

        bool configured = false;

        void setTag(const char *t) {tag = t;};
        void setPrefix(const char *p) {prefix = p;};

        void setErr(int err_code) {last_err = err_code;};

        int parsePrefs(JsonDocument *jctx);

        int savePrefsToFile(JsonDocument *jctx);

        int urlDecode(char * decoded, char * source, size_t len); 
        int urlEncode(char * encoded, char * source, size_t len);


    private:

        // error code of the last error
        int last_err = 0;

        char prefs[TAG_LENGTH] = "prefs.json";
};

#endif