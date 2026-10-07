# preview10 安装后桌面验证

2026-10-05，用户完成安装后验证。环境：Windows 11 build 26200、x64、Ryzen 9 9950X。
源码批次 fab1840；本轮只补充实机证据，不修改交付二进制、不重新打包。

## 安装与实际加载

- 卸载注册显示 0.1.0-preview10；COM InprocServer32 指向 `C:\Program Files\ChengyinIME\0.1.0-preview10\chengyin_tsf.dll`。
- 安装版 probe 的完整文件清单校验、DLL 导出/COM 工厂/系统 TSF 探测通过。
- 安装后的 chengyin_tsf.dll、chengyin_settings.exe、chengyin_probe.exe 与本地交付构建 SHA-256 一致。
- 独立 Notepad3 测试窗口进程 11324 确认实际加载 preview10 DLL。
- 用户原有两个 Notepad3 仍加载 preview7；以前的独立测试窗口仍加载 preview8。没有关闭或编辑这些窗口。
  已加载的 DLL 不会因安装自动替换；对应应用重新打开后才加载新版本。

## 真实按键验证

使用 computer-use 的物理按键接口，逐键观察，不以粘贴字符串代替输入法按键。
暂时开启 zh↔z 和四项键盘失误（MatchingOptions=983041）；字号 18、横排、系统暗色主题。

| 宿主 / 输入 | 观察结果 |
| --- | --- |
| Notepad3：zhnag | 首选“张”，标准拼音 zhang 中 an 加粗；未选中候选的 an 同时有强调色 |
| Notepad3：zhnag、Space、Shift+6、Shift+7、Shift+j | 保存为 `zhnag^&J`；Space 未转中文、未纠正原文、未追加空格；三个 Shift 组合正常 |
| Notepad3：zang | 第一页优先精确 zang；第二页“张”的 zh 加粗并高亮；PageUp 返回第一页，Esc 取消组合 |
| 原生 Edit：zhng | 出现“张 / zhang”，漏输的 a 加粗并高亮 |
| 原生 Edit：zhng、Left、Left、a、Space | 中间补入 a 后原文为 zhang，提交成功 |
| RichEdit：zhsng | 首选“张 / zhang”，误按位置 a 加粗；Space 提交原始 zhsng |
| RichEdit：zhaang | 精确句子组合优先；第二页能找到“张 / zhang”，重复位置 a 加粗并高亮；Esc 取消后保留先前 zhsng |
| RichEdit：普通 zha | 可直接内嵌编辑时，普通候选不重复显示拼音；纠错候选需要标记时恢复标准拼音 |
| 安装版主题页 | 下拉列表只有 Windows 系统、白、黑，三个角色主题已删除 |

Notepad3 最终专用文档字节：`7A686E61675E264A0D0A`，即 `zhnag^&J` 加 CRLF。
独立文档在 build/desktop-preview10-notepad3.txt；测试内容不写入用户已有文档。
Edit/RichEdit 内容仅留在设置页测试控件，本页不持久化。

## 设置与数据恢复

通过设置 UI 取消五个临时开关并应用，最终 MatchingOptions=0。
原设置字段保持一致；序列化新增显式的默认字段 MatchingOptions=0、AutoUpdate=1。
未选择中文候选，未更改学习开关；学习文件测试前后 SHA-256 完全相同。
未操作卡巴斯基提示、未改变系统安全配置；此前 build-only 辅助工具的检测分类仍未排除。

## 证据边界与后续

这次覆盖安装负载和 Notepad3 / Edit / RichEdit 的上述实际操作。
未声称完整应用矩阵、物理混合 DPI 多屏、隔离安装/修复/回滚/卸载或远程更新已经验收。
zhaang 会被解析为 zha+ang，精确句子优先使纠错单字位于第二页；后续独立输入质量评测应包含这类歧义，
再确定精确组合与键盘纠错候选的排序取舍。
