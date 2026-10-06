# 项目状态

## 长拼音分段输入学习完整词组 · 2026-10-06

`crates/ime-core/src/session.rs` 增量模式原先每段上屏后 `learn_commit()` 即清空 `learning_key`，只学到单段，长拼音经分段选词后不会学到合并后的完整词组。
现新增 `PhraseBuffer` 累计同一次组合内各段的拼音与文本；组合在最后一段完成上屏时标记 `pending`，宿主确认写入后 `learn_commit()` 在同一次调用内额外 `record_selection(完整拼音, 完整词组)`，返回值语义不变（仍表示是否有学习成功）。
取消 / Esc / 中途清空 / 非汉字上屏 / `learning_enabled=false` 均不学习；超过 `MAX_INPUT_BYTES`、`MAX_TEXT_BYTES` 或非汉字时整句放弃，不截断出错配键值；内存仍计入 64 KiB 上限。
被学习行也可作为收尾分段（第二次及以后输入走该路径），同样累计。非增量（分段暂存）模式原路径已通过测试确认会学到完整词组，仅补回归。
新增 `crates/ime-core/tests/phrase_learning.rs` 7 项；`scripts/check.sh` 全绿（fmt、Clippy -D warnings、全工作区测试、Release、C ABI smoke、CLI）。

## preview23 准确整句与前缀保护 · 2026-10-06

