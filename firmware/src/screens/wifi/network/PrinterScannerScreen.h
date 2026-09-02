#pragma once
#include "ui/templates/ListScreen.h"
#include "utils/network/SsdpScanUtil.h"
#include "utils/network/MdnsScanUtil.h"

class PrinterScannerScreen : public ListScreen
{
public:
  const char* title() override { return "Printers"; }
  bool inhibitPowerOff() override { return _state == STATE_SCANNING; }

  void onInit() override;
  void onBack() override;
  void onItemSelected(uint8_t index) override;

private:
  enum State {
    STATE_CONFIG,
    STATE_SCANNING,
    STATE_RESULTS,
    STATE_DETAILS,
  };

  enum ScanMode { MODE_RANGE, MODE_TARGETS };

  enum ConfigAction {
    CFG_MODE,
    CFG_TARGET_1,
    CFG_TARGET_2,
    CFG_TARGET_3,
    CFG_TARGET_4,
    CFG_START_IP,
    CFG_END_IP,
    CFG_START_SCAN,
  };

  struct Printer {
    char name[48];
    char ip[16];
    char host[64];
    char source[16];
    uint16_t port;
    char service[48];
    char server[96];
    char location[200];
  };

  static constexpr uint8_t MAX_PRINTERS = 24;
  static constexpr uint8_t MAX_TARGETS = 4;
  static constexpr uint8_t MAX_CONFIG_ITEMS = 7;

  State _state = STATE_CONFIG;
  ScanMode _scanMode = MODE_RANGE;

  String _targets[MAX_TARGETS];
  String _targetSubs[MAX_TARGETS];
  int _startIp = 1;
  int _endIp = 254;
  String _startIpSub;
  String _endIpSub;

  ListItem _configItems[MAX_CONFIG_ITEMS];
  ConfigAction _configActions[MAX_CONFIG_ITEMS];
  uint8_t _configCount = 0;

  Printer _printers[MAX_PRINTERS];
  uint8_t _printerCount = 0;

  ListItem _resultItems[MAX_PRINTERS];
  String _resultSubs[MAX_PRINTERS];

  static constexpr uint8_t DETAIL_ROWS = 8;
  ListItem _detailItems[DETAIL_ROWS];
  String _detailSubs[DETAIL_ROWS];

  void _showConfig(uint8_t selectedIndex = 0);
  void _scan();
  void _showResults();
  void _showDetails(uint8_t index);
  void _editTarget(uint8_t targetIndex, uint8_t selectedIndex);
  bool _validIp(const String& ip) const;
  bool _hasTargets() const;
  bool _acceptIp(const char* ip) const;
  String _networkPrefix() const;

  Printer* _findByIp(const char* ip);
  Printer* _findByHost(const char* host);
  Printer* _addPrinter();
  void _mergeSsdp(const SsdpScanUtil::Device& dev);
  void _mergeMdns(const MdnsScanUtil::Service& svc);
};
