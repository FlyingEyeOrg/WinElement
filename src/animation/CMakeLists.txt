add_library(winelement_animation STATIC
    easing.cpp
    physics.cpp
    timeline.cpp
)

add_library(WinElement::animation ALIAS winelement_animation)

target_include_directories(winelement_animation
    PUBLIC
        $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

target_link_libraries(winelement_animation
    PUBLIC
    WinElement::core
)

winelement_configure_library(winelement_animation)
