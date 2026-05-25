# API 概述

WinElement 提供分层的 CMake 目标和对应的聚合头文件。

## Core（核心）

头文件：`#include <winelement/core.hpp>`

关键 API：

- `core::FrameScheduler`：带合并功能的优先级帧任务队列。
- `core::Property<T>` 和 `core::PropertyStore`：用于 UI 状态和隐式动画的类型化自定义属性。
- `core::EventHandler<Args...>`：线程安全的轻量事件列表，用于控件事件和平台回调。
- `core::ObservableObject`、`core::ObservableList<T>`：带线程安全观察者列表的简单绑定模型。
- `core::LruCache<Key, Value>`：用于布局和样式子系统的小容量缓存。
- 几何体原语：`core::Point`、`core::Size`、`core::Rect`、`core::Color`、`core::Transform2D`。

类型化属性示例：

```cpp
const winelement::core::Property<float>& progress_property() {
    static const auto property = winelement::core::make_property<float>(
        "demo.progress", winelement::core::PropertyInvalidation::Paint);
    return property;
}

element.set_property(progress_property(), 0.0F);
element.animate_property(progress_property(), 1.0F);
auto progress = element.properties().value(progress_property(), 0.0F);
auto maybe_progress = element.properties().try_value(progress_property());
auto fallback_progress = element.properties().value_or(progress_property(), 0.0F);
```

## Layout（布局）

头文件：`#include <winelement/layout.hpp>`

关键 API：

- `layout::LayoutEngine`
- `layout::LayoutElement`
- `layout::Length`、`layout::Edge`、`layout::FlexDirection`、`layout::Overflow`
- `layout::GridLayoutPlanner`

大多数控件提供 `configure_layout()`，使调用者无需直接管理布局树所有权即可使用 `LayoutElement`。

## Rendering（渲染）

头文件：`#include <winelement/rendering.hpp>`

关键 API：

- `rendering::RenderCommandList`
- `rendering::RenderScene`
- `rendering::RenderContext`
- `rendering::TextEngine`
- `rendering::RenderFrameGraph`
- `rendering::CompositorPromotionPlan`

渲染代码首先记录命令流，然后由平台后端将其转换为 D3D11 工作负载。

## Animation（动画）

头文件：`#include <winelement/animation.hpp>`

关键 API：

- `animation::Timeline`
- `animation::KeyframeTrack<T>`
- `animation::Storyboard`
- `animation::make_transition_timing`
- `animation::SpringSimulation`
- `animation::FrictionSimulation`

## Style（样式）

头文件：`#include <winelement/style.hpp>`

关键 API：

- `style::UIElementStyle`
- `style::Theme`
- `style::set_theme`、`style::make_default_theme`、`style::make_dark_theme`
- `style::set_theme_style_class`
- `style::ComputedStyleCache`

主题通过 `Theme::generation` 进行版本管理，样式类变更仅在值实际发生变化时才推进 generation，从而防止缓存失效的同时避免不必要的重计算。

## Elements（元素）

头文件：`#include <winelement/elements.hpp>`

关键 API：

- `elements::UIElement`
- `elements::ThemeManager`
- `elements::EventRouter`
- `elements::FocusManager`
- `elements::PopupManager`
- `elements::PlacementEngine`

`UIElement` 管理树结构、布局挂载、样式应用、命中测试、命令缓存失效、顶层图层集成和文本编辑钩子。

常用的布局、可见性、启用状态、滚动和虚拟子节点 API 可以直接链式调用：

```cpp
content.set_layout_size(320.0F, 240.0F)
    .set_padding(12.0F)
    .set_min_size(120.0F, 40.0F)
    .enable_subtree_virtualization();

content.set_vertical_virtual_children(10000U, 28.0F, make_row, 480.0F);
content.scroll_to_bottom();
auto metrics = content.virtualization_metrics();
```

## Controls（控件）

头文件：`#include <winelement/controls.hpp>`

重要控件：

- `controls::Panel`
- `controls::StackPanel`
- `controls::Text`
- `controls::Button`
- `controls::Input`
- `controls::Select`
- `controls::ItemsControl`
- `controls::Image`
- `controls::Scrollbar`
- `controls::Dialog`、`Message`、`MessageBox`、`Loading`
- `controls::FileDialog`

控件是普通的 `UIElement` 子类。它们应暴露领域相关的状态，同时将通用的样式、布局、输入和渲染行为委托给元素层处理。

`FileDialog` 提供静态便利方法封装原生对话框，支持文件打开、保存和文件夹选取：

```cpp
const auto result = controls::FileDialog::open(
    controls::FileDialogOptions{
        .title = "Open file",
        .filters = {controls::FileDialogFilter{.name = "Images",
                                               .pattern = "*.png;*.jpg"}},
        .owner = host_window});

if (result.accepted) {
    // result.paths, result.selected_filter_index
}
```

其他模式：`FileDialog::save()`、`FileDialog::pick_folder()`、`FileDialog::show(mode, ...)`。

## Platform（平台）

头文件：`#include <winelement/platform.hpp>`

关键 API：

- `platform::Application`
- `platform::Window`
- `platform::Dispatcher`
- `platform::ImageLoader`
- `platform::RenderThreadPool`
- `platform::FileDialog`

平台层目前仅支持 Windows，使用 Win32、D3D11、DirectWrite、DirectComposition、WIC 和 IMM。

`platform::FileDialog` 是原生文件对话框的底层封装，`controls::FileDialog` 在其上提供控件层便利接口：

```cpp
const auto result = platform::FileDialog::open(
    platform::FileDialogOptions{
        .title = L"Choose file",
        .filters = {{L"All files", L"*.*"}},
        .owner = window_ptr});
```

`WindowOptions` 可以在构造窗口时集中配置 Win32 创建参数、消息拦截和关闭回调：

```cpp
platform::Window window(platform::WindowOptions{
    .title = L"Custom",
    .width = 1280,
    .height = 720,
    .on_before_create =
        [](platform::WindowCreateParams& params) {
            params.style &= ~WS_MAXIMIZEBOX;
        },
    .on_message =
        [](platform::WindowMessage& message) {
            if (message.id == WM_APP + 1U) {
                message.handled = true;
                message.result = 1;
            }
        },
    .on_closed = [] {}});
```

运行期仍可通过 `add_window_message_filter()`、`add_post_window_message_filter()`、
`window_message_observers()` 和 `closed_event()` 追加或移除扩展点。
