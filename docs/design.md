# Design

WinElement is designed around a retained UI tree with explicit invalidation and
platform-neutral render command recording.

## Design Goals

- Smooth resizing and scrolling.
- Low idle CPU usage.
- Memory usage that scales with visible UI and bounded caches.
- Controls that feel close to Element Plus while staying native to C++.
- Testable rendering and layout without requiring a live window.

## Retained Tree

The UI tree is retained by `UIElement`. Elements keep layout, style, input, and
render cache metadata. Dirty flags decide which work needs to run on the next
frame.

This avoids rebuilding the full tree for every frame and lets clean subtrees
reuse command caches.

## Invalidation Model

Invalidation is explicit:

- Layout invalidation schedules measurement and layout.
- Paint invalidation refreshes render commands.
- Style invalidation detaches or reapplies theme-managed style.
- Semantics invalidation marks accessibility snapshots dirty.

Property metadata declares invalidation intent, so custom properties can
participate in the same pipeline.

## Styling

Styles are value objects. Controls read style tokens during paint and state
transitions. Themes map string class names to `UIElementStyle` values.

The theme system intentionally keeps inheritance simple. Local themes cascade
through the element tree, while individual style classes remain explicit and
cacheable.

## Rendering

Rendering is split into two steps:

1. Record a platform-neutral `RenderCommandList` or `RenderScene`.
2. Submit it to the platform renderer.

The D3D11 backend can reuse prepared resources, text glyphs, geometries, and
retained layer metadata. The render frame graph groups commands into passes for
better scheduling and diagnostics.

## Controls

Controls should avoid owning platform details. They render through
`RenderContext`, expose stateful C++ methods, and use the base element for
layout, focus, pointer, theme, and paint invalidation.

Complex controls should split reusable policy out of rendering code. For
example, virtualization planning is separate from `ItemsControl` container
creation.

## Public API Stability

Public headers should be small, direct, and useful. Unused experimental helpers
should not be exported through umbrella headers. If an API is not used by the
framework or samples, it needs either tests and documentation or removal.

## Packaging

The installed CMake package exports `WinElement::` targets and a
`WinElementConfig.cmake` file. Applications should use `find_package(WinElement
CONFIG REQUIRED)` instead of relying on source-tree paths.
