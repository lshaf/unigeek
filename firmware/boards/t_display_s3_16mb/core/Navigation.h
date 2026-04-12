#pragma once
#include "core/INavigation.h"

class NavigationImpl : public INavigation {
public:
    void begin() override {
        pinMode(BTN_UP, INPUT_PULLUP);
        pinMode(BTN_B,  INPUT_PULLUP);
    }

    void update() override {
        bool btnUp   = (digitalRead(BTN_UP) == LOW);
        bool btnDown = (digitalRead(BTN_B)  == LOW);
        uint32_t now = millis();

        if (btnUp) {
            _waitDblClick = false;
            updateState(DIR_UP);
            return;
        }

        // Synthetic single-click: emit DIR_DOWN for one cycle then DIR_NONE
        if (_syntheticDown) {
            _syntheticDown = false;
            updateState(DIR_NONE);
            return;
        }

        if (btnDown && !_btnBWasDown) {
            _btnBWasDown = true;

            if (_waitDblClick && (now - _lastDownTime) <= DBL_CLICK_MS) {
                _waitDblClick = false;
                _heldDir = DIR_PRESS;
                updateState(DIR_PRESS);
            } else {
                _waitDblClick = true;
                _lastDownTime = now;
                _heldDir = DIR_NONE;
                updateState(DIR_NONE);
            }
        } else if (btnDown) {
            if (_waitDblClick) {
                if ((now - _lastDownTime) > DBL_CLICK_MS) {
                    _waitDblClick = false;
                    _heldDir = DIR_DOWN;
                    updateState(DIR_DOWN);
                } else {
                    updateState(DIR_NONE);
                }
            } else {
                updateState(_heldDir);
            }
        } else {
            _btnBWasDown = false;
            _heldDir = DIR_NONE;

            if (_waitDblClick) {
                if ((now - _lastDownTime) > DBL_CLICK_MS) {
                    _waitDblClick = false;
                    _syntheticDown = true;
                    updateState(DIR_DOWN);
                    return;
                } else {
                    updateState(DIR_NONE);
                }
            } else {
                updateState(DIR_NONE);
            }
        }
    }

private:
    static constexpr uint32_t DBL_CLICK_MS = 250;

    uint32_t  _lastDownTime   = 0;
    bool      _btnBWasDown    = false;
    bool      _waitDblClick   = false;
    bool      _syntheticDown  = false;
    Direction _heldDir        = DIR_NONE;
};
