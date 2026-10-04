# preview11：明确通用纠错与示例

用户指出四项“常见键盘失误”均使用 zhang，容易误解为仅支持该拼音。
已核对 dictionary.rs 的 trie 通用匹配和 fuzzy.rs 的字母对齐：算法不限定 zhang。

- [x] 每项设置标注“示例”，并说明适用于词库拼音，不限示例。
- [x] 使用不同拼音：zhnag→zhang、png→ping、hso→hao、shii→shi。
- [x] 扩展既有键盘失误回归到 hao / ping / shi / ni'hao，验证匹配、标记与中文提交。
- [x] 保留相邻交换、单字母漏输、QWERTY 相邻误按、重复字母的范围、关闭默认值和原文提交契约。
- [x] Windows 原生编译、七项 CTest 与八页/DPI/滚动截图检查。
- [x] preview11 单 EXE 包及完整负载校验。

只修改设置说明与版本，不修改解码算法；preview10 实机证据见 WINDOWS_PREVIEW10_DESKTOP_VALIDATION。
新包安装后的实机验证与完整应用矩阵仍需分别记录。

交付：chengyin-windows-x64-0.1.0-preview11-msvc.exe，4,333,865 bytes，
SHA-256 82b4b325aad21e910d15145c0b911cef1b1bba1bf0e8c539f51dc153f93fb246。
五项模糊音回归（其中键盘测试扩展为 12 个案例）、fmt、Clippy -D warnings 和七项 Windows CTest 通过。
截图 build/ui-preview11/settings-fuzzy.bmp 显示通用规则说明与“示例”；
96/120/144/192/288 模拟 DPI、窄视口与滚动检查通过，不代替物理多屏验收。
包预检查、7-Zip、解包清单/COM 和三二进制一致性验证通过。
