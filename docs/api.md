# API Overview

WinElement exposes layered CMake targets and matching umbrella headers.

## Core

Header: `#include <winelement/core.hpp>`

Key APIs:

- `core::FrameScheduler`: priority frame task queue with coalescing.
- `core::Property<T>` and `core::PropertyStore`: typed custom properties for UI state and implicit animations.
- `core::LruCache<Key, Value>`: small-capacity cache used by layout and style subsystems.
- Geometry primitives: `core::Point`, `core::Size`, `core::Rect`, `core::Color`, `core::Transform2D`.

Typed property example:

```cpp
const winelement::core::Property<float>& progress_property() {
    static const auto property = winelement::core::make_property<float>(
        "demo.progress", winelement::core::PropertyInvalidation::Paint);
    return property;
}

element.set_property(progress_property(), 0.0F);
element.animate_property(progress_property(), 1.0F);
auto progress = element.properties().value(progress_property(), 0.0F);
```

## Layout

Header: `#include <winelement/layout.hpp>`

Key APIs:

- `layout::LayoutEngine`
- `layout::LayoutElement`
- `layout::Length`, `layout::Edge`, `layout::FlexDirection`, `layout::Overflow`
- `layout::GridLayoutPlanner`

Most controls expose `configure_layout()` so callers can work with
`LayoutElement` without directly managing layout tree ownership.

## Rendering

Header: `#include <winelement/rendering.hpp>`

Key APIs:

- `rendering::RenderCommandList`
- `rendering::RenderScene`
- `rendering::RenderContext`
- `rendering::TextEngine`
- `rendering::RenderFrameGraph`
- `rendering::CompositorPromotionPlan`

Rendering code records command streams first, then platform backends translate
them into D3D11 work.

## Animation

Header: `#include <winelement/animation.hpp>`

Key APIs:

- `animation::Timeline`
- `animation::KeyframeTrack<T>`
- `animation::Storyboard`
- `animation::make_transition_timing`
- `animation::SpringSimulation`
- `animation::FrictionSimulation`

## Style

Header: `#include <winelement/style.hpp>`

Key APIs:

- `style::UIElementStyle`
- `style::Theme`
- `style::set_theme`, `style::make_default_theme`, `style::make_dark_theme`
- `style::set_theme_style_class`
- `style::ComputedStyleCache`

Themes are versioned by `Theme::generation`, so style caches and already-applied
theme metadata can detect real style changes.

## Elements

Header: `#include <winelement/elements.hpp>`

Key APIs:

- `elements::UIElement`
- `elements::ThemeManager`
- `elements::EventRouter`
- `elements::FocusManager`
- `elements::PopupManager`
- `elements::PlacementEngine`

`UIElement` owns tree structure, layout attachment, style application, hit
testing, command cache invalidation, top layer integration, and text editing
hooks.

## Controls

Header: `#include <winelement/controls.hpp>`

Important controls:

- `controls::Panel`
- `controls::StackPanel`
- `controls::Text`
- `controls::Button`
- `controls::Input`
- `controls::Select`
- `controls::ItemsControl`
- `controls::Image`
- `controls::Scrollbar`
- `controls::Dialog`, `Message`, `MessageBox`, `Loading`

Controls are normal `UIElement` subclasses. They should expose domain-specific
state while delegating common style, layout, input, and rendering behavior to
the element layer.

## Platform

Header: `#include <winelement/platform.hpp>`

Key APIs:

- `platform::Application`
- `platform::Window`
- `platform::Dispatcher`
- `platform::ImageLoader`
- `platform::RenderThreadPool`

The platform layer is currently Windows-only and uses Win32, D3D11, DirectWrite,
DirectComposition, WIC, and IMM.
