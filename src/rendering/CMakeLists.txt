add_library(winelement_rendering STATIC
    compositor.cpp
    render_command_list.cpp
    render_context.cpp
    render_frame_graph.cpp
    render_resource_queue.cpp
    render_scene.cpp
    svg_path_parser.cpp
    text_engine.cpp
)

add_library(WinElement::rendering ALIAS winelement_rendering)

target_include_directories(winelement_rendering
    PUBLIC
        ${PROJECT_SOURCE_DIR}/include
)

target_link_libraries(winelement_rendering
    PUBLIC
        WinElement::core
    PRIVATE
        dwrite
)

winelement_configure_library(winelement_rendering)

