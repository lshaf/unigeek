//
// Created by L Shaf on 2026-02-23.
//

#pragma once

class IKeyboard
{
public:
  virtual ~IKeyboard() = default;
  virtual void begin()  = 0;
  virtual void update() = 0;
  virtual bool available() = 0;
  virtual char peekKey()   = 0;  // read without consuming
  virtual char getKey()    = 0;
};