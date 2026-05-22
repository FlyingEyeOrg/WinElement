cmake_minimum_required(VERSION 3.21)
project(winelement-helloworld LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(PROJECT_IS_TOP_LEVEL)
    find_package(WinElement CONFIG REQUIRED)
endif()

add_executable(winelement_helloworld main.cpp)

if(TARGET WinElement::winelement)
    target_link_libraries(winelement_helloworld PRIVATE WinElement::winelement)
else()
    target_link_libraries(winelement_helloworld
        PRIVATE
            WinElement::controls
            WinElement::layout
            WinElement::platform
    )
    winelement_configure_executable(winelement_helloworld)
endif()

set_target_properties(winelement_helloworld
    PROPERTIES
        OUTPUT_NAME helloworld
)
