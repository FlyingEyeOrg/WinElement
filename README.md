# WinElement

WinElement is a Windows-first C++20 UI framework focused on retained rendering,
predictable layout, reusable controls, and low steady-state CPU usage.

The project currently targets Win32/D3D11 and is organized as small static
libraries so applications can use only the layers they need:

- `WinElement::core`
- `WinElement::layout`
- `WinElement::rendering`
- `WinElement::animation`
- `WinElement::style`
- `WinElement::elements`
- `WinElement::controls`
- `WinElement::platform`
- `WinElement::winelement`

## Build

```powershell
cmake --preset vs2022-x64
cmake --build build\vs2022-x64 --config Release
```

To build tests and samples:

```powershell
cmake --preset vs2022-x64 -DWINELEMENT_BUILD_TESTS=ON -DWINELEMENT_BUILD_SAMPLES=ON
cmake --build build\vs2022-x64 --config Release
ctest --test-dir build\vs2022-x64 -C Release --output-on-failure
```

The root `vcpkg.json` keeps normal library dependencies minimal. Optional
features install dependencies for tests and samples:

```powershell
vcpkg install --x-feature=tests --x-feature=samples
```

## Use From CMake

After installation:

```cmake
find_package(WinElement CONFIG REQUIRED)
target_link_libraries(my_app PRIVATE WinElement::winelement)
```

## Documentation

- [API overview](docs/api.md)
- [Architecture](docs/architecture.md)
- [Design](docs/design.md)
- [Performance notes](docs/performance.md)
- [vcpkg packaging](docs/vcpkg.md)

## Showcase

```powershell
cmake --build build\vs2022-x64\samples\controls_showcase --config Release --target winelement_controls_showcase
build\vs2022-x64\samples\controls_showcase\Release\controls_showcase.exe --headless
```

Use the normal executable without `--headless` to open the interactive showcase.
