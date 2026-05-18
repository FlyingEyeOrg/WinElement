add_library(winelement_platform STATIC
    render_thread_pool.cpp
    win32/d3d11_display_list_renderer.cpp
    win32/dx_render_device.cpp
    win32/dx_render_resource_cache.cpp
)

add_library(WinElement::platform ALIAS winelement_platform)

target_include_directories(winelement_platform
    PUBLIC
        ${PROJECT_SOURCE_DIR}/include
)

target_link_libraries(winelement_platform
    PUBLIC
        WinElement::rendering
    PRIVATE
        d3d11
        dwrite
        dxguid
        dxgi
)

winelement_configure_library(winelement_platform)
