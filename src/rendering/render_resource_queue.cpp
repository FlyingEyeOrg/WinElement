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
    uploads.reserve(uploads_.size());
    while (!uploads_.empty()) {
        uploads.push_back(std::move(uploads_.front()));
        uploads_.pop_front();
    }
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