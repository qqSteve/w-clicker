#pragma once

#include <cstdint>

namespace wclicker {

class Input {
public:
    virtual ~Input() = default;
    virtual bool HoldForward() = 0;
    virtual bool ClickLeft() = 0;
    // Idempotent: release only input injected by this instance.
    virtual bool ReleaseAll() = 0;
};

enum class State { Stopped, Running, Paused };
enum class Error { None, Hold, Click, Release };

class Engine {
public:
    static constexpr std::uint64_t kIntervalMs = 1000;

    explicit Engine(Input& input) : input_(input) {}
    Engine(const Engine&) = delete;
    Engine& operator=(const Engine&) = delete;

    State state() const { return state_; }
    Error error() const { return error_; }

    bool Start(std::uint64_t now) {
        if (state_ == State::Running) return true;
        error_ = Error::None;
        if (!input_.ReleaseAll()) return Fail(Error::Release);
        if (!input_.HoldForward()) return Fail(Error::Hold);
        if (!input_.ClickLeft()) return Fail(Error::Click);
        state_ = State::Running;
        next_click_ = now + kIntervalMs;
        return true;
    }

    bool Pause() {
        if (state_ != State::Running) return true;
        state_ = State::Paused;
        if (!input_.ReleaseAll()) return Fail(Error::Release);
        return true;
    }

    bool Toggle(std::uint64_t now) {
        return state_ == State::Running ? Pause() : Start(now);
    }

    bool Stop() {
        state_ = State::Stopped;
        next_click_ = 0;
        if (!input_.ReleaseAll()) {
            error_ = Error::Release;
            return false;
        }
        error_ = Error::None;
        return true;
    }

    bool Tick(std::uint64_t now) {
        // Retry failed releases even while stopped. Never resume automatically.
        if (state_ != State::Running) {
            if (error_ != Error::Release) return true;
            if (!input_.ReleaseAll()) return false;
            error_ = Error::None;
            return true;
        }
        if (now < next_click_) return true;
        if (!input_.ClickLeft()) return Fail(Error::Click);
        // A delayed message must not cause a burst of catch-up clicks.
        next_click_ = now + kIntervalMs;
        return true;
    }

private:
    bool Fail(Error error) {
        state_ = State::Stopped;
        next_click_ = 0;
        error_ = input_.ReleaseAll() ? error : Error::Release;
        return false;
    }

    Input& input_;
    State state_ = State::Stopped;
    Error error_ = Error::None;
    std::uint64_t next_click_ = 0;
};

}  // namespace wclicker
