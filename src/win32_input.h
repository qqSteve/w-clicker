#pragma once

#include <windows.h>
#include "engine.h"

namespace wclicker {

// Injectable transport lets Windows tests exercise partial SendInput failures
// without moving the runner's mouse or typing into another application.
using SendInputFunction = UINT(WINAPI*)(UINT, LPINPUT, int);

class Win32Input final : public Input {
public:
    explicit Win32Input(SendInputFunction send = &::SendInput) : send_(send) {}
    ~Win32Input() override { ReleaseAll(); }

    bool HoldForward() override {
        if (forward_down_) return true;
        INPUT event{};
        event.type = INPUT_KEYBOARD;
        // Physical W works even when a Russian keyboard layout is selected.
        event.ki.wScan = 0x11;
        event.ki.dwFlags = KEYEVENTF_SCANCODE;
        if (!Send(event)) return false;
        forward_down_ = true;
        return true;
    }

    bool ClickLeft() override {
        if (left_down_ && !ReleaseLeft()) return false;
        INPUT event{};
        event.type = INPUT_MOUSE;
        event.mi.dwFlags = MOUSEEVENTF_LEFTDOWN;
        if (!Send(event)) return false;
        left_down_ = true;
        return ReleaseLeft();
    }

    bool ReleaseAll() override {
        const bool mouse_ok = ReleaseLeft();
        bool keyboard_ok = true;
        if (forward_down_) {
            INPUT event{};
            event.type = INPUT_KEYBOARD;
            event.ki.wScan = 0x11;
            event.ki.dwFlags = KEYEVENTF_SCANCODE | KEYEVENTF_KEYUP;
            keyboard_ok = Send(event);
            if (keyboard_ok) forward_down_ = false;
        }
        return mouse_ok && keyboard_ok;
    }

private:
    bool Send(INPUT& event) {
        return send_(1, &event, sizeof(INPUT)) == 1;
    }

    bool ReleaseLeft() {
        if (!left_down_) return true;
        INPUT event{};
        event.type = INPUT_MOUSE;
        event.mi.dwFlags = MOUSEEVENTF_LEFTUP;
        if (!Send(event)) return false;
        left_down_ = false;
        return true;
    }

    SendInputFunction send_;
    bool forward_down_ = false;
    bool left_down_ = false;
};

}  // namespace wclicker
