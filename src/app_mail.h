#ifndef APP_MAIL_H
#define APP_MAIL_H

#include "app_defines.h"
#include "app_component.h"
#include "app_cam.h"

#define ENABLE_SMTP
#define ENABLE_DEBUG

#include <WiFiClientSecure.h>
#include <ReadyMail.h>

#include <esp_log.h>

#define SSL_MODE true
#define NO_WAIT false
#define NOTIFY "SUCCESS,FAILURE,DELAY"

const char MAIL_USERNAME[] PROGMEM = "username";
const char MAIL_PASSWORD[] PROGMEM = "password";
const char MAIL_SMTP_SERVER[] PROGMEM = "smtp_server";
const char MAIL_SMTP_PORT[] PROGMEM = "smtp_port";
const char MAIL_FROM[] PROGMEM = "from";
const char MAIL_TO[] PROGMEM = "to";
const char MAIL_SUBJECT[] PROGMEM = "subject";
const char MAIL_MESSAGE[] PROGMEM = "message";
const char MAIL_HTML_MESSAGE[] PROGMEM = "html_message";

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

        unsigned long ms;

        WiFiClientSecure* ssl_client;
        SMTPClient* smtp_client;

};

extern CLAppMailSender AppMailSender;

#endif
