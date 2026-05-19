add_executable(winelement_dialog_spacing_probe
    main.cpp
)

target_include_directories(winelement_dialog_spacing_probe
    PRIVATE
        ${PROJECT_SOURCE_DIR}/include
        ${PROJECT_SOURCE_DIR}/src/platform/win32
)

target_link_libraries(winelement_dialog_spacing_probe
    PRIVATE
        WinElement::controls
        WinElement::platform
        fmt::fmt
        ole32
        user32
        windowscodecs
)

set_target_properties(winelement_dialog_spacing_probe
    PROPERTIES
        OUTPUT_NAME dialog_spacing_probe
)

winelement_configure_executable(winelement_dialog_spacing_probe)
