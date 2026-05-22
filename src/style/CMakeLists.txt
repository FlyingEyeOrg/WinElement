add_library(winelement_style STATIC
    computed_style.cpp
    ui_element_style.cpp
)

add_library(WinElement::style ALIAS winelement_style)

target_include_directories(winelement_style
    PUBLIC
        $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

target_link_libraries(winelement_style
    PUBLIC
        WinElement::animation
        WinElement::layout
        WinElement::rendering
)

winelement_configure_library(winelement_style)
