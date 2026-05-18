#include <winelement/rendering/render_resource_queue.hpp>

#include <utility>

namespace winelement::rendering {

void RenderResourceUploadQueue::push(RenderResourceUpload upload) {
    const std::scoped_lock lock(mutex_);
    uploads_.push_back(std::move(upload));
}

std::vector<RenderResourceUpload> RenderResourceUploadQueue::drain() {
    const std::scoped_lock lock(mutex_);
    std::vector<RenderResourceUpload> uploads;
    uploads.swap(uploads_);
    return uploads;
}

bool RenderResourceUploadQueue::empty() const noexcept {
    const std::scoped_lock lock(mutex_);
    return uploads_.empty();
}

std::size_t RenderResourceUploadQueue::size() const noexcept {
    const std::scoped_lock lock(mutex_);
    return uploads_.size();
}

} // namespace winelement::rendering
