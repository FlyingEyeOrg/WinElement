#include <winelement/platform/file_dialog.hpp>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <shobjidl.h>
#include <windows.h>
#include <wrl/client.h>
#endif

#include <algorithm>
#include <string>
#include <utility>
#include <vector>

namespace winelement::platform {

#ifdef _WIN32
namespace {

class ScopedComApartment final {
  public:
    ScopedComApartment() noexcept {
        result_ = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    }

    ~ScopedComApartment() noexcept {
        if (SUCCEEDED(result_)) {
            CoUninitialize();
        }
    }

    [[nodiscard]] HRESULT result() const noexcept {
        return result_;
    }

  private:
    HRESULT result_ = E_FAIL;
};

[[nodiscard]] std::wstring utf16_from_item(IShellItem& item) {
    PWSTR raw_path = nullptr;
    if (FAILED(item.GetDisplayName(SIGDN_FILESYSPATH, &raw_path)) || raw_path == nullptr) {
        return {};
    }

    auto* buffer = raw_path;
    const auto release = std::unique_ptr<wchar_t, decltype(&CoTaskMemFree)>(buffer, &CoTaskMemFree);
    return std::wstring(buffer);
}

[[nodiscard]] Microsoft::WRL::ComPtr<IShellItem>
shell_item_from_path(const std::wstring& path) noexcept {
    auto item = Microsoft::WRL::ComPtr<IShellItem>{};
    if (path.empty()) {
        return item;
    }

    const auto result =
        SHCreateItemFromParsingName(path.c_str(), nullptr, IID_PPV_ARGS(item.GetAddressOf()));
    if (FAILED(result)) {
        item.Reset();
    }
    return item;
}

void apply_dialog_options(IFileDialog& dialog, FileDialogMode mode,
                          const FileDialogOptions& options) {
    DWORD dialog_flags = 0U;
    static_cast<void>(dialog.GetOptions(&dialog_flags));
    dialog_flags |= FOS_FORCEFILESYSTEM;
    if (options.show_hidden_items) {
        dialog_flags |= FOS_FORCESHOWHIDDEN;
    }
    if (options.path_must_exist) {
        dialog_flags |= FOS_PATHMUSTEXIST;
    }
    if (options.file_must_exist && mode == FileDialogMode::OpenFile) {
        dialog_flags |= FOS_FILEMUSTEXIST;
    }
    if (options.prompt_on_overwrite && mode == FileDialogMode::SaveFile) {
        dialog_flags |= FOS_OVERWRITEPROMPT;
    }
    if (options.multi_select && mode == FileDialogMode::OpenFile) {
        dialog_flags |= FOS_ALLOWMULTISELECT;
    }
    if (mode == FileDialogMode::PickFolder) {
        dialog_flags |= FOS_PICKFOLDERS | FOS_PATHMUSTEXIST;
    }
    static_cast<void>(dialog.SetOptions(dialog_flags));

    if (!options.title.empty()) {
        static_cast<void>(dialog.SetTitle(options.title.c_str()));
    }
    if (!options.confirm_button_text.empty()) {
        static_cast<void>(dialog.SetOkButtonLabel(options.confirm_button_text.c_str()));
    }
    if (!options.file_name.empty()) {
        static_cast<void>(dialog.SetFileName(options.file_name.c_str()));
    }
    if (!options.default_extension.empty()) {
        static_cast<void>(dialog.SetDefaultExtension(options.default_extension.c_str()));
    }
    if (!options.initial_directory.empty()) {
        auto folder = shell_item_from_path(options.initial_directory);
        if (folder) {
            static_cast<void>(dialog.SetDefaultFolder(folder.Get()));
            static_cast<void>(dialog.SetFolder(folder.Get()));
        }
    }

    std::vector<COMDLG_FILTERSPEC> filter_specs;
    filter_specs.reserve(options.filters.size());
    for (const auto& filter : options.filters) {
        if (filter.name.empty() || filter.pattern.empty()) {
            continue;
        }
        filter_specs.push_back(COMDLG_FILTERSPEC{filter.name.c_str(), filter.pattern.c_str()});
    }
    if (!filter_specs.empty()) {
        static_cast<void>(
            dialog.SetFileTypes(static_cast<UINT>(filter_specs.size()), filter_specs.data()));
        const auto file_type_index =
            static_cast<UINT>(std::min(options.selected_filter_index + 1U, filter_specs.size()));
        static_cast<void>(dialog.SetFileTypeIndex(std::max(file_type_index, 1U)));
    }
}

[[nodiscard]] std::vector<std::wstring> selected_paths_from_open_dialog(IFileOpenDialog& dialog,
                                                                        bool multi_select) {
    auto paths = std::vector<std::wstring>{};
    if (multi_select) {
        auto items = Microsoft::WRL::ComPtr<IShellItemArray>{};
        if (FAILED(dialog.GetResults(items.GetAddressOf())) || items == nullptr) {
            return paths;
        }
        DWORD count = 0U;
        static_cast<void>(items->GetCount(&count));
        paths.reserve(static_cast<std::size_t>(count));
        for (DWORD index = 0U; index < count; ++index) {
            auto item = Microsoft::WRL::ComPtr<IShellItem>{};
            if (SUCCEEDED(items->GetItemAt(index, item.GetAddressOf())) && item != nullptr) {
                auto path = utf16_from_item(*item.Get());
                if (!path.empty()) {
                    paths.push_back(std::move(path));
                }
            }
        }
        return paths;
    }

    auto item = Microsoft::WRL::ComPtr<IShellItem>{};
    if (FAILED(dialog.GetResult(item.GetAddressOf())) || item == nullptr) {
        return paths;
    }
    auto path = utf16_from_item(*item.Get());
    if (!path.empty()) {
        paths.push_back(std::move(path));
    }
    return paths;
}

[[nodiscard]] std::vector<std::wstring> selected_paths_from_save_dialog(IFileSaveDialog& dialog) {
    auto paths = std::vector<std::wstring>{};
    auto item = Microsoft::WRL::ComPtr<IShellItem>{};
    if (FAILED(dialog.GetResult(item.GetAddressOf())) || item == nullptr) {
        return paths;
    }
    auto path = utf16_from_item(*item.Get());
    if (!path.empty()) {
        paths.push_back(std::move(path));
    }
    return paths;
}

[[nodiscard]] HWND owner_window_handle(const FileDialogOptions& options) noexcept {
    return options.owner != nullptr ? static_cast<HWND>(options.owner->native_handle()) : nullptr;
}

} // namespace
#endif

FileDialogResult FileDialog::show(FileDialogMode mode, FileDialogOptions options) {
    auto result = FileDialogResult{};

#ifdef _WIN32
    const auto apartment = ScopedComApartment{};
    if (FAILED(apartment.result()) && apartment.result() != RPC_E_CHANGED_MODE) {
        return result;
    }

    const auto owner = owner_window_handle(options);
    UINT file_type_index = 0U;
    if (mode == FileDialogMode::SaveFile) {
        auto save_dialog = Microsoft::WRL::ComPtr<IFileSaveDialog>{};
        if (FAILED(CoCreateInstance(CLSID_FileSaveDialog, nullptr, CLSCTX_INPROC_SERVER,
                                    IID_PPV_ARGS(save_dialog.GetAddressOf()))) ||
            save_dialog == nullptr) {
            return result;
        }
        apply_dialog_options(*save_dialog.Get(), mode, options);
        const auto show_result = save_dialog->Show(owner);
        if (show_result == HRESULT_FROM_WIN32(ERROR_CANCELLED)) {
            return result;
        }
        if (FAILED(show_result)) {
            return result;
        }
        result.paths = selected_paths_from_save_dialog(*save_dialog.Get());
        static_cast<void>(save_dialog->GetFileTypeIndex(&file_type_index));
    } else {
        auto open_dialog = Microsoft::WRL::ComPtr<IFileOpenDialog>{};
        if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER,
                                    IID_PPV_ARGS(open_dialog.GetAddressOf()))) ||
            open_dialog == nullptr) {
            return result;
        }
        apply_dialog_options(*open_dialog.Get(), mode, options);
        const auto show_result = open_dialog->Show(owner);
        if (show_result == HRESULT_FROM_WIN32(ERROR_CANCELLED)) {
            return result;
        }
        if (FAILED(show_result)) {
            return result;
        }
        result.paths = selected_paths_from_open_dialog(
            *open_dialog.Get(), options.multi_select && mode == FileDialogMode::OpenFile);
        static_cast<void>(open_dialog->GetFileTypeIndex(&file_type_index));
    }

    result.accepted = !result.paths.empty();
    if (file_type_index > 0U) {
        result.selected_filter_index = static_cast<std::size_t>(file_type_index - 1U);
    }
#else
    static_cast<void>(mode);
    static_cast<void>(options);
#endif

    return result;
}

FileDialogResult FileDialog::open(FileDialogOptions options) {
    return show(FileDialogMode::OpenFile, std::move(options));
}

FileDialogResult FileDialog::save(FileDialogOptions options) {
    return show(FileDialogMode::SaveFile, std::move(options));
}

FileDialogResult FileDialog::pick_folder(FileDialogOptions options) {
    return show(FileDialogMode::PickFolder, std::move(options));
}

} // namespace winelement::platform
