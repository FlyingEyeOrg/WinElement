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
#endif

#include <algorithm>
#include <memory>
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

template <typename T>
using ComPtr = std::unique_ptr<T, void (*)(T*)>;

template <typename T>
[[nodiscard]] ComPtr<T> make_com_ptr(T* value) noexcept {
    return ComPtr<T>(value, [](T* pointer) {
        if (pointer != nullptr) {
            pointer->Release();
        }
    });
}

[[nodiscard]] std::wstring utf16_from_item(IShellItem& item) {
    PWSTR raw_path = nullptr;
    if (FAILED(item.GetDisplayName(SIGDN_FILESYSPATH, &raw_path)) || raw_path == nullptr) {
        return {};
    }

    auto* buffer = raw_path;
    const auto release = std::unique_ptr<wchar_t, decltype(&CoTaskMemFree)>(buffer, &CoTaskMemFree);
    return std::wstring(buffer);
}

[[nodiscard]] ComPtr<IShellItem> shell_item_from_path(const std::wstring& path) noexcept {
    if (path.empty()) {
        return make_com_ptr<IShellItem>(nullptr);
    }

    IShellItem* item = nullptr;
    const auto result =
        SHCreateItemFromParsingName(path.c_str(), nullptr, IID_PPV_ARGS(&item));
    if (FAILED(result)) {
        return make_com_ptr<IShellItem>(nullptr);
    }
    return make_com_ptr(item);
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
            static_cast<void>(dialog.SetDefaultFolder(folder.get()));
            static_cast<void>(dialog.SetFolder(folder.get()));
        }
    }

    std::vector<COMDLG_FILTERSPEC> filter_specs;
    filter_specs.reserve(options.filters.size());
    for (const auto& filter : options.filters) {
        if (filter.name.empty() || filter.pattern.empty()) {
            continue;
        }
        filter_specs.push_back(
            COMDLG_FILTERSPEC{filter.name.c_str(), filter.pattern.c_str()});
    }
    if (!filter_specs.empty()) {
        static_cast<void>(dialog.SetFileTypes(static_cast<UINT>(filter_specs.size()),
                                              filter_specs.data()));
        const auto file_type_index =
            static_cast<UINT>(std::min(options.selected_filter_index + 1U, filter_specs.size()));
        static_cast<void>(dialog.SetFileTypeIndex(std::max(file_type_index, 1U)));
    }
}

[[nodiscard]] std::vector<std::wstring> selected_paths_from_open_dialog(IFileOpenDialog& dialog,
                                                                        bool multi_select) {
    auto paths = std::vector<std::wstring>{};
    if (multi_select) {
        IShellItemArray* items = nullptr;
        if (FAILED(dialog.GetResults(&items)) || items == nullptr) {
            return paths;
        }
        const auto items_owner = make_com_ptr(items);
        DWORD count = 0U;
        static_cast<void>(items_owner->GetCount(&count));
        paths.reserve(static_cast<std::size_t>(count));
        for (DWORD index = 0U; index < count; ++index) {
            IShellItem* item = nullptr;
            if (SUCCEEDED(items_owner->GetItemAt(index, &item)) && item != nullptr) {
                const auto item_owner = make_com_ptr(item);
                auto path = utf16_from_item(*item_owner);
                if (!path.empty()) {
                    paths.push_back(std::move(path));
                }
            }
        }
        return paths;
    }

    IShellItem* item = nullptr;
    if (FAILED(dialog.GetResult(&item)) || item == nullptr) {
        return paths;
    }
    const auto item_owner = make_com_ptr(item);
    auto path = utf16_from_item(*item_owner);
    if (!path.empty()) {
        paths.push_back(std::move(path));
    }
    return paths;
}

[[nodiscard]] std::vector<std::wstring> selected_paths_from_save_dialog(IFileSaveDialog& dialog) {
    auto paths = std::vector<std::wstring>{};
    IShellItem* item = nullptr;
    if (FAILED(dialog.GetResult(&item)) || item == nullptr) {
        return paths;
    }
    const auto item_owner = make_com_ptr(item);
    auto path = utf16_from_item(*item_owner);
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
        IFileSaveDialog* save_dialog = nullptr;
        if (FAILED(CoCreateInstance(CLSID_FileSaveDialog, nullptr, CLSCTX_INPROC_SERVER,
                                    IID_PPV_ARGS(&save_dialog))) ||
            save_dialog == nullptr) {
            return result;
        }
        const auto dialog_owner = make_com_ptr(save_dialog);
        apply_dialog_options(*dialog_owner, mode, options);
        const auto show_result = dialog_owner->Show(owner);
        if (show_result == HRESULT_FROM_WIN32(ERROR_CANCELLED)) {
            return result;
        }
        if (FAILED(show_result)) {
            return result;
        }
        result.paths = selected_paths_from_save_dialog(*dialog_owner);
        static_cast<void>(dialog_owner->GetFileTypeIndex(&file_type_index));
    } else {
        IFileOpenDialog* open_dialog = nullptr;
        if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER,
                                    IID_PPV_ARGS(&open_dialog))) ||
            open_dialog == nullptr) {
            return result;
        }
        const auto dialog_owner = make_com_ptr(open_dialog);
        apply_dialog_options(*dialog_owner, mode, options);
        const auto show_result = dialog_owner->Show(owner);
        if (show_result == HRESULT_FROM_WIN32(ERROR_CANCELLED)) {
            return result;
        }
        if (FAILED(show_result)) {
            return result;
        }
        result.paths = selected_paths_from_open_dialog(*dialog_owner,
                                                       options.multi_select &&
                                                           mode == FileDialogMode::OpenFile);
        static_cast<void>(dialog_owner->GetFileTypeIndex(&file_type_index));
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
