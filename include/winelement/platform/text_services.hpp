#pragma once

#include <winelement/layout/layout_types.hpp>

#include <cstddef>
#include <string>
#include <string_view>
#include <utility>

namespace winelement::platform {

struct TextCompositionRange {
    std::size_t start = 0U;
    std::size_t length = 0U;
};

struct TextCompositionState {
    std::string text;
    TextCompositionRange replacement_range{};
    layout::Rect caret_rect{};
    bool active = false;
};

class TextServiceClient {
  public:
    virtual ~TextServiceClient() = default;
    virtual void update_composition(TextCompositionState state) = 0;
    virtual void commit_text(std::string_view text) = 0;
    virtual void cancel_composition() noexcept = 0;
};

class TextServiceAdapter final {
  public:
    void attach(TextServiceClient& client) noexcept {
        client_ = &client;
    }

    void detach() noexcept {
        client_ = nullptr;
    }

    [[nodiscard]] bool attached() const noexcept {
        return client_ != nullptr;
    }

    void update_composition(TextCompositionState state) {
        if (client_ != nullptr) {
            client_->update_composition(std::move(state));
        }
    }

    void commit_text(std::string_view text) {
        if (client_ != nullptr) {
            client_->commit_text(text);
        }
    }

    void cancel_composition() noexcept {
        if (client_ != nullptr) {
            client_->cancel_composition();
        }
    }

  private:
    TextServiceClient* client_ = nullptr;
};

} // namespace winelement::platform
