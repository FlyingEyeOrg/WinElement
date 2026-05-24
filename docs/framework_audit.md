# 框架问题清单与优化记录

本文记录当前 WinElement 框架的结构体检。每一项都给出对应优化动作；低风险、兼容性的改动已在本轮落地，涉及拆分大型模块、重写渲染后端或 breaking API 的项作为后续结构化任务继续推进。

## 本轮已落地

- 修正架构文档中的层依赖图，避免把实际依赖关系描述成线性堆栈。
- 修正性能文档中过时的 `ItemsControl` 虚拟化描述，明确虚拟化唯一入口收敛到 `UIElement` 树层。
- 为 `UIElement` 增加更直接的 `show()`、`hide()`、`enable()`、`disable()`、`set_enabled()`、`set_hit_test_enabled()`、`set_min_size()`。
- 为 `PropertyStore` 增加 `try_value()`、`value_or()`、`contains()`、`empty()`，减少调用方写样板判断。
- 为 `PropertyChange` 增加 `requires_layout()`、`requires_paint()`、`requires_style()`、`requires_semantics()`、`is_inherited()`。
- 将测试里的虚拟子节点样例迁移到更易读的 `set_vertical_virtual_children(...)`。
- 增加 API、属性系统和虚拟化指标相关测试。

## 逐项清单

| # | 分类 | 问题 | 优化动作 |
|---|---|---|---|
| 1 | 代码结构 | `src/elements/ui_element.cpp` 体积过大，树管理、样式、布局、输入、虚拟化、渲染缓存都集中在一个文件。 | 后续拆成 `ui_element_tree.cpp`、`ui_element_layout.cpp`、`ui_element_render.cpp`、`ui_element_virtualization.cpp`；本轮先补 API 测试锁住行为。 |
| 2 | 代码结构 | `include/winelement/elements/ui_element.hpp` public API 过长，调用者很难快速发现常用入口。 | 本轮增加更直觉的别名 API；后续按 Tree/Layout/Style/Input/Virtualization 分区整理注释。 |
| 3 | 代码结构 | `src/controls/input.cpp` 过大，文本模型、指针选择、格式化、布局、绘制耦合。 | 后续抽出 textarea viewport、selection painter、display text mapper；本轮通过属性/API 测试减少改动风险。 |
| 4 | 代码结构 | `src/controls/feedback.cpp` 同时承载 Message、MessageBox、Loading、Dialog。 | 后续按控件拆分源文件，保持 public header 不变。 |
| 5 | 代码结构 | D3D11 renderer 单文件过大，shader、批处理、资源上传、命令执行逻辑交织。 | 后续分离 resource binding、geometry pipeline、text pipeline、debug capture。 |
| 6 | 代码结构 | showcase 文件包含控件演示、诊断、采样和布局 helper，阅读成本高。 | 后续拆成 section builders 和 diagnostics；本轮保留 headless/profile 验证路径。 |
| 7 | 代码结构 | 测试文件 `ui_element_tests.cpp` 和 `basic_controls_tests.cpp` 偏大。 | 后续按 layout/render/event/virtualization/control families 拆分；本轮新增测试仍靠近相关行为。 |
| 8 | 框架分层 | 架构文档原依赖图不准确，容易让贡献者误解模块方向。 | 本轮已修正依赖图。 |
| 9 | 框架分层 | `style` 同时依赖 layout/rendering/animation，边界偏宽。 | 后续把纯 token 与绘制 helper 分层，减少使用纯样式时的链接面。 |
| 10 | 框架分层 | `platform` public 依赖 `elements`，但部分渲染后端细节泄露在 platform 源内。 | 后续增加 platform backend internal target，降低 public 平台头暴露。 |
| 11 | 框架分层 | 控件层仍有局部样式计算和绘制细节，和 style 层职责有重叠。 | 后续将常见 control metrics/token resolver 下沉到 style/control_style。 |
| 12 | 框架分层 | `ItemsControl` 仍保留 `set_realized_range`，容易被误认为显式虚拟化 API。 | 后续标注为测试/高级窗口 API 或改名；文档已明确树级虚拟化优先。 |
| 13 | 框架分层 | 元素层承担主题应用、属性、布局、动画写入，缺少明确子服务边界。 | 后续抽 internal services；本轮先让 PropertyChange 语义更可读。 |
| 14 | UI 设计 | showcase 大量使用固定宽高，移动/窄宽表现不够系统化。 | 后续提取 responsive helper 和 section grid 规则。 |
| 15 | UI 设计 | 视觉令牌有 Element Plus 风格，但尺寸、间距、状态密度还未形成完整设计矩阵。 | 后续补充 spacing/size scale token；本轮保留样式文档入口。 |
| 16 | UI 设计 | 长内容区性能采样与普通演示混在同一页面，认知负担偏重。 | 后续将 diagnostics 面板独立或折叠。 |
| 17 | UI 设计 | 部分示例仍通过低层 `configure_layout` 配置常见尺寸。 | 本轮新增 `set_layout_size` 等 API 并迁移部分示例/测试；后续继续替换 showcase 样板代码。 |
| 18 | UI 设计 | 控件状态文字、可访问语义和 visual state 绑定不够集中。 | 后续为 control state 增加统一 state descriptor。 |
| 19 | 控件设计 | 多数控件暴露大量 `set_xxx(bool)`，缺少 `enable/disable/show/hide` 风格快捷入口。 | 本轮在 `UIElement` 层增加通用入口，控件自动继承。 |
| 20 | 控件设计 | `Button` flag 较多，组合状态的合法性不易发现。 | 后续引入 ButtonAppearance/Behavior options，保留旧 API 兼容。 |
| 21 | 控件设计 | `Input` 支持能力很完整，但 builder/options 不足，常见配置链太长。 | 后续提供 `InputOptions` 和 preset helpers。 |
| 22 | 控件设计 | `Select` 同时处理本地过滤、远程搜索、多选、tag 绘制，内部复杂度高。 | 后续拆 popup model 和 option presenter。 |
| 23 | 控件设计 | `Scrollbar` 既是独立控件又能 wrap content，模式较多。 | 后续明确 standalone/container 两个内部路径，API 上减少互斥状态。 |
| 24 | 控件设计 | `Panel`/`Border`/`StackPanel` 职责部分重叠。 | 后续定义 primitive、surface、layout container 的命名边界。 |
| 25 | 控件设计 | `ItemsControl` 仍保留容器池，与 UIElement virtual children 的关系需要更清楚。 | 文档已说明其职责是控件语义和复用，不是唯一虚拟化入口。 |
| 26 | 样式系统 | `UIElementStyle` 字段很多，纯矩形、文本、交互、材质、transition 混在一个 struct。 | 后续拆分为 token groups 或保持 struct 但增加 typed accessors。 |
| 27 | 样式系统 | 主题类是字符串，拼写错误只能运行时发现。 | 后续引入 typed theme class id；当前 `theme_class` 常量已降低风险。 |
| 28 | 样式系统 | 样式 fallback 路径靠调用者传入，缺少统一 missing-class 诊断。 | 后续给 ThemeManager 增加可选 diagnostics hook。 |
| 29 | 样式系统 | transition 只表达单 property，复杂状态动画仍靠控件手写。 | 后续扩展 multi-property transition set。 |
| 30 | 样式系统 | semantic tokens 与 control-specific tokens 还未完全分离。 | 后续增加 control token structs，避免控件硬编码颜色。 |
| 31 | 样式系统 | style mutation 会 detach theme management，调用者不一定意识到。 | 后续增加 `configure_local_style`/`configure_theme_style` 命名区分。 |
| 32 | 样式系统 | 暗色主题覆盖范围需要继续用视觉回归加强。 | 后续增加 dark showcase snapshot。 |
| 33 | 属性系统 | 原 `PropertyStore::value()` 需要默认值，不适合区分“未设置”和“设置为默认”。 | 本轮新增 `try_value()`。 |
| 34 | 属性系统 | 原本判断本地属性需要记 `has_local_value` 名称，和调用者常用语义不完全贴合。 | 本轮新增 `contains()`。 |
| 35 | 属性系统 | 原本读取“值或默认”只有 `value()`，名字不够表达 fallback 行为。 | 本轮新增 `value_or()`。 |
| 36 | 属性系统 | `PropertyChange` 调用方要手动解析 bit flag，代码可读性差。 | 本轮新增 `requires_layout()` 等语义方法。 |
| 37 | 属性系统 | `PropertyStore` 缺少 `empty()`，调用方只能比较 `local_value_count()`。 | 本轮新增 `empty()`。 |
| 38 | 属性系统 | property metadata 使用运行时 type_index，编译期错误暴露不足。 | 后续探索 typed metadata handle，保持动态属性能力。 |
| 39 | 属性系统 | inherited property 与普通 invalidation 共用 flag，语义略混。 | 本轮新增 `is_inherited()`；后续考虑独立字段。 |
| 40 | 虚拟化 | 普通子树和数据驱动虚拟子节点是两条路径，命名需要持续压平。 | 本轮文档统一为 UIElement 树级虚拟化；API 保留 `set_virtual_children`。 |
| 41 | 虚拟化 | virtual children v1 只支持固定 item extent。 | 后续增加 measured/estimated extent 策略。 |
| 42 | 虚拟化 | overscan 默认策略对不同输入设备尚未自适应。 | 后续按滚动速度动态调整 overscan。 |
| 43 | 虚拟化 | focus/capture/top layer pinning 规则需要更多端到端示例。 | 后续补 showcase diagnostics 和交互测试。 |
| 44 | 虚拟化 | materializer 使用 `std::function`，热路径有间接调用成本。 | 后续为高频列表提供 small-function 或 template builder 方案。 |
| 45 | 虚拟化 | 目前 metrics 面向元素本身，缺少整棵树聚合统计。 | 后续增加 subtree metrics collector；本轮保留 `virtualization_metrics()` 作为基础。 |
| 46 | 渲染 | render command list 与 prepared cache 交互复杂，调试入口偏少。 | 后续增加命令摘要和 cache hit 计数。 |
| 47 | 渲染 | 文本引擎文件较大，字体 fallback、layout、glyph atlas 准备混合。 | 后续拆 font resolver、text shaper、atlas prep。 |
| 48 | 测试 | visual regression 受字体/GPU 影响，稳定性边界需要更清楚。 | 后续将机器相关项标记 non-blocking 并增加 command-level regression。 |
| 49 | 文档 | API 文档缺少最新 UIElement ergonomics 示例。 | 本轮已补充 `set_min_size`、`set_vertical_virtual_children`、`virtualization_metrics` 示例。 |
| 50 | 文档 | 性能文档未明确 memory sampling 的解释边界。 | 现有文档已说明 working set/private bytes；后续补历史基线表。 |
