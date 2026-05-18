#pragma once

#include <winelement/rendering/render_resource_queue.hpp>

#include <cstdint>
#include <filesystem>
#include <future>

namespace winelement::platform {

struct ImageLoadOptions {
    rendering::RenderResourceId id{};
    std::uint32_t reference_count = 1U;
};

class ImageResourceLoader final {
  public:
    [[nodiscard]] static std::future<rendering::RenderResourceUpload>
    load_file_async(std::filesystem::path path, ImageLoadOptions options);
};

} // namespace winelement::platform