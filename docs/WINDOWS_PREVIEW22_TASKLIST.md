# preview22：Q 版大肥鱼与准确结构组句

分支 `codex/q-dafeiyu-skin`。用户指定网站首页 Q 版全身形象，并明确允许本地裁切、去背景。
来源：https://treapgogo.github.io/deepseek-whale-girl/ 。素材及许可说明见 assets/skins/README.md。

- [x] 改用左上正面全身形象，保留发箍、围裙、鲸尾、鞋袜；透明 RGBA 449 × 512 部署。
- [x] 主题名“Q 版大肥鱼”，保留 ID 12，自定义与其余预置保持兼容。
- [x] 深色衬底与六主题横纵候选截图核对；五档 DPI 模拟通过。
- [x] 空历史、生产词库复现 `kaibukai`、`diannaode`、`shoujide` 的错误候选。
- [x] 字典词锚定的准确 A/不A、A不/A 和多字词＋的结构；不使用这两条输入的硬编码词表。
- [x] 有可靠准确组句时抑制猜测生成句；完整拼音的纠错/缩写组句不扩张音节数。
  独立词库模糊词召回、每线历史与分段上屏继续保留。
- [x] 跨词例/自定义词库/分段上屏回归；96 Rust Release、fmt/Clippy、零按键分配/64 KiB、MSVC C ABI 通过。
- [x] 修正 key fixture 的 200% DPI 坐标虚拟化，侧栏不存在时不误测侧栏点击。
- [x] GDI+ 在解码/首次缩放调用内关闭；共享资源仅保留 RGBA 和至多四项 DIB，不在 DLL 全局析构等待 GDI+ 工作线程。
- [x] UI/COM 自动回归隔离个人 TIP。UI 用进程内 COM 类工厂绑定 fixture，子窗口关闭等待处理 COM 消息；
  文件 probe 不激活安装 TIP，registered 生命周期模式保留真实注册激活。
- [x] 8/8 Windows CTest（31.04 s）、完整 UI 截图回归、包校验及来源说明通过。

- [x] 合并 main 的 B/C/D 基线，保留 revision、writer 重试、词库 loader 和多库合并。
- [x] version.json preview22 生成头文件；CI 包名派生；模拟 bump 校验。
- [x] 488 条独立质量集重放，与 I03 总体/分类/半集指标一致，未据此宣称所有输入达标。

## 交付

`chengyin-windows-x64-0.1.0-preview22-msvc.exe`，5104648 bytes。
SHA-256 `110e733b16b1a7a1ef77de40abde114988a66d52ba93b118e6491e2242a6786c`。
PE、7-Zip、全部负载 SHA-256、COM、三生产二进制逐字节一致、测试通知隔离通过；
22 项运行时许可 CRC 通过，插画来源说明嵌入 RUNTIME_LICENSES.zip。
已复制至主工作目录 build/packages。

证据：`build/grammar-before.txt`、`grammar-after-preview22-integrated.txt`、`tests-release-preview22-integrated.txt`、`allocations-preview22.txt`、
`abi-test-preview22-integrated.txt`、`native-test-preview22-integrated.txt`、`ui-test-preview22-integrated.txt`、`ui-preview22/`、
`grammar-bench-preview22-integrated.txt`、`skin-paint-bench-preview22-integrated.txt`、`package-verify-preview22-integrated.txt`、`quality-preview22-integrated.txt`、`delivery-preview22.json`。

## 保留边界与下一批

旧 preview21 在实际测试进程的 CoUninitialize 中调用 GdiplusShutdown，等工作线程退出形成卸载停滞；
调用栈见 `build/ui-shutdown-stack-preview22.txt`，此缺陷已从新源码的资源生命周期消除。
自动 fixture 不等于个人升级后的实机验证。个人安装、设置与学习未改；
新版已安装宿主、物理混合 DPI 仍待集中验证。源码 `40fec9d` 的 [Actions 37445555758](https://github.com/zzttzzmyswy/myswyIm/actions/runs/37445555758) 六作业全部成功，含 Windows 隔离安装、升级、回滚、卸载。
本次同步保留主分支 B/C/D 基线修复；后续 D 优化与 E 继续按 REVIEW_REPAIR_PLAN 排期。
