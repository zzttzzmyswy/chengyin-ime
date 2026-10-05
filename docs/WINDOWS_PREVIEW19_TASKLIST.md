# Windows preview19：拍照候选质量与旧学习可信度

用户报告 `paizhao` 首屏被“拍找、派找、排找、牌找、拍赵”等无词库依据的拼接占据。
本批分支 `codex/photo-word-quality`；共享核心没有对“拍照”增加特殊排序或拼接黑名单。

## 诊断

- 在隔离的 preview12 源码 `299dae9`、内置词库、空历史上复现：全部规则开启时，
  “拍找、派找、排找、牌找”先于“拍照”；关闭规则后完整词先出现但仍附带无依据的拼接。
  该缺陷已由 preview13/15 的整词线、组句质量保护修正。旧版此次输出的第五项为“拍照”，
  与用户第五项“拍赵”不同，不能认定已经复现截图所有细节。
- 读取在运行的模块，Chrome / steamwebhelper 等仍加载 preview12，ChatGPT / explorer 等为 preview7；
  steam 主进程为 preview17。未确定截图的具体进程，也未读取个人历史或输入内容来推测原因。
- preview18 空历史已输出“拍照、牌照”；但另有真实漏洞：旧 v1 累计次数 >=3 的未收录组合，
  即使没有近期命中证据，仍可获得历史提权。新增合成 fixture 在修改前失败、修改后通过。

## 实现和验收

- [x] 未收录历史的提权门槛改为累计 >=3 且衰减后的近期命中 >=3；
  词库可证明的旧习惯保留，近期低命中降权继续生效。
- [x] 历史条目没有被查询删除，v1/v2 格式保持兼容；明确重复选择未登录词仍能获得提权。
- [x] 整词、单字、从开头选择词组/单字的两线及前缀提交协议保持不变。
- [x] 五项新回归：全部 11 独立模糊音/四类纠错/关闭/合并开关、分隔符、翻页和两种分段模式；
  v1 次数 1/3/999 和 0–3 次近期选择；v2 保存重载；正常旧词偏好；错拼召回；衰减后数据保留。
- [x] 原生候选窗口复现 `paizhao/pai'zhao` 首两项“拍照、牌照”，数字选择完整上屏“拍照”。
- [x] 82 项 Rust **Release** 测试、fmt、Clippy -D warnings、零按键分配/64 KiB、MSVC /W4 /WX C ABI。
- [x] 原生 7/7 CTest（22.08 s）、UI fixture 和截图、安装包 PE/7-Zip/完整负载 SHA-256/COM 校验。
- [x] 三生产二进制与安装包逐字节一致，测试通知通道未进入生产；21 项许可 ZIP CRC 通过。
- [x] 100 轮真实词库和 8192 历史微基准记录，见 PERFORMANCE。

Debug workspace 检查两次在启动已有 `initials-*.exe` 时受到 Windows 文件占用错误 32；
该可执行文件未运行。未发现该文件对应的 Defender 检测记录，未停用安全软件或改名规避。
使用正常 Release 构建后全量 82 项通过。证据分别保留在 tests-preview19.txt / tests-release-preview19.txt。
版本升级时同步更新了未来 release fixture 和独立的错误标签样例，离线更新测试最终通过。

## 交付与边界

`build/packages/chengyin-windows-x64-0.1.0-preview19-msvc.exe`，4384807 bytes；
SHA-256 `f9f460e54f372af89fa83feda5201080c24ed28cd3d7bcdb224b4385e773132b`。
已复制到主工作目录同路径。证据 `build/*-preview19.txt`、`delivery-preview19.json`、
`loaded-ime-versions-preview19.json`、`ui-preview19/candidate-mapping-paizhao.bmp`。

本批未替换个人安装、历史和设置，也没有终止用户应用。**新 DLL 无法安全替换其他进程已经加载的旧 DLL；
安装后需要完整退出并重开相应应用（包括仍在后台的进程）。** 设置热更新和二进制升级是两个不同机制。
自动化 native/fixture 不能代替更新后每个真实宿主的验收；全量独立语义质量、物理混合 DPI 仍待验证。
后续审查修复 B–E 按 REVIEW_REPAIR_PLAN 推进，不声称已经完成。
