#pragma once

class IScreen
{
public:
  virtual ~IScreen() = default;

  virtual void init()   = 0;   // called once when screen becomes active
  virtual void update() = 0;   // called every loop
  virtual void render() = 0;   // called on demand when redraw needed

  virtual void onRestore() {}  // called when screen is popped back from the stack (goBack)

  virtual bool inhibitPowerSave() { return false; }  // override to keep display always on (no screen-off, no power-off)
  virtual bool inhibitPowerOff()  { return false; }  // override to allow screen-off but block auto power-off
};