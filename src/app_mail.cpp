#include "app_mail.h"

void smtpStatusCallback(SMTPStatus status) {
    if (status.progress.available) {
        ESP_LOGI(AppMailSender.getTag(), "[smtp][%d] Uploading file %s, %d %% completed", status.state,
                         status.progress.filename.c_str(), status.progress.value);
    }
    else
        ESP_LOGI(AppMailSender.getTag(), "[smtp][%d]%s\n", status.state, status.text.c_str());
}

int CLAppMailSender::start() {
    int ret = loadPrefs();
    if(ret != OK) {
        return ret;
    }

    ssl_client = new WiFiClientSecure();

    ssl_client->setInsecure();

    smtp_client = new SMTPClient(*ssl_client);

    return OK;
}

int CLAppMailSender::loadPrefs() {
    JsonDocument doc;
    int ret  = parsePrefs(&doc);
    if(ret != OK) {
        return ret;
    }
    username = doc[FPSTR(MAIL_USERNAME)] | "";
    password = doc[FPSTR(MAIL_PASSWORD)] | "";
    smtp_server = doc[FPSTR(MAIL_SMTP_SERVER)] | "";
    smtp_port = doc[FPSTR(MAIL_SMTP_PORT)] | 465;
    from_email = doc[FPSTR(MAIL_FROM)] | "";
    to_email = doc[FPSTR(MAIL_TO)] | "";
    subject = doc[FPSTR(MAIL_SUBJECT)] | "";
    message = doc[FPSTR(MAIL_MESSAGE)] | "";
    html_message = doc[FPSTR(MAIL_HTML_MESSAGE)] | "";
    
    configured = doc[FPSTR(APP_CONFIGURED_PARAM)] | false;

    return configured?OK:FAIL;
}

int CLAppMailSender::savePrefs() {
    JsonDocument doc;
    JsonObject jstr = doc.to<JsonObject>();

    jstr[FPSTR(MAIL_USERNAME)] = username;
    jstr[FPSTR(MAIL_PASSWORD)] = password;
    jstr[FPSTR(MAIL_SMTP_SERVER)] = smtp_server;
    jstr[FPSTR(MAIL_SMTP_PORT)] = smtp_port;
    jstr[FPSTR(MAIL_FROM)] = from_email;
    jstr[FPSTR(MAIL_TO)] = to_email;
    jstr[FPSTR(MAIL_SUBJECT)] = subject;
    jstr[FPSTR(MAIL_MESSAGE)] = message;
    jstr[FPSTR(MAIL_HTML_MESSAGE)] = html_message;

    jstr[FPSTR(APP_CONFIGURED_PARAM)] = configured;

    return savePrefsToFile(&doc);
}

int IRAM_ATTR storeBufImgCallback(uint8_t* buffer, size_t size) {
    return AppMailSender.storeBufImg(buffer, size);
}

int CLAppMailSender::mailImage() {
    ESP_LOGI(tag, "Mailing image...");
    return AppCam.snapFrame(storeBufImgCallback);
}

int CLAppMailSender::storeBufImg(uint8_t* buffer, size_t size) {
    if(!buffer || !size) {
        return FAIL;
    }

    // store the image in the file system and set the path to the message
    // for later retrieval when sending the email

    return OK;
}

void CLAppMailSender::sendMail() {
    if(!isConfigured()) {
        ESP_LOGE(tag, "SMTP client not initialized");
        return;
    }

    SMTPMessage &msg = smtp_client->getMessage();

    msg.headers.add(rfc822_header_types::rfc822_subject, subject);
    msg.headers.add(rfc822_header_types::rfc822_from, from_email);
    msg.headers.add(rfc822_header_types::rfc822_to, to_email);

    msg.text.body(message);
    if(html_message) {
        msg.html.body(html_message);
    }
    msg.timestamp = time(nullptr);

    // addBlobAttachment(msg, "green.png", "image/png", "green.png", (const uint8_t *)greenImg, strlen(greenImg), "base64");
    smtp_client->send(msg, NOTIFY, NO_WAIT);

}


void CLAppMailSender::process() {
    if(!isConfigured()) {
        return;
    }

    if(!smtp_client) {
        return;
    }

    smtp_client->loop();

    if(smtp_client->isProcessing()) {
        return;
    }

    if(!smtp_client->isConnected()) {
        smtp_client->connect(smtp_server.c_str(), smtp_port, smtpStatusCallback, SSL_MODE, NO_WAIT);
    }

    if(smtp_client->isConnected() && !smtp_client->isAuthenticated()) {
        smtp_client->authenticate(username, password, readymail_auth_password, NO_WAIT);
    }

    if ((millis() - ms > 20 * 1000 || ms == 0) && smtp_client->isAuthenticated()) {
        ms = millis();
        sendMail();
    }
}

CLAppMailSender AppMailSender;

