#ifndef APP_MAIL_H
#define APP_MAIL_H

#include "app_defines.h"
#include "app_component.h"
#include "app_cam.h"
#include "app_conn.h"
#include "utils.h"

#define ENABLE_SMTP
#define ENABLE_DEBUG
#define MAIL_TIMEOUT 30 * 1000

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

const char MAIL_USERNAME[] PROGMEM = "smtp_user";
const char MAIL_PASSWORD[] PROGMEM = "smtp_pass";
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

class CLAppMailSender : public CLAppComponent {
    public:
        CLAppMailSender() {
            setTag("mail");
            ms_on_send = 0;
        };

        int start();
        void process();

        int loadFromJson(JsonObject jctx, bool full_set = true);
        int saveToJson(JsonObject jctx, bool full_set = true);
    
        int mailImage();
        int storeBufImg(uint8_t* buffer, size_t size);

        void resetBuffer() {
            img_buffer.reset();
            img_in_buffer = false;
            buffer_sent = false;
        }

        void resetTimeout() {ms_on_send = 0;};
        
        void disconnect() { smtp_client->stop(); };

        void scheduleNext();

        const char* getSMTPServer() {return smtp_server.c_str();};
        uint16_t getSMTPPort() {return smtp_port;};
        const char* getTo() {return to_email.c_str();};
        const char* getFrom() {return from_email.c_str();};
        const char* getUser() {return username.c_str();};
        uint8_t getPeriod() {return period;};
        uint16_t getNumPeriods() {return num_periods;};


        void saveStartAtToJson(JsonObject jctx);
        void saveFinishAtToJson(JsonObject jctx);

        bool isSnapOnStart() { return snaponstart; };
        bool isPendingSnap() {return pendingsnap;};
        bool isSleepOnComplete() { return sleeponcomplete;};

        void setPendingSnap() {pendingsnap = isConfigured();};

        // returns number of seconds till schedule event. 0 means period is NONE or 
        // finish time is in past.
        uint32_t getSecondsTillFire();

        void setSMTPServer(const char* server) {smtp_server = server; };
        void setSMTPPort(uint16_t port) {smtp_port = port; };
        void setFrom(const char* email) {from_email = email;};
        void setTo(const char* email) {to_email = email;};
        void setSnapOnStart(bool val) {snaponstart = val;};
        void setSleepOnComplete(bool val) {sleeponcomplete = val;};
        void setUser(const char* user) {username = user;}
        void setPwd(const char* pwd) {password = pwd;}
        void setPeriod(uint8_t p) { period = (TimePeriod)p;};
        void setNumPeriods(uint16_t np) {num_periods = np;};

        void setStartAt(const String& stime);
        void setFinishAt(const String& stime);

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

        unsigned long ms_on_send;

        bool buffer_sent;

        WiFiClientSecure* ssl_client;
        SMTPClient* smtp_client;

        MailSharedBuffer img_buffer;
        bool img_in_buffer = false;

};

extern CLAppMailSender AppMailSender;

#endif
