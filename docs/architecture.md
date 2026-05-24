# 架构

WinElement 按层划分。上层依赖下层，下层不得依赖控件或平台窗口。

## 层依赖图

```text
winelement aggregate
  platform ───────────────┐
  controls ───────────┐   │
  elements ───────┐   │   │
  style ───────┐  │   │   │
  animation ─┐ │  │   │   │
  layout ────┼─┼──┘   │   │
  rendering ─┘ │      │   │
  core <───────┴──────┴───┘
```

顶层 `WinElement::winelement` 目标链接了整个堆栈，适用于不需要精细链接的应用。

## Core（核心层）

核心层包含通用原语：

- 几何体和颜色值类型。
- 帧调度。
- 小型本地缓存。
- 类型化属性存储。

不应向核心层添加 UI 树相关的知识。

## Layout（布局层）

布局层封装了 Yoga，并提供对 WinElement 友好的布局类型。负责测量、flex 布局、网格规划、滚动边界和布局缓存辅助。

UI 树拥有 `LayoutElement` 的挂载关系。控件应通过 `configure_layout()` 修改布局，而不应绕过所有权边界。

## Rendering（渲染层）

渲染层与平台无关。它记录绘制命令、场景、帧图元数据、文本布局命令、图像资源引用和合成器提升意图。

平台渲染器消费这些命令流。这种分离使测试保持快速，并允许无头验证无需打开窗口即可运行。

## Animation（动画层）

动画层负责时间轴采样、关键帧、过渡和简单物理模拟。通过回调或类型化属性写入值。

动画不会直接修改平台资源。元素层在动画写入后应用失效标记。

## Style（样式层）

样式层提供 `UIElementStyle`、语义令牌、内置的 Element Plus 风格样式、主题类、深色主题变体和计算样式缓存。

主题携带 generation 编号。只有在值实际发生变化时，样式类变更才会推进 generation，这防止了过期缓存被复用，同时避免了不必要的失效。

## Elements（元素层）

`UIElement` 是保留式树结构原语。它管理：

- 父子关系。
- 布局树绑定。
- 主题类和本地主题状态。
- 绘制和布局失效。
- 命中测试和路由输入。
- 顶层图层所有权。
- 渲染命令缓存。
- 属性存储。

`EventRouter`、`FocusManager`、`PopupManager` 和 `ThemeManager` 等管理器操作 `UIElement`，但将各自的策略逻辑保留在控件之外。

## Controls（控件层）

控件是 `UIElement` 的专注封装。一个控件应：

- 暴露有用的有状态 API。
- 尽可能使用样式令牌而非硬编码绘制规则。
- 复用来自下层的布局、输入、主题和渲染原语。
- 避免存储 `UIElement` 已经表示的重复状态。

长内容虚拟化应优先使用 `UIElement::set_virtual_children(...)` 或
`set_vertical_virtual_children(...)`。`ItemsControl` 只保留选择、分组和容器复用等控件语义；
不再作为框架唯一虚拟化入口，避免控件层和元素树层出现两套重复机制。

## Platform（平台层）

平台层管理 Win32 窗口、分发、文本服务、资源加载、渲染线程、DirectComposition 和 D3D11 渲染器。

该层可能依赖完整的 UI 堆栈，但跨平台抽象应保留在下层。
