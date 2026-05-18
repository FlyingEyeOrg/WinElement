add_executable(winelement_render_pipeline_demo
    main.cpp
)

target_include_directories(winelement_render_pipeline_demo
    PRIVATE
        ${PROJECT_SOURCE_DIR}/include
        ${PROJECT_SOURCE_DIR}/src/platform/win32
)

target_link_libraries(winelement_render_pipeline_demo
    PRIVATE
        WinElement::platform
    fmt::fmt
        ole32
        user32
        windowscodecs
)

set_target_properties(winelement_render_pipeline_demo
    PROPERTIES
        OUTPUT_NAME render_pipeline_demo
)

winelement_configure_executable(winelement_render_pipeline_demo)