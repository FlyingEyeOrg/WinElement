vcpkg_from_github(
    OUT_SOURCE_PATH SOURCE_PATH
    REPO FlyingEyeOrg/WinElement
    REF "v${VERSION}"
    SHA512 cf19d640a55e2f29f285ebe3b2740bd7252c5ded126f23ddf680c773126424678706d54fb4376120b11f93e9173e906a303a9a401d12807a36490e1f86df8901
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
