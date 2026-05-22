add_executable(winelement_rendering_tests
    rendering/render_context_tests.cpp
)

add_executable(winelement_core_tests
    core/core_tests.cpp
)

add_executable(winelement_animation_tests
    animation/animation_tests.cpp
)

add_executable(winelement_style_tests
    style/style_tests.cpp
)

add_executable(winelement_elements_tests
    elements/ui_element_tests.cpp
)

add_executable(winelement_controls_tests
    controls/basic_controls_tests.cpp
)

add_executable(winelement_platform_tests
    platform/deferred_context_tests.cpp
    platform/window_tests.cpp
)

add_executable(winelement_visual_regression_tests
    platform/visual_regression_tests.cpp
)

target_link_libraries(winelement_rendering_tests
    PRIVATE
        WinElement::rendering
        GTest::gtest_main
)

target_link_libraries(winelement_core_tests
    PRIVATE
        WinElement::core
        GTest::gtest_main
)

target_link_libraries(winelement_animation_tests
    PRIVATE
        WinElement::animation
        GTest::gtest_main
)

target_link_libraries(winelement_style_tests
    PRIVATE
        WinElement::style
        GTest::gtest_main
)

target_link_libraries(winelement_elements_tests
    PRIVATE
        WinElement::elements
        GTest::gtest_main
)

target_link_libraries(winelement_controls_tests
    PRIVATE
        WinElement::controls
        WinElement::style
        GTest::gtest_main
)

target_link_libraries(winelement_platform_tests
    PRIVATE
        WinElement::controls
        WinElement::platform
        WinElement::rendering
        GTest::gtest_main
)

target_link_libraries(winelement_visual_regression_tests
    PRIVATE
        WinElement::platform
        WinElement::rendering
        GTest::gtest_main
    fmt::fmt
        ole32
        user32
        windowscodecs
)

target_include_directories(winelement_platform_tests
    PRIVATE
        ${PROJECT_SOURCE_DIR}/src/platform/win32
)

target_include_directories(winelement_visual_regression_tests
    PRIVATE
        ${PROJECT_SOURCE_DIR}/src/platform/win32
)

target_compile_definitions(winelement_visual_regression_tests
    PRIVATE
        WINELEMENT_SOURCE_DIR="${PROJECT_SOURCE_DIR}"
)

winelement_configure_executable(winelement_rendering_tests)
winelement_configure_executable(winelement_core_tests)
winelement_configure_executable(winelement_animation_tests)
winelement_configure_executable(winelement_style_tests)
winelement_configure_executable(winelement_elements_tests)
winelement_configure_executable(winelement_controls_tests)
winelement_configure_executable(winelement_platform_tests)
winelement_configure_executable(winelement_visual_regression_tests)

include(GoogleTest)
gtest_discover_tests(winelement_rendering_tests)
gtest_discover_tests(winelement_core_tests)
gtest_discover_tests(winelement_animation_tests)
gtest_discover_tests(winelement_style_tests)
gtest_discover_tests(winelement_elements_tests)
gtest_discover_tests(winelement_controls_tests)
gtest_discover_tests(winelement_platform_tests)
gtest_discover_tests(winelement_visual_regression_tests)
