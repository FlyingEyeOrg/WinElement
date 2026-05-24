# 性能说明

WinElement 首先追求流畅的交互，然后通过保持稳态缓存有界且本地化来降低内存。

## UI 虚拟化

UI 虚拟化收敛在 `UIElement` 树层：

- 普通子树离开有效视口后由元素层自动 detach/trim，可恢复子树可通过 materializer 压缩后重建。
- 数据驱动长列表使用 `UIElement::set_virtual_children(...)` 或
  `set_vertical_virtual_children(...)`，只 materialize 可视窗口和 overscan 内的真实子节点。
- `ItemsControl` 保留选择、分组和容器复用语义，但不再是框架级虚拟化的唯一入口。

这使大型列表不会为每个逻辑项目都创建渲染缓存和元素子树，同时滚动范围仍按完整数据集保持稳定。

## 渲染命令复用

`UIElement` 在渲染状态上存储渲染命令缓存。当布局 generation 和绘制脏状态匹配时，干净的子树可以复用缓存的内容和覆盖层命令。

可滚动的视口跳离屏子项的命令录制，因此滚动大页面不需要重绘所有后代。

## 脏区域渲染

绘制失效可以收集脏区域。重绘边界允许子树在 UI 其余部分干净时局部化重绘工作。

## 资源缓存纪律

Win32/D3D11 渲染器缓存昂贵的平台资源，如预渲染文本、图像资源、几何体和字形图集条目。缓存条目有界或老化，使内存在交互后能回归稳态。

## 属性存储

自定义属性存储在排序的连续向量中。大多数元素有零个或极少的自定义属性，这避免了 `unordered_map` 的桶和节点开销，同时保持查找成本小且缓存友好。

无操作的属性写入不会通知观察者或使 UI 失效。

## 小型 LRU 缓存

`core::LruCache` 针对小容量进行了优化。它使用连续存储而非 `list + unordered_map`，减少了布局和样式缓存的分配和内存碎片。

这适用于诸如测量结果和计算样式等缓存，这些场景的容量故意很小，局部性比大哈希表吞吐量更有价值。

## 主题缓存正确性

主题具有 generation 令牌。只有当存储的样式实际发生变化时，样式类变更才会推进 generation。这避免了过期的计算样式缓存命中，同时不会因等效写入而强制缓存抖动。

## 测量内存

在 Windows 上，比较以下进程计数器：

- 工作集（Working set）：进程当前驻留的物理页。
- 私有字节（Private bytes）：进程私有的已提交内存。

工作集可能保持高位，因为 Windows 会保持最近使用的页面驻留。私有字节通常是框架持有内存增长的更好指标。

推荐检查方法：

```powershell
build\vs2022-x64\samples\controls_showcase\Release\controls_showcase.exe --headless
build\vs2022-x64\samples\controls_showcase\Release\controls_showcase.exe --profile-memory
```

使用无头模式进行快速回归检查，在需要滚动最大化展示窗口时获取实时进程计数器时使用 `--profile-memory`。
