add_executable(winelement_rendering_tests
    rendering/render_context_tests.cpp
)

add_executable(winelement_platform_tests
    platform/deferred_context_tests.cpp
)

add_executable(winelement_visual_regression_tests
    platform/visual_regression_tests.cpp
)

target_link_libraries(winelement_rendering_tests
    PRIVATE
        WinElement::rendering
        GTest::gtest_main
)

target_link_libraries(winelement_platform_tests
    PRIVATE
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
winelement_configure_executable(winelement_platform_tests)
winelement_configure_executable(winelement_visual_regression_tests)

include(GoogleTest)
gtest_discover_tests(winelement_rendering_tests)
gtest_discover_tests(winelement_platform_tests)
gtest_discover_tests(winelement_visual_regression_tests)