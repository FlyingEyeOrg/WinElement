#include <winelement/layout/grid_layout.hpp>

#include <cmath>
#include <utility>

namespace winelement::layout {
namespace {

[[nodiscard]] float safe_extent(float value) noexcept {
    return std::isfinite(value) && value > 0.0F ? value : 0.0F;
}

[[nodiscard]] float clamp_track(float value, const GridTrack& track) noexcept {
    return std::clamp(value, safe_extent(track.min),
                      std::isfinite(track.max) ? std::max(track.max, safe_extent(track.min))
                                               : std::numeric_limits<float>::infinity());
}

[[nodiscard]] float prefix_sum(const std::vector<float>& values, std::size_t count,
                               float gap) noexcept {
    const auto capped = std::min(count, values.size());
    auto sum = 0.0F;
    for (auto index = std::size_t{0}; index < capped; ++index) {
        sum += values[index];
    }
    if (capped > 0U) {
        sum += gap * static_cast<float>(capped);
    }
    return sum;
}

} // namespace

void GridLayoutPlanner::set_rows(std::vector<GridTrack> rows) {
    rows_ = rows.empty() ? std::vector<GridTrack>{{.sizing = GridTrackSizing::Star, .value = 1.0F}}
                         : std::move(rows);
}

void GridLayoutPlanner::set_columns(std::vector<GridTrack> columns) {
    columns_ = columns.empty()
                   ? std::vector<GridTrack>{{.sizing = GridTrackSizing::Star, .value = 1.0F}}
                   : std::move(columns);
}

void GridLayoutPlanner::set_gap(float row_gap, float column_gap) noexcept {
    row_gap_ = safe_extent(row_gap);
    column_gap_ = safe_extent(column_gap);
}

GridLayoutResult GridLayoutPlanner::arrange(Size available_size,
                                            const std::vector<GridItem>& items) const {
    auto result = GridLayoutResult{};
    result.rows = resolve_tracks(rows_, safe_extent(available_size.height), row_gap_);
    result.columns = resolve_tracks(columns_, safe_extent(available_size.width), column_gap_);
    result.item_frames.reserve(items.size());

    for (const auto& item : items) {
        const auto row =
            std::min(item.row, result.rows.empty() ? std::size_t{0} : result.rows.size() - 1U);
        const auto column = std::min(
            item.column, result.columns.empty() ? std::size_t{0} : result.columns.size() - 1U);
        const auto row_end =
            std::min(result.rows.size(), row + std::max(item.row_span, std::size_t{1}));
        const auto column_end =
            std::min(result.columns.size(), column + std::max(item.column_span, std::size_t{1}));

        const auto x = prefix_sum(result.columns, column, column_gap_);
        const auto y = prefix_sum(result.rows, row, row_gap_);
        const auto width = prefix_sum(result.columns, column_end, column_gap_) - x - column_gap_;
        const auto height = prefix_sum(result.rows, row_end, row_gap_) - y - row_gap_;
        result.item_frames.push_back(Rect{x, y, std::max(width, 0.0F), std::max(height, 0.0F)});
    }

    return result;
}

std::vector<float> GridLayoutPlanner::resolve_tracks(const std::vector<GridTrack>& tracks,
                                                     float available_extent, float gap) {
    auto resolved = std::vector<float>(tracks.size(), 0.0F);
    if (tracks.empty()) {
        return resolved;
    }

    const auto total_gap = gap * static_cast<float>(tracks.size() > 0U ? tracks.size() - 1U : 0U);
    auto remaining = std::max(0.0F, available_extent - total_gap);
    auto total_star = 0.0F;
    for (auto index = std::size_t{0}; index < tracks.size(); ++index) {
        const auto& track = tracks[index];
        if (track.sizing == GridTrackSizing::Fixed) {
            resolved[index] = clamp_track(safe_extent(track.value), track);
            remaining = std::max(0.0F, remaining - resolved[index]);
        } else {
            total_star += std::max(track.value, 0.0F);
        }
    }

    const auto safe_total_star = total_star > 0.0F ? total_star : 1.0F;
    for (auto index = std::size_t{0}; index < tracks.size(); ++index) {
        const auto& track = tracks[index];
        if (track.sizing == GridTrackSizing::Star) {
            const auto share = remaining * (std::max(track.value, 0.0F) / safe_total_star);
            resolved[index] = clamp_track(share, track);
        }
    }

    return resolved;
}

} // namespace winelement::layout