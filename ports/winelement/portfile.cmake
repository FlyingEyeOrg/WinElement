vcpkg_from_github(
    OUT_SOURCE_PATH SOURCE_PATH
    REPO FlyingEyeOrg/WinElement
    REF "v${VERSION}"
    SHA512 51fd3f344c4bf4c96f8dcf13f6b55e2e320e0a61088073200bf1c1cd5dde6872f22b381d40db5d3c6118fd3c29f9508e955a6e6f352c7c3267f507c7a584b899
    HEAD_REF main
)

vcpkg_cmake_configure(
    SOURCE_PATH "${SOURCE_PATH}"
    OPTIONS
        -DWINELEMENT_BUILD_SAMPLES=OFF
        -DWINELEMENT_BUILD_TESTS=OFF
        -DWINELEMENT_WARNINGS_AS_ERRORS=OFF
)

vcpkg_cmake_install()
vcpkg_cmake_config_fixup(CONFIG_PATH lib/cmake/WinElement)

file(REMOVE_RECURSE "${CURRENT_PACKAGES_DIR}/debug/include")

if(EXISTS "${SOURCE_PATH}/LICENSE")
    vcpkg_install_copyright(FILE_LIST "${SOURCE_PATH}/LICENSE")
else()
    file(
        WRITE "${CURRENT_PACKAGES_DIR}/share/${PORT}/copyright"
        "WinElement does not currently publish a repository LICENSE file. Verify licensing with the upstream project before redistribution.\n"
    )
endif()
