# vcpkg Packaging

This project is prepared for two vcpkg workflows:

- Manifest mode for local development.
- A port template under `packaging/vcpkg/winelement` for registry publication.

The vcpkg documentation describes manifest mode as the recommended workflow for
most projects, with dependencies declared in `vcpkg.json`. It also requires
ports to contain both package metadata and build/install instructions through a
`vcpkg.json` plus `portfile.cmake`.

References:

- https://learn.microsoft.com/en-us/vcpkg/concepts/manifest-mode
- https://learn.microsoft.com/en-us/vcpkg/concepts/ports
- https://learn.microsoft.com/en-us/vcpkg/reference/vcpkg-json
- https://learn.microsoft.com/en-us/vcpkg/users/versioning

## Local Development

Install the normal library dependency:

```powershell
vcpkg install
```

Install dependencies for tests and samples:

```powershell
vcpkg install --x-feature=tests --x-feature=samples
```

Then configure with the vcpkg toolchain:

```powershell
cmake --preset vs2022-x64
cmake --build build\vs2022-x64 --config Release
```

## Installed CMake Package

WinElement now installs:

- Static libraries.
- Public headers.
- `WinElementTargets.cmake`.
- `WinElementConfig.cmake`.
- `WinElementConfigVersion.cmake`.

Consumer usage:

```cmake
find_package(WinElement CONFIG REQUIRED)
target_link_libraries(app PRIVATE WinElement::winelement)
```

Fine-grained usage:

```cmake
target_link_libraries(app PRIVATE WinElement::controls WinElement::platform)
```

## Testing The Install Locally

```powershell
cmake --build build\vs2022-x64 --config Release
cmake --install build\vs2022-x64 --config Release --prefix build\install\winelement
```

Then point a consumer project at:

```powershell
-DCMAKE_PREFIX_PATH=E:\users\lanxf01\Desktop\cpplib\WinElement\build\install\winelement
```

## Registry Port Template

The template lives here:

```text
packaging/vcpkg/winelement/
  vcpkg.json
  portfile.cmake
```

Before submitting to the official vcpkg registry:

1. Replace `REPO lanxf01/WinElement` in `portfile.cmake` with the actual GitHub repository.
2. Create a release tag matching the package version, for example `v0.1.0`.
3. Replace `SHA512 0` with the real archive hash reported by vcpkg.
4. Add a repository `LICENSE` file or decide the correct SPDX license expression.
5. If only vcpkg packaging changes, increment `port-version`; if upstream code changes, update `version-semver` and reset `port-version`.
6. Run vcpkg's port validation for the target triplets you intend to support.

## Current Package Boundaries

The package is Windows-only:

```json
"supports": "windows"
```

Only `yoga` is required for the library package. `fmt` and `gtest` are feature
dependencies for samples and tests, not runtime dependencies for consumers.

## License Note

The current repository does not include a license file. The manifests therefore
use `license: null`. This is valid vcpkg metadata, but official publication
still requires a clear copyright file in the installed package. Choose and add a
real license before public redistribution.
