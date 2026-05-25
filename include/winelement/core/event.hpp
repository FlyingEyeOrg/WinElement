#pragma once

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <utility>
#include <vector>

namespace winelement::core {

using EventToken = std::uint64_t;

template <typename... Args> class EventHandler final {
  public:
    using Handler = std::function<void(Args...)>;

    EventHandler() = default;

    EventHandler(const EventHandler&) = delete;
    EventHandler& operator=(const EventHandler&) = delete;
    EventHandler(EventHandler&& other) noexcept {
        const std::unique_lock lock(other.mutex_);
        handlers_ = std::move(other.handlers_);
        next_token_ = other.next_token_;
        other.next_token_ = 1U;
    }

    EventHandler& operator=(EventHandler&& other) noexcept {
        if (this == &other) {
            return *this;
        }

        const std::scoped_lock lock(mutex_, other.mutex_);
        handlers_ = std::move(other.handlers_);
        next_token_ = other.next_token_;
        other.next_token_ = 1U;
        return *this;
    }

    EventToken add(Handler handler) {
        if (!handler) {
            return 0U;
        }

        const std::unique_lock lock(mutex_);
        compact_locked();
        auto token = next_token_++;
        if (token == 0U) {
            token = next_token_++;
        }
        handlers_.push_back(std::make_shared<Entry>(token, std::move(handler)));
        return token;
    }

    EventToken operator+=(Handler handler) {
        return add(std::move(handler));
    }

    void remove(EventToken token) noexcept {
        if (token == 0U) {
            return;
        }
        const std::unique_lock lock(mutex_);
        for (auto& entry : handlers_) {
            if (entry->token != token) {
                continue;
            }
            entry->active.store(false, std::memory_order_release);
            compact_locked();
            return;
        }
    }

    void operator-=(EventToken token) noexcept {
        remove(token);
    }

    void clear() noexcept {
        const std::unique_lock lock(mutex_);
        for (const auto& entry : handlers_) {
            entry->active.store(false, std::memory_order_release);
        }
        handlers_.clear();
    }

    [[nodiscard]] bool empty() const noexcept {
        const std::shared_lock lock(mutex_);
        return std::none_of(handlers_.begin(), handlers_.end(), [](const auto& entry) {
            return entry->active.load(std::memory_order_acquire);
        });
    }

    [[nodiscard]] std::size_t size() const noexcept {
        const std::shared_lock lock(mutex_);
        return static_cast<std::size_t>(
            std::count_if(handlers_.begin(), handlers_.end(), [](const auto& entry) {
                return entry->active.load(std::memory_order_acquire);
            }));
    }

    void emit(Args... args) const {
        auto handlers = std::vector<std::shared_ptr<Entry>>{};
        {
            const std::shared_lock lock(mutex_);
            handlers = handlers_;
        }

        for (const auto& entry : handlers) {
            if (entry->active.load(std::memory_order_acquire) && entry->handler) {
                entry->handler(args...);
            }
        }
    }

  private:
    struct Entry {
        Entry(EventToken token_value, Handler handler_value)
            : token(token_value), handler(std::move(handler_value)) {}

        EventToken token = 0U;
        Handler handler;
        std::atomic_bool active = true;
    };

    void compact_locked() const {
        handlers_.erase(std::remove_if(handlers_.begin(), handlers_.end(),
                                       [](const auto& entry) {
                                           return !entry->active.load(std::memory_order_acquire);
                                       }),
                        handlers_.end());
    }

    mutable std::shared_mutex mutex_;
    mutable std::vector<std::shared_ptr<Entry>> handlers_;
    EventToken next_token_ = 1U;
};

} // namespace winelement::core
