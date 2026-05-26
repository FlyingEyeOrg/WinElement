vcpkg_from_github(
    OUT_SOURCE_PATH SOURCE_PATH
    REPO FlyingEyeOrg/WinElement
    REF "v${VERSION}"
    SHA512 4442c281e8c369eb95aa6a7e10dffcec49d63bdcd1328441ff31c0106ea6bb4fd51f68144be620c29d2600271c547b734dedd4ea73c4ad32b26b6680d3fb24f0
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
