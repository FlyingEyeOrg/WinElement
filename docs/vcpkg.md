# vcpkg 打包

本项目为两种 vcpkg 工作流做好了准备：

- 清单模式：用于本地开发。
- 自有 vcpkg 注册表：推送版本标签（如 `v1.0.0`）时，GitHub Actions 自动发布到 `vcpkg-registry` 分支。
- 端口模板：位于 `packaging/vcpkg/winelement`，如需提交到官方注册表可基于此模板修改。

vcpkg 文档将清单模式描述为大多数项目的推荐工作流，依赖项在 `vcpkg.json` 中声明。同时，端口需要通过 `vcpkg.json` 和 `portfile.cmake` 包含包元数据以及构建/安装说明。

参考文档：

- https://learn.microsoft.com/zh-cn/vcpkg/concepts/manifest-mode
- https://learn.microsoft.com/zh-cn/vcpkg/concepts/ports
- https://learn.microsoft.com/zh-cn/vcpkg/reference/vcpkg-json
- https://learn.microsoft.com/zh-cn/vcpkg/users/versioning

## 本地开发

安装常规库依赖：

```powershell
vcpkg install
```

安装测试和示例的依赖：

```powershell
vcpkg install --x-feature=tests --x-feature=samples
```

然后使用 vcpkg 工具链进行配置：

```powershell
cmake --preset vs2022-x64
cmake --build build\vs2022-x64 --config Release
```

## 已安装的 CMake 包

WinElement 现在安装以下内容：

- 静态库。
- 公共头文件。
- `WinElementTargets.cmake`。
- `WinElementConfig.cmake`。
- `WinElementConfigVersion.cmake`。

消费者使用方式：

```cmake
find_package(WinElement CONFIG REQUIRED)
target_link_libraries(app PRIVATE WinElement::winelement)
```

细粒度使用方式：

```cmake
target_link_libraries(app PRIVATE WinElement::controls WinElement::platform)
```

## 本地测试安装

```powershell
cmake --build build\vs2022-x64 --config Release
cmake --install build\vs2022-x64 --config Release --prefix build\install\winelement
```

然后将消费者项目指向：

```powershell
-DCMAKE_PREFIX_PATH=E:\users\lanxf01\Desktop\cpplib\WinElement\build\install\winelement
```

## 自有 vcpkg 注册表（自动发布）

推送以 `v` 开头的语义化版本标签（如 `v1.0.0`）时，GitHub Actions 自动：

1. 从 GitHub 归档计算 SHA512 哈希。
2. 更新端口文件中的版本号和哈希。
3. 调用 `vcpkg x-add-version` 将包发布到 `vcpkg-registry` 分支。

工作流文件：`.github/workflows/publish-vcpkg.yml`

消费者使用此注册表时，需在项目中添加 `vcpkg-configuration.json`：

```json
{
  "registries": [
    {
      "kind": "git",
      "repository": "https://github.com/lanxf01/WinElement",
      "reference": "vcpkg-registry",
      "packages": ["winelement"]
    }
  ]
}
```

然后在 `vcpkg.json` 中声明依赖：

```json
{
  "dependencies": ["winelement"]
}
```

之后正常使用 `vcpkg install` 即可从本仓库的自有注册表安装 WinElement。

## 端口模板（官方注册表）

模板位于：

```text
packaging/vcpkg/winelement/
  vcpkg.json
  portfile.cmake
```

提交到官方 vcpkg 注册表之前：

1. 将 `portfile.cmake` 中的 `REPO lanxf01/WinElement` 替换为实际的 GitHub 仓库。
2. 创建与包版本匹配的发布标签，例如 `v0.1.0`。
3. 将 `SHA512 0` 替换为 vcpkg 报告的实际归档哈希值。
4. 添加仓库的 `LICENSE` 文件或确定正确的 SPDX 许可表达式。
5. 如果仅更改 vcpkg 打包，递增 `port-version`；如果上游代码变更，更新 `version-semver` 并重置 `port-version`。
6. 对目标 triplet 运行 vcpkg 的端口验证。

## 当前包边界

该包仅支持 Windows：

```json
"supports": "windows"
```

库包仅需要 `yoga`。`fmt` 和 `gtest` 是示例和测试的特性依赖，而非消费者的运行时依赖。

## 许可说明

当前仓库不包含许可文件。因此清单使用 `license: null`。这是有效的 vcpkg 元数据，但官方发布仍然需要在安装包中包含清晰的版权文件。在公开发布之前，请选择并添加实际许可。
