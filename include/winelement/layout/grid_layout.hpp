#pragma once

#include <winelement/layout/layout_types.hpp>

#include <algorithm>
#include <cstddef>
#include <functional>
#include <limits>
#include <vector>

namespace winelement::layout {

enum class GridTrackSizing { Fixed, Star };

struct GridTrack {
    GridTrackSizing sizing = GridTrackSizing::Star;
    float value = 1.0F;
    float min = 0.0F;
    float max = std::numeric_limits<float>::infinity();
};

struct GridItem {
    std::size_t row = 0U;
    std::size_t column = 0U;
    std::size_t row_span = 1U;
    std::size_t column_span = 1U;
};

struct GridLayoutResult {
    std::vector<float> rows;
    std::vector<float> columns;
    std::vector<Rect> item_frames;
};

class GridLayoutPlanner final {
  public:
    void set_rows(std::vector<GridTrack> rows);
    void set_columns(std::vector<GridTrack> columns);
    void set_gap(float row_gap, float column_gap) noexcept;

    [[nodiscard]] GridLayoutResult arrange(Size available_size,
                                           const std::vector<GridItem>& items) const;

  private:
    [[nodiscard]] static std::vector<float> resolve_tracks(const std::vector<GridTrack>& tracks,
                                                           float available_extent, float gap);

    std::vector<GridTrack> rows_{{.sizing = GridTrackSizing::Star, .value = 1.0F}};
    std::vector<GridTrack> columns_{{.sizing = GridTrackSizing::Star, .value = 1.0F}};
    float row_gap_ = 0.0F;
    float column_gap_ = 0.0F;
};

using CustomLayoutCallback =
    std::function<std::vector<Rect>(Size available_size, std::size_t child_count)>;

class CustomLayoutPlanner final {
  public:
    explicit CustomLayoutPlanner(CustomLayoutCallback callback = {})
        : callback_(std::move(callback)) {}

    void set_callback(CustomLayoutCallback callback) {
        callback_ = std::move(callback);
    }

    [[nodiscard]] std::vector<Rect> arrange(Size available_size, std::size_t child_count) const {
        if (!callback_) {
            return std::vector<Rect>(child_count);
        }
        auto frames = callback_(available_size, child_count);
        frames.resize(child_count);
        return frames;
    }

  private:
    CustomLayoutCallback callback_;
};

} // namespace winelement::layout