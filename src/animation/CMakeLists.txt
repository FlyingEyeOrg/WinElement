add_library(winelement_animation STATIC
    easing.cpp
    physics.cpp
    timeline.cpp
)

add_library(WinElement::animation ALIAS winelement_animation)

target_include_directories(winelement_animation
    PUBLIC
        ${PROJECT_SOURCE_DIR}/include
)

target_link_libraries(winelement_animation
    PUBLIC
    WinElement::core
)

winelement_configure_library(winelement_animation)
