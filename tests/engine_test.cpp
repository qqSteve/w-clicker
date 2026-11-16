#include "../src/engine.h"

#include <cstdlib>
#include <iostream>

namespace {

void Check(bool condition, const char* expression, int line) {
    if (!condition) {
        std::cerr << "line " << line << ": " << expression << '\n';
        std::exit(EXIT_FAILURE);
    }
}

#define CHECK(expression) Check((expression), #expression, __LINE__)

class FakeInput final : public wclicker::Input {
public:
    bool HoldForward() override {
        ++hold_calls;
        if (!hold_ok) return false;
        forward_down = true;
        return true;
    }

    bool ClickLeft() override {
        ++click_calls;
        if (!click_ok) {
            left_down = true;  // Simulate a down accepted but an up rejected.
            return false;
        }
        return true;
    }

    bool ReleaseAll() override {
        ++release_calls;
        if (release_failures > 0) {
            --release_failures;
            return false;
        }
        if (!release_ok) return false;
        forward_down = false;
        left_down = false;
        return true;
    }

    bool hold_ok = true;
    bool click_ok = true;
    bool release_ok = true;
    bool forward_down = false;
    bool left_down = false;
    int hold_calls = 0;
    int click_calls = 0;
    int release_calls = 0;
    int release_failures = 0;
};

void TestTiming() {
    FakeInput input;
    wclicker::Engine engine(input);
    CHECK(engine.state() == wclicker::State::Stopped);
    CHECK(engine.error() == wclicker::Error::None);
    CHECK(engine.Tick(500));
    CHECK(input.hold_calls == 0);
    CHECK(input.click_calls == 0);
    CHECK(engine.Start(200));
    CHECK(engine.state() == wclicker::State::Running);
    CHECK(input.forward_down);
    CHECK(input.click_calls == 1);
    CHECK(engine.Tick(1199));
    CHECK(input.click_calls == 1);
    CHECK(engine.Tick(1200));
    CHECK(input.click_calls == 2);
    CHECK(engine.Tick(1201));
    CHECK(input.click_calls == 2);
    CHECK(engine.Tick(5000));
    CHECK(input.click_calls == 3);
    CHECK(engine.Tick(5000));
    CHECK(engine.Tick(5999));
    CHECK(input.click_calls == 3);
    CHECK(engine.Tick(6000));
    CHECK(input.click_calls == 4);
    CHECK(engine.Start(6100));
    CHECK(input.hold_calls == 1);
    CHECK(input.click_calls == 4);
    CHECK(engine.Stop());
}

void TestPauseResumeAndStop() {
    FakeInput input;
    wclicker::Engine engine(input);
    CHECK(engine.Pause());
    CHECK(engine.state() == wclicker::State::Stopped);
    CHECK(engine.Toggle(100));
    CHECK(input.click_calls == 1);
    CHECK(engine.Toggle(700));
    CHECK(engine.state() == wclicker::State::Paused);
    CHECK(!input.forward_down);
    CHECK(engine.Tick(100000));
    CHECK(input.click_calls == 1);
    CHECK(engine.Toggle(101000));
    CHECK(engine.state() == wclicker::State::Running);
    CHECK(input.forward_down);
    CHECK(input.click_calls == 2);
    CHECK(engine.Tick(101999));
    CHECK(input.click_calls == 2);
    CHECK(engine.Tick(102000));
    CHECK(input.click_calls == 3);
    CHECK(engine.Stop());
    CHECK(engine.state() == wclicker::State::Stopped);
    CHECK(!input.forward_down);
    CHECK(engine.Tick(200000));
    CHECK(input.click_calls == 3);
    CHECK(engine.Stop());
    CHECK(engine.Toggle(201000));
    CHECK(input.click_calls == 4);
    CHECK(engine.Stop());
}

void TestInjectionFailures() {
    {
        FakeInput input;
        input.hold_ok = false;
        wclicker::Engine engine(input);
        CHECK(!engine.Start(0));
        CHECK(engine.state() == wclicker::State::Stopped);
        CHECK(engine.error() == wclicker::Error::Hold);
        CHECK(input.click_calls == 0);
        CHECK(!input.forward_down);
        CHECK(input.release_calls >= 2);
        CHECK(engine.Tick(5000));
        CHECK(input.hold_calls == 1);
    }
    {
        FakeInput input;
        input.click_ok = false;
        wclicker::Engine engine(input);
        CHECK(!engine.Start(0));
        CHECK(engine.state() == wclicker::State::Stopped);
        CHECK(engine.error() == wclicker::Error::Click);
        CHECK(!input.forward_down);
        CHECK(!input.left_down);
        CHECK(engine.Tick(10000));
        CHECK(input.click_calls == 1);
        input.click_ok = true;
        CHECK(engine.Start(11000));
        CHECK(engine.error() == wclicker::Error::None);
        CHECK(engine.Stop());
    }
    {
        FakeInput input;
        wclicker::Engine engine(input);
        CHECK(engine.Start(0));
        input.click_ok = false;
        CHECK(!engine.Tick(1000));
        CHECK(engine.error() == wclicker::Error::Click);
        CHECK(engine.state() == wclicker::State::Stopped);
        CHECK(!input.forward_down);
        CHECK(!input.left_down);
        CHECK(engine.Tick(9000));
        CHECK(input.click_calls == 2);
    }
}

void TestReleaseRetry() {
    {
        FakeInput input;
        wclicker::Engine engine(input);
        CHECK(engine.Start(0));
        input.release_failures = 3;
        CHECK(!engine.Pause());
        CHECK(engine.state() == wclicker::State::Stopped);
        CHECK(engine.error() == wclicker::Error::Release);
        CHECK(input.forward_down);
        CHECK(!engine.Tick(50));
        CHECK(engine.Tick(100));
        CHECK(engine.error() == wclicker::Error::None);
        CHECK(!input.forward_down);
        CHECK(input.click_calls == 1);
        CHECK(engine.state() == wclicker::State::Stopped);
        CHECK(engine.Stop());
        CHECK(engine.error() == wclicker::Error::None);
    }
    {
        FakeInput input;
        wclicker::Engine engine(input);
        CHECK(engine.Start(0));
        input.release_ok = false;
        CHECK(!engine.Stop());
        CHECK(engine.state() == wclicker::State::Stopped);
        CHECK(engine.error() == wclicker::Error::Release);
        CHECK(!engine.Start(1000));
        CHECK(input.hold_calls == 1);
        CHECK(input.click_calls == 1);
        input.release_ok = true;
        CHECK(engine.Tick(2000));
        CHECK(!input.forward_down);
        CHECK(engine.Start(3000));
        CHECK(input.hold_calls == 2);
        CHECK(input.click_calls == 2);
        CHECK(engine.Stop());
    }
}

}  // namespace

int main() {
    TestTiming();
    TestPauseResumeAndStop();
    TestInjectionFailures();
    TestReleaseRetry();
    std::cout << "Engine tests passed\n";
    return EXIT_SUCCESS;
}
