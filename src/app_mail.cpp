#include "app_mail.h"

MailSharedBuffer makeSharedBuffer(const uint8_t *message, size_t len) {
  auto buffer = std::make_shared<std::vector<uint8_t>>(len);
  std::memcpy(buffer->data(), message, len);
  return buffer;
}

void smtpStatusCallback(SMTPStatus status) {
    if (status.progress.available) {
        ESP_LOGI(AppMailSender.getTag(), "[smtp][%d] Uploading file %s, %d %% completed", status.state,
                         status.progress.filename.c_str(), status.progress.value);
    }
    else {
        ESP_LOGI(AppMailSender.getTag(), "[smtp][%d]%s\n", status.state, status.text.c_str());
        if(status.isComplete) {
            AppMailSender.resetBuffer();
            if(AppMailSender.isSleepOnComplete()) {
                AppMailSender.hybernate();
            }
        }
    }
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
    snaponstart = doc[FPSTR(MAIL_SNAPONSTART)] | false;
    sleeponcomplete = doc[FPSTR(MAIL_SLEEPONCOMPLETE)] | false;
    period = doc[FPSTR(MAIL_PERIOD)] | SnapToMailPeriod::NONE;
    shift = doc[FPSTR(MAIL_SHIFT)] | 0;
    shift_unit = doc[FPSTR(MAIL_SHIFT_UNIT)] | SnapToMailPeriod::NONE;
    num_periods = doc[FPSTR(MAIL_NUM_PERIODS)] | 0;
    start_at = doc[FPSTR(MAIL_START_AT)] | 0;
    finish_at = doc[FPSTR(MAIL_FINISH_AT)] | 0;
    
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
    jstr[FPSTR(MAIL_SNAPONSTART)] = snaponstart;
    jstr[FPSTR(MAIL_SLEEPONCOMPLETE)] = sleeponcomplete;
    jstr[FPSTR(MAIL_PERIOD)] = period;
    jstr[FPSTR(MAIL_SHIFT)] = shift;
    jstr[FPSTR(MAIL_SHIFT_UNIT)] = shift_unit;
    jstr[FPSTR(MAIL_NUM_PERIODS)] = num_periods;
    jstr[FPSTR(MAIL_START_AT)] = start_at;
    jstr[FPSTR(MAIL_FINISH_AT)] = finish_at;

    jstr[FPSTR(APP_CONFIGURED_PARAM)] = configured;

    return savePrefsToFile(&doc);
}

int IRAM_ATTR storeBufImgCallback(uint8_t* buffer, size_t size) {
    return AppMailSender.storeBufImg(buffer, size);
}

int CLAppMailSender::mailImage(const String& localtime) {
    _localtime = localtime;
    return AppCam.snapStillImage(storeBufImgCallback);
}

int CLAppMailSender::storeBufImg(uint8_t* buffer, size_t size) {
    if(img_in_buffer) {
        ESP_LOGI(tag, "Unsent image already in buffer, dropping");
        img_buffer.reset();
        img_in_buffer = false;
    }

    if(!buffer || !size) {
        return FAIL;
    }

    img_buffer = makeSharedBuffer(buffer, size);
    img_in_buffer = true;
    // store the image in the file system and set the path to the message
    // for later retrieval when sending the email

    return OK;
}

void addBlobAttachment(SMTPMessage &msg, const uint8_t *blob, size_t size, const String &encoding = "", const String &cid = "")
{
    Attachment attachment;
    attachment.filename = FPSTR(MAIL_IMG_FILENAME);
    attachment.mime = IMAGE_MIME;
    attachment.name = FPSTR(MAIL_IMG_FILENAME);
    // The inline content disposition.
    // Should be matched the image src's cid in html body
    attachment.content_id = cid;
    attachment.attach_file.blob = blob;
    attachment.attach_file.blob_size = size;
    // Specify only when content is already encoded.
    attachment.content_encoding = encoding;
    msg.attachments.add(attachment, cid.length() > 0 ? attach_type_inline : attach_type_attachment);
}

void CLAppMailSender::sendMail() {

    ESP_LOGI(tag, "Sending image to %s", to_email.c_str());

    SMTPMessage &msg = smtp_client->getMessage();

    msg.headers.add(rfc822_header_types::rfc822_subject, subject);
    msg.headers.add(rfc822_header_types::rfc822_from, from_email);
    msg.headers.add(rfc822_header_types::rfc822_to, to_email);

    String txt = message;
    txt.replace("%TIME%", _localtime);
    String html = html_message;
    html.replace("%TIME%", _localtime);
    msg.text.body(txt);
    if(html_message) {
        msg.html.body(html);
    }
    msg.timestamp = time(nullptr);

    addBlobAttachment(msg, img_buffer->data(), img_buffer->size());
    smtp_client->send(msg, NOTIFY, NO_WAIT);

}


void CLAppMailSender::process() {
    if(!img_in_buffer) {
        return;
    }

    if(!isConfigured()) {
        ESP_LOGE(tag, "SMTP client not configured");
        resetBuffer();
        return;
    }

    if(!smtp_client) {
        ESP_LOGE(tag, "SMTP client not initialized");
        resetBuffer();
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

    if ((millis() - ms > 20 * 1000 || ms == 0) && smtp_client->isAuthenticated() ) {
        ms = millis();
        sendMail();
    }
}

CLAppMailSender AppMailSender;

