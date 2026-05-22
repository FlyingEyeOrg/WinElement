add_library(winelement_core STATIC
    binding_expression.cpp
    frame_scheduler.cpp
    observable.cpp
    property.cpp
)

add_library(WinElement::core ALIAS winelement_core)

target_include_directories(winelement_core
    PUBLIC
        $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

winelement_configure_library(winelement_core)
