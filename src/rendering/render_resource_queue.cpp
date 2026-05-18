#include <winelement/rendering/render_resource_queue.hpp>

#include <iterator>
#include <utility>

namespace winelement::rendering {

RenderResourceUploadLane
RenderResourceUploadQueue::lane_for(const RenderResourceUpload& upload) noexcept {
    constexpr auto small_upload_threshold = std::size_t{64U * 1024U};
    if (upload.kind == RenderResourceKind::GlyphAtlas || upload.payload.size() <= small_upload_threshold) {
        return RenderResourceUploadLane::HighFrequencySmall;
    }
    return RenderResourceUploadLane::LowFrequencyLarge;
}

void RenderResourceUploadQueue::push(RenderResourceUpload upload) {
    const auto lane = lane_for(upload);
    if (lane == RenderResourceUploadLane::HighFrequencySmall) {
        const std::scoped_lock lock(high_frequency_mutex_);
        high_frequency_uploads_.push_back(std::move(upload));
    } else {
        const std::scoped_lock lock(low_frequency_mutex_);
        low_frequency_uploads_.push_back(std::move(upload));
    }
}

std::vector<RenderResourceUpload> RenderResourceUploadQueue::drain() {
    const std::scoped_lock lock(high_frequency_mutex_, low_frequency_mutex_);
    std::vector<RenderResourceUpload> uploads;
    uploads.reserve(high_frequency_uploads_.size() + low_frequency_uploads_.size());
    uploads.insert(uploads.end(), std::make_move_iterator(high_frequency_uploads_.begin()),
                   std::make_move_iterator(high_frequency_uploads_.end()));
    uploads.insert(uploads.end(), std::make_move_iterator(low_frequency_uploads_.begin()),
                   std::make_move_iterator(low_frequency_uploads_.end()));
    high_frequency_uploads_.clear();
    low_frequency_uploads_.clear();
    return uploads;
}

std::vector<RenderResourceUpload> RenderResourceUploadQueue::drain(RenderResourceUploadLane lane) {
    auto uploads = std::vector<RenderResourceUpload>{};
    if (lane == RenderResourceUploadLane::HighFrequencySmall) {
        const std::scoped_lock lock(high_frequency_mutex_);
        uploads.swap(high_frequency_uploads_);
    } else {
        const std::scoped_lock lock(low_frequency_mutex_);
        uploads.swap(low_frequency_uploads_);
    }
    return uploads;
}

bool RenderResourceUploadQueue::empty() const noexcept {
    const std::scoped_lock lock(high_frequency_mutex_, low_frequency_mutex_);
    return high_frequency_uploads_.empty() && low_frequency_uploads_.empty();
}

bool RenderResourceUploadQueue::empty(RenderResourceUploadLane lane) const noexcept {
    if (lane == RenderResourceUploadLane::HighFrequencySmall) {
        const std::scoped_lock lock(high_frequency_mutex_);
        return high_frequency_uploads_.empty();
    }
    const std::scoped_lock lock(low_frequency_mutex_);
    return low_frequency_uploads_.empty();
}

std::size_t RenderResourceUploadQueue::size() const noexcept {
    const std::scoped_lock lock(high_frequency_mutex_, low_frequency_mutex_);
    return high_frequency_uploads_.size() + low_frequency_uploads_.size();
}

std::size_t RenderResourceUploadQueue::size(RenderResourceUploadLane lane) const noexcept {
    if (lane == RenderResourceUploadLane::HighFrequencySmall) {
        const std::scoped_lock lock(high_frequency_mutex_);
        return high_frequency_uploads_.size();
    }
    const std::scoped_lock lock(low_frequency_mutex_);
    return low_frequency_uploads_.size();
}

} // namespace winelement::rendering
