#include "../src/win32_input.h"

#include <cstdlib>
#include <iostream>
#include <set>
#include <vector>

namespace {

void Check(bool condition, const char* expression, int line) {
    if (!condition) {
        std::cerr << "line " << line << ": " << expression << '\n';
        std::exit(EXIT_FAILURE);
    }
}

#define CHECK(expression) Check((expression), #expression, __LINE__)

std::vector<INPUT> attempts;
std::set<std::size_t> failures;
bool forward_down = false;
bool left_down = false;

// Never calls SendInput: tests cannot type or click into the CI desktop.
UINT WINAPI RecordInput(UINT count, LPINPUT events, int size) {
    CHECK(count == 1);
    CHECK(size == static_cast<int>(sizeof(INPUT)));
    CHECK(events != nullptr);
    const std::size_t index = attempts.size();
    attempts.push_back(events[0]);
    if (failures.count(index) != 0) return 0;
    if (events[0].type == INPUT_KEYBOARD) {
        forward_down = (events[0].ki.dwFlags & KEYEVENTF_KEYUP) == 0;
    } else if (events[0].type == INPUT_MOUSE) {
        left_down = (events[0].mi.dwFlags & MOUSEEVENTF_LEFTDOWN) != 0;
    } else {
        CHECK(false);
    }
    return 1;
}

void Reset() {
    CHECK(!forward_down);
    CHECK(!left_down);
    attempts.clear();
    failures.clear();
}

void CheckKeyboard(std::size_t index, bool up) {
    CHECK(index < attempts.size());
    const INPUT& event = attempts[index];
    CHECK(event.type == INPUT_KEYBOARD);
    CHECK(event.ki.wVk == 0);
    CHECK(event.ki.wScan == 0x11);
    const DWORD expected = static_cast<DWORD>(KEYEVENTF_SCANCODE | (up ? KEYEVENTF_KEYUP : 0));
    CHECK(event.ki.dwFlags == expected);
    CHECK(event.ki.time == 0);
    CHECK(event.ki.dwExtraInfo == 0);
}

void CheckMouse(std::size_t index, DWORD flags) {
    CHECK(index < attempts.size());
    const INPUT& event = attempts[index];
    CHECK(event.type == INPUT_MOUSE);
    CHECK(event.mi.dwFlags == flags);
    CHECK(event.mi.dx == 0);
    CHECK(event.mi.dy == 0);
    CHECK(event.mi.mouseData == 0);
    CHECK(event.mi.time == 0);
    CHECK(event.mi.dwExtraInfo == 0);
}

void TestNormalInput() {
    Reset();
    {
        wclicker::Win32Input input(&RecordInput);
        CHECK(input.ReleaseAll());
        CHECK(attempts.empty());
        CHECK(input.HoldForward());
        CHECK(forward_down);
        CHECK(input.HoldForward());
        CHECK(attempts.size() == 1);
        CheckKeyboard(0, false);
        CHECK(input.ClickLeft());
        CHECK(!left_down);
        CheckMouse(1, MOUSEEVENTF_LEFTDOWN);
        CheckMouse(2, MOUSEEVENTF_LEFTUP);
        CHECK(input.ReleaseAll());
        CheckKeyboard(3, true);
        CHECK(!forward_down);
        CHECK(input.ReleaseAll());
        CHECK(attempts.size() == 4);
    }
    CHECK(attempts.size() == 4);
}

void TestPartialMouseFailureCleanup() {
    Reset();
    {
        wclicker::Win32Input input(&RecordInput);
        CHECK(input.HoldForward());
        failures.insert(2);  // Reject only the first mouse up.
        CHECK(!input.ClickLeft());
        CHECK(forward_down);
        CHECK(left_down);
        CHECK(input.ReleaseAll());
        CheckMouse(3, MOUSEEVENTF_LEFTUP);
        CheckKeyboard(4, true);
        CHECK(!left_down);
        CHECK(!forward_down);
        CHECK(input.ReleaseAll());
        CHECK(attempts.size() == 5);
    }
}

void TestReleaseRetries() {
    Reset();
    {
        wclicker::Win32Input input(&RecordInput);
        CHECK(input.HoldForward());
        failures.insert(1);
        CHECK(!input.ReleaseAll());
        CHECK(forward_down);
        CheckKeyboard(1, true);
        CHECK(input.ReleaseAll());
        CheckKeyboard(2, true);
        CHECK(!forward_down);
    }
    Reset();
    {
        wclicker::Win32Input input(&RecordInput);
        CHECK(input.HoldForward());
        failures.insert(2);
        failures.insert(3);
        CHECK(!input.ClickLeft());
        CHECK(!input.ReleaseAll());
        CHECK(left_down);
        CHECK(!forward_down);  // A mouse-up failure must not skip the key-up.
        CheckKeyboard(4, true);
        CHECK(input.ReleaseAll());
        CheckMouse(5, MOUSEEVENTF_LEFTUP);
        CHECK(!left_down);
    }
}

void TestRejectedDownAndDestructor() {
    Reset();
    {
        wclicker::Win32Input input(&RecordInput);
        failures.insert(0);
        CHECK(!input.HoldForward());
        CHECK(input.ReleaseAll());
        CHECK(attempts.size() == 1);
        failures.insert(1);
        CHECK(!input.ClickLeft());
        CHECK(input.ReleaseAll());
        CHECK(attempts.size() == 2);
    }
    Reset();
    {
        wclicker::Win32Input input(&RecordInput);
        CHECK(input.HoldForward());
        CHECK(forward_down);
    }
    CHECK(!forward_down);
    CheckKeyboard(1, true);
}

void TestEngineWithPartialSendFailure() {
    Reset();
    {
        wclicker::Win32Input input(&RecordInput);
        wclicker::Engine engine(input);
        failures.insert(2);
        CHECK(!engine.Start(0));
        CHECK(engine.state() == wclicker::State::Stopped);
        CHECK(engine.error() == wclicker::Error::Click);
        CHECK(!forward_down);
        CHECK(!left_down);
        CHECK(attempts.size() == 5);
        CHECK(engine.Tick(10000));
        CHECK(attempts.size() == 5);
    }
}

}  // namespace

int main() {
    TestNormalInput();
    TestPartialMouseFailureCleanup();
    TestReleaseRetries();
    TestRejectedDownAndDestructor();
    TestEngineWithPartialSendFailure();
    std::cout << "Win32 input tests passed (mock transport; no desktop input)\n";
    return EXIT_SUCCESS;
}
