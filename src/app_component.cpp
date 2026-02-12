#include "app_component.h"

char * CLAppComponent::getPrefsFileName(bool forsave) {
    if(tag) {
        snprintf(prefs, TAG_LENGTH, "/%s.json", tag);
        if(Storage.exists(prefs) || forsave)
            return prefs;
        else {
            ESP_LOGW(tag, "Pref file %s not found, falling back to default", prefs);
            if(prefix)
              snprintf(prefs, TAG_LENGTH, "/%s_%s.json", prefix, tag);
            else
              snprintf(prefs, TAG_LENGTH, "/default_%s.json", tag);
            return prefs;
        }
    }
    else
        return prefs;
}

void CLAppComponent::dumpPrefs() {
    char *prefs_file = getPrefsFileName(); 
    String s;
    if(Storage.readFileToString(prefs_file, &s) != OK) {
        ESP_LOGE(tag,"Preference file %s not found.", prefs_file);
        return;
    }
    Serial.println(s);
}

int CLAppComponent::removePrefs() {
  char *prefs_file = getPrefsFileName(true);  
  if (Storage.exists(prefs_file)) {
    ESP_LOGI(tag, "Removing %s\r\n", prefs_file);
    if (!Storage.remove(prefs_file)) {
      ESP_LOGE(tag,"Error removing %s preferences", tag);
      return FAIL;
    }
  } else {
    ESP_LOGW(tag,"No saved %s preferences to remove", tag);
  }
  return OK;
}

int CLAppComponent::parsePrefs(JsonDocument *doc) {
  char *pref_file = getPrefsFileName(); 

  String pref_json;

  if(Storage.readFileToString(pref_file, &pref_json) != OK) {
      ESP_LOGE(tag, "Failed to open settings from %s", pref_file);
      return FAIL;
  }

  DeserializationError ret = deserializeJson(*doc, pref_json);

  if(ret != DeserializationError::Ok) {
      ESP_LOGW(tag,"Preference file %s could not be parsed; using system defaults.", pref_file);
      return FAIL;
  }

  configured = true;

  return OK;
}

int CLAppComponent::savePrefsToFile(JsonDocument *doc) {
    char * prefs_file = getPrefsFileName(true); 

    File file = Storage.open(prefs_file, FILE_WRITE);
    if(file) {
        ESP_LOGI(tag,"Saving preferences to file %s", prefs_file);
        serializeJson(*doc, file);
        file.close();
        return OK;
    }
    else {
        ESP_LOGW(tag,"Failed to save preferences to file %s", prefs_file);
        return FAIL;
    }
}

int CLAppComponent::urlDecode(char * decoded, char * source, size_t len) {
  char temp[] = "0x00";
  int i=0;
  char * ptr = decoded;
  while (i < len){
    char decodedChar;
    char encodedChar = *(source+i);
    i++;
    if ((encodedChar == '%') && (i + 1 < len)){
      temp[2] = *(source+i); i++;
      temp[3] = *(source+i); i++;
      decodedChar = strtol(temp, NULL, 16);
    } else if (encodedChar == '+') {
      decodedChar = ' ';
    } else {
      decodedChar = encodedChar;  // normal ascii char
    }
    *ptr = decodedChar;
    ptr++;
    if(decodedChar == '\0') break;
  }
  return OK;
}

int CLAppComponent::urlEncode(char * encoded, char * source, size_t len) {
  char c, code0, code1, code2;

  char * ptr = encoded;

  for(int i=0; i < len; i++) {
    c = *(source+i);
    if(c == '\0') {
      break;
    }
    else if(c == ' ') {
      *ptr = '+'; ptr++;
    }
    else if(isalnum(c)) {
      *ptr = c; ptr++;
    }
    else {

        code1 = (c & 0xf)+'0';
        if ((c & 0xf) > 9) {
            code1 = (c & 0xf) - 10 + 'A';
        }
        c = (c >> 4) & 0xf;
        code0 = c + '0';
        if (c > 9) {
            code0 = c - 10 + 'A';
        }

        *ptr = '%'; ptr++;
        *ptr = code0; ptr++;
        *ptr = code1; ptr++;

    }
  }
  *ptr = '\0';
  return OK;
}