#pragma once

#include <chrono>
#include <cstdint>

namespace winelement::platform {

struct VSyncFrameInfo {
    std::uint64_t frame_id = 0U;
    std::chrono::steady_clock::time_point timestamp{};
    bool visible = true;
};

class VSyncFrameClock final {
  public:
    void set_visible(bool visible) noexcept {
        visible_ = visible;
    }

    [[nodiscard]] VSyncFrameInfo next_frame(
        std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now()) noexcept {
        if (visible_) {
            ++frame_id_;
        }
        return VSyncFrameInfo{.frame_id = frame_id_, .timestamp = now, .visible = visible_};
    }

    [[nodiscard]] std::uint64_t frame_id() const noexcept {
        return frame_id_;
    }

  private:
    std::uint64_t frame_id_ = 0U;
    bool visible_ = true;
};

} // namespace winelement::platform
