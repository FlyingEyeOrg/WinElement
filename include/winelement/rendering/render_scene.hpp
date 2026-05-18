#pragma once

#include <winelement/core/cache.hpp>
#include <winelement/rendering/render_command_list.hpp>
#include <winelement/rendering/render_types.hpp>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace winelement::rendering {

enum class RenderNodeKind { Picture, Clip, Layer, Surface };

struct RenderCachePolicy {
    bool cacheable = true;
    std::size_t estimated_cost = 1U;
    std::uint64_t generation = 0U;
};

struct RenderNode {
    RenderNodeKind kind = RenderNodeKind::Picture;
    layout::Rect bounds{};
    Transform2D transform{};
    float opacity = 1.0F;
    bool clips_to_bounds = true;
    std::string debug_name;
    std::uint64_t fingerprint = 0U;
    RenderCachePolicy cache_policy{};
    RenderCommandList commands{};
    std::vector<RenderNode> children;
};

class RenderPictureCache final {
  public:
    explicit RenderPictureCache(std::size_t capacity = 128U);

    void set_capacity(std::size_t capacity);
    [[nodiscard]] std::size_t capacity() const noexcept;
    [[nodiscard]] std::size_t size() const noexcept;
    void clear() noexcept;

    void store(std::uint64_t fingerprint, RenderCommandList commands);
    [[nodiscard]] const RenderCommandList* find(std::uint64_t fingerprint) const noexcept;

  private:
    core::LruCache<std::uint64_t, RenderCommandList> cache_;
};

class RenderScene final {
  public:
    RenderScene();
    explicit RenderScene(std::shared_ptr<PreparedRenderCache> prepared_cache);
    RenderScene(const RenderScene& other);
    RenderScene& operator=(const RenderScene& other);
    RenderScene(RenderScene&&) noexcept = default;
    RenderScene& operator=(RenderScene&&) noexcept = default;

    void clear() noexcept;
    void set_root(RenderNode root);
    void update_from_commands(RenderCommandList command_list, std::string debug_name = {});

    [[nodiscard]] bool empty() const noexcept;
    [[nodiscard]] const RenderNode* root() const noexcept;
    [[nodiscard]] layout::Rect bounds() const noexcept;
    [[nodiscard]] std::uint64_t fingerprint() const noexcept;
    [[nodiscard]] std::shared_ptr<PreparedRenderCache> prepared_cache() const noexcept;

  private:
    std::unique_ptr<RenderNode> root_;
    std::shared_ptr<PreparedRenderCache> prepared_cache_;
    std::uint64_t command_fingerprint_ = 0U;
    std::size_t command_count_ = 0U;
};

[[nodiscard]] RenderNode render_node_from_commands(RenderCommandList command_list,
                                                   std::string debug_name = {});

} // namespace winelement::rendering
