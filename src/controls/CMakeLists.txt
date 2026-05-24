add_library(winelement_controls STATIC
    border.cpp
    button.cpp
    control.cpp
    context_menu.cpp
    feedback.cpp
    image.cpp
    input.cpp
    items_control.cpp
    panel.cpp
    path.cpp
    popup_menu_surface.cpp
    radio.cpp
    scrollbar.cpp
    select.cpp
    stack_panel.cpp
    switch.cpp
    text.cpp
)

add_library(WinElement::controls ALIAS winelement_controls)

target_include_directories(winelement_controls
    PUBLIC
        $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

target_link_libraries(winelement_controls
    PUBLIC
        WinElement::animation
        WinElement::elements
        WinElement::rendering
        WinElement::style
)

winelement_configure_library(winelement_controls)
