# Architecture

WinElement is split into layers. Higher layers depend on lower layers, and
lower layers must not depend on controls or platform windowing.

## Layer Graph

```text
platform
  controls
    elements
      style
        animation
        layout
        rendering
          core
```

The top-level `WinElement::winelement` target links the full stack for
applications that do not need fine-grained linking.

## Core

The core layer contains generic primitives:

- Geometry and color value types.
- Frame scheduling.
- Small local caches.
- Typed property storage.

No UI tree knowledge should be added to core.

## Layout

The layout layer wraps Yoga and provides WinElement-friendly layout types. It is
responsible for measurement, flex layout, grid planning, scroll bounds, and
layout cache helpers.

The UI tree owns `LayoutElement` attachment. Controls should modify layout
through `configure_layout()` instead of reaching around ownership boundaries.

## Rendering

The rendering layer is platform-neutral. It records draw commands, scenes,
frame graph metadata, text layout commands, image resource references, and
compositor promotion intent.

Platform renderers consume these command streams. This separation keeps tests
fast and lets headless verification run without opening a window.

## Animation

Animation owns timeline sampling, keyframes, transitions, and simple physics
simulation. It writes values through callbacks or through typed properties.

Animations do not directly mutate platform resources. The element layer applies
invalidations after animation writes.

## Style

The style layer provides `UIElementStyle`, semantic tokens, built-in Element
Plus inspired styles, theme classes, dark theme variants, and computed style
caching.

Themes carry a generation number. Mutating a style class advances the
generation only when the value actually changed, which prevents stale cache
reuse without causing unnecessary invalidation.

## Elements

`UIElement` is the retained tree primitive. It owns:

- Parent and child relationships.
- Layout tree binding.
- Theme class and local theme state.
- Paint and layout invalidation.
- Hit testing and routed input.
- Top layer ownership.
- Render command cache.
- Property store.

Managers such as `EventRouter`, `FocusManager`, `PopupManager`, and
`ThemeManager` operate on `UIElement` but keep their own policy logic out of
controls.

## Controls

Controls are focused wrappers over `UIElement`. A control should:

- Expose a useful stateful API.
- Use style tokens instead of hard-coded drawing rules where possible.
- Reuse layout, input, theme, and rendering primitives from lower layers.
- Avoid storing duplicate state already represented by `UIElement`.

`ItemsControl` is the primary virtualization boundary. It realizes only the
visible window plus overscan and recycles containers through a bounded pool.

## Platform

The platform layer owns Win32 windows, dispatch, text services, resource
loading, render threading, DirectComposition, and the D3D11 renderer.

This layer may depend on the full UI stack, but cross-platform abstractions
should stay in lower layers.
