# WinElement

WinElement 是一个面向 Windows 的 C++20 UI 框架，专注于保留式渲染、可预测的布局、可复用的控件和低稳态 CPU 占用。

项目目前面向 Win32/D3D11，组织为多个小型静态库，以便应用只使用所需的层：

- `WinElement::core`
- `WinElement::layout`
- `WinElement::rendering`
- `WinElement::animation`
- `WinElement::style`
- `WinElement::elements`
- `WinElement::controls`
- `WinElement::platform`
- `WinElement::winelement`

## 构建

```powershell
cmake --preset vs2022-x64
cmake --build build\vs2022-x64 --config Release
```

构建测试和示例：

```powershell
cmake --preset vs2022-x64 -DWINELEMENT_BUILD_TESTS=ON -DWINELEMENT_BUILD_SAMPLES=ON
cmake --build build\vs2022-x64 --config Release
ctest --test-dir build\vs2022-x64 -C Release --output-on-failure
```

根目录的 `vcpkg.json` 保持常规库依赖最小化。可选特性会安装测试和示例所需的依赖：

```powershell
vcpkg install --x-feature=tests --x-feature=samples
```

## 从 CMake 使用

安装后：

```cmake
find_package(WinElement CONFIG REQUIRED)
target_link_libraries(my_app PRIVATE WinElement::winelement)
```

## 文档

- [API 概述](docs/api.md)
- [架构](docs/architecture.md)
- [设计](docs/design.md)
- [性能说明](docs/performance.md)
- [vcpkg 打包](docs/vcpkg.md)

## 展示

```powershell
cmake --build build\vs2022-x64\samples\controls_showcase --config Release --target winelement_controls_showcase
build\vs2022-x64\samples\controls_showcase\Release\controls_showcase.exe --headless
```

不使用 `--headless` 运行普通可执行文件可打开交互式展示窗口。
