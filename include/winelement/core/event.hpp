#pragma once

#include <algorithm>
#include <cstdint>
#include <functional>
#include <utility>
#include <vector>

namespace winelement::core {

using EventToken = std::uint64_t;

template <typename... Args> class EventSignal final {
  public:
    using Handler = std::function<void(Args...)>;

    EventSignal() = default;

    EventSignal(const EventSignal&) = delete;
    EventSignal& operator=(const EventSignal&) = delete;
    EventSignal(EventSignal&&) noexcept = default;
    EventSignal& operator=(EventSignal&&) noexcept = default;

    EventToken add(Handler handler) {
        if (!handler) {
            return 0U;
        }
        auto token = next_token_++;
        if (token == 0U) {
            token = next_token_++;
        }
        handlers_.push_back(Entry{.token = token, .handler = std::move(handler)});
        return token;
    }

    EventToken operator+=(Handler handler) {
        return add(std::move(handler));
    }

    void remove(EventToken token) noexcept {
        if (token == 0U) {
            return;
        }
        for (auto& entry : handlers_) {
            if (entry.token != token) {
                continue;
            }
            entry.handler = nullptr;
            if (dispatch_depth_ == 0U) {
                compact();
            } else {
                needs_compaction_ = true;
            }
            return;
        }
    }

    void operator-=(EventToken token) noexcept {
        remove(token);
    }

    void clear() noexcept {
        if (dispatch_depth_ == 0U) {
            handlers_.clear();
            needs_compaction_ = false;
            return;
        }
        for (auto& entry : handlers_) {
            entry.handler = nullptr;
        }
        needs_compaction_ = true;
    }

    [[nodiscard]] bool empty() const noexcept {
        return handlers_.empty();
    }

    [[nodiscard]] std::size_t size() const noexcept {
        return handlers_.size();
    }

    void emit(Args... args) const {
        ++dispatch_depth_;
        for (const auto& entry : handlers_) {
            if (entry.handler) {
                entry.handler(args...);
            }
        }
        --dispatch_depth_;
        if (dispatch_depth_ == 0U && needs_compaction_) {
            compact();
        }
    }

  private:
    struct Entry {
        EventToken token = 0U;
        Handler handler;
    };

    void compact() const {
        handlers_.erase(std::remove_if(handlers_.begin(), handlers_.end(),
                                       [](const Entry& entry) { return !entry.handler; }),
                        handlers_.end());
        needs_compaction_ = false;
    }

    mutable std::vector<Entry> handlers_;
    mutable std::size_t dispatch_depth_ = 0U;
    mutable bool needs_compaction_ = false;
    EventToken next_token_ = 1U;
};

} // namespace winelement::core
