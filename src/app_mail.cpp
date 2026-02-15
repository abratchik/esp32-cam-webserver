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
            AppMailSender.scheduleNext();
        }
    }
}

void onlineTimerCallback(void* arg) {
    AppMailSender.setPendingSnap();
}

int CLAppMailSender::start() {
    int ret = loadPrefs();
    if(ret != OK) {
        return ret;
    }

    ssl_client = new WiFiClientSecure();

    ssl_client->setInsecure();

    smtp_client = new SMTPClient(*ssl_client);

    esp_timer_create_args_t timer_args = {
        .callback = &onlineTimerCallback,
        .arg = nullptr,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "online_timer"
    };

    esp_timer_create(&timer_args, &online_timer);

    return OK;
}

time_t parseTimeFromToken(JsonObject jctx, const __FlashStringHelper* token) {
    String stime = jctx[token];
    if(stime.isEmpty() || stime == "") return (time_t)0;
    struct tm tm_time = {0};
    if(strptime(stime.c_str(), APP_DATETIME_FORMAT, &tm_time) != NULL) {
        return mktime(&tm_time);
    }
    return (time_t)0;
}

void timeToStr(char* strbuf, size_t size, time_t* t_value) {
    struct tm tm_time;
    localtime_r(t_value, &tm_time);
    strftime(strbuf, size, APP_DATETIME_FORMAT, &tm_time);
}

void saveTimeToToken(JsonObject jctx, const __FlashStringHelper* token, time_t* t_value) {
    char strbuf[20];
    timeToStr(strbuf, sizeof(strbuf), t_value);
    jctx[token] = strbuf;
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
    period = doc[FPSTR(MAIL_PERIOD)] | TimePeriod::NONE;
    num_periods = doc[FPSTR(MAIL_NUM_PERIODS)] | 0;
    start_at = parseTimeFromToken(doc.as<JsonObject>(), FPSTR(MAIL_START_AT));
    finish_at = parseTimeFromToken(doc.as<JsonObject>(), FPSTR(MAIL_FINISH_AT));

    configured = doc[FPSTR(APP_CONFIGURED_PARAM)] | false;

    pendingsnap = configured && snaponstart;
    buffer_sent = false;

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
    jstr[FPSTR(MAIL_NUM_PERIODS)] = num_periods;
    saveTimeToToken(jstr, FPSTR(MAIL_START_AT), &start_at);
    saveTimeToToken(jstr, FPSTR(MAIL_FINISH_AT), &finish_at);

    jstr[FPSTR(APP_CONFIGURED_PARAM)] = configured;

    return savePrefsToFile(&doc);
}

int IRAM_ATTR storeBufImgCallback(uint8_t* buffer, size_t size) {
    return AppMailSender.storeBufImg(buffer, size);
}

int CLAppMailSender::mailImage() {
    time(&snaptime);
    pendingsnap = false;
    return AppCam.snapStillImage(storeBufImgCallback);
}

uint32_t CLAppMailSender::getSecondsTillFire() {
    // if start date is in future, we need to sleep till that date
    time_t current_time = time(nullptr);
    if(start_at > current_time) {
        return start_at - current_time;  
    }

    // if finish date is in past, we return 0 (no sleep!)
    if(finish_at !=0 && finish_at < time(nullptr)) {
        return 0;
    }

    return time_periods[period] * num_periods;
}

void CLAppMailSender::scheduleNext() {
    uint32_t seconds_till_fire = getSecondsTillFire();
    if(seconds_till_fire) {
        if(sleeponcomplete) {
            ESP_LOGI(tag, "Going to hybernate for %lu seconds", seconds_till_fire);
            esp_sleep_enable_timer_wakeup(uS_TO_S_FACTOR * seconds_till_fire);
            esp_deep_sleep_disable_rom_logging();
            esp_deep_sleep_start();
        }
        else {
            ESP_LOGI(tag, "Schedule snap & mail image in %lu seconds", seconds_till_fire);
            esp_timer_start_once(online_timer, uS_TO_S_FACTOR * seconds_till_fire);
        }
    }
}

int CLAppMailSender::storeBufImg(uint8_t* buffer, size_t size) {
    if(img_in_buffer) {
        ESP_LOGI(tag, "Unsent image already in buffer, dropping");
        img_in_buffer = false;
    }

    img_buffer.reset();

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

    if(buffer_sent) return;

    ESP_LOGI(tag, "Sending image to %s", to_email.c_str());

    SMTPMessage &msg = smtp_client->getMessage();

    msg.headers.add(rfc822_header_types::rfc822_subject, subject);
    msg.headers.add(rfc822_header_types::rfc822_from, from_email);
    msg.headers.add(rfc822_header_types::rfc822_to, to_email);

    char localtime[20];
    timeToStr(localtime, sizeof(localtime), &snaptime);

    String txt = message;
    txt.replace("%TIME%", localtime);
    String html = html_message;
    html.replace("%TIME%", localtime);
    msg.text.body(txt);
    if(html_message) {
        msg.html.body(html);
    }
    msg.timestamp = time(nullptr);

    addBlobAttachment(msg, img_buffer->data(), img_buffer->size());
    smtp_client->send(msg, NOTIFY, NO_WAIT);
    buffer_sent = true;


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

    if ( (millis() - ms > 20 * 1000 || ms == 0) && smtp_client->isAuthenticated() ) {
        ms = millis();
        sendMail();
    }
}

CLAppMailSender AppMailSender;

