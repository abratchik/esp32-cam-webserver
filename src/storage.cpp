#include "storage.h"

void CLStorage::listDir(const char * dirname, uint8_t levels){
#if (CONFIG_LOG_DEFAULT_LEVEL >= CORE_INFO_LEVEL )
  ESP_LOGI(tag, "Listing directory: %s", dirname);

  File root = fsStorage->open(dirname);
  if(!root){
    ESP_LOGW(tag,"Failed to open directory");
    return;
  }
  if(!root.isDirectory()){
    ESP_LOGW(tag,"Not a directory");
    return;
  }
  
  root.rewindDirectory();

  File file = root.openNextFile();
  while(file){
    if(file.isDirectory()) {
      Serial.print("  DIR : ");
      Serial.println(file.name());
      if(levels){
        listDir(file.name(), levels -1);
      }
    } else {
      Serial.print("  FILE: ");
      Serial.print(file.name());
      Serial.print("\tSIZE: ");
      Serial.println(file.size());
    }
    file = root.openNextFile();
  }
#endif
}


bool CLStorage::init() {
#ifdef USE_LittleFS
  return fsStorage->begin(FORMAT_LITTLEFS_IF_FAILED);
#else
#if defined(CAMERA_MODEL_LILYGO_T_SIMCAM)
  SPI.begin(SD_SCLK_PIN, SD_MISO_PIN, SD_MOSI_PIN, SD_CS_PIN);
  if(!fsStorage->begin(SD_CS_PIN, SPI)) return false;
#else
  if(!fsStorage->begin("/root", true, false, SDMMC_FREQ_DEFAULT)) return false;
#endif

  uint8_t cardType = fsStorage->cardType();

  switch(cardType) {
    case CARD_NONE:
      ESP_LOGE(tag,"No SD card attached");
      return false;
    case CARD_MMC:
      ESP_LOGI(tag,"MMC");
      break;
    case CARD_SD:
      ESP_LOGI(tag,"SDSC");
      break;
    case CARD_SDHC:
      ESP_LOGI(tag,"SDHC");
      break;
    default:
      ESP_LOGW(tag,"Unknown Type");
      break;    
  }

  ESP_LOGI(tag,"Card size: %dMB", getSize());
  
  return true;
#endif
}


int CLStorage::readFileToString(char *path, String *s)
{
	File file = fsStorage->open(path);
	if (!file)
		return FAIL;

	while (file.available())
	{
		char charRead = file.read();
		*s += charRead;
	}
  file.close();
	return OK;
}

uint16_t CLStorage::getSize() {
  return (uint16_t) ((double) fsStorage->totalBytes() / pow(1024, STORAGE_UNITS));
}

int CLStorage::getUsed() {
  return (int) ((double) fsStorage->usedBytes() / pow(1024, STORAGE_UNITS));
}

int CLStorage::capacityUnits() {
  return STORAGE_UNITS;
}

CLStorage Storage;
