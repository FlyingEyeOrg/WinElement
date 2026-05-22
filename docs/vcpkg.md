# vcpkg 打包

本项目支持两种 vcpkg 工作流：

- **清单模式**：用于本地开发，依赖项在 `vcpkg.json` 中声明。
- **自有 vcpkg 注册表**：推送版本标签（如 `v0.1.0`）时，GitHub Actions 自动发布到 `vcpkg-registry` 分支。

端口模板位于 `packaging/vcpkg/winelement`，如需提交到官方注册表可基于此模板修改。

参考文档：

- https://learn.microsoft.com/zh-cn/vcpkg/concepts/manifest-mode
- https://learn.microsoft.com/zh-cn/vcpkg/concepts/ports
- https://learn.microsoft.com/zh-cn/vcpkg/reference/vcpkg-json
- https://learn.microsoft.com/zh-cn/vcpkg/users/versioning

## 本地开发

安装依赖并配置 CMake：

```powershell
cmake --preset vs2022-x64
```

构建：

```powershell
cmake --build --preset vs2022-x64-release
```

CMake 预设会自动调用 vcpkg 安装 `vcpkg.json` 中声明的依赖。

## 消费者使用已发布的包

使用此注册表时，需要在项目中同时配置 `vcpkg-configuration.json` 和 `vcpkg.json`。

`vcpkg-configuration.json`：

```json
{
  "default-registry": {
    "kind": "builtin",
    "baseline": "74e6536215718009aae747d86d84b78376bf9e09"
  },
  "registries": [
    {
      "kind": "git",
      "repository": "https://github.com/FlyingEyeOrg/WinElement",
      "baseline": "8bbfc21b3742cc1f4a8c3cfe2981cc7abcb0738b",
      "reference": "vcpkg-registry",
      "packages": ["winelement"]
    }
  ]
}
```

注意：

- `default-registry` 必须有，用于解析 `winelement` 的传递依赖（如 `yoga`）。
- `baseline` 必须是 40 位 Git commit SHA，不能是分支名。每次发布新版本后需更新为 `vcpkg-registry` 分支的最新 commit。

`vcpkg.json`：

```json
{
  "name": "my-app",
  "version-semver": "1.0.0",
  "dependencies": ["winelement"]
}
```

CMakeLists.txt：

```cmake
cmake_minimum_required(VERSION 3.21)
project(my-app LANGUAGES CXX)

find_package(WinElement CONFIG REQUIRED)
add_executable(my-app main.cpp)
target_link_libraries(my-app PRIVATE WinElement::winelement)
```

也可使用细粒度 target：

```cmake
target_link_libraries(my-app PRIVATE WinElement::core WinElement::controls)
```

CMakePresets.json 中需指定 vcpkg 工具链：

```json
{
  "version": 3,
  "configurePresets": [
    {
      "name": "vs2022-x64",
      "generator": "Visual Studio 17 2022",
      "architecture": "x64",
      "binaryDir": "${sourceDir}/build/vs2022-x64",
      "cacheVariables": {
        "CMAKE_TOOLCHAIN_FILE": "$env{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake"
      }
    }
  ]
}
```

之后正常使用 `cmake --preset vs2022-x64 && cmake --build --preset vs2022-x64-release` 即可。

完整可运行的示例见 `samples/helloworld/`。

## 自有 vcpkg 注册表（自动发布）

推送以 `v` 开头的语义化版本标签（如 `v0.1.0`）时，GitHub Actions 自动：

1. 从 GitHub 归档计算 SHA512 哈希。
2. 更新端口文件中的版本号和哈希。
3. 调用 `vcpkg format-manifest` 确保格式正确。
4. 调用 `vcpkg x-add-version` 将包发布到 `vcpkg-registry` 分支。

工作流文件：`.github/workflows/publish-vcpkg.yml`

## 端口模板（官方注册表）

模板位于：

```text
packaging/vcpkg/winelement/
  vcpkg.json
  portfile.cmake
```

提交到官方 vcpkg 注册表之前：

1. 将 `portfile.cmake` 中的 `REPO FlyingEyeOrg/WinElement` 替换为实际的 GitHub 仓库。
2. 创建与包版本匹配的发布标签，例如 `v0.1.0`。
3. 将 `SHA512` 替换为 vcpkg 报告的实际归档哈希值。
4. 添加仓库的 `LICENSE` 文件或确定正确的 SPDX 许可表达式。
5. 如果仅更改 vcpkg 打包，递增 `port-version`；如果上游代码变更，更新 `version-semver` 并重置 `port-version`。
6. 对目标 triplet 运行 vcpkg 的端口验证。

## 当前包边界

该包仅支持 Windows：

```json
"supports": "windows"
```

核心依赖仅 `yoga`。`fmt` 和 `gtest` 是示例和测试的特性依赖，而非消费者的运行时依赖。

## 许可说明

当前仓库不包含许可文件。因此清单使用 `license: null`。公开发布前请选择并添加实际许可。
