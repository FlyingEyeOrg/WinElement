#pragma once

#include <functional>
#include <optional>
#include <string>
#include <string_view>

namespace winelement::elements {

class TextClipboardService final {
  public:
    using Reader = std::function<std::optional<std::string>()>;
    using Writer = std::function<bool(std::string_view)>;

    TextClipboardService() = default;

    TextClipboardService(const TextClipboardService&) = delete;
    TextClipboardService& operator=(const TextClipboardService&) = delete;

    void set_system_callbacks(Reader reader, Writer writer) {
        system_reader_ = std::move(reader);
        system_writer_ = std::move(writer);
    }

    void clear_system_callbacks() noexcept {
        system_reader_ = nullptr;
        system_writer_ = nullptr;
    }

    void copy_text(std::string_view text) const {
        fallback_text_ = std::string(text);
        if (system_writer_) {
            static_cast<void>(system_writer_(text));
        }
    }

    [[nodiscard]] std::string text() const {
        if (system_reader_) {
            if (auto system_text = system_reader_()) {
                fallback_text_ = *system_text;
                return *system_text;
            }
        }
        return fallback_text_;
    }

    [[nodiscard]] bool has_text() const {
        return !text().empty();
    }

    [[nodiscard]] const std::string& fallback_text() const noexcept {
        return fallback_text_;
    }

  private:
    mutable std::string fallback_text_;
    Reader system_reader_;
    Writer system_writer_;
};

} // namespace winelement::elements