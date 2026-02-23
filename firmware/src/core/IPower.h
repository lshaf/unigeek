//
// Created by L Shaf on 2026-02-19.
//

#pragma once

class IPower
{
public:
  virtual ~IPower() = default;
  virtual void begin() = 0;
  virtual uint8_t getBatteryPercentage() = 0;
  virtual bool isCharging() = 0;
  virtual void powerOff() = 0;
};