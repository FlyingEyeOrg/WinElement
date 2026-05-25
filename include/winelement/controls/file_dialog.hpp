#pragma once

#include <winelement/platform/file_dialog.hpp>

#include <cstddef>
#include <string>
#include <vector>

namespace winelement::controls {

using FileDialogMode = platform::FileDialogMode;

struct FileDialogFilter {
    std::string name;
    std::string pattern;
};

struct FileDialogOptions {
    std::string title;
    std::string confirm_button_text;
    std::string file_name;
    std::string default_extension;
    std::string initial_directory;
    std::vector<FileDialogFilter> filters;
    std::size_t selected_filter_index = 0U;
    bool multi_select = false;
    bool prompt_on_overwrite = true;
    bool path_must_exist = true;
    bool file_must_exist = true;
    bool show_hidden_items = false;
    platform::Window* owner = nullptr;
};

struct FileDialogResult {
    bool accepted = false;
    std::vector<std::string> paths;
    std::size_t selected_filter_index = 0U;
};

class FileDialog final {
  public:
    static FileDialogResult show(FileDialogMode mode, FileDialogOptions options = {});
    static FileDialogResult open(FileDialogOptions options = {});
    static FileDialogResult save(FileDialogOptions options = {});
    static FileDialogResult pick_folder(FileDialogOptions options = {});
};

} // namespace winelement::controls
