#include <winelement/controls/file_dialog.hpp>

#include <utility>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

namespace winelement::controls {

namespace {

[[nodiscard]] std::wstring utf8_to_wide(std::string_view value) {
#ifdef _WIN32
    if (value.empty()) {
        return {};
    }

    const auto size = MultiByteToWideChar(CP_UTF8, 0, value.data(),
                                          static_cast<int>(value.size()), nullptr, 0);
    if (size <= 0) {
        return {};
    }

    auto text = std::wstring(static_cast<std::size_t>(size), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), text.data(),
                        size);
    return text;
#else
    return std::wstring(value.begin(), value.end());
#endif
}

[[nodiscard]] std::string wide_to_utf8(std::wstring_view value) {
#ifdef _WIN32
    if (value.empty()) {
        return {};
    }

    const auto size = WideCharToMultiByte(CP_UTF8, 0, value.data(),
                                          static_cast<int>(value.size()), nullptr, 0, nullptr,
                                          nullptr);
    if (size <= 0) {
        return {};
    }

    auto text = std::string(static_cast<std::size_t>(size), '\0');
    WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), text.data(),
                        size, nullptr, nullptr);
    return text;
#else
    return std::string(value.begin(), value.end());
#endif
}

[[nodiscard]] platform::FileDialogOptions to_platform_options(const FileDialogOptions& options) {
    auto platform_options = platform::FileDialogOptions{};
    platform_options.title = utf8_to_wide(options.title);
    platform_options.confirm_button_text = utf8_to_wide(options.confirm_button_text);
    platform_options.file_name = utf8_to_wide(options.file_name);
    platform_options.default_extension = utf8_to_wide(options.default_extension);
    platform_options.initial_directory = utf8_to_wide(options.initial_directory);
    platform_options.selected_filter_index = options.selected_filter_index;
    platform_options.multi_select = options.multi_select;
    platform_options.prompt_on_overwrite = options.prompt_on_overwrite;
    platform_options.path_must_exist = options.path_must_exist;
    platform_options.file_must_exist = options.file_must_exist;
    platform_options.show_hidden_items = options.show_hidden_items;
    platform_options.owner = options.owner;
    platform_options.filters.reserve(options.filters.size());
    for (const auto& filter : options.filters) {
        platform_options.filters.push_back(
            platform::FileDialogFilter{.name = utf8_to_wide(filter.name),
                                       .pattern = utf8_to_wide(filter.pattern)});
    }
    return platform_options;
}

[[nodiscard]] FileDialogResult to_controls_result(platform::FileDialogResult result) {
    auto controls_result = FileDialogResult{};
    controls_result.accepted = result.accepted;
    controls_result.selected_filter_index = result.selected_filter_index;
    controls_result.paths.reserve(result.paths.size());
    for (auto& path : result.paths) {
        controls_result.paths.push_back(wide_to_utf8(path));
    }
    return controls_result;
}

} // namespace

FileDialogResult FileDialog::show(FileDialogMode mode, FileDialogOptions options) {
    return to_controls_result(platform::FileDialog::show(mode, to_platform_options(options)));
}

FileDialogResult FileDialog::open(FileDialogOptions options) {
    return to_controls_result(platform::FileDialog::open(to_platform_options(options)));
}

FileDialogResult FileDialog::save(FileDialogOptions options) {
    return to_controls_result(platform::FileDialog::save(to_platform_options(options)));
}

FileDialogResult FileDialog::pick_folder(FileDialogOptions options) {
    return to_controls_result(platform::FileDialog::pick_folder(to_platform_options(options)));
}

} // namespace winelement::controls
