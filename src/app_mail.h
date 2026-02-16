#ifndef APP_MAIL_H
#define APP_MAIL_H

#include "app_defines.h"
#include "app_component.h"
#include "app_cam.h"

#define ENABLE_SMTP
#define ENABLE_DEBUG

#define uS_TO_S_FACTOR 1000000ULL /* Conversion factor for micro seconds to seconds */

#include <WiFiClientSecure.h>
#include <ReadyMail.h>

#include <cstring>
#include <esp_timer.h>

#include <esp_log.h>

#define SSL_MODE true
#define NO_WAIT false
#define NOTIFY "SUCCESS,FAILURE,DELAY"

#define IMAGE_MIME "image/jpeg"

using MailSharedBuffer = std::shared_ptr<std::vector<uint8_t>>;

const char MAIL_USERNAME[] PROGMEM = "username";
const char MAIL_PASSWORD[] PROGMEM = "password";
const char MAIL_SMTP_SERVER[] PROGMEM = "smtp_server";
const char MAIL_SMTP_PORT[] PROGMEM = "smtp_port";
const char MAIL_FROM[] PROGMEM = "from";
const char MAIL_TO[] PROGMEM = "to";
const char MAIL_SUBJECT[] PROGMEM = "subject";
const char MAIL_MESSAGE[] PROGMEM = "message";
const char MAIL_HTML_MESSAGE[] PROGMEM = "html_message";
const char MAIL_SNAPONSTART[] PROGMEM = "snaponstart";
const char MAIL_SLEEPONCOMPLETE[] PROGMEM = "sleeponcomplete";
const char MAIL_PERIOD[] PROGMEM = "period"; 
const char MAIL_NUM_PERIODS[] PROGMEM = "num_periods";
const char MAIL_START_AT[] PROGMEM = "start";
const char MAIL_FINISH_AT[] PROGMEM = "finish";

const char MAIL_IMG_FILENAME[] PROGMEM = "photo.jpg";

enum TimePeriod : uint8_t {
    NONE=0,
    MINUTE=1,
    HOUR=2,
    DAY=3,
    WEEK=4
};

const uint32_t time_periods[] = {0, 60, 3600, 86400, 604800};

class CLAppMailSender : public CLAppComponent {
    public:
        CLAppMailSender() {
            setTag("mail");
            ms = 0;
        };

        int start();
        void process();

        int loadPrefs();
        int savePrefs();
    
        int mailImage();
        int storeBufImg(uint8_t* buffer, size_t size);

        void resetBuffer() {
            img_buffer.reset();
            img_in_buffer = false;
            buffer_sent = false;
        }

        void scheduleNext();

        const char* getSMTPServer() {return smtp_server.c_str();};
        uint16_t getSMTPPort() {return smtp_port;};
        const char* getToEmail() {return to_email.c_str();};
        const char* getFromEmail() {return from_email.c_str();};

        bool isSnapOnStart() { return snaponstart; };
        bool isPendingSnap() {return pendingsnap;};
        bool isSleepOnComplete() { return sleeponcomplete;};

        void setPendingSnap() {pendingsnap = configured;};

        // returns number of seconds till schedule event. 0 means period is NONE or 
        // finish time is in past.
        uint32_t getSecondsTillFire();

    protected:
        void sendMail();

    private:
        String username;
        String password;
        String smtp_server;
        uint16_t smtp_port;
        String from_email;
        String to_email;
        String subject;
        String message;
        String html_message;
        bool snaponstart;
        bool pendingsnap;
        bool sleeponcomplete;
        TimePeriod period;
        uint16_t num_periods;

        // start date/time of the periodic snapshots (UTC)
        time_t start_at;

        // stop date/time of the periodic snapshots (UTC)
        time_t finish_at;

        time_t snaptime;

        esp_timer_handle_t online_timer;

        unsigned long ms;

        bool buffer_sent;

        WiFiClientSecure* ssl_client;
        SMTPClient* smtp_client;

        MailSharedBuffer img_buffer;
        bool img_in_buffer = false;

};

extern CLAppMailSender AppMailSender;

#endif
