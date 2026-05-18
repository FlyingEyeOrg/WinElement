#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <stdexcept>
#include <unordered_map>
#include <utility>
#include <vector>

namespace winelement::core {

class SignalConnection final {
  public:
    using Disconnect = std::function<void(std::uint64_t)>;

    SignalConnection() = default;
    SignalConnection(std::uint64_t id, Disconnect disconnect)
        : id_(id), disconnect_(std::move(disconnect)) {}
    ~SignalConnection() {
        disconnect();
    }

    SignalConnection(const SignalConnection&) = delete;
    SignalConnection& operator=(const SignalConnection&) = delete;

    SignalConnection(SignalConnection&& other) noexcept
        : id_(std::exchange(other.id_, 0U)), disconnect_(std::move(other.disconnect_)) {}

    SignalConnection& operator=(SignalConnection&& other) noexcept {
        if (this != &other) {
            disconnect();
            id_ = std::exchange(other.id_, 0U);
            disconnect_ = std::move(other.disconnect_);
        }
        return *this;
    }

    void disconnect() noexcept {
        if (id_ == 0U || !disconnect_) {
            return;
        }
        auto disconnect = std::move(disconnect_);
        const auto id = std::exchange(id_, 0U);
        disconnect(id);
    }

    [[nodiscard]] bool connected() const noexcept {
        return id_ != 0U;
    }

  private:
    std::uint64_t id_ = 0U;
    Disconnect disconnect_;
};

template <typename T> class Signal final {
  public:
    using Observer = std::function<void(const T&)>;

    Signal() = default;
    explicit Signal(T value) : value_(std::move(value)) {}

    [[nodiscard]] const T& value() const noexcept {
        return value_;
    }

    void set(T value) {
        if constexpr (requires { value_ == value; }) {
            if (value_ == value) {
                return;
            }
        }
        value_ = std::move(value);
        notify();
    }

    [[nodiscard]] SignalConnection subscribe(Observer observer) {
        if (!observer) {
            throw std::invalid_argument("signal observer must not be empty");
        }

        const auto id = next_id_++;
        observers_.emplace(id, std::move(observer));
        return SignalConnection{id,
                                [this](std::uint64_t observer_id) { unsubscribe(observer_id); }};
    }

    void notify() const {
        auto snapshot = std::vector<Observer>{};
        snapshot.reserve(observers_.size());
        for (const auto& [id, observer] : observers_) {
            static_cast<void>(id);
            snapshot.push_back(observer);
        }
        for (const auto& observer : snapshot) {
            observer(value_);
        }
    }

    [[nodiscard]] std::size_t observer_count() const noexcept {
        return observers_.size();
    }

  private:
    void unsubscribe(std::uint64_t id) noexcept {
        observers_.erase(id);
    }

    T value_{};
    std::uint64_t next_id_ = 1U;
    std::unordered_map<std::uint64_t, Observer> observers_;
};

template <typename T> class Computed final {
  public:
    using Compute = std::function<T()>;

    explicit Computed(Compute compute) : compute_(std::move(compute)) {
        if (!compute_) {
            throw std::invalid_argument("computed value requires a compute callback");
        }
        [[maybe_unused]] const auto initialized = refresh();
    }

    [[nodiscard]] const T& value() const noexcept {
        return value_;
    }

    [[nodiscard]] bool refresh() {
        auto next = compute_();
        if constexpr (requires { value_ == next; }) {
            if (value_ == next) {
                return false;
            }
        }
        value_ = std::move(next);
        return true;
    }

  private:
    Compute compute_;
    T value_{};
};

} // namespace winelement::core