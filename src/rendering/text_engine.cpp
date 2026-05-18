#include <winelement/rendering/text_engine.hpp>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <dwrite.h>
#include <wrl/client.h>

#include <algorithm>
#include <cmath>
#include <iterator>
#include <limits>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace winelement::rendering {
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

[[nodiscard]] Microsoft::WRL::ComPtr<IDWriteFactory> create_dwrite_factory() {
    Microsoft::WRL::ComPtr<IDWriteFactory> factory;
    const auto result = DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
                                            reinterpret_cast<IUnknown**>(factory.GetAddressOf()));
    if (FAILED(result)) {
        throw make_hresult_error("failed to create DirectWrite factory", result);
    }

    return factory;
}

[[nodiscard]] IDWriteFactory& shared_dwrite_factory() {
    static auto factory = create_dwrite_factory();
    return *factory.Get();
}

void validate_text_style(const TextStyle& style) {
    if (!std::isfinite(style.font_size) || style.font_size <= 0.0F) {
        throw std::invalid_argument("font size must be finite and positive");
    }
    if (!std::isfinite(style.line_spacing) || style.line_spacing < 0.0F) {
        throw std::invalid_argument("line spacing must be finite and non-negative");
    }
    if (!std::isfinite(style.baseline) || style.baseline < 0.0F) {
        throw std::invalid_argument("text baseline must be finite and non-negative");
    }
    if (style.line_spacing_method != LineSpacingMethod::Default && style.line_spacing <= 0.0F) {
        throw std::invalid_argument(
            "custom line spacing must be positive when a line spacing method is set");
    }
}

[[nodiscard]] float default_line_height(const TextStyle& style) noexcept {
    if (style.line_spacing_method != LineSpacingMethod::Default && style.line_spacing > 0.0F) {
        return style.line_spacing;
    }
    return std::max(style.font_size, 1.0F) * 1.25F;
}

[[nodiscard]] float default_baseline(const TextStyle& style, float line_height) noexcept {
    if (style.baseline > 0.0F) {
        return std::min(style.baseline, line_height);
    }
    return std::min(std::max(style.font_size, 1.0F), line_height);
}

[[nodiscard]] int checked_win32_text_length(std::size_t size, std::string_view label) {
    if (size > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
        throw std::length_error(std::string(label) + " is too large for Win32 text APIs");
    }
    return static_cast<int>(size);
}

[[nodiscard]] std::wstring utf8_to_wide(std::string_view text) {
    if (text.empty()) {
        return {};
    }

    const auto byte_count = checked_win32_text_length(text.size(), "UTF-8 text");
    const auto wide_count =
        MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), byte_count, nullptr, 0);
    if (wide_count <= 0) {
        throw std::invalid_argument("text must be valid UTF-8");
    }

    std::wstring wide_text(static_cast<std::size_t>(wide_count), L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), byte_count, wide_text.data(),
                        wide_count);
    return wide_text;
}

[[nodiscard]] std::wstring utf8_to_wide_lossless_family(std::string_view text) {
    if (text.empty()) {
        return L"Segoe UI";
    }

    const auto byte_count = checked_win32_text_length(text.size(), "font family");
    const auto wide_count =
        MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), byte_count, nullptr, 0);
    if (wide_count <= 0) {
        return L"Segoe UI";
    }

    std::wstring wide_text(static_cast<std::size_t>(wide_count), L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), byte_count, wide_text.data(),
                        wide_count);
    return wide_text;
}

[[nodiscard]] std::string wide_to_utf8_lossless(std::wstring_view text) {
    if (text.empty()) {
        return {};
    }

    const auto wide_count = checked_win32_text_length(text.size(), "wide text");
    const auto byte_count =
        WideCharToMultiByte(CP_UTF8, 0, text.data(), wide_count, nullptr, 0, nullptr, nullptr);
    if (byte_count <= 0) {
        return {};
    }

    std::string utf8_text(static_cast<std::size_t>(byte_count), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.data(), wide_count, utf8_text.data(), byte_count, nullptr,
                        nullptr);
    return utf8_text;
}

[[nodiscard]] std::vector<std::wstring> font_family_candidates(const TextStyle& style) {
    std::vector<std::wstring> candidates;
    candidates.push_back(utf8_to_wide_lossless_family(style.font_family));
    for (const auto& family : style.fallback_font_families) {
        const auto wide_family = utf8_to_wide_lossless_family(family);
        if (!wide_family.empty() &&
            std::find(candidates.begin(), candidates.end(), wide_family) == candidates.end()) {
            candidates.push_back(wide_family);
        }
    }
    if (std::find(candidates.begin(), candidates.end(), std::wstring(L"Segoe UI")) ==
        candidates.end()) {
        candidates.push_back(L"Segoe UI");
    }
    return candidates;
}

[[nodiscard]] std::string localized_string(IDWriteLocalizedStrings& strings,
                                           std::string_view preferred_locale) {
    UINT32 index = 0;
    BOOL exists = FALSE;
    if (!preferred_locale.empty()) {
        const auto locale = utf8_to_wide_lossless_family(preferred_locale);
        const auto result = strings.FindLocaleName(locale.c_str(), &index, &exists);
        if (FAILED(result)) {
            exists = FALSE;
        }
    }
    if (exists == FALSE) {
        const auto result = strings.FindLocaleName(L"en-us", &index, &exists);
        if (FAILED(result)) {
            exists = FALSE;
        }
    }
    if (exists == FALSE) {
        index = 0;
    }

    UINT32 length = 0;
    const auto length_result = strings.GetStringLength(index, &length);
    if (FAILED(length_result)) {
        return {};
    }

    std::wstring value(static_cast<std::size_t>(length) + 1U, L'\0');
    const auto string_result = strings.GetString(index, value.data(), length + 1U);
    if (FAILED(string_result)) {
        return {};
    }
    value.resize(length);
    return wide_to_utf8_lossless(value);
}

[[nodiscard]] std::vector<std::size_t> build_utf16_to_utf8_map(std::string_view utf8,
                                                               std::wstring_view utf16) {
    std::vector<std::size_t> map(utf16.size() + 1U, utf8.size());
    std::size_t utf8_offset = 0;
    std::size_t utf16_offset = 0;

    while (utf8_offset < utf8.size() && utf16_offset < utf16.size()) {
        map[utf16_offset] = utf8_offset;

        const auto first = static_cast<unsigned char>(utf8[utf8_offset]);
        auto utf8_length = std::size_t{1};
        if ((first & 0xE0U) == 0xC0U) {
            utf8_length = 2;
        } else if ((first & 0xF0U) == 0xE0U) {
            utf8_length = 3;
        } else if ((first & 0xF8U) == 0xF0U) {
            utf8_length = 4;
        }

        const auto utf16_length = (utf16[utf16_offset] >= 0xD800 && utf16[utf16_offset] <= 0xDBFF &&
                                   utf16_offset + 1U < utf16.size())
                                      ? std::size_t{2}
                                      : std::size_t{1};
        for (std::size_t i = 1; i < utf16_length && utf16_offset + i < map.size(); ++i) {
            map[utf16_offset + i] = utf8_offset;
        }

        utf8_offset = std::min(utf8_offset + utf8_length, utf8.size());
        utf16_offset = std::min(utf16_offset + utf16_length, utf16.size());
    }

    map[utf16.size()] = utf8.size();
    return map;
}

[[nodiscard]] std::size_t byte_offset_from_utf16(const std::vector<std::size_t>& map,
                                                 std::size_t utf16_offset) noexcept {
    if (map.empty()) {
        return 0U;
    }
    return map[std::min(utf16_offset, map.size() - 1U)];
}

[[nodiscard]] DWRITE_TEXT_ALIGNMENT to_dwrite_alignment(TextAlignment alignment) noexcept {
    switch (alignment) {
    case TextAlignment::Center:
        return DWRITE_TEXT_ALIGNMENT_CENTER;
    case TextAlignment::End:
        return DWRITE_TEXT_ALIGNMENT_TRAILING;
    case TextAlignment::Start:
    default:
        return DWRITE_TEXT_ALIGNMENT_LEADING;
    }
}

[[nodiscard]] DWRITE_PARAGRAPH_ALIGNMENT
to_dwrite_vertical_alignment(TextVerticalAlignment alignment) noexcept {
    switch (alignment) {
    case TextVerticalAlignment::Center:
        return DWRITE_PARAGRAPH_ALIGNMENT_CENTER;
    case TextVerticalAlignment::Bottom:
        return DWRITE_PARAGRAPH_ALIGNMENT_FAR;
    case TextVerticalAlignment::Top:
    default:
        return DWRITE_PARAGRAPH_ALIGNMENT_NEAR;
    }
}

[[nodiscard]] DWRITE_WORD_WRAPPING to_dwrite_wrapping(TextWrapping wrapping) noexcept {
    return wrapping == TextWrapping::Wrap ? DWRITE_WORD_WRAPPING_WRAP
                                          : DWRITE_WORD_WRAPPING_NO_WRAP;
}

[[nodiscard]] DWRITE_READING_DIRECTION
to_dwrite_reading_direction(ReadingDirection direction) noexcept {
    return direction == ReadingDirection::RightToLeft ? DWRITE_READING_DIRECTION_RIGHT_TO_LEFT
                                                      : DWRITE_READING_DIRECTION_LEFT_TO_RIGHT;
}

[[nodiscard]] DWRITE_FONT_STYLE to_dwrite_font_style(FontStyle style) noexcept {
    switch (style) {
    case FontStyle::Italic:
        return DWRITE_FONT_STYLE_ITALIC;
    case FontStyle::Oblique:
        return DWRITE_FONT_STYLE_OBLIQUE;
    case FontStyle::Normal:
    default:
        return DWRITE_FONT_STYLE_NORMAL;
    }
}

[[nodiscard]] FontStyle from_dwrite_font_style(DWRITE_FONT_STYLE style) noexcept {
    switch (style) {
    case DWRITE_FONT_STYLE_ITALIC:
        return FontStyle::Italic;
    case DWRITE_FONT_STYLE_OBLIQUE:
        return FontStyle::Oblique;
    case DWRITE_FONT_STYLE_NORMAL:
    default:
        return FontStyle::Normal;
    }
}

[[nodiscard]] DWRITE_LINE_SPACING_METHOD
to_dwrite_line_spacing_method(LineSpacingMethod method) noexcept {
    switch (method) {
    case LineSpacingMethod::Uniform:
        return DWRITE_LINE_SPACING_METHOD_UNIFORM;
    case LineSpacingMethod::Proportional:
        return DWRITE_LINE_SPACING_METHOD_PROPORTIONAL;
    case LineSpacingMethod::Default:
    default:
        return DWRITE_LINE_SPACING_METHOD_DEFAULT;
    }
}

[[nodiscard]] NORM_FORM to_windows_normalization_form(TextNormalizationForm form) noexcept {
    switch (form) {
    case TextNormalizationForm::CanonicalDecomposition:
        return NormalizationD;
    case TextNormalizationForm::CompatibilityComposition:
        return NormalizationKC;
    case TextNormalizationForm::CompatibilityDecomposition:
        return NormalizationKD;
    case TextNormalizationForm::CanonicalComposition:
    default:
        return NormalizationC;
    }
}

[[nodiscard]] std::string normalize_utf8_text(std::string_view text, TextNormalizationForm form) {
    const auto wide_text = utf8_to_wide(text);
    if (wide_text.empty()) {
        return {};
    }

    const auto norm_form = to_windows_normalization_form(form);
    const auto wide_count = checked_win32_text_length(wide_text.size(), "normalized text");
    auto required_length = NormalizeString(norm_form, wide_text.data(), wide_count, nullptr, 0);
    if (required_length <= 0) {
        throw make_hresult_error("failed to query normalized text length",
                                 HRESULT_FROM_WIN32(GetLastError()));
    }

    std::wstring normalized(static_cast<std::size_t>(required_length), L'\0');
    const auto normalized_length = NormalizeString(norm_form, wide_text.data(), wide_count,
                                                   normalized.data(), required_length);
    if (normalized_length <= 0) {
        throw make_hresult_error("failed to normalize text", HRESULT_FROM_WIN32(GetLastError()));
    }
    normalized.resize(static_cast<std::size_t>(normalized_length));
    return wide_to_utf8_lossless(normalized);
}

[[nodiscard]] TextFontFaceInfo make_font_face_info(IDWriteFontFamily& family, IDWriteFont& font,
                                                   std::string_view locale) {
    Microsoft::WRL::ComPtr<IDWriteLocalizedStrings> family_names;
    auto result = family.GetFamilyNames(&family_names);
    auto family_name = std::string{};
    if (SUCCEEDED(result) && family_names != nullptr) {
        family_name = localized_string(*family_names.Get(), locale);
    }

    Microsoft::WRL::ComPtr<IDWriteLocalizedStrings> face_names;
    result = font.GetFaceNames(&face_names);
    auto face_name = std::string{};
    if (SUCCEEDED(result) && face_names != nullptr) {
        face_name = localized_string(*face_names.Get(), locale);
    }

    const auto simulations = font.GetSimulations();
    return TextFontFaceInfo{
        .family_name = std::move(family_name),
        .face_name = std::move(face_name),
        .weight = static_cast<std::uint16_t>(font.GetWeight()),
        .stretch = static_cast<std::uint16_t>(font.GetStretch()),
        .style = from_dwrite_font_style(font.GetStyle()),
        .is_symbol_font = font.IsSymbolFont() != FALSE,
        .has_bold_simulation = (simulations & DWRITE_FONT_SIMULATIONS_BOLD) != 0,
        .has_oblique_simulation = (simulations & DWRITE_FONT_SIMULATIONS_OBLIQUE) != 0};
}

struct ResolvedFontFace {
    TextFontFaceInfo info{};
    Microsoft::WRL::ComPtr<IDWriteFont> font;
};

[[nodiscard]] std::string font_family_name_for_face(IDWriteFontFace* face,
                                                    std::string_view locale) noexcept {
    if (face == nullptr) {
        return {};
    }

    try {
        struct FontFaceFamilyCacheKey {
            IDWriteFontFace* face = nullptr;
            std::string locale;

            [[nodiscard]] bool operator==(const FontFaceFamilyCacheKey& right) const noexcept {
                return face == right.face && locale == right.locale;
            }
        };
        struct FontFaceFamilyCacheKeyHash {
            [[nodiscard]] std::size_t
            operator()(const FontFaceFamilyCacheKey& key) const noexcept {
                auto seed = std::hash<IDWriteFontFace*>{}(key.face);
                seed ^= std::hash<std::string>{}(key.locale) + 0x9E3779B9U + (seed << 6U) +
                        (seed >> 2U);
                return seed;
            }
        };

        static std::mutex cache_mutex;
        static std::unordered_map<FontFaceFamilyCacheKey, std::string, FontFaceFamilyCacheKeyHash>
            family_cache;
        const auto key = FontFaceFamilyCacheKey{.face = face, .locale = std::string(locale)};
        {
            const std::scoped_lock lock(cache_mutex);
            if (const auto cached = family_cache.find(key); cached != family_cache.end()) {
                return cached->second;
            }
        }

        Microsoft::WRL::ComPtr<IDWriteFontCollection> font_collection;
        auto result = shared_dwrite_factory().GetSystemFontCollection(&font_collection);
        if (FAILED(result) || font_collection == nullptr) {
            return {};
        }

        Microsoft::WRL::ComPtr<IDWriteFont> font;
        result = font_collection->GetFontFromFontFace(face, &font);
        if (FAILED(result) || font == nullptr) {
            return {};
        }

        Microsoft::WRL::ComPtr<IDWriteFontFamily> font_family;
        result = font->GetFontFamily(&font_family);
        if (FAILED(result) || font_family == nullptr) {
            return {};
        }

        Microsoft::WRL::ComPtr<IDWriteLocalizedStrings> family_names;
        result = font_family->GetFamilyNames(&family_names);
        if (FAILED(result) || family_names == nullptr) {
            return {};
        }

        auto family_name = localized_string(*family_names.Get(), locale);
        {
            const std::scoped_lock lock(cache_mutex);
            constexpr auto max_cached_faces = 512U;
            if (family_cache.size() >= max_cached_faces) {
                family_cache.clear();
            }
            family_cache.emplace(key, family_name);
        }
        return family_name;
    } catch (...) {
        return {};
    }
}

[[nodiscard]] std::optional<ResolvedFontFace> resolve_matching_font(IDWriteFactory& factory,
                                                                    const TextStyle& style) {
    validate_text_style(style);

    Microsoft::WRL::ComPtr<IDWriteFontCollection> font_collection;
    auto result = factory.GetSystemFontCollection(&font_collection);
    if (FAILED(result)) {
        throw make_hresult_error("failed to get DirectWrite system font collection", result);
    }

    for (const auto& family_name : font_family_candidates(style)) {
        UINT32 family_index = 0;
        BOOL family_exists = FALSE;
        result =
            font_collection->FindFamilyName(family_name.c_str(), &family_index, &family_exists);
        if (FAILED(result) || family_exists == FALSE) {
            continue;
        }

        Microsoft::WRL::ComPtr<IDWriteFontFamily> font_family;
        result = font_collection->GetFontFamily(family_index, &font_family);
        if (FAILED(result) || font_family == nullptr) {
            continue;
        }

        Microsoft::WRL::ComPtr<IDWriteFont> font;
        result =
            font_family->GetFirstMatchingFont(static_cast<DWRITE_FONT_WEIGHT>(style.font_weight),
                                              static_cast<DWRITE_FONT_STRETCH>(style.font_stretch),
                                              to_dwrite_font_style(style.font_style), &font);
        if (FAILED(result) || font == nullptr) {
            continue;
        }

        return ResolvedFontFace{
            .info = make_font_face_info(*font_family.Get(), *font.Get(), style.locale),
            .font = font};
    }

    return std::nullopt;
}

[[nodiscard]] std::vector<TextFontFamilyInfo> query_system_font_families(IDWriteFactory& factory,
                                                                         std::string_view locale) {
    Microsoft::WRL::ComPtr<IDWriteFontCollection> font_collection;
    const auto result = factory.GetSystemFontCollection(&font_collection);
    if (FAILED(result)) {
        throw make_hresult_error("failed to get DirectWrite system font collection", result);
    }

    std::vector<TextFontFamilyInfo> families;
    families.reserve(font_collection->GetFontFamilyCount());
    for (UINT32 family_index = 0; family_index < font_collection->GetFontFamilyCount();
         ++family_index) {
        Microsoft::WRL::ComPtr<IDWriteFontFamily> font_family;
        const auto family_result = font_collection->GetFontFamily(family_index, &font_family);
        if (FAILED(family_result) || font_family == nullptr) {
            continue;
        }

        Microsoft::WRL::ComPtr<IDWriteLocalizedStrings> family_names;
        auto family_name = std::string{};
        const auto names_result = font_family->GetFamilyNames(&family_names);
        if (SUCCEEDED(names_result) && family_names != nullptr) {
            family_name = localized_string(*family_names.Get(), locale);
        }

        TextFontFamilyInfo family{.family_name = std::move(family_name)};
        family.faces.reserve(font_family->GetFontCount());
        for (UINT32 font_index = 0; font_index < font_family->GetFontCount(); ++font_index) {
            Microsoft::WRL::ComPtr<IDWriteFont> font;
            const auto font_result = font_family->GetFont(font_index, &font);
            if (FAILED(font_result) || font == nullptr) {
                continue;
            }
            family.faces.push_back(make_font_face_info(*font_family.Get(), *font.Get(), locale));
        }

        if (family.family_name.empty() && !family.faces.empty()) {
            family.family_name = family.faces.front().family_name;
        }
        families.push_back(std::move(family));
    }

    return families;
}

[[nodiscard]] Microsoft::WRL::ComPtr<IDWriteTextFormat> create_text_format(IDWriteFactory& factory,
                                                                           const TextStyle& style) {
    validate_text_style(style);

    Microsoft::WRL::ComPtr<IDWriteTextFormat> format;
    auto last_result = HRESULT{S_OK};
    const auto locale = utf8_to_wide_lossless_family(style.locale);
    for (const auto& family : font_family_candidates(style)) {
        last_result = factory.CreateTextFormat(family.c_str(), nullptr,
                                               static_cast<DWRITE_FONT_WEIGHT>(style.font_weight),
                                               to_dwrite_font_style(style.font_style),
                                               static_cast<DWRITE_FONT_STRETCH>(style.font_stretch),
                                               style.font_size, locale.c_str(), &format);
        if (SUCCEEDED(last_result) && format != nullptr) {
            break;
        }
    }
    if (format == nullptr) {
        throw make_hresult_error("failed to create DirectWrite text format", last_result);
    }

    format->SetTextAlignment(to_dwrite_alignment(style.alignment));
    format->SetParagraphAlignment(to_dwrite_vertical_alignment(style.vertical_alignment));
    format->SetWordWrapping(to_dwrite_wrapping(style.wrapping));
    format->SetReadingDirection(to_dwrite_reading_direction(style.reading_direction));
    if (style.line_spacing_method != LineSpacingMethod::Default && style.line_spacing > 0.0F) {
        format->SetLineSpacing(to_dwrite_line_spacing_method(style.line_spacing_method),
                               style.line_spacing,
                               style.baseline > 0.0F ? style.baseline : style.line_spacing);
    }
    return format;
}

