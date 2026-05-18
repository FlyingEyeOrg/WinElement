add_library(winelement_core STATIC
    frame_scheduler.cpp
    core.cpp
    property.cpp
)

add_library(WinElement::core ALIAS winelement_core)

target_include_directories(winelement_core
    PUBLIC
        ${PROJECT_SOURCE_DIR}/include
)

winelement_configure_library(winelement_core)
