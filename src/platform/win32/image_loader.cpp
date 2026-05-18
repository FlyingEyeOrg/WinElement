#include <winelement/platform/image_loader.hpp>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <wincodec.h>
#include <windows.h>
#include <wrl/client.h>

#include <combaseapi.h>

#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace winelement::platform {
namespace {

[[nodiscard]] std::runtime_error make_hresult_error(std::string_view message, HRESULT result) {
    auto text = std::string(message);
    text += " HRESULT=0x";
    constexpr auto digits = "0123456789ABCDEF";
    for (auto shift = 28; shift >= 0; shift -= 4) {
        text += digits[(static_cast<unsigned long>(result) >> shift) & 0x0F];
    }
    return std::runtime_error(text);
}

class ScopedComApartment final {
  public:
    ScopedComApartment() {
        result_ = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        initialized_ = SUCCEEDED(result_);
        if (result_ == RPC_E_CHANGED_MODE) {
            initialized_ = false;
            result_ = S_OK;
        }
        if (FAILED(result_)) {
            throw make_hresult_error("failed to initialize COM for WIC image decode", result_);
        }
    }

    ~ScopedComApartment() {
        if (initialized_) {
            CoUninitialize();
        }
    }

    ScopedComApartment(const ScopedComApartment&) = delete;
    ScopedComApartment& operator=(const ScopedComApartment&) = delete;

  private:
    HRESULT result_ = S_OK;
    bool initialized_ = false;
};

[[nodiscard]] rendering::RenderResourceUpload load_file(std::filesystem::path path,
                                                        ImageLoadOptions options) {
    ScopedComApartment com;

    Microsoft::WRL::ComPtr<IWICImagingFactory> factory;
    auto result = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                                   IID_PPV_ARGS(&factory));
    if (FAILED(result)) {
        throw make_hresult_error("failed to create WIC imaging factory", result);
    }

    Microsoft::WRL::ComPtr<IWICBitmapDecoder> decoder;
    result = factory->CreateDecoderFromFilename(path.c_str(), nullptr, GENERIC_READ,
                                                WICDecodeMetadataCacheOnLoad, &decoder);
    if (FAILED(result)) {
        throw make_hresult_error("failed to create WIC bitmap decoder", result);
    }

    Microsoft::WRL::ComPtr<IWICBitmapFrameDecode> frame;
    result = decoder->GetFrame(0U, &frame);
    if (FAILED(result)) {
        throw make_hresult_error("failed to read WIC bitmap frame", result);
    }

    Microsoft::WRL::ComPtr<IWICFormatConverter> converter;
    result = factory->CreateFormatConverter(&converter);
    if (FAILED(result)) {
        throw make_hresult_error("failed to create WIC format converter", result);
    }

    result =
        converter->Initialize(frame.Get(), GUID_WICPixelFormat32bppPBGRA, WICBitmapDitherTypeNone,
                              nullptr, 0.0, WICBitmapPaletteTypeCustom);
    if (FAILED(result)) {
        throw make_hresult_error("failed to convert WIC image to premultiplied BGRA", result);
    }

    UINT width = 0U;
    UINT height = 0U;
    result = converter->GetSize(&width, &height);
    if (FAILED(result) || width == 0U || height == 0U) {
        throw make_hresult_error("failed to query WIC image size", result);
    }

    const auto stride = width * 4U;
    auto upload = rendering::RenderResourceUpload{
        .id = options.id,
        .action = rendering::RenderResourceAction::Upload,
        .kind = rendering::RenderResourceKind::Image,
        .format = rendering::RenderResourceFormat::Bgra8Premultiplied,
        .reference_count = options.reference_count,
        .width = width,
        .height = height,
        .stride = stride};
    upload.payload.resize(static_cast<std::size_t>(stride) * height);

    result = converter->CopyPixels(nullptr, stride, static_cast<UINT>(upload.payload.size()),
                                   reinterpret_cast<BYTE*>(upload.payload.data()));
    if (FAILED(result)) {
        throw make_hresult_error("failed to copy WIC image pixels", result);
    }

    return upload;
}

} // namespace

std::future<rendering::RenderResourceUpload>
ImageResourceLoader::load_file_async(std::filesystem::path path, ImageLoadOptions options) {
    return std::async(std::launch::async,
                      [path = std::move(path), options] { return load_file(path, options); });
}

} // namespace winelement::platform