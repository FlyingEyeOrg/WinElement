#pragma once

#include <cstddef>
#include <cstdint>
#include <mutex>
#include <vector>

namespace winelement::rendering {

struct RenderResourceId {
    std::uint64_t value = 0;

    [[nodiscard]] friend constexpr bool operator==(RenderResourceId,
                                                   RenderResourceId) noexcept = default;
};

enum class RenderResourceKind { Image, GlyphAtlas, Effect, User };
enum class RenderResourceFormat { Bgra8Premultiplied, Bgra8IgnoreAlpha, Alpha8 };
enum class RenderEffectKind { Shadow, GaussianBlur };
enum class RenderResourceAction { Upload, Retain, Release, Discard };

struct RenderResourceUpload {
    RenderResourceId id{};
    RenderResourceAction action = RenderResourceAction::Upload;
    RenderResourceKind kind = RenderResourceKind::User;
    RenderResourceFormat format = RenderResourceFormat::Bgra8Premultiplied;
    RenderEffectKind effect_kind = RenderEffectKind::Shadow;
    std::uint32_t reference_count = 1;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::uint32_t stride = 0;
    std::vector<std::byte> payload;
};

class RenderResourceUploadQueue final {
  public:
    RenderResourceUploadQueue() = default;

    RenderResourceUploadQueue(const RenderResourceUploadQueue&) = delete;
    RenderResourceUploadQueue& operator=(const RenderResourceUploadQueue&) = delete;

    void push(RenderResourceUpload upload);
    [[nodiscard]] std::vector<RenderResourceUpload> drain();
    [[nodiscard]] bool empty() const noexcept;
    [[nodiscard]] std::size_t size() const noexcept;

  private:
    mutable std::mutex mutex_;
    std::vector<RenderResourceUpload> uploads_;
};

} // namespace winelement::rendering
