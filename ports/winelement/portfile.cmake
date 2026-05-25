vcpkg_from_github(
    OUT_SOURCE_PATH SOURCE_PATH
    REPO FlyingEyeOrg/WinElement
    REF "v${VERSION}"
    SHA512 fd3e278bec455f0e0762c37d920dd3ef37ff1c921b2568f215f65e620718d8a0661ed7ed250400cf180ccd0586f04603967b7a8f6b4552496a2ce292edb1c43a
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
