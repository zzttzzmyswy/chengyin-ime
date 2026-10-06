## 根因

`platforms/windows/candidate.cpp` 的更新流程对**已可见**的窗口做了两次移动：

1. 原 `:112`：为了先取目标显示器 DPI，每次按键都把可见窗口先挪到 `caret.left, caret.bottom`（光标下方）；
2. 原 `:296–305`：算完尺寸后才按 `y + height > work.bottom` 翻转到光标上方，再 `SetWindowPos`。

两次移动之间窗口停留在光标下方，DWM 会合成这个中间帧：位于屏幕下方时先闪一下下方，连续输入时每按键都跳一次。同时 `height` 随候选数变化，在 `work.bottom` 边界附近令翻转判断来回变化，于是单行编辑器里高度和位置上下抖动。

## 修复点

**1. 不再预移动可见窗口**（`candidate.cpp:147–171`）

预移动只服务于 DPI 探测（`windowDpi` 依赖窗口当前所在显示器），因此只在两种情况下执行：

- 窗口当前不可见（首次显示，本就隐藏，移动无可见影响）；
- `MonitorFromRect(&caret)` 与窗口当前显示器（成员 `placementMonitor_`）不同，需要换 DPI。

预移动目标改为目标显示器 `rcWork` 左上角，不再落在光标下方；跨显示器时先 `ShowWindow(SW_HIDE)`，最终定位时再 `SWP_SHOWWINDOW`。同显示器时直接用 `GetDpiForWindow` 现值，完全不移动。

**2. 放置决策抽成纯函数并加滞回**（`candidate.h:12–33`、`candidate.cpp:25–60`）

新增无 Win32 依赖的 `placeCandidates(PlacementInput, PlacementState)`，一次算出上下方向与坐标：

- 下方的判定仍是 `caret.bottom + gap + height <= work.bottom`；
- 滞回：`caret.top` 变化不超过 `slack`（`scale(2)`）时视为同一次组合的同一行，此时若上次已选“上方”，则只有下方多出一整排候选（`band = rowHeight_`）的空间才回落下方。`work.bottom` 处 `caret.bottom + height` 随候选数变化正好是一排的高度差，边界处不再来回翻转；
- 组合开始/结束（`hide()`）复位 `placement_`，新组合按空间重新判定。

**3. 上方放置只依赖 `caret.top`**（`candidate.cpp:46–49`）

`y = caret.top - height - gap`，不受下方放置结果影响，光标矩形高度的小幅波动只改顶部、不改方向。

DPI、字体、皮肤、圆角区域逻辑未改动。

## 测试

新增用例覆盖：下方放得下→下方；放不下→上方；同一光标上/下方向在 `height` 变化（边界附近 ±一排）时保持不抖动；`margin` 足够时才回落；`caret` 高度 1px 波动只改顶部；超工作区高度时钳制在工作区内；换显示器（工作区起点 2000）时的放置与右边界钳制。另有对真实弹窗的几何断言（上方时 `bottom <= caret.top`、下方时 `top >= caret.bottom`、1px 高度波动不移动窗口）。用真实测试代码块对纯函数做了本地执行验证。

```
$ bash scripts/check.sh
cargo fmt --all -- --check            # 通过
cargo clippy --workspace --all-targets -- -D warnings   # 通过
cargo test --workspace --locked       # 通过
cargo build --release --workspace --locked              # 通过
C ABI smoke test passed
1. 你好 [ni'hao]  /  西安 [xi'an]      # 通过
```

Windows 侧无法在 13.24 本地编译（TSF/MSVC），由 GitHub Actions 的 `windows-tsf` job 构建并跑 `ctest`（含 `windows_key_test`、`windows_ui_test`）。

## 未做实机验证

**未在真实 Windows 桌面验证。** 闪烁与抖动是 DWM 合成行为，只能由 MYSWY 在实机确认：多行编辑器、单行编辑器、屏幕底部/多显示器边界、连续输入与候选数变化等场景。本 PR 不得视为已通过兼容性验收。

🤖 Generated with [Claude Code](https://claude.com/claude-code)
