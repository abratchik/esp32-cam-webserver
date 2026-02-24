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
            AppMailSender.disconnect();
            AppMailSender.resetBuffer();
            AppMailSender.scheduleNext();
            AppMailSender.resetTimeout();
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


int CLAppMailSender::loadFromJson(JsonObject jctx, bool full_set) {
    username = jctx[FPSTR(MAIL_USERNAME)] | "";
    password = jctx[FPSTR(MAIL_PASSWORD)] | "";
    smtp_server = jctx[FPSTR(MAIL_SMTP_SERVER)] | "";
    smtp_port = jctx[FPSTR(MAIL_SMTP_PORT)] | 465;
    from_email = jctx[FPSTR(MAIL_FROM)] | "";
    to_email = jctx[FPSTR(MAIL_TO)] | "";
    subject = jctx[FPSTR(MAIL_SUBJECT)] | "";
    message = jctx[FPSTR(MAIL_MESSAGE)] | "";
    html_message = jctx[FPSTR(MAIL_HTML_MESSAGE)] | "";
    snaponstart = jctx[FPSTR(MAIL_SNAPONSTART)] | false;
    sleeponcomplete = jctx[FPSTR(MAIL_SLEEPONCOMPLETE)] | false;
    period = jctx[FPSTR(MAIL_PERIOD)] | TimePeriod::NONE;
    num_periods = jctx[FPSTR(MAIL_NUM_PERIODS)] | 0;
    start_at = parseTimeFromToken(jctx, FPSTR(MAIL_START_AT));
    finish_at = parseTimeFromToken(jctx, FPSTR(MAIL_FINISH_AT));

    configured = jctx[FPSTR(APP_CONFIGURED_PARAM)] | false;

    pendingsnap = configured && snaponstart;
    buffer_sent = false;

    return configured?OK:FAIL;
}


int CLAppMailSender::saveToJson(JsonObject jctx, bool full_set) {
    jctx[FPSTR(MAIL_SMTP_SERVER)] = smtp_server;
    jctx[FPSTR(MAIL_SMTP_PORT)] = smtp_port;
    jctx[FPSTR(MAIL_FROM)] = from_email;
    jctx[FPSTR(MAIL_TO)] = to_email;
    jctx[FPSTR(MAIL_SUBJECT)] = subject;
    jctx[FPSTR(MAIL_MESSAGE)] = message;
    jctx[FPSTR(MAIL_HTML_MESSAGE)] = html_message;
    jctx[FPSTR(MAIL_SNAPONSTART)] = snaponstart;
    jctx[FPSTR(MAIL_SLEEPONCOMPLETE)] = sleeponcomplete;
    jctx[FPSTR(MAIL_PERIOD)] = period;
    jctx[FPSTR(MAIL_NUM_PERIODS)] = num_periods;
    saveTimeToToken(jctx, FPSTR(MAIL_START_AT), &start_at);
    saveTimeToToken(jctx, FPSTR(MAIL_FINISH_AT), &finish_at);

    if(!full_set) return OK;
    
    jctx[FPSTR(MAIL_USERNAME)] = username;
    jctx[FPSTR(MAIL_PASSWORD)] = password;
    jctx[FPSTR(APP_CONFIGURED_PARAM)] = configured;
    return OK;
}

int IRAM_ATTR storeBufImgCallback(uint8_t* buffer, size_t size) {
    return AppMailSender.storeBufImg(buffer, size);
}

int CLAppMailSender::mailImage() {
    time(&snaptime);
    pendingsnap = false;
    int result = AppCam.snapStillImage(storeBufImgCallback);
    return result;
}

uint32_t CLAppMailSender::getSecondsTillFire() {
    // if start date is in future, we need to sleep till that date
    time_t current_time = time(nullptr) + AppConn.getGmtOffset_sec(); 
    if(start_at > current_time) {
        return start_at - current_time;  
    }

    // if finish date is in past, we return 0 (no sleep!)
    if(finish_at !=0 && finish_at < current_time) {
        return 0;
    }

    return TIME_PERIODS[period] * num_periods;
}

void CLAppMailSender::scheduleNext() {
    uint32_t seconds_till_fire = getSecondsTillFire();
    if(seconds_till_fire) {
        if(sleeponcomplete) {
            ESP_LOGI(tag, "Going to hibernate for %lu seconds", seconds_till_fire);
            hibernate(seconds_till_fire);
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

    // store the time we sent the mail
    ms_on_send = millis();

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

    if(ms_on_send > 0 && millis() - ms_on_send > MAIL_TIMEOUT) {
        disconnect();
        resetTimeout();
        resetBuffer();
        scheduleNext();
        return;
    }

    if(smtp_client->isProcessing()) {
        return;
    }

    if(img_in_buffer && !smtp_client->isConnected()) {
        smtp_client->connect(smtp_server.c_str(), smtp_port, smtpStatusCallback, SSL_MODE, NO_WAIT);
    }

    if(smtp_client->isConnected() && !smtp_client->isAuthenticated()) {
        smtp_client->authenticate(username, password, readymail_auth_password, NO_WAIT);
    }

    if ( smtp_client->isAuthenticated() ) {
        sendMail();
    }

}

CLAppMailSender AppMailSender;

