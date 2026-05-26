# 02 — 三部曲：Device / Context / SwapChain

D3D11 编程的第一步，永远是创建这三个对象。理解了它们，就理解了 D3D11 的一半。

## 三者关系

```
你的程序
    │
    ▼
ID3D11Device  ←───────────── 工厂，只负责创建资源
    │
    ├── ID3D11Buffer         (顶点缓冲)
    ├── ID3D11VertexShader   (顶点着色器)
    ├── ID3D11PixelShader    (像素着色器)
    ├── ID3D11BlendState     (混合状态)
    ├── ID3D11Texture2D      (纹理)
    └── ... 所有 ID3D11* 对象
    │
    ▼
ID3D11DeviceContext  ←────── 命令队列，只负责执行
    │
    ├── IASetVertexBuffers   (绑定顶点数据)
    ├── VSSetShader          (绑定顶点着色器)
    ├── PSSetShader          (绑定像素着色器)
    ├── OMSetBlendState      (设置混合模式)
    ├── Draw                 (画！)
    └── ... 所有执行命令
    │
    ▼
IDXGISwapChain  ←─────────── 显示链，管理前后缓冲
    │
    ├── Present              (把画好的帧显示到屏幕)
    └── ResizeBuffers        (窗口大小变了重建缓冲)
```

## Device（设备）

```cpp
ID3D11Device* device = nullptr;
D3D11CreateDevice(
    nullptr,                              // 用默认显卡
    D3D_DRIVER_TYPE_HARDWARE,             // 用硬件加速
    nullptr,                              // 不用软件驱动
    0,                                    // 创建标志（Debug 模式可加 D3D11_CREATE_DEVICE_DEBUG）
    feature_levels, sizeof(feature_levels),
    D3D11_SDK_VERSION,
    &device,
    nullptr,                              // 实际使用的 feature level
    &context                              // 同时拿到 context
);
```

Device 创建的瞬间，D3D11 就在背后加载了显卡驱动、分配了驱动上下文。之后所有 `Create*` 调用（CreateBuffer、CreateVertexShader、CreateBlendState）都是在 GPU 显存或驱动内存中分配资源。

**在 WinElement 中**：创建在 `d3d11_render_device.cpp`，存储在 `device_` 成员。

## Context（设备上下文 / 命令队列）

Context 是**执行者**。它本身不持有资源数据，它只记录你要做什么：

```cpp
// Context 的典型调用序列
context->IASetVertexBuffers(...);     // 告诉 GPU：接下来用这个顶点数据
context->VSSetShader(vertex_shader);  // 告诉 GPU：接下来用这个顶点着色器
context->PSSetShader(pixel_shader);   // 告诉 GPU：接下来用这个像素着色器
context->Draw(6, 0);                  // 告诉 GPU：画 6 个顶点！
```

每次调用 `Draw` 时，Context 会把当前绑定的所有状态打包成一个"绘制调用"，发送给 GPU。

## SwapChain（交换链）

SwapChain 解决的是**双缓冲**问题：

```
后缓冲（Back Buffer）         前缓冲（Front Buffer）
    你正在画的内容              屏幕上显示的内容
    
    Draw → Draw → Draw          不动
    
    Present() 交换！
    ──────────────────────────►
    
    不动                        新内容显示到屏幕
```

为什么需要双缓冲？如果只有一个缓冲区，你画到一半的时候屏幕刷新了，用户就会看到"上半截是新图，下半截是旧图"的撕裂画面。双缓冲保证用户永远只看到完整的帧。

**`Present(1, 0)` 参数的意义**：
- 第一个参数 `1`：等待垂直同步（VSync）。最多等 1 帧，如果没赶上就等下一帧。设为 0 则不等 VSync，可能画面撕裂但延迟更低
- 第二个参数 `0`：无特殊标志

**在 WinElement 中**：SwapChain 管理在 `d3d11_composition_surface.cpp`，与 DirectComposition 集成（不是普通的 `CreateSwapChainForHwnd`，而是 `CreateSwapChainForComposition`）。

## 创建顺序

```
1. 注册窗口类 → 创建 HWND
2. D3D11CreateDevice → 拿到 device + context
3. 通过 DXGI 创建 SwapChain
4. SwapChain->GetBuffer(0) → 拿到后缓冲纹理
5. CreateRenderTargetView → 创建渲染目标视图
6. 开始渲染循环
```

你的 `on_paint()` 就是在第 6 步里反复执行的。

## WinElement 的对应代码

| 你的对象 | 所在文件 | 变量名 |
|----------|----------|--------|
| Device | `d3d11_render_device.cpp` | `device_` |
| Context | `d3d11_display_list_renderer.hpp` | `primary_deferred_context_` |
| SwapChain | `d3d11_composition_surface.cpp` | `swapchain_`（在 Impl 里） |

## 下一步

有了这三个对象后，我们来看怎么画一个三角形——理解顶点、着色器和 Draw Call 的完整流程。
