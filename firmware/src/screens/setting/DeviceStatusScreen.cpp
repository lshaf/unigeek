#include "screens/setting/DeviceStatusScreen.h"
#include "screens/setting/SettingScreen.h"
#include "core/Device.h"
#include "core/ScreenManager.h"
#include "core/AchievementManager.h"

static String _fmtSize(uint64_t bytes) {
  if (bytes >= 1024ULL * 1024)
    return String((float)bytes / (1024.0f * 1024.0f), 1) + " MB";
  return String((uint32_t)(bytes / 1024)) + " KB";
}

static String _fmtUsedTotal(uint64_t used, uint64_t total) {
  return _fmtSize(used) + " / " + _fmtSize(total);
}

void DeviceStatusScreen::onInit() {
  uint8_t i = 0;

  _cpuFreq = String(ESP.getCpuFreqMHz()) + " MHz";
  _rows[i++] = {"CPU", _cpuFreq};

  uint32_t ramTotal = ESP.getHeapSize();
  uint32_t ramFree  = ESP.getFreeHeap();
  _ramFree  = _fmtUsedTotal(ramTotal - ramFree, ramTotal);
  _rows[i++] = {"RAM", _ramFree};

  if (ESP.getPsramSize() > 0) {
    uint32_t psTotal = ESP.getPsramSize();
    uint32_t psFree  = ESP.getFreePsram();
    _psramFree  = _fmtUsedTotal(psTotal - psFree, psTotal);
    _rows[i++]  = {"PSRAM", _psramFree};
    _totalFree  = _fmtSize(ramFree + psFree);
    _rows[i++]  = {"Total Free", _totalFree};
  }

  if (Uni.StorageLFS && Uni.StorageLFS->isAvailable()) {
    uint64_t lfsTotal = Uni.StorageLFS->totalBytes();
    uint64_t lfsUsed  = Uni.StorageLFS->usedBytes();
    _lfsFree = _fmtUsedTotal(lfsUsed, lfsTotal);
    _rows[i++] = {"LittleFS", _lfsFree};
  } else {
    _rows[i++] = {"LittleFS", "N/A"};
  }

  if (Uni.StorageSD && Uni.StorageSD->isAvailable()) {
    uint64_t sdTotal = Uni.StorageSD->totalBytes();
    uint64_t sdUsed  = Uni.StorageSD->usedBytes();
    _sdFree = _fmtUsedTotal(sdUsed, sdTotal);
    _rows[i++] = {"SD Card", _sdFree};
  } else {
    _rows[i++] = {"SD Card", "N/A"};
  }

  _rowCount = i;
  setRows(_rows, _rowCount);

  int n = Achievement.inc("device_status_viewed");
  if (n == 1) Achievement.unlock("device_status_viewed");
}

void DeviceStatusScreen::onBack() {
  Screen.goBack();
}
