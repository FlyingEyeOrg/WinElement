add_executable(winelement_render_pipeline_perf
    main.cpp
)

target_include_directories(winelement_render_pipeline_perf
    PRIVATE
        ${PROJECT_SOURCE_DIR}/include
        ${PROJECT_SOURCE_DIR}/src/platform/win32
)

target_link_libraries(winelement_render_pipeline_perf
    PRIVATE
        WinElement::platform
        fmt::fmt
        ole32
        user32
        windowscodecs
)

set_target_properties(winelement_render_pipeline_perf
    PROPERTIES
        OUTPUT_NAME render_pipeline_perf
)

winelement_configure_executable(winelement_render_pipeline_perf)