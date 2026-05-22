add_library(winelement_platform STATIC
    render_thread_pool.cpp
    win32/application.cpp
    win32/d3d11_composition_surface.cpp
    win32/d3d11_display_list_renderer.cpp
    win32/d3d11_render_device.cpp
    win32/d3d11_render_resource_cache.cpp
    win32/direct_composition_bridge.cpp
    win32/dispatcher.cpp
    win32/image_loader.cpp
    win32/window.cpp
    win32/window_render_worker.cpp
)

add_library(WinElement::platform ALIAS winelement_platform)

target_include_directories(winelement_platform
    PUBLIC
        $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

target_link_libraries(winelement_platform
    PUBLIC
        WinElement::elements
    PRIVATE
        WinElement::rendering
        dcomp
        d3d11
        dwrite
        dxguid
        dxgi
        imm32
        ole32
        user32
        windowscodecs
)

winelement_configure_library(winelement_platform)
