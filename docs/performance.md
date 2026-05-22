# Performance Notes

WinElement prioritizes smooth interaction first, then reduces memory by keeping
steady-state caches bounded and local.

## UI Virtualization

`ItemsControl` uses a virtualization planner to compute the visible item range
plus overscan. It realizes only that range and reuses containers through a
bounded recycle pool.

This keeps large lists from creating a render cache and element subtree for
every logical item.

## Render Command Reuse

`UIElement` stores render command caches on render state. Clean subtrees can
reuse cached content and overlay commands when layout generation and paint dirty
state match.

Scrollable viewports skip offscreen child command recording, so scrolling a
large page does not require repainting every descendant.

## Dirty Region Rendering

Paint invalidation can collect dirty regions. Repaint boundaries allow a subtree
to localize repaint work when the rest of the UI is clean.

## Resource Cache Discipline

The Win32/D3D11 renderer caches expensive platform resources such as prepared
text, image resources, geometries, and glyph atlas entries. Cache entries are
bounded or aged so memory can return toward a steady state after interaction.

## Property Storage

Custom properties are stored in a sorted contiguous vector. Most elements have
zero or very few custom properties, so this avoids the bucket and node overhead
of `unordered_map` while keeping lookup costs small and cache-friendly.

No-op property writes do not notify observers or invalidate the UI.

## Small LRU Caches

`core::LruCache` is optimized for small capacities. It now uses contiguous
storage instead of `list + unordered_map`, reducing allocations and memory
fragmentation for layout and style caches.

This is appropriate for caches such as measure results and computed styles,
where capacities are intentionally small and locality is more valuable than
large hash-table throughput.

## Theme Cache Correctness

Themes have generation tokens. Style class mutation advances the generation
only when the stored style actually changes. This avoids stale computed style
cache hits without forcing cache churn on equivalent writes.

## Measuring Memory

On Windows, compare these process counters:

- Working set: physical pages currently resident for the process.
- Private bytes: committed memory private to the process.

Working set can stay high because Windows keeps recently used pages resident.
Private bytes is usually the better counter for framework-owned memory growth.

Recommended checks:

```powershell
build\vs2022-x64\samples\controls_showcase\Release\controls_showcase.exe --headless
build\vs2022-x64\samples\controls_showcase\Release\controls_showcase.exe --profile-memory
```

Use the headless path for quick regression checks and `--profile-memory` when
you need live process counters while scrolling a maximized showcase window.
