#pragma once

#include <winelement/platform/window.hpp>

#include <cstddef>
#include <string>
#include <vector>

namespace winelement::platform {

enum class FileDialogMode { OpenFile, SaveFile, PickFolder };

struct FileDialogFilter {
    std::wstring name;
    std::wstring pattern;
};

struct FileDialogOptions {
    std::wstring title;
    std::wstring confirm_button_text;
    std::wstring file_name;
    std::wstring default_extension;
    std::wstring initial_directory;
    std::vector<FileDialogFilter> filters;
    std::size_t selected_filter_index = 0U;
    bool multi_select = false;
    bool prompt_on_overwrite = true;
    bool path_must_exist = true;
    bool file_must_exist = true;
    bool show_hidden_items = false;
    Window* owner = nullptr;
};

struct FileDialogResult {
    bool accepted = false;
    std::vector<std::wstring> paths;
    std::size_t selected_filter_index = 0U;
};

class FileDialog final {
  public:
    static FileDialogResult show(FileDialogMode mode, FileDialogOptions options = {});
    static FileDialogResult open(FileDialogOptions options = {});
    static FileDialogResult save(FileDialogOptions options = {});
    static FileDialogResult pick_folder(FileDialogOptions options = {});
};

} // namespace winelement::platform
