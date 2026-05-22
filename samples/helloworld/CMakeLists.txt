cmake_minimum_required(VERSION 3.21)
project(winelement-helloworld LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(WinElement CONFIG REQUIRED)

add_executable(helloworld main.cpp)
target_link_libraries(helloworld PRIVATE WinElement::winelement)
