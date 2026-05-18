#pragma once

#include <winelement/elements/ui_element.hpp>

#include <chrono>
#include <cstddef>
#include <deque>
#include <string>
#include <string_view>
#include <vector>

namespace winelement::elements {

struct ElementInspectionNode {
    layout::Rect frame{};
    std::string theme_class;
    SemanticsRole role = SemanticsRole::Generic;
    bool visible = true;
    bool needs_layout = false;
    bool needs_paint = false;
    std::vector<ElementInspectionNode> children;
};

class ElementInspector final {
  public:
    [[nodiscard]] ElementInspectionNode inspect(const UIElement& root) const {
        auto node = ElementInspectionNode{.frame = root.frame(),
                                          .theme_class = std::string(root.theme_class()),
                                          .role = root.semantics_role(),
                                          .visible = root.visible(),
                                          .needs_layout = root.needs_layout(),
                                          .needs_paint = root.needs_paint()};
        node.children.reserve(root.child_count());
        for (auto index = std::size_t{0}; index < root.child_count(); ++index) {
            node.children.push_back(inspect(root.child_at(index)));
        }
        return node;
    }

    [[nodiscard]] static std::size_t count_nodes(const ElementInspectionNode& node) noexcept {
        auto count = std::size_t{1};
        for (const auto& child : node.children) {
            count += count_nodes(child);
        }
        return count;
    }
};

struct FrameRateSnapshot {
    double frames_per_second = 0.0;
    std::size_t sample_count = 0U;
};

class FrameRateMonitor final {
  public:
    using Clock = std::chrono::steady_clock;

    void sample(Clock::time_point now = Clock::now()) {
        samples_.push_back(now);
        const auto cutoff = now - window_;
        while (!samples_.empty() && samples_.front() < cutoff) {
            samples_.pop_front();
        }
    }

    [[nodiscard]] FrameRateSnapshot snapshot() const noexcept {
        if (samples_.size() < 2U) {
            return FrameRateSnapshot{.frames_per_second = 0.0, .sample_count = samples_.size()};
        }
        const auto duration = std::chrono::duration<double>(samples_.back() - samples_.front());
        const auto fps = duration.count() > 0.0
                             ? static_cast<double>(samples_.size() - 1U) / duration.count()
                             : 0.0;
        return FrameRateSnapshot{.frames_per_second = fps, .sample_count = samples_.size()};
    }

    void clear() noexcept {
        samples_.clear();
    }

  private:
    std::chrono::seconds window_{1};
    std::deque<Clock::time_point> samples_;
};

} // namespace winelement::elements
