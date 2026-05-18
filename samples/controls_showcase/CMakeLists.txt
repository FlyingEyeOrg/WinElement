add_executable(winelement_controls_showcase
    main.cpp
)

target_link_libraries(winelement_controls_showcase
    PRIVATE
        WinElement::controls
        WinElement::rendering
)

set_target_properties(winelement_controls_showcase
    PROPERTIES
        OUTPUT_NAME controls_showcase
)

winelement_configure_executable(winelement_controls_showcase)
