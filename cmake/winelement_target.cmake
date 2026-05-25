function(winelement_configure_common target visibility)
    if(NOT TARGET ${target})
        message(FATAL_ERROR "WinElement target '${target}' does not exist")
    endif()

    target_compile_definitions(${target}
        ${visibility}
            WINVER=0x0A00
            _WIN32_WINNT=0x0A00
            UNICODE
            _UNICODE
            NOMINMAX
            WIN32_LEAN_AND_MEAN
    )

    if(MSVC)
        if(NOT DEFINED CMAKE_MSVC_RUNTIME_LIBRARY)
            set_property(TARGET ${target} PROPERTY MSVC_RUNTIME_LIBRARY
                "MultiThreaded$<$<CONFIG:Debug>:Debug>")
        endif()
        target_compile_options(${target} PRIVATE /W4 /permissive- /Zc:__cplusplus)
        if(WINELEMENT_WARNINGS_AS_ERRORS)
            target_compile_options(${target} PRIVATE /WX)
        endif()
    else()
        target_compile_options(${target} PRIVATE
            -Wall
            -Wextra
            -Wpedantic
            -Wconversion
            -Wsign-conversion
            -Wshadow
        )
        if(WINELEMENT_WARNINGS_AS_ERRORS)
            target_compile_options(${target} PRIVATE -Werror)
        endif()
    endif()
endfunction()

function(winelement_configure_library target)
    winelement_configure_common(${target} PUBLIC)
    target_compile_features(${target} PUBLIC cxx_std_20)
endfunction()

function(winelement_configure_executable target)
    winelement_configure_common(${target} PRIVATE)
    target_compile_features(${target} PRIVATE cxx_std_20)
endfunction()