[[nodiscard]] TextFontMetrics query_font_metrics(IDWriteFactory& factory, const TextStyle& style) {
    const auto resolved = resolve_matching_font(factory, style);
    if (!resolved) {
        throw make_hresult_error("failed to resolve DirectWrite font metrics", DWRITE_E_NOFONT);
    }

    Microsoft::WRL::ComPtr<IDWriteFontFace> font_face;
    const auto face_result = resolved->font->CreateFontFace(&font_face);
    if (FAILED(face_result) || font_face == nullptr) {
        throw make_hresult_error("failed to create DirectWrite font face", face_result);
    }

    DWRITE_FONT_METRICS metrics{};
    font_face->GetMetrics(&metrics);
    if (metrics.designUnitsPerEm == 0U) {
        throw make_hresult_error("failed to resolve DirectWrite font metrics", E_FAIL);
    }

    const auto scale = style.font_size / static_cast<float>(metrics.designUnitsPerEm);
    const auto simulations = resolved->font->GetSimulations();
    return TextFontMetrics{
        .em_size = style.font_size,
        .design_units_per_em = static_cast<float>(metrics.designUnitsPerEm),
        .ascent = static_cast<float>(metrics.ascent) * scale,
        .descent = static_cast<float>(metrics.descent) * scale,
        .line_gap = static_cast<float>(metrics.lineGap) * scale,
        .line_height =
            static_cast<float>(metrics.ascent + metrics.descent + metrics.lineGap) * scale,
        .cap_height = static_cast<float>(metrics.capHeight) * scale,
        .x_height = static_cast<float>(metrics.xHeight) * scale,
        .underline_position = static_cast<float>(metrics.underlinePosition) * scale,
        .underline_thickness = static_cast<float>(metrics.underlineThickness) * scale,
        .strikethrough_position = static_cast<float>(metrics.strikethroughPosition) * scale,
        .strikethrough_thickness = static_cast<float>(metrics.strikethroughThickness) * scale,
        .has_bold_simulation = (simulations & DWRITE_FONT_SIMULATIONS_BOLD) != 0,
        .has_oblique_simulation = (simulations & DWRITE_FONT_SIMULATIONS_OBLIQUE) != 0};
}

void apply_text_decorations(IDWriteTextLayout& layout, const TextStyle& style, UINT32 text_length) {
    if (text_length == 0U) {
        return;
    }

    const auto range = DWRITE_TEXT_RANGE{0, text_length};
    if (has_text_decoration(style.decoration_line, TextDecorationLine::Underline)) {
        const auto result = layout.SetUnderline(TRUE, range);
        if (FAILED(result)) {
            throw make_hresult_error("failed to apply DirectWrite underline", result);
        }
    }
    if (has_text_decoration(style.decoration_line, TextDecorationLine::Strikethrough)) {
        const auto result = layout.SetStrikethrough(TRUE, range);
        if (FAILED(result)) {
            throw make_hresult_error("failed to apply DirectWrite strikethrough", result);
        }
    }
}

void validate_layout_options(TextLayoutOptions options) {
    const auto valid_extent = [](float value) noexcept {
        return value == 0.0F || (std::isfinite(value) && value > 0.0F);
    };
    if (!valid_extent(options.max_width) || !valid_extent(options.max_height)) {
        throw std::invalid_argument("text layout max extent must be zero or finite and positive");
    }
    if (!std::isfinite(options.pixels_per_dip) || options.pixels_per_dip <= 0.0F) {
        throw std::invalid_argument("pixels_per_dip must be finite and positive");
    }
    if (options.viewport &&
        (!std::isfinite(options.viewport->x) || !std::isfinite(options.viewport->y) ||
         !std::isfinite(options.viewport->width) || !std::isfinite(options.viewport->height) ||
         options.viewport->width < 0.0F || options.viewport->height < 0.0F)) {
        throw std::invalid_argument("text layout viewport must be finite and non-negative");
    }
}

void apply_typography(IDWriteFactory& factory, IDWriteTextLayout& layout, const TextStyle& style,
                      UINT32 text_length) {
    if (style.features.empty() || text_length == 0U) {
        return;
    }

    Microsoft::WRL::ComPtr<IDWriteTypography> typography;
    const auto typography_result = factory.CreateTypography(&typography);
    if (FAILED(typography_result)) {
        throw make_hresult_error("failed to create DirectWrite typography", typography_result);
    }

    for (const auto& feature : style.features) {
        const DWRITE_FONT_FEATURE dwrite_feature{static_cast<DWRITE_FONT_FEATURE_TAG>(feature.tag),
                                                 feature.parameter};
        const auto feature_result = typography->AddFontFeature(dwrite_feature);
        if (FAILED(feature_result)) {
            throw make_hresult_error("failed to add OpenType feature", feature_result);
        }
    }

    const auto typography_result_apply =
        layout.SetTypography(typography.Get(), DWRITE_TEXT_RANGE{0, text_length});
    if (FAILED(typography_result_apply)) {
        throw make_hresult_error("failed to apply OpenType features", typography_result_apply);
    }
}

void apply_trimming(IDWriteFactory& factory, IDWriteTextLayout& layout, TextTrimming trimming) {
    if (trimming == TextTrimming::None) {
        return;
    }

    Microsoft::WRL::ComPtr<IDWriteInlineObject> ellipsis;
    const auto ellipsis_result = factory.CreateEllipsisTrimmingSign(&layout, &ellipsis);
    if (FAILED(ellipsis_result)) {
        throw make_hresult_error("failed to create DirectWrite trimming sign", ellipsis_result);
    }

    const DWRITE_TRIMMING trimming_options{trimming == TextTrimming::WordEllipsis
                                               ? DWRITE_TRIMMING_GRANULARITY_WORD
                                               : DWRITE_TRIMMING_GRANULARITY_CHARACTER,
                                           0, 0};
    const auto trimming_result = layout.SetTrimming(&trimming_options, ellipsis.Get());
    if (FAILED(trimming_result)) {
        throw make_hresult_error("failed to apply DirectWrite trimming", trimming_result);
    }
}

