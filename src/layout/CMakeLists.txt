add_library(winelement_layout STATIC
    grid_layout.cpp
    layout_cache.cpp
    layout_engine.cpp
    layout_element.cpp
    pixel_snap.cpp
    yoga_conversions.cpp
)

add_library(WinElement::layout ALIAS winelement_layout)

target_include_directories(winelement_layout
    PUBLIC
        $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(winelement_layout
    PUBLIC
        WinElement::core
    PRIVATE
        yoga::yogacore
)

winelement_configure_library(winelement_layout)
