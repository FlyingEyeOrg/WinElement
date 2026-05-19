add_library(winelement_elements STATIC
    command_cache.cpp
    element_descriptor.cpp
    event_routing_policy.cpp
    event_router.cpp
    focus_manager.cpp
    placement_engine.cpp
    popup_manager.cpp
    semantics.cpp
    svg_icon.cpp
    text_edit_model.cpp
    theme_manager.cpp
    top_layer_manager.cpp
    ui_element.cpp
)

add_library(WinElement::elements ALIAS winelement_elements)

target_include_directories(winelement_elements
    PUBLIC
        ${PROJECT_SOURCE_DIR}/include
)

target_link_libraries(winelement_elements
    PUBLIC
    WinElement::animation
        WinElement::layout
        WinElement::rendering
        WinElement::style
)

winelement_configure_library(winelement_elements)