class CapturingTextRenderer final : public IDWriteTextRenderer {
  public:
    CapturingTextRenderer(TextLayout& text_layout, const std::vector<std::size_t>& utf16_to_utf8)
        : layout_(text_layout), utf16_to_utf8_(utf16_to_utf8),
          cluster_lookup_(build_cluster_lookup(text_layout.clusters)),
          pixels_per_dip_(text_layout.options.pixels_per_dip) {}

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** object) override {
        if (object == nullptr) {
            return E_POINTER;
        }
        if (iid == __uuidof(IUnknown) || iid == __uuidof(IDWritePixelSnapping) ||
            iid == __uuidof(IDWriteTextRenderer)) {
            *object = static_cast<IDWriteTextRenderer*>(this);
            AddRef();
            return S_OK;
        }
        *object = nullptr;
        return E_NOINTERFACE;
    }

    ULONG STDMETHODCALLTYPE AddRef() override {
        return ++ref_count_;
    }

    ULONG STDMETHODCALLTYPE Release() override {
        const auto count = --ref_count_;
        if (count == 0U) {
            delete this;
        }
        return count;
    }

    HRESULT STDMETHODCALLTYPE IsPixelSnappingDisabled(void*, BOOL* is_disabled) override {
        *is_disabled = FALSE;
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE GetCurrentTransform(void*, DWRITE_MATRIX* transform) override {
        *transform = DWRITE_MATRIX{1.0F, 0.0F, 0.0F, 1.0F, 0.0F, 0.0F};
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE GetPixelsPerDip(void*, FLOAT* pixels_per_dip) override {
        *pixels_per_dip = pixels_per_dip_;
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE DrawGlyphRun(void*, FLOAT baseline_origin_x, FLOAT baseline_origin_y,
                                           DWRITE_MEASURING_MODE, const DWRITE_GLYPH_RUN* glyph_run,
                                           const DWRITE_GLYPH_RUN_DESCRIPTION* description,
                                           IUnknown*) override {
        if (glyph_run == nullptr || description == nullptr) {
            return E_INVALIDARG;
        }

        const auto glyph_font_family =
            font_family_name_for_face(glyph_run->fontFace, layout_.style.locale);
        const auto glyph_source_ranges =
            build_glyph_source_ranges(*description, glyph_run->glyphCount);
        auto x = baseline_origin_x;
        for (UINT32 glyph_index = 0; glyph_index < glyph_run->glyphCount; ++glyph_index) {
            const auto cluster = source_range_for_glyph(*description, glyph_index,
                                                        glyph_source_ranges);
            const auto offset = glyph_run->glyphOffsets == nullptr
                                    ? DWRITE_GLYPH_OFFSET{}
                                    : glyph_run->glyphOffsets[glyph_index];
            const auto advance =
                glyph_run->glyphAdvances == nullptr ? 0.0F : glyph_run->glyphAdvances[glyph_index];
            const auto glyph_id = glyph_run->glyphIndices[glyph_index];
            const auto is_missing_glyph = glyph_id == 0U;
            layout_.has_missing_glyphs = layout_.has_missing_glyphs || is_missing_glyph;
            layout_.glyphs.push_back(TextGlyph{
                .font_family = glyph_font_family,
                .byte_offset = cluster.first,
                .byte_length = cluster.second,
                .glyph_index = glyph_id,
                .is_missing_glyph = is_missing_glyph,
                .cluster_index = cluster_index_for_source_range(cluster.first, cluster.second),
                .origin = layout::Point{x + offset.advanceOffset,
                                        baseline_origin_y + offset.ascenderOffset},
                .advance = advance,
                .advance_offset = layout::Point{offset.advanceOffset, offset.ascenderOffset},
                .is_cluster_start =
                    glyph_index < glyph_source_ranges.size()
                        ? glyph_source_ranges[glyph_index].is_cluster_start
                        : glyph_index == 0U,
                .is_right_to_left = glyph_run->bidiLevel % 2U != 0U});
            x += advance;
        }

        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE DrawUnderline(void*, FLOAT baseline_origin_x, FLOAT baseline_origin_y,
                                            const DWRITE_UNDERLINE* underline, IUnknown*) override {
        if (underline == nullptr) {
            return E_INVALIDARG;
        }
        append_decoration(TextDecorationLine::Underline, baseline_origin_x, baseline_origin_y,
                          underline->width, underline->offset, underline->thickness,
                          underline->readingDirection);
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE DrawStrikethrough(void*, FLOAT baseline_origin_x,
                                                FLOAT baseline_origin_y,
                                                const DWRITE_STRIKETHROUGH* strikethrough,
                                                IUnknown*) override {
        if (strikethrough == nullptr) {
            return E_INVALIDARG;
        }
        append_decoration(TextDecorationLine::Strikethrough, baseline_origin_x, baseline_origin_y,
                          strikethrough->width, strikethrough->offset, strikethrough->thickness,
                          strikethrough->readingDirection);
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE DrawInlineObject(void*, FLOAT, FLOAT, IDWriteInlineObject*, BOOL,
                                               BOOL, IUnknown*) override {
        return S_OK;
    }

  private:
    struct ClusterLookup {
        std::size_t byte_offset = 0;
        std::size_t byte_end = 0;
        std::uint32_t index = 0;
    };

    struct GlyphSourceRange {
        UINT32 first_utf16 = 0U;
        UINT32 last_utf16 = 0U;
        bool has_mapping = false;
        bool is_cluster_start = false;
    };

    [[nodiscard]] static std::vector<ClusterLookup>
    build_cluster_lookup(const std::vector<TextCluster>& clusters) {
        std::vector<ClusterLookup> lookup;
        lookup.reserve(clusters.size());
        for (std::size_t index = 0; index < clusters.size(); ++index) {
            const auto& cluster = clusters[index];
            lookup.push_back(ClusterLookup{.byte_offset = cluster.byte_offset,
                                           .byte_end = cluster.byte_offset + cluster.byte_length,
                                           .index = static_cast<std::uint32_t>(index)});
        }
        if (!std::is_sorted(lookup.begin(), lookup.end(), [](const auto& left,
                                                             const auto& right) {
                if (left.byte_offset != right.byte_offset) {
                    return left.byte_offset < right.byte_offset;
                }
                return left.byte_end < right.byte_end;
            })) {
            std::sort(lookup.begin(), lookup.end(), [](const auto& left, const auto& right) {
                if (left.byte_offset != right.byte_offset) {
                    return left.byte_offset < right.byte_offset;
                }
                return left.byte_end < right.byte_end;
            });
        }
        return lookup;
    }

    [[nodiscard]] static std::vector<GlyphSourceRange>
    build_glyph_source_ranges(const DWRITE_GLYPH_RUN_DESCRIPTION& description,
                              UINT32 glyph_count) {
        std::vector<GlyphSourceRange> ranges(glyph_count);
        if (description.clusterMap == nullptr) {
            return ranges;
        }

        for (UINT32 text_index = 0; text_index < description.stringLength; ++text_index) {
            const auto glyph_index = description.clusterMap[text_index];
            if (glyph_index >= glyph_count) {
                continue;
            }
            auto& range = ranges[glyph_index];
            if (!range.has_mapping) {
                range.first_utf16 = text_index;
                range.last_utf16 = text_index + 1U;
                range.has_mapping = true;
                range.is_cluster_start = true;
            } else {
                range.first_utf16 = std::min(range.first_utf16, text_index);
                range.last_utf16 = std::max(range.last_utf16, text_index + 1U);
            }
        }
        return ranges;
    }

    [[nodiscard]] std::pair<std::size_t, std::size_t>
    source_range_for_glyph(const DWRITE_GLYPH_RUN_DESCRIPTION& description,
                           UINT32 glyph_index,
                           const std::vector<GlyphSourceRange>& ranges) const noexcept {
        auto first_utf16 = description.stringLength;
        auto last_utf16 = UINT32{0};
        if (glyph_index < ranges.size() && ranges[glyph_index].has_mapping) {
            first_utf16 = ranges[glyph_index].first_utf16;
            last_utf16 = ranges[glyph_index].last_utf16;
        } else {
            first_utf16 = std::min(glyph_index, description.stringLength);
            last_utf16 = first_utf16;
        }

        const auto start =
            byte_offset_from_utf16(utf16_to_utf8_, description.textPosition + first_utf16);
        const auto end =
            byte_offset_from_utf16(utf16_to_utf8_, description.textPosition + last_utf16);
        return {start, end >= start ? end - start : 0U};
    }

    TextLayout& layout_;
    const std::vector<std::size_t>& utf16_to_utf8_;
    std::vector<ClusterLookup> cluster_lookup_;
    float pixels_per_dip_ = 1.0F;
    ULONG ref_count_ = 1;

    [[nodiscard]] std::uint32_t
    cluster_index_for_source_range(std::size_t byte_offset,
                                   std::size_t byte_length) const noexcept {
        const auto byte_end = byte_offset + byte_length;
        const auto exact = std::lower_bound(
            cluster_lookup_.begin(), cluster_lookup_.end(), byte_offset,
            [](const ClusterLookup& item, std::size_t value) { return item.byte_offset < value; });
        for (auto it = exact; it != cluster_lookup_.end() && it->byte_offset == byte_offset; ++it) {
            if (it->byte_end == byte_end) {
                return it->index;
            }
        }
        if (exact != cluster_lookup_.begin()) {
            const auto candidate = std::prev(exact);
            if (byte_offset >= candidate->byte_offset && byte_offset < candidate->byte_end) {
                return candidate->index;
            }
        }
        return 0U;
    }

    [[nodiscard]] std::uint32_t line_index_for_baseline(float baseline_y) const noexcept {
        for (std::size_t index = 0; index < layout_.lines.size(); ++index) {
            const auto& line = layout_.lines[index];
            if (baseline_y >= line.rect.y - 0.01F &&
                baseline_y <= line.rect.y + line.rect.height + 0.01F) {
                return static_cast<std::uint32_t>(index);
            }
        }
        return 0U;
    }

    void append_decoration(TextDecorationLine line, float baseline_origin_x,
                           float baseline_origin_y, float width, float offset, float thickness,
                           DWRITE_READING_DIRECTION reading_direction) {
        if (!std::isfinite(width) || width <= 0.0F || !std::isfinite(thickness) ||
            thickness <= 0.0F || !std::isfinite(offset)) {
            return;
        }

        const auto y = baseline_origin_y + offset;
        layout_.decorations.push_back(TextDecorationRun{
            .line = line,
            .line_index = line_index_for_baseline(baseline_origin_y),
            .rect = layout::Rect{baseline_origin_x, y, width, thickness},
            .is_right_to_left = reading_direction == DWRITE_READING_DIRECTION_RIGHT_TO_LEFT});
    }
};

[[nodiscard]] std::vector<DWRITE_LINE_METRICS>
query_line_metrics(IDWriteTextLayout& dwrite_layout) {
    UINT32 line_count = 0;
    auto result = dwrite_layout.GetLineMetrics(nullptr, 0, &line_count);
    if (result != E_NOT_SUFFICIENT_BUFFER && FAILED(result)) {
        throw make_hresult_error("failed to query DirectWrite line metrics", result);
    }
    if (line_count == 0U) {
        return {};
    }

    std::vector<DWRITE_LINE_METRICS> line_metrics(line_count);
    result = dwrite_layout.GetLineMetrics(line_metrics.data(), line_count, &line_count);
    if (FAILED(result)) {
        throw make_hresult_error("failed to get DirectWrite line metrics", result);
    }
    return line_metrics;
}

[[nodiscard]] float max_height_for_line_limit(const std::vector<DWRITE_LINE_METRICS>& line_metrics,
                                              std::size_t max_lines) noexcept {
    if (max_lines == 0U || line_metrics.size() <= max_lines) {
        return 0.0F;
    }
    auto height = 0.0F;
    for (std::size_t index = 0; index < max_lines; ++index) {
        height += line_metrics[index].height;
    }
    return height;
}

void populate_lines(const std::vector<DWRITE_LINE_METRICS>& line_metrics, TextLayout& layout,
                    const std::vector<std::size_t>& utf16_to_utf8) {
    auto text_position = std::size_t{0};
    auto line_top = 0.0F;
    const auto line_count = layout.options.max_lines == 0U
                                ? line_metrics.size()
                                : std::min(line_metrics.size(), layout.options.max_lines);
    for (std::size_t index = 0; index < line_count; ++index) {
        const auto& line = line_metrics[index];
        const auto start = byte_offset_from_utf16(utf16_to_utf8, text_position);
        const auto end = byte_offset_from_utf16(utf16_to_utf8, text_position + line.length);
        layout.lines.push_back(
            TextLine{.byte_offset = start,
                     .byte_length = end >= start ? end - start : 0U,
                     .rect = layout::Rect{0.0F, line_top, layout.size.width, line.height},
                     .baseline = line.baseline,
                     .has_trailing_newline = line.newlineLength > 0U});
        text_position += line.length;
        line_top += line.height;
    }
    if (line_count < line_metrics.size()) {
        layout.truncated = true;
    }
}

[[nodiscard]] std::uint32_t line_index_for_y(const TextLayout& layout, float y) noexcept {
    if (layout.lines.empty()) {
        return 0U;
    }
    if (y < layout.lines.front().rect.y) {
        return 0U;
    }
    const auto iterator =
        std::upper_bound(layout.lines.begin(), layout.lines.end(), y,
                         [](float value, const TextLine& line) { return value < line.rect.y; });
    if (iterator != layout.lines.begin()) {
        const auto index = static_cast<std::size_t>(std::distance(layout.lines.begin(),
                                                                  std::prev(iterator)));
        return static_cast<std::uint32_t>(index);
    }
    return static_cast<std::uint32_t>(layout.lines.size() - 1U);
}

[[nodiscard]] std::optional<std::uint32_t> visible_line_index_for_y(const TextLayout& layout,
                                                                    float y) noexcept {
    constexpr auto epsilon = 0.01F;
    if (layout.lines.empty()) {
        return std::nullopt;
    }
    const auto index = line_index_for_y(layout, y);
    if (index < layout.lines.size()) {
        const auto& line = layout.lines[index];
        if (std::abs(y - line.rect.y) < epsilon ||
            (y > line.rect.y && y < line.rect.y + line.rect.height)) {
            return index;
        }
    }
    return std::nullopt;
}

[[nodiscard]] TextLineRange visible_line_range_for_layout(const TextLayout& layout) noexcept {
    const auto line_count = static_cast<std::uint32_t>(layout.lines.size());
    if (line_count == 0U) {
        return {};
    }
    if (!layout.options.viewport) {
        return TextLineRange{.start_line_index = 0U, .end_line_index = line_count};
    }

    const auto viewport = *layout.options.viewport;
    if (viewport.width <= 0.0F || viewport.height <= 0.0F) {
        return {};
    }

    const auto viewport_bottom = viewport.y + viewport.height;
    auto start = line_count;
    auto end = std::uint32_t{0};
    for (std::size_t index = 0; index < layout.lines.size(); ++index) {
        const auto& line = layout.lines[index];
        const auto line_bottom = line.rect.y + line.rect.height;
        if (line_bottom <= viewport.y || line.rect.y >= viewport_bottom) {
            continue;
        }
        const auto line_index = static_cast<std::uint32_t>(index);
        start = std::min(start, line_index);
        end = std::max(end, line_index + 1U);
    }

    if (start == line_count) {
        return {};
    }
    return TextLineRange{.start_line_index = start, .end_line_index = end};
}

[[nodiscard]] std::uint64_t caret_stop_key(std::size_t byte_offset, std::uint32_t line_index,
                                           float x, float y) noexcept {
    const auto quantize = [](float value) noexcept {
        return static_cast<std::int64_t>(std::round(value * 100.0F));
    };
    auto seed = std::uint64_t{1469598103934665603ULL};
    const auto mix = [&](std::uint64_t value) noexcept {
        seed ^= value;
        seed *= 1099511628211ULL;
    };
    mix(static_cast<std::uint64_t>(byte_offset));
    mix(static_cast<std::uint64_t>(line_index));
    mix(static_cast<std::uint64_t>(quantize(x)));
    mix(static_cast<std::uint64_t>(quantize(y)));
    return seed;
}

void append_caret_stop_from_metrics(TextLayout& layout, std::size_t byte_offset,
                                    std::uint32_t line_index, float x, float y, float height,
                                    std::unordered_set<std::uint64_t>* deduper) {
    const auto key = caret_stop_key(byte_offset, line_index, x, y);
    if (deduper != nullptr) {
        if (!deduper->insert(key).second) {
            return;
        }
    } else {
        const auto is_same_stop = [&](const TextCaretStop& stop) noexcept {
            return stop.byte_offset == byte_offset && stop.line_index == line_index &&
                   std::abs(stop.origin.x - x) < 0.01F &&
                   std::abs(stop.origin.y - y) < 0.01F;
        };
        if (std::find_if(layout.caret_stops.begin(), layout.caret_stops.end(), is_same_stop) !=
            layout.caret_stops.end()) {
            return;
        }
    }

    layout.caret_stops.push_back(TextCaretStop{.byte_offset = byte_offset,
                                               .line_index = line_index,
                                               .origin = layout::Point{x, y},
                                               .height = height});
}

void append_caret_stop(IDWriteTextLayout& dwrite_layout, TextLayout& layout,
                       const std::vector<std::size_t>& utf16_to_utf8, std::size_t utf16_position,
                       std::unordered_set<std::uint64_t>* deduper = nullptr) {
    FLOAT x = 0.0F;
    FLOAT y = 0.0F;
    DWRITE_HIT_TEST_METRICS hit_metrics{};
    const auto result = dwrite_layout.HitTestTextPosition(
        static_cast<UINT32>(std::min(utf16_position, utf16_to_utf8.size() - 1U)), FALSE, &x, &y,
        &hit_metrics);
    if (FAILED(result)) {
        throw make_hresult_error("failed to hit test DirectWrite caret stop", result);
    }

    const auto line_index = visible_line_index_for_y(layout, hit_metrics.top);
    if (!line_index) {
        return;
    }

    const auto byte_offset = byte_offset_from_utf16(utf16_to_utf8, utf16_position);
    const auto height = std::max(hit_metrics.height, default_line_height(layout.style));
    append_caret_stop_from_metrics(layout, byte_offset, *line_index, x, y, height, deduper);
}

void populate_clusters(IDWriteTextLayout& dwrite_layout, TextLayout& layout,
                       const std::vector<std::size_t>& utf16_to_utf8) {
    UINT32 cluster_count = 0;
    auto result = dwrite_layout.GetClusterMetrics(nullptr, 0, &cluster_count);
    if (result != E_NOT_SUFFICIENT_BUFFER && FAILED(result)) {
        throw make_hresult_error("failed to query DirectWrite cluster metrics", result);
    }

    if (cluster_count == 0U) {
        return;
    }

    std::vector<DWRITE_CLUSTER_METRICS> metrics(cluster_count);
    result = dwrite_layout.GetClusterMetrics(metrics.data(), cluster_count, &cluster_count);
    if (FAILED(result)) {
        throw make_hresult_error("failed to get DirectWrite cluster metrics", result);
    }

    layout.clusters.reserve(layout.clusters.size() + cluster_count);
    layout.caret_stops.reserve(layout.caret_stops.size() + cluster_count * 2U);
    std::unordered_set<std::uint64_t> caret_deduper;
    caret_deduper.reserve(cluster_count * 2U + layout.caret_stops.size());
    for (const auto& stop : layout.caret_stops) {
        caret_deduper.insert(
            caret_stop_key(stop.byte_offset, stop.line_index, stop.origin.x, stop.origin.y));
    }

    auto utf16_position = std::size_t{0};
    for (const auto& metric : metrics) {
        const auto start = byte_offset_from_utf16(utf16_to_utf8, utf16_position);
        const auto end = byte_offset_from_utf16(utf16_to_utf8, utf16_position + metric.length);
        if (layout.visible_byte_end < layout.text.size() &&
            (start >= layout.visible_byte_end || end > layout.visible_byte_end)) {
            utf16_position += metric.length;
            continue;
        }
        DWRITE_HIT_TEST_METRICS hit_metrics{};
        auto x = 0.0F;
        auto y = 0.0F;
        const auto hit_result = dwrite_layout.HitTestTextPosition(
            static_cast<UINT32>(utf16_position), FALSE, &x, &y, &hit_metrics);
        if (FAILED(hit_result)) {
            throw make_hresult_error("failed to hit test DirectWrite cluster", hit_result);
        }

        const auto line_index = visible_line_index_for_y(layout, hit_metrics.top);
        if (!line_index) {
            utf16_position += metric.length;
            continue;
        }

        const auto start_height = std::max(hit_metrics.height, default_line_height(layout.style));
        append_caret_stop_from_metrics(layout, start, *line_index, x, y, start_height,
                                       &caret_deduper);
        layout.clusters.push_back(
            TextCluster{.byte_offset = start,
                        .byte_length = end >= start ? end - start : 0U,
                        .origin = layout::Point{hit_metrics.left, hit_metrics.top},
                        .size = layout::Size{hit_metrics.width, hit_metrics.height},
                        .advance = metric.width,
                        .line_index = *line_index,
                        .is_whitespace = metric.isWhitespace != 0,
                        .is_newline = metric.isNewline != 0,
                        .is_right_to_left = metric.isRightToLeft != 0});
        utf16_position += metric.length;
        append_caret_stop(dwrite_layout, layout, utf16_to_utf8, utf16_position, &caret_deduper);
    }
}

[[nodiscard]] std::uint32_t line_index_for_cluster_offset(const TextLayout& layout,
                                                          std::size_t byte_offset) noexcept {
    if (layout.lines.empty()) {
        return 0U;
    }

    for (std::size_t index = 0; index < layout.lines.size(); ++index) {
        const auto& line = layout.lines[index];
        const auto line_end = line.byte_offset + line.byte_length;
        const auto is_last_line = index + 1U == layout.lines.size();
        if (byte_offset >= line.byte_offset &&
            (byte_offset < line_end || (is_last_line && byte_offset <= line_end))) {
            return static_cast<std::uint32_t>(index);
        }
    }
    return static_cast<std::uint32_t>(layout.lines.size() - 1U);
}

void append_caret_stop_for_cluster(TextLayout& layout, std::size_t byte_offset,
                                   std::uint32_t line_index, layout::Point origin, float height,
                                   std::unordered_set<std::uint64_t>* deduper = nullptr) {
    append_caret_stop_from_metrics(layout, byte_offset, line_index, origin.x, origin.y,
                                   std::max(height, default_line_height(layout.style)), deduper);
}

[[nodiscard]] std::uint32_t cluster_index_for_range(const TextLayout& layout,
                                                    std::size_t byte_offset,
                                                    std::size_t byte_length) noexcept {
    const auto byte_end = byte_offset + byte_length;
    for (std::size_t index = 0; index < layout.clusters.size(); ++index) {
        const auto& cluster = layout.clusters[index];
        if (cluster.byte_offset == byte_offset && cluster.byte_length == byte_length) {
            return static_cast<std::uint32_t>(index);
        }
    }
    for (std::size_t index = 0; index < layout.clusters.size(); ++index) {
        const auto& cluster = layout.clusters[index];
        const auto cluster_end = cluster.byte_offset + cluster.byte_length;
        if (byte_offset >= cluster.byte_offset && byte_end <= cluster_end) {
            return static_cast<std::uint32_t>(index);
        }
    }
    return 0U;
}

void synthesize_missing_clusters_from_glyphs(TextLayout& layout) {
    std::unordered_set<std::uint64_t> cluster_ranges;
    cluster_ranges.reserve(layout.clusters.size() + layout.glyphs.size());
    const auto cluster_range_key = [](std::size_t byte_offset, std::size_t byte_length) noexcept {
        auto seed = std::uint64_t{1469598103934665603ULL};
        seed ^= static_cast<std::uint64_t>(byte_offset);
        seed *= 1099511628211ULL;
        seed ^= static_cast<std::uint64_t>(byte_length);
        seed *= 1099511628211ULL;
        return seed;
    };
    for (const auto& cluster : layout.clusters) {
        cluster_ranges.insert(cluster_range_key(cluster.byte_offset, cluster.byte_length));
    }

    std::unordered_set<std::uint64_t> caret_deduper;
    caret_deduper.reserve(layout.caret_stops.size() + layout.glyphs.size() * 2U);
    for (const auto& stop : layout.caret_stops) {
        caret_deduper.insert(
            caret_stop_key(stop.byte_offset, stop.line_index, stop.origin.x, stop.origin.y));
    }

    for (const auto& glyph : layout.glyphs) {
        if (glyph.byte_length == 0U || glyph.byte_offset >= layout.text.size()) {
            continue;
        }
        const auto byte_end = std::min(layout.text.size(), glyph.byte_offset + glyph.byte_length);
        const auto byte_length = byte_end - glyph.byte_offset;
        if (byte_length == 0U ||
            cluster_ranges.contains(cluster_range_key(glyph.byte_offset, byte_length))) {
            continue;
        }

        const auto line_index = line_index_for_cluster_offset(layout, glyph.byte_offset);
        const auto line_top =
            line_index < layout.lines.size() ? layout.lines[line_index].rect.y : glyph.origin.y;
        const auto line_height = line_index < layout.lines.size()
                                     ? layout.lines[line_index].rect.height
                                     : default_line_height(layout.style);
        const auto width = std::max(glyph.advance, 0.0F);
        const auto leading_x = glyph.is_right_to_left ? glyph.origin.x + width : glyph.origin.x;
        const auto trailing_x = glyph.is_right_to_left ? glyph.origin.x : glyph.origin.x + width;

        layout.clusters.push_back(
            TextCluster{.byte_offset = glyph.byte_offset,
                        .byte_length = byte_length,
                        .origin = layout::Point{std::min(leading_x, trailing_x), line_top},
                        .size = layout::Size{width, line_height},
                        .advance = width,
                        .line_index = line_index,
                        .is_right_to_left = glyph.is_right_to_left});
        cluster_ranges.insert(cluster_range_key(glyph.byte_offset, byte_length));
        append_caret_stop_for_cluster(layout, glyph.byte_offset, line_index,
                                      layout::Point{leading_x, line_top}, line_height,
                                      &caret_deduper);
        append_caret_stop_for_cluster(layout, byte_end, line_index,
                                      layout::Point{trailing_x, line_top}, line_height,
                                      &caret_deduper);
    }

    std::stable_sort(layout.clusters.begin(), layout.clusters.end(),
                     [](const auto& left, const auto& right) {
                         if (left.line_index != right.line_index) {
                             return left.line_index < right.line_index;
                         }
                         if (left.byte_offset != right.byte_offset) {
                             return left.byte_offset < right.byte_offset;
                         }
                         return left.byte_length < right.byte_length;
                     });
    std::unordered_map<std::uint64_t, std::uint32_t> exact_cluster_indices;
    exact_cluster_indices.reserve(layout.clusters.size());
    for (std::size_t index = 0U; index < layout.clusters.size(); ++index) {
        const auto& cluster = layout.clusters[index];
        exact_cluster_indices.emplace(cluster_range_key(cluster.byte_offset, cluster.byte_length),
                                      static_cast<std::uint32_t>(index));
    }
    for (auto& glyph : layout.glyphs) {
        if (const auto match =
                exact_cluster_indices.find(cluster_range_key(glyph.byte_offset, glyph.byte_length));
            match != exact_cluster_indices.end()) {
            glyph.cluster_index = match->second;
        } else {
            glyph.cluster_index =
                cluster_index_for_range(layout, glyph.byte_offset, glyph.byte_length);
        }
    }
}

} // namespace

TextLayout TextEngine::layout_text(std::string_view text, const TextStyle& style,
                                   TextLayoutOptions options) const {
    validate_text_style(style);
    validate_layout_options(options);
    {
        const std::scoped_lock lock(cache_mutex_);
        for (const auto& entry : cache_) {
            if (entry.text == text && entry.style == style && entry.options == options) {
                return entry.layout;
            }
        }
    }

    auto layout = layout_text_uncached(text, style, options);
    {
        const std::scoped_lock lock(cache_mutex_);
        if (max_cached_layouts_ > 0U) {
            cache_.push_back(CachedLayout{
                .text = std::string(text), .style = style, .options = options, .layout = layout});
            while (cache_.size() > max_cached_layouts_) {
                cache_.erase(cache_.begin());
            }
        }
    }

    return layout;
}

std::string TextEngine::normalize_text(std::string_view text, TextNormalizationForm form) const {
    return normalize_utf8_text(text, form);
}

TextLayout TextEngine::shape_single_line(std::string_view text, const TextStyle& style) const {
    auto single_line_style = style;
    single_line_style.wrapping = TextWrapping::NoWrap;
    return layout_text(text, single_line_style);
}

TextLayout TextEngine::layout_text_uncached(std::string_view text, const TextStyle& style,
                                            TextLayoutOptions options) const {
    TextLayout layout;
    layout.text = std::string(text);
    layout.style = style;
    layout.options = options;
    layout.visible_byte_end = layout.text.size();
    if (text.empty()) {
        const auto line_height = default_line_height(style);
        layout.size = layout::Size{0.0F, line_height};
        layout.baseline = default_baseline(style, line_height);
        layout.lines.push_back(TextLine{.rect = layout::Rect{0.0F, 0.0F, 0.0F, line_height},
                                        .baseline = layout.baseline});
        layout.visible_line_range = visible_line_range_for_layout(layout);
        layout.caret_stops.push_back(
            TextCaretStop{.line_index = 0U, .origin = layout::Point{}, .height = line_height});
        return layout;
    }

    auto& factory = shared_dwrite_factory();
    const auto wide_text = utf8_to_wide(text);
    if (wide_text.size() > static_cast<std::size_t>(std::numeric_limits<UINT32>::max())) {
        throw std::invalid_argument("text is too long for DirectWrite layout");
    }
    const auto text_length = static_cast<UINT32>(wide_text.size());
    const auto utf16_to_utf8 = build_utf16_to_utf8_map(text, wide_text);
    const auto format = create_text_format(factory, style);
    constexpr auto unbounded_layout_extent = 1000000.0F;
    const auto has_width_constraint = options.max_width > 0.0F;
    auto layout_width = has_width_constraint ? options.max_width : unbounded_layout_extent;
    auto max_height = options.max_height > 0.0F ? options.max_height : unbounded_layout_extent;

    Microsoft::WRL::ComPtr<IDWriteTextLayout> dwrite_layout;
    const auto create_layout = [&](float width, float height) {
        Microsoft::WRL::ComPtr<IDWriteTextLayout> created_layout;
        const auto layout_result = factory.CreateTextLayout(
            wide_text.data(), text_length, format.Get(), width, height, &created_layout);
        if (FAILED(layout_result)) {
            throw make_hresult_error("failed to create DirectWrite text layout", layout_result);
        }
        apply_typography(factory, *created_layout.Get(), style, text_length);
        apply_text_decorations(*created_layout.Get(), style, text_length);
        apply_trimming(factory, *created_layout.Get(), style.trimming);
        return created_layout;
    };
    const auto set_layout_extent = [&](float width, float height) {
        const auto width_result = dwrite_layout->SetMaxWidth(width);
        if (FAILED(width_result)) {
            throw make_hresult_error("failed to set DirectWrite text layout width",
                                     width_result);
        }
        const auto height_result = dwrite_layout->SetMaxHeight(height);
        if (FAILED(height_result)) {
            throw make_hresult_error("failed to set DirectWrite text layout height",
                                     height_result);
        }
    };

    dwrite_layout = create_layout(layout_width, max_height);
    if (!has_width_constraint) {
        DWRITE_TEXT_METRICS unbounded_metrics{};
        const auto unbounded_metrics_result = dwrite_layout->GetMetrics(&unbounded_metrics);
        if (FAILED(unbounded_metrics_result)) {
            throw make_hresult_error("failed to get DirectWrite text metrics",
                                     unbounded_metrics_result);
        }
        const auto tight_width =
            std::max(1.0F, std::ceil(std::max(unbounded_metrics.widthIncludingTrailingWhitespace,
                                              unbounded_metrics.width)));
        if (std::isfinite(tight_width) && tight_width < layout_width) {
            layout_width = tight_width;
            set_layout_extent(layout_width, max_height);
        }
    }
    auto line_metrics = query_line_metrics(*dwrite_layout.Get());
    const auto line_limited_height =
        max_height_for_line_limit(line_metrics, options.max_lines);
    if (line_limited_height > 0.0F && line_limited_height < max_height) {
        max_height = line_limited_height;
        set_layout_extent(layout_width, max_height);
        line_metrics = query_line_metrics(*dwrite_layout.Get());
        layout.truncated = true;
    }

    DWRITE_TEXT_METRICS text_metrics{};
    const auto metrics_result = dwrite_layout->GetMetrics(&text_metrics);
    if (FAILED(metrics_result)) {
        throw make_hresult_error("failed to get DirectWrite text metrics", metrics_result);
    }

    layout.size = layout::Size{text_metrics.widthIncludingTrailingWhitespace, text_metrics.height};
    layout.truncated =
        layout.truncated || text_metrics.height > max_height ||
        (has_width_constraint && (text_metrics.widthIncludingTrailingWhitespace > layout_width ||
                                  text_metrics.width > layout_width));
    populate_lines(line_metrics, layout, utf16_to_utf8);
    layout.visible_line_range = visible_line_range_for_layout(layout);
    if (!layout.lines.empty() && options.max_lines > 0U &&
        layout.lines.size() <= options.max_lines) {
        const auto& last_line = layout.lines.back();
        layout.size.height = std::min(layout.size.height, last_line.rect.y + last_line.rect.height);
        layout.visible_byte_end =
            std::min(layout.text.size(), last_line.byte_offset + last_line.byte_length);
    }
    layout.baseline =
        !layout.lines.empty() ? layout.lines.front().baseline : std::max(style.font_size, 1.0F);
    populate_clusters(*dwrite_layout.Get(), layout, utf16_to_utf8);
    if (options.viewport) {
        auto visible_start = layout.text.size();
        auto visible_end = std::size_t{0};
        for (const auto& cluster : layout.clusters) {
            if (!layout.lines.empty() && cluster.line_index >= layout.lines.size()) {
                continue;
            }
            const auto width = std::max(cluster.advance, cluster.size.width);
            const auto height = std::max(cluster.size.height, style.font_size);
            const auto right = cluster.origin.x + width;
            const auto bottom = cluster.origin.y + height;
            const auto viewport_right = options.viewport->x + options.viewport->width;
            const auto viewport_bottom = options.viewport->y + options.viewport->height;
            if (right <= options.viewport->x || cluster.origin.x >= viewport_right ||
                bottom <= options.viewport->y || cluster.origin.y >= viewport_bottom) {
                continue;
            }
            visible_start = std::min(visible_start, cluster.byte_offset);
            visible_end = std::max(visible_end, cluster.byte_offset + cluster.byte_length);
        }
        layout.visible_byte_start = visible_start == layout.text.size() ? 0U : visible_start;
        layout.visible_byte_end = visible_end;
    }

    auto* renderer = new CapturingTextRenderer(layout, utf16_to_utf8);
    const auto draw_result = dwrite_layout->Draw(nullptr, renderer, 0.0F, 0.0F);
    renderer->Release();
    if (FAILED(draw_result)) {
        throw make_hresult_error("failed to capture DirectWrite glyph runs", draw_result);
    }
    synthesize_missing_clusters_from_glyphs(layout);

    return layout;
}

layout::Size TextEngine::measure(std::string_view text, const TextStyle& style,
                                 TextLayoutOptions options) const {
    return layout_text(text, style, options).size;
}

layout::Size TextEngine::measure_single_line(std::string_view text, const TextStyle& style) const {
    return shape_single_line(text, style).size;
}

float TextEngine::caret_x_for_byte_offset(const TextLayout& layout,
                                          std::size_t byte_offset) const noexcept {
    return caret_position_for_byte_offset(layout, byte_offset).x;
}

layout::Point TextEngine::caret_position_for_byte_offset(const TextLayout& layout,
                                                         std::size_t byte_offset) const noexcept {
    const auto clamped_offset = clamp_utf8_boundary(layout.text, byte_offset);

    if (!layout.caret_stops.empty()) {
        const auto desired_line = line_index_for_byte_offset(layout, clamped_offset);
        for (const auto& stop : layout.caret_stops) {
            if (stop.byte_offset == clamped_offset && stop.line_index == desired_line) {
                return stop.origin;
            }
        }
        for (const auto& stop : layout.caret_stops) {
            if (stop.byte_offset == clamped_offset) {
                return stop.origin;
            }
        }
    }

    if (clamped_offset == 0U || layout.clusters.empty()) {
        return {};
    }
    if (clamped_offset >= layout.text.size()) {
        const auto& cluster = layout.clusters.back();
        return layout::Point{cluster.origin.x + cluster.advance, cluster.origin.y};
    }

    for (const auto& cluster : layout.clusters) {
        if (clamped_offset <= cluster.byte_offset ||
            clamped_offset < cluster.byte_offset + cluster.byte_length) {
            return cluster.origin;
        }
    }

    return layout::Point{layout.size.width, 0.0F};
}

std::size_t TextEngine::hit_test_byte_offset(const TextLayout& layout, float x) const noexcept {
    return hit_test_byte_offset(layout, layout::Point{x, 0.0F});
}

std::size_t TextEngine::hit_test_byte_offset(const TextLayout& layout,
                                             layout::Point point) const noexcept {
    return hit_test_point(layout, point).byte_offset;
}

TextHitTestResult TextEngine::hit_test_point(const TextLayout& layout,
                                             layout::Point point) const noexcept {
    TextHitTestResult result;
    result.line_index = line_index_for_y(layout, point.y);
    const auto line_top = layout.lines.empty() ? 0.0F : layout.lines[result.line_index].rect.y;
    const auto line_bottom = layout.lines.empty() ? layout.size.height
                                                  : layout.lines[result.line_index].rect.y +
                                                        layout.lines[result.line_index].rect.height;
    const auto inside_line_y = point.y >= line_top && point.y <= line_bottom;
    if (layout.clusters.empty()) {
        if (!layout.lines.empty()) {
            result.byte_offset = layout.lines[result.line_index].byte_offset;
        }
        result.point = caret_position_for_byte_offset(layout, result.byte_offset);
        return result;
    }

    const TextCaretStop* first_stop = nullptr;
    const TextCaretStop* last_stop = nullptr;
    const TextCaretStop* closest_stop = nullptr;
    auto closest_distance = std::numeric_limits<float>::max();
    auto line_stop_count = std::size_t{0};
    for (const auto& stop : layout.caret_stops) {
        if (stop.line_index == result.line_index) {
            ++line_stop_count;
            if (first_stop == nullptr || stop.origin.x < first_stop->origin.x ||
                (std::abs(stop.origin.x - first_stop->origin.x) < 0.01F &&
                 stop.byte_offset < first_stop->byte_offset)) {
                first_stop = &stop;
            }
            if (last_stop == nullptr || stop.origin.x > last_stop->origin.x ||
                (std::abs(stop.origin.x - last_stop->origin.x) < 0.01F &&
                 stop.byte_offset > last_stop->byte_offset)) {
                last_stop = &stop;
            }

            const auto distance = std::abs(stop.origin.x - point.x);
            if (closest_stop == nullptr || distance < closest_distance ||
                (std::abs(distance - closest_distance) < 0.01F &&
                 (stop.origin.x > closest_stop->origin.x ||
                  (std::abs(stop.origin.x - closest_stop->origin.x) < 0.01F &&
                   stop.byte_offset > closest_stop->byte_offset)))) {
                closest_stop = &stop;
                closest_distance = distance;
            }
        }
    }
    if (line_stop_count >= 2U && first_stop != nullptr && last_stop != nullptr &&
        closest_stop != nullptr) {
        const auto assign_stop_hit = [&](const TextCaretStop& stop, bool inside) {
            result.byte_offset = stop.byte_offset;
            result.point = stop.origin;
            result.is_inside_text = inside && inside_line_y;
            result.is_trailing_hit = false;
        };

        const auto first_x = first_stop->origin.x;
        const auto last_x = last_stop->origin.x;
        if (point.x <= first_x) {
            assign_stop_hit(*first_stop, false);
            return result;
        }
        if (point.x > last_x) {
            assign_stop_hit(*last_stop, false);
            return result;
        }
        assign_stop_hit(*closest_stop, true);
        return result;
    }

    std::vector<const TextCluster*> line_clusters;
    for (const auto& cluster : layout.clusters) {
        if (cluster.line_index != result.line_index) {
            continue;
        }
        line_clusters.push_back(&cluster);
    }
    std::sort(line_clusters.begin(), line_clusters.end(), [](const auto* left, const auto* right) {
        if (std::abs(left->origin.x - right->origin.x) >= 0.01F) {
            return left->origin.x < right->origin.x;
        }
        return left->byte_offset < right->byte_offset;
    });

    if (line_clusters.empty()) {
        if (!layout.lines.empty()) {
            result.byte_offset = layout.lines[result.line_index].byte_offset;
        }
        result.point = caret_position_for_byte_offset(layout, result.byte_offset);
        return result;
    }

    const auto assign_hit = [&](std::size_t offset, bool inside, bool trailing) {
        result.byte_offset = clamp_utf8_boundary(layout.text, offset);
        result.point = caret_position_for_byte_offset(layout, result.byte_offset);
        result.is_inside_text = inside && inside_line_y;
        result.is_trailing_hit = trailing;
    };

    const auto& first_cluster = *line_clusters.front();
    const auto first_left = first_cluster.origin.x;
    if (point.x <= first_left) {
        const auto offset = first_cluster.is_right_to_left
                                ? first_cluster.byte_offset + first_cluster.byte_length
                                : first_cluster.byte_offset;
        assign_hit(offset, false, offset == first_cluster.byte_offset + first_cluster.byte_length);
        return result;
    }

    for (const auto* cluster_ptr : line_clusters) {
        const auto& cluster = *cluster_ptr;

        const auto cluster_width = std::max(cluster.advance, cluster.size.width);
        const auto cluster_left = cluster.origin.x;
        const auto cluster_right = cluster_left + cluster_width;
        const auto midpoint = cluster.origin.x + cluster_width * 0.5F;
        if (point.x <= cluster_right) {
            const auto leading_offset = cluster.byte_offset;
            const auto trailing_offset = cluster.byte_offset + cluster.byte_length;
            const auto choose_trailing = point.x >= midpoint;
            const auto offset =
                cluster.is_right_to_left == choose_trailing ? leading_offset : trailing_offset;
            assign_hit(offset, point.x >= cluster_left, offset == trailing_offset);
            return result;
        }
    }

    const auto& last_cluster = *line_clusters.back();
    result.byte_offset = last_cluster.is_right_to_left
                             ? last_cluster.byte_offset
                             : last_cluster.byte_offset + last_cluster.byte_length;
    result.point = caret_position_for_byte_offset(layout, result.byte_offset);
    result.is_trailing_hit =
        result.byte_offset == last_cluster.byte_offset + last_cluster.byte_length;
    return result;
}

TextCaretMetrics TextEngine::caret_metrics_for_byte_offset(const TextLayout& layout,
                                                           std::size_t byte_offset) const noexcept {
    const auto clamped_offset = clamp_utf8_boundary(layout.text, byte_offset);
    const auto line_index = line_index_for_byte_offset(layout, clamped_offset);
    const auto position = caret_position_for_byte_offset(layout, clamped_offset);
    auto height = std::max(layout.style.font_size, 1.0F);
    auto y = position.y;
    if (!layout.lines.empty() && line_index < layout.lines.size()) {
        const auto& line = layout.lines[line_index];
        height = std::max(line.rect.height, height);
        y = line.rect.y;
    } else if (layout.size.height > 0.0F) {
        height = layout.size.height;
    }
    return TextCaretMetrics{.byte_offset = clamped_offset,
                            .line_index = line_index,
                            .rect = layout::Rect{position.x, y, 1.0F, height}};
}

std::size_t TextEngine::caret_offset_for_visual_left(const TextLayout& layout,
                                                     std::size_t byte_offset) const noexcept {
    constexpr auto epsilon = 0.01F;
    const auto clamped_offset = clamp_utf8_boundary(layout.text, byte_offset);
    const auto line_index = line_index_for_byte_offset(layout, clamped_offset);
    const auto current_position = caret_position_for_byte_offset(layout, clamped_offset);
    const TextCaretStop* candidate = nullptr;
    for (const auto& stop : layout.caret_stops) {
        if (stop.line_index != line_index || stop.origin.x >= current_position.x - epsilon) {
            continue;
        }
        if (candidate == nullptr || stop.origin.x > candidate->origin.x ||
            (std::abs(stop.origin.x - candidate->origin.x) < epsilon &&
             stop.byte_offset > candidate->byte_offset)) {
            candidate = &stop;
        }
    }
    return candidate == nullptr ? clamped_offset : candidate->byte_offset;
}

std::size_t TextEngine::caret_offset_for_visual_right(const TextLayout& layout,
                                                      std::size_t byte_offset) const noexcept {
    constexpr auto epsilon = 0.01F;
    const auto clamped_offset = clamp_utf8_boundary(layout.text, byte_offset);
    const auto line_index = line_index_for_byte_offset(layout, clamped_offset);
    const auto current_position = caret_position_for_byte_offset(layout, clamped_offset);
    const TextCaretStop* candidate = nullptr;
    for (const auto& stop : layout.caret_stops) {
        if (stop.line_index != line_index || stop.origin.x <= current_position.x + epsilon) {
            continue;
        }
        if (candidate == nullptr || stop.origin.x < candidate->origin.x ||
            (std::abs(stop.origin.x - candidate->origin.x) < epsilon &&
             stop.byte_offset < candidate->byte_offset)) {
            candidate = &stop;
        }
    }
    return candidate == nullptr ? clamped_offset : candidate->byte_offset;
}

std::vector<layout::Rect> TextEngine::selection_rects(const TextLayout& layout,
                                                      TextSelectionRange range,
                                                      std::optional<layout::Rect> viewport) const {
    const auto aligned_range = cluster_aligned_range(layout, range);
    const auto start = aligned_range.start_byte_offset;
    const auto end = aligned_range.end_byte_offset;
    std::vector<layout::Rect> rects;
    std::vector<layout::Rect> raw_rects;
    if (start == end) {
        return rects;
    }

    for (const auto& cluster : layout.clusters) {
        if (!layout.lines.empty() && cluster.line_index >= layout.lines.size()) {
            continue;
        }
        const auto cluster_start = cluster.byte_offset;
        const auto cluster_end = cluster.byte_offset + cluster.byte_length;
        if (cluster_end <= start || cluster_start >= end) {
            continue;
        }

        auto rect = layout::Rect{cluster.origin.x, cluster.origin.y,
                                 std::max(cluster.advance, cluster.size.width),
                                 std::max(cluster.size.height, layout.style.font_size)};
        if (!layout.lines.empty() && cluster.line_index < layout.lines.size()) {
            const auto& line = layout.lines[cluster.line_index];
            rect.y = line.rect.y;
            rect.height = std::max(rect.height, line.rect.height);
        }
        if (viewport) {
            rect.x -= viewport->x;
            rect.y -= viewport->y;
            const auto rect_right = rect.x + rect.width;
            const auto rect_bottom = rect.y + rect.height;
            const auto clipped_right = std::min(rect_right, viewport->width);
            const auto clipped_bottom = std::min(rect_bottom, viewport->height);
            rect.x = std::max(rect.x, 0.0F);
            rect.y = std::max(rect.y, 0.0F);
            rect.width = std::max(clipped_right - rect.x, 0.0F);
            rect.height = std::max(clipped_bottom - rect.y, 0.0F);
            if (rect.width <= 0.0F || rect.height <= 0.0F) {
                continue;
            }
        }
        raw_rects.push_back(rect);
    }

    std::sort(raw_rects.begin(), raw_rects.end(), [](layout::Rect left, layout::Rect right) {
        if (std::abs(left.y - right.y) >= 0.01F) {
            return left.y < right.y;
        }
        return left.x < right.x;
    });

    for (const auto rect : raw_rects) {
        if (!rects.empty()) {
            auto& previous = rects.back();
            const auto previous_right = previous.x + previous.width;
            if (std::abs(previous.y - rect.y) < 0.01F &&
                std::abs(previous.height - rect.height) < 0.01F &&
                std::abs(previous_right - rect.x) < 0.5F) {
                previous.width = std::max(previous.width, rect.x + rect.width - previous.x);
                continue;
            }
        }
        rects.push_back(rect);
    }

    return rects;
}

std::size_t TextEngine::previous_cluster_boundary(const TextLayout& layout,
                                                  std::size_t byte_offset) const noexcept {
    const auto clamped_offset = clamp_utf8_boundary(layout.text, byte_offset);
    auto previous = std::size_t{0};
    for (const auto& cluster : layout.clusters) {
        if (cluster.byte_offset >= clamped_offset) {
            return previous;
        }
        previous = cluster.byte_offset;
    }
    return previous;
}

std::size_t TextEngine::next_cluster_boundary(const TextLayout& layout,
                                              std::size_t byte_offset) const noexcept {
    const auto clamped_offset = clamp_utf8_boundary(layout.text, byte_offset);
    for (const auto& cluster : layout.clusters) {
        const auto end = cluster.byte_offset + cluster.byte_length;
        if (end > clamped_offset) {
            return end;
        }
    }
    return layout.text.size();
}

TextSelectionRange TextEngine::cluster_aligned_range(const TextLayout& layout,
                                                     TextSelectionRange range) const noexcept {
    auto start =
        clamp_utf8_boundary(layout.text, std::min(range.start_byte_offset, range.end_byte_offset));
    auto end =
        clamp_utf8_boundary(layout.text, std::max(range.start_byte_offset, range.end_byte_offset));
    if (start == end) {
        return TextSelectionRange{.start_byte_offset = start, .end_byte_offset = end};
    }

    for (const auto& cluster : layout.clusters) {
        const auto cluster_start = cluster.byte_offset;
        const auto cluster_end = cluster.byte_offset + cluster.byte_length;
        if (start > cluster_start && start < cluster_end) {
            start = cluster_start;
        }
        if (end > cluster_start && end < cluster_end) {
            end = cluster_end;
        }
    }

    return TextSelectionRange{.start_byte_offset = start, .end_byte_offset = end};
}

std::uint32_t TextEngine::line_index_for_byte_offset(const TextLayout& layout,
                                                     std::size_t byte_offset) const noexcept {
    if (layout.lines.empty()) {
        return 0U;
    }

    const auto clamped_offset = clamp_utf8_boundary(layout.text, byte_offset);
    for (std::size_t index = 0; index < layout.lines.size(); ++index) {
        const auto& line = layout.lines[index];
        const auto line_end = line.byte_offset + line.byte_length;
        const auto is_last_line = index + 1U == layout.lines.size();
        if (clamped_offset >= line.byte_offset &&
            (clamped_offset < line_end || (is_last_line && clamped_offset <= line_end))) {
            return static_cast<std::uint32_t>(index);
        }
    }
    return static_cast<std::uint32_t>(layout.lines.size() - 1U);
}

TextSelectionRange TextEngine::visible_byte_range(const TextLayout& layout) const noexcept {
    return TextSelectionRange{.start_byte_offset = layout.visible_byte_start,
                              .end_byte_offset = layout.visible_byte_end};
}

TextLineRange TextEngine::visible_line_range(const TextLayout& layout) const noexcept {
    return layout.visible_line_range;
}

std::vector<TextFontFamilyInfo> TextEngine::system_font_families() const {
    const std::scoped_lock lock(font_cache_mutex_);
    if (!system_font_cache_) {
        system_font_cache_ = query_system_font_families(shared_dwrite_factory(), "en-us");
    }
    return *system_font_cache_;
}

std::optional<TextFontFaceInfo> TextEngine::match_font(const TextStyle& style) const {
    const auto resolved = resolve_matching_font(shared_dwrite_factory(), style);
    if (!resolved) {
        return std::nullopt;
    }
    return resolved->info;
}

TextFontMetrics TextEngine::font_metrics(const TextStyle& style) const {
    return query_font_metrics(shared_dwrite_factory(), style);
}

void TextEngine::clear_cache() const {
    const std::scoped_lock lock(cache_mutex_);
    cache_.clear();
}

void TextEngine::clear_font_cache() const {
    const std::scoped_lock lock(font_cache_mutex_);
    system_font_cache_.reset();
}

void TextEngine::set_max_cached_layouts(std::size_t max_cached_layouts) {
    const std::scoped_lock lock(cache_mutex_);
    max_cached_layouts_ = max_cached_layouts;
    while (cache_.size() > max_cached_layouts_) {
        cache_.erase(cache_.begin());
    }
}

std::size_t TextEngine::max_cached_layouts() const {
    const std::scoped_lock lock(cache_mutex_);
    return max_cached_layouts_;
}

std::size_t TextEngine::cached_layout_count() const {
    const std::scoped_lock lock(cache_mutex_);
    return cache_.size();
}

bool is_utf8_continuation_byte(unsigned char value) noexcept {
    return (value & 0xC0U) == 0x80U;
}

std::size_t previous_utf8_boundary(std::string_view text, std::size_t byte_offset) noexcept {
    auto offset = std::min(byte_offset, text.size());
    if (offset == 0U) {
        return 0U;
    }

    --offset;
    while (offset > 0U && is_utf8_continuation_byte(static_cast<unsigned char>(text[offset]))) {
        --offset;
    }
    return offset;
}

std::size_t next_utf8_boundary(std::string_view text, std::size_t byte_offset) noexcept {
    auto offset = std::min(byte_offset, text.size());
    if (offset >= text.size()) {
        return text.size();
    }

    ++offset;
    while (offset < text.size() &&
           is_utf8_continuation_byte(static_cast<unsigned char>(text[offset]))) {
        ++offset;
    }
    return offset;
}

std::size_t clamp_utf8_boundary(std::string_view text, std::size_t byte_offset) noexcept {
    auto offset = std::min(byte_offset, text.size());
    if (offset == text.size()) {
        return offset;
    }

    while (offset > 0U && is_utf8_continuation_byte(static_cast<unsigned char>(text[offset]))) {
        --offset;
    }
    return offset;
}

std::size_t utf8_code_point_count(std::string_view text) noexcept {
    std::size_t count = 0;
    for (const auto value : text) {
        if (!is_utf8_continuation_byte(static_cast<unsigned char>(value))) {
            ++count;
        }
    }
    return count;
}

} // namespace winelement::rendering