见 WINDOWS_PREVIEW23_TASKLIST。用户输入 xianzaikaishiba 在键盘纠错开启时被“想再开时报”等多词局部纠错组合抢占。
新增准确句末语气词连接、整句纠错总预算与规范拼音校验；立即分段保护准确多音节词组前缀。
四 flags 首选“现在开始吧”，同样修复明天开始吧/我们现在开始吧等，第一页保留“现在开始”；选中只消费13字节，ba继续匹配。
完整词语模糊召回保留；自造词库、高频错误组合、上下文、默认/staged与立即上屏、助词终止位置均有回归。
102 Rust Release、fmt/Clippy、零按键分配/64KiB、MSVC C ABI、8/8 Windows CTest（38.61s）通过，
含TSF host UI准确整句/前缀接受与取消余音节不丢已上屏文字。
独立488条集，在旧/新 Windows立即分段配置下总体/分类/半集指标一致：Top1/Top9/可达92.0%/97.7%/98.8%。
评测集保持不变。
本机交付 chengyin-windows-x64-0.1.0-preview23-msvc.exe，5135063 bytes，SHA256 `628a33c65c765c74a11edd6c9870cb6ff3ca16c9068111f6b2fceeb58f44e126`。
PE/7-Zip/全负载SHA/COM/生产字节一致/22项许可与插画署名CRC通过，已复制主目录。
源码 `2fc0fa3` 的 [Actions 37453639077](https://github.com/zzttzzmyswy/myswyIm/actions/runs/37453639077) 六作业成功，含 Windows 隔离安装、升级、回滚、卸载。
个人安装/设置/学习未改，真实宿主/混合DPI仍待E，D其余优化继续。

## preview22 Q 版大肥鱼与准确结构组句 · 2026-10-06

见 [本批清单](WINDOWS_PREVIEW22_TASKLIST.md)。用户授权本地裁切、去背景后，已替换为指定设定图正面全身形象；
主题 ID 12 保持，插画来源与权利说明随安装包保留。
准确 A/不A、A不/A 和多字词＋“的”得到词典锚定的组句支持；
`kaibukai / diannaode / shoujide` 空历史、关闭/全开匹配时首选分别为“开不开 / 电脑的 / 手机的”。
准确句可用时抑制猜测生成句；完整拼音的纠错/缩写组句不扩张音节数，词库模糊词及分段选词仍保留。
合并 main 至 0b2ae4b：B 学习 revision/重试、C 词库加载恢复/一次合并、D 独立评测基线保留。
版本由 version.json 统一生成，CI 安装测试包名也动态派生；模拟 bump 检查通过。
488 条独立标注集本机重放：Top-1 92.0%、Top-9 97.7%、200 页内可达 98.8%，与 I03 基线一致；
测试半集 244 条 Top-1 91.4%，整词/单字类别首选均 100%。此集未覆盖所有自然输入。

96 Rust Release、fmt/Clippy、零按键分配/64 KiB、MSVC /W4 /WX C ABI、8/8 原生 CTest（31.04 s）通过。
全身插画横纵候选和五档 DPI fixture 已核对。旧 preview21 GDI+ 全局退出停滞由实际线程栈证实；
新图片资源仅存 RGBA 与有界 DIB，GDI+ 在调用内结束。200% DPI fixture 坐标与测试进程 TIP 隔离也已修正。
五条结构输入、全部匹配规则、4100 个逐键＋可见标注样本，核心 P50/P95/P99 = 261.9/4994.4/7234.7 µs；
192 DPI 全身皮肤暖重绘为 841.8/1079.0/1337.1 µs，GDI 对象 15→15。口径见 PERFORMANCE。

交付 `chengyin-windows-x64-0.1.0-preview22-msvc.exe`，5104648 bytes，SHA-256
`110e733b16b1a7a1ef77de40abde114988a66d52ba93b118e6491e2242a6786c`，已复制至主工作目录。
PE、7-Zip、全部负载 SHA-256/COM、三生产二进制一致、22 项运行时许可 CRC 与插画署名包校验通过。
证据见 tasklist。个人安装/设置/学习未改；真实升级宿主、物理混合 DPI 和 review D 优化与 E 继续保留；B/C/D 基线已合并。
源码 `40fec9d` 的 [Actions 37445555758](https://github.com/zzttzzmyswy/myswyIm/actions/runs/37445555758) 六作业全部成功，含 Windows 隔离安装、升级、回滚、卸载。

## preview21 候选皮肤系统 · 2026-10-06

按用户新需求重新开启主题开发，分支 `codex/theme-skins`；见 [任务清单](WINDOWS_PREVIEW21_TASKLIST.md)、[皮肤指南](SKINS.md)。
系统/白/黑之外，新增青瓷枝叶、夜航星图、深蓝来信 Q 版鲸鱼娘。主题控制面板、圆角、选中样式、装饰侧栏；
文字按原字号排版，空间不足时隐藏装饰，可手动关闭。高对比度优先系统配色。
支持最多 64 个自定义 .cyskin 收藏、导入/导出、PNG 插画、配色、圆角/留白/纹样编辑，预览后应用。
包体/图片有界，主要/次要/纠错文字对比度校验；图片与布局配置作为快照加载，不在按键或绘制时读文件。
缩放图片使用最多四项有界缓存；同一自定义 ID 更换图片或关闭装饰时，活动候选几何同步刷新。
旧 3–5 主题继续迁移系统，新标识 10–13 不复用已删除角色主题。

最终直接采用用户指定 https://treapgogo.github.io/deepseek-whale-girl/ 网站的透明 Q 版头像，字节保持；来源说明随源码和 THIRD_PARTY 保留。
最终素材下 86 Rust Release、MSVC /W4 /WX C ABI、8/8 原生 CTest（24.89 s）及六主题横纵截图/五档 DPI 模拟通过。
候选窗口 300 次暖重绘，鲸鱼娘 P50/P95/P99=524.2/696.9/983.2 µs；GDI 对象稳定，详见 PERFORMANCE。
交付 `chengyin-windows-x64-0.1.0-preview21-msvc.exe`，4628751 bytes，SHA-256
`1a701b8f1f342effc87636062637c9d510fd8dd291baaf5d6e25cac500fd07c5`，已复制至主工作目录。
PE、7-Zip、全负载 SHA-256、COM、三二进制逐字节一致、测试通知隔离与 21 项运行时许可 CRC 校验通过。
证据 `build/native-test-preview21.txt`、`tests-release-preview21.txt`、`abi-test-preview21.txt`、
`ui-preview21/`、`skin-paint-bench-preview21.txt`、`package-verify-preview21.txt`、`delivery-preview21.json`。
preview21 源码 9a005e5 的 [远程 CI 37386348117](https://github.com/zzttzzmyswy/myswyIm/actions/runs/37386348117) 已完成且成功；个人实机升级仍单独验收。
没有修改个人安装/学习记录。旧 Explorer 升级验证、物理混合 DPI、review D 优化阶段与 E 保持待办（B、C、D 基线已于 2026-10-06 合并，见 REVIEW_REPAIR_PLAN、QUALITY_BASELINE）。

## 最新：preview20 单音节与受限 TSF 激活 · 2026-10-05

见 [本批清单](WINDOWS_PREVIEW20_TASKLIST.md)。完整单音节优先准确/模糊单字历史及单字，
词语和补全继续保留；分隔、多音节、首拼和前缀上屏后的剩余输入分别解析。
受限 TSF 激活从原先直接拒绝改为内嵌词库/内存配置路径，注册 SECUREMODE 能力。
该模式不读写个人设置/词库/学习、不提供设置入口、不产生学习和上屏联想；普通路径保持。
URL/search/default、密码/private、UI-only 与 100 次交替激活回归通过。

86 Rust Release、fmt/Clippy、零按键分配/64902 bytes、MSVC /W4 /WX C ABI、7/7 CTest（31.00 s）通过；
最终 Service 修改后 COM/TSF 两项再次通过（9.17 s）。横纵 bao/shi 窗口及选字 ABI、PE/7-Zip、
完整负载 SHA-256/COM、三生产二进制一致、21 项许可 CRC 通过。
安装包 preview20，4392201 bytes，SHA-256
`6b68ee3ad027b4e25f8957d194f49529d98c1e48ecb0210329f9a18eac643b41`，复制至主工作目录。
五单音节全规则逐键/可见标注 P50/P95/P99 = 15.5/176.0/199.7 µs；长串预算仍未全部达标，见 PERFORMANCE。

已在实际 Explorer 的 WinUI 地址栏/搜索框复现旧 preview7 直接输出英文且无候选。
该进程尚未加载新 DLL；安装器提权被 Windows 返回“操作已被用户取消”，安装没有完成，
真实新版本控件验收仍待完成。当前调用进程无管理员权限，不重复触发 UAC。
明确的受限激活缺口已修复，但不能认定它是这两个实际控件的唯一原因。
下一步先完成升级后的 Explorer 双输入框验证，再回到普通学习 revision 与有界 writer 重试。

preview19 a55c97c 的 [Actions 37318044390](https://github.com/zzttzzmyswy/myswyIm/actions/runs/37318044390)
六作业和隔离安装生命周期通过；preview20 的远程 CI 单独追踪。

## 最新：preview19 拍照候选与旧学习保护 · 2026-10-05

任务见 [preview19 清单](WINDOWS_PREVIEW19_TASKLIST.md)。隔离 preview12、空历史、全部规则复现
无依据的“拍找、派找”等先于“拍照”；在运行的 Chrome/steamwebhelper 等确实仍加载 preview12。
preview18 已有完整词保护；本次补齐未收录旧历史累计计数可直接提权的漏洞：
须有至少三次近期命中才提权，词库可证明的旧习惯继续可用，个人历史没有清空。
`paizhao/pai'zhao` 的空历史首两项“拍照、牌照”；错拼召回、正常学习与前缀选词保持回归。

82 项 Rust Release、fmt、Clippy、零按键分配/64902 bytes、MSVC /W4 /WX C ABI 通过；
原生 7/7 CTest（22.08 s）、窗口绘制/拍照选词、三二进制和包内负载一致、PE/7-Zip/完整 SHA-256/COM、
21 项许可 CRC 通过。Debug 有一项已有测试 EXE 启动时文件占用错误，未把它算作通过，详见本批清单。
交付 `chengyin-windows-x64-0.1.0-preview19-msvc.exe`，4384807 bytes，SHA-256
`f9f460e54f372af89fa83feda5201080c24ed28cd3d7bcdb224b4385e773132b`，复制至主工作目录。
尚未安装到个人电脑；升级后完整退出重开加载旧 DLL 的应用才能使用新算法。
下一步仍优先学习跨应用 revision/有界重试，再按审查计划推进词库、性能与实装矩阵。

## 最新：preview18 代码审查修复 · 2026-10-05

原 preview8–17 的 12 个未推送提交已快进合入并推送 main `d07fe57`。
[Actions 37309334271](https://github.com/zzttzzmyswy/myswyIm/actions/runs/37309334271)
六作业全部通过，包括隔离安装/升级/回滚/卸载；补足下述 preview17 的远程验证记录。
审查与修复在 `codex/review-hardening`；详细结论见 [代码审查](CODE_REVIEW_2026-10-05.md)，
[修复排期](REVIEW_REPAIR_PLAN.md)、[本批清单](WINDOWS_PREVIEW18_TASKLIST.md)和[精简交接](CONTEXT_HANDOFF.md)。

本批修复显式组合声母误读、词库音节数与字数解析遗漏、尾部分隔的整词候选/学习、
学习文件代际未隔离、更新版本/包名/下载 URL 未绑定、测试配置通知干扰；补齐 a/e/o 首字母索引。
77 项 Rust、fmt、Clippy、零按键分配/64 KiB、MSVC /W4 /WX C ABI 已通过。
最终 MSVC 原生 **7/7 CTest（28.50 s）**、隔离 UI 与截图检查通过；生产三个二进制无测试通知通道。
安装包 `build/packages/chengyin-windows-x64-0.1.0-preview18-msvc.exe`，**4388187 bytes**，
SHA-256 **57604fadbb04978675cd651008b1e03e43430ee7319f5721e5d9f400ccb428a4**。
7-Zip、完整 SHA-256/COM、三生产二进制一致与 21 项许可 CRC 通过，并复制到主工作目录同路径。
证据 `build/tests-preview18.txt`、`clippy-preview18.txt`、`abi-test-preview18.txt`、
`native-test-preview18.txt`、`ui-test-preview18.txt`、`package-preview18.txt`、`delivery-preview18.json`。
截图 `build/ui-preview18/`；没有替换个人安装或使用个人历史作为 fixture。
源码 `ad84c9d` 的 [Actions 37314439254](https://github.com/zzttzzmyswy/myswyIm/actions/runs/37314439254) **六作业全部通过**，
包括 Linux/Windows Rust 与 ABI、MSRV、portable core、Fcitx、原生 TSF 和隔离安装/升级/回滚/卸载。
本地交付与远程 runner 交付各有自己的构建环境及摘要，不能混用哈希。

100 轮、87540 条词库，Win11 build26200 / Ryzen9950X / Rust1.99 MSVC bench：
全规则六缩写逐键 P50/P95/P99 **2.5/348.2/3147.2 µs**，最终键 **1.2/3.5/6.4 µs**；
六纠错/长串逐键 **1122.7/12380.6/15105.1 µs**。同期间旧版长串 P99 14491.7 µs，
单次差值不能证明因果或未退化，性能预算仍未满足；详见 [性能口径](PERFORMANCE.md)。
会话仍 **64902 bytes**，共享词库堆估算 **13209127 bytes**，单次加载 80.944 ms。

下一批优先解决普通学习跨应用快照同步和后台存储失败的有界重试；再处理词库失败重载、
峰值/导入规范化、版本统一、独立质量与性能基准、实装宿主/物理混合 DPI。
排期以测试门槛推进，不能用核心或模拟 TSF 检查宣布全部实机兼容。

## preview17 完整声母缩写优先 · 2026-10-05

任务见 [preview17 清单](WINDOWS_PREVIEW17_TASKLIST.md)，新分支 `codex/initials-word-priority`。
修复开启全部模糊音/纠错后 `ssdd` 首页被“舍得、杀得”等较少字候选占据的问题。
共享不可变首字母索引优先完整的一字一个声母匹配，`ssdd` 首选“世世代代”，
`zgrm` 首选“中国人民”，`zhrm` 首选四字的“走火入魔”。有完整同字数词条时仅显示对应词语和
最多两项对应字数的准确历史；没有才走既有较少字、纠错、组句回退。全拼和前缀选择保持原行为。
完整索引避免旧有界首拼搜索提前裁掉低频词，完整同音词分页；正常缩写不标为错拼。

**73 项 Rust**、fmt、Clippy -D warnings、MSVC /W4 /WX C ABI、**7/7 原生 CTest（25.91 s）**通过。
新增 6 项回归覆盖 11 单独模糊音规则、四类独立纠错及合并规则、137 同音词分页、历史上限/短词过滤、
缓存、编辑、词库替换、分隔符、长规范拼音安全及 2/3/4/5/40 字输入；初始化后路径仍零按键分配。
会话 inline+预留 **64,902 bytes** <64 KiB，共享词库堆估算 **13,209,127 bytes**；单次加载 86.957 ms。
100 轮、87540 条真实词库、全规则六个声母缩写的全部逐键/可见标记 P50/P95/P99 为
**1.3/217.8/2260.9 µs**；只计完整匹配的最后一键为 **0.9/1.2/2.3 µs**，不可代替整串或 UI 延迟。
Win11 build 26200、Ryzen 9 9950X 16C/32T、Rust 1.99 MSVC release，详见 [性能口径](PERFORMANCE.md)。

原生生产词库的横/纵候选窗口及 ABI 选词上屏检查通过，截图 build/ui-preview17/。
交付：build/packages/chengyin-windows-x64-0.1.0-preview17-msvc.exe，**4353979 bytes**；
SHA-256 **71b82e45dcf68750d8a095a4ce64407646a055a04ffe7082ef58259e5a610173**。
7-Zip、完整负载/COM、最终三二进制逐字节一致与 21 项许可 ZIP CRC 通过。
证据：build/tests-preview17.txt、initials-test-preview17.txt、clippy-preview17.txt、abi-test-preview17.txt、
native-test-preview17.txt、bench-preview17.txt、ui-test-preview17.txt、package-preview17.txt、delivery-preview17.json。
未修改个人安装、设置、学习；未运行新远程 CI、公开 Release 或安装生命周期。
下一步：安装 preview17 后复验真实宿主的首拼输入及前缀上屏，物理混合 DPI 和安装生命周期继续独立验收。

## 已结项阶段：preview8–16

**2026-10-05：Windows preview8–16 开发阶段已结束。** 整理、分支合入及交付清单见
[阶段结项记录](WINDOWS_PHASE_WRAPUP.md)。后续需求另起开发分支，未验收范围保留在记录中。

本阶段最终批次：**preview16 增加从前到后的词组/单字选择，立即上屏所选部分，再匹配剩余拼音。**
任务见 [WINDOWS_PREVIEW16_TASKLIST](WINDOWS_PREVIEW16_TASKLIST.md)。整词、整句和准确/模糊历史字词两线保持优先。
前缀来自准确词库边，按消耗长度优先并支持同音分页；不依赖组句猜测，不受单项未知整句兜底额度约束。
`zhengzebiaodashi` 首选“正则表达式”，随后“正则”、“正”等前缀；选“正则”即上屏这两个字，
剩余 `biaodashi` 重新给出“表达式”、“表达”、“表”等选项，也可连选“正”→“则”→“表达式”。
单个完整音节不做不必要的内部截断；未知尾部仍可修改、取消或通过空格提交其原始拼音。

Windows 默认启用新模式；C ABI v1 追加显式 configure_incremental，旧适配器默认暂存段协议保持不变。
TSF 同一写锁提交选择及剩余预编辑，再收缩 composition 起点，已选文字退出临时编辑。
Home、退格和 Escape 仅影响剩余 raw；数字、鼠标和宿主 Finalize 同一路径，重复异步选择仍合并。
学习只记录被接受的前缀拼音与文字，末尾分隔符不进入学习键，未接受的范围变更不训练。
故障注入发现短输出后的旧光标超界，已在 ShiftStart/GetRange 失败时恢复有效光标、结束编辑并吞掉选择。
鼠标持续编辑检查还补全了模拟宿主写后 EditRecord，使立即及异步选择后的剩余 composition 都受到真实式通知核对。

**67 项 Rust 测试**、fmt、Clippy -D warnings、MSVC /W4 /WX C ABI、
**7/7 原生 Windows CTest（18.44 s）**通过。新增真实词库及 137 同音前缀分页、分隔符、连选、取消、
模式忙拒绝、C ABI 双输出/旧模式兼容、三种宿主选择与 SetText/ShiftStart 故障回归。
8192 历史的初始化后按键路径零分配测试启用立即分段，会话 inline + 预分配容量 **64,894 bytes** <64 KiB。
候选横/纵截图显示整词加前缀，选中后的剩余窗口已人工核对；八页原生设置、字体、模拟多 DPI 回归通过。

性能：Win11 build 26200，Ryzen 9 9950X 16C/32T，Rust 1.99 MSVC release，87540 条词库，100 轮；
以下 real Session 均启用立即分段，计核心 process 与可见字母标记，排除宿主/UI/磁盘：

| 语料/范围 | 样本 | P50 | P95 | P99 |
| --- | --- | --- | --- | --- |
| 全规则六输入，逐键及可见标记 | 4900 | 1.001 ms | 10.713 ms | 13.004 ms |
| 全规则 yingshe/yinshe/yignshe，逐键及标记 | 2000 | 0.261 ms | 3.291 ms | 3.395 ms |
| 无规则五输入，含 60 bytes 长句，逐键 | 12100 | 0.066 ms | 2.683 ms | 3.512 ms |

词库加载 **55.508 ms**，共享词库堆估算 **12,637,655 bytes**。零按键分配不包含成功学习确认的快照分配。
合成 8192 历史与缓存、确认和查询基准另列 build/bench-preview16.txt，不能替代独立输入质量或端到端延迟测量。

交付：build/packages/chengyin-windows-x64-0.1.0-preview16-msvc.exe，**4385302 bytes**；
SHA-256 **c0d0bc1e9203033e5506194adca18632813afa77560dfac1cd1eee66e437c299**。
包预检查、7-Zip、解包全负载哈希/COM、三二进制与最终构建一致及 21 项许可 ZIP CRC 通过。
证据：build/tests-preview16.txt、clippy-preview16.txt、abi-test-preview16.txt、native-test-preview16.txt、
bench-preview16.txt、ui-test-preview16.txt、package-preview16.txt、delivery-preview16.json；截图 build/ui-preview16/。
本批没有替换个人安装或修改个人设置/学习；无新安装生命周期、远程 CI 或公开 Release。

下一步：安装后验证更多真实宿主的分段上屏与 composition 属性；物理混合 DPI 多屏、隔离安装生命周期及独立
长句/歧义切分质量语料继续单独验收；继续降低长句高百分位成本。
升级后需重开已加载旧 DLL 的应用一次，既有学习和用户词库无需清除。

历次状态记录见 [历史归档](history/STATUS_THROUGH_PREVIEW15.md)，各版任务清单继续保留。
