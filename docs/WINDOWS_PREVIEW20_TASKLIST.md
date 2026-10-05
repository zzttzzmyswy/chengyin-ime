# Windows preview20：完整单音节与资源管理器接入

用户报告 `bao/shi` 首页全部是词语，以及资源管理器搜索框、地址栏不能触发。
分支 `codex/single-syllable-explorer`，基于 main `a55c97c`。

## 诊断与处理

- 完整合法单音节原先仍使用多音节的词语优先管线，开启模糊音/纠错时容易被词语占据。
  使用通用音节解析判断输入意图，不硬编码 `bao/shi`；`xian` 优先单字，显式 `xi'an` 按词语。
- 完整单音节先按准确单字历史（最多两项）→准确单字→模糊单字历史（最多两项）→模糊/纠错单字查询，
  然后继续词语线及补全。惰性查询保证分页后仍保持线的顺序；同文本去重，历史记录不删除。
- 重新判断前缀提交后的剩余拼音；单字选择消耗整个音节，原始拼音与标注协议不变。
- 资源管理器实际运行进程仍加载 `C:\Program Files\MyswyIME\0.1.0-preview7\myswy_tsf.dll`。
  旧版在 WinUI 地址栏、搜索框输入 `n` 直接产生英文，无候选；经典列表控件曾触发旧候选。
  没有把旧 DLL 的现象当作 preview20 的实机结果，也没有用个人历史作为回归 fixture。
- 发现当前接入明确拒绝 `TF_TMAE_SECUREMODE`，且注册没有对应能力类别。
  增加 SECUREMODE 类别的注册/卸载和受限激活，独立从 DLL 内嵌词库创建只读字典，保持正常共享字典计数。
  受限模式使用内存默认配置，不读取或保存个人设置/词库/学习，不启动后台 writer/watcher，
  不提供设置入口，不学习、不产生上屏联想；仍允许 URL/search 常规输入，密码/private 继续透传。
  这是已确认的接入缺口修复，尚不能认定是两个实际 WinUI 控件的唯一原因。

依据：[Microsoft ActivateEx](https://learn.microsoft.com/en-us/windows/win32/api/msctf/nf-msctf-itftextinputprocessorex-activateex)、
[TSF 能力类别](https://learn.microsoft.com/en-us/windows/win32/tsf/predefined-category-values)。
受限激活本身不等于密码输入；限制个人文件和学习是本项目采取的实现策略。

## 验收清单

- [x] 4 项新增核心回归：生产字典/所有独立规则、137 单字完整分页和词语保留、独立历史槽位及缓存、
  显式分隔/前缀上屏后的剩余单音节/原文提交/尾分隔。
- [x] 原有深层同音历史、整词/首拼、模糊标注、学习衰减和前缀回归保持通过。
- [x] 86 项 Rust Release、fmt、Clippy -D warnings、MSVC /W4 /WX C ABI；
  按键零堆分配回归增补 bao/shi/xian，Session inline+reserved 64902 bytes <64 KiB。
- [x] 原生受限与 UI-only 组合激活、URL/search/default 上屏、空格原文、密码/private 透传、
  禁止启用学习、100 次交替正常/受限激活和资源释放。
- [x] `--registered` 增加实际安装的 SECUREMODE/IMMERSIVE/COMLESS/host-UI 能力枚举，供安装生命周期检查。
- [x] 最终原生 7/7 CTest（31.00 s）、横纵单字候选窗口与 ABI 上屏截图；
  保持配置代际映射生命周期的最终修改后，COM/TSF 两项再次通过（9.17 s）。
- [x] 单 EXE、PE/7-Zip/完整负载 SHA-256/COM、三生产二进制一致、21 项许可 CRC 校验。
- [x] 100 轮微基准：Win11 build26200 / Ryzen9950X 16C/32T / Rust1.99 MSVC / 87540 条词库，
  五单音节全部逐键与可见标注 P50/P95/P99 = 15.5/176.0/199.7 µs。
  六错拼/长串 P99 = 14.635 ms，仍未满足全局核心预算；不是 UI 延迟或整个语言质量证明。
- [ ] 安装 preview20 后重新启动实际 Explorer，确认搜索框和地址栏加载新 DLL，并实测候选、选词、空格与焦点切换。

## 交付边界

安装包 `build/packages/chengyin-windows-x64-0.1.0-preview20-msvc.exe`，4392201 bytes，
SHA-256 `6b68ee3ad027b4e25f8957d194f49529d98c1e48ecb0210329f9a18eac643b41`，已复制到主工作目录。
证据：`build/tests-release-preview20.txt`、`clippy-preview20.txt`、`abi-test-preview20.txt`、
`native-test-preview20.txt`、`native-service-test-preview20.txt`、`bench-preview20.txt`、
`ui-test-preview20.txt`、`ui-preview20/`、`package-preview20.txt`、`delivery-preview20.json`。
尝试启动该经过校验的安装器时，Windows 提权返回“操作已被用户取消”，安装没有完成。
当前调用进程没有管理员权限，不重复触发 UAC；已准备好安装包，等待后续手动安装后继续实机验证。
preview19 的 [Actions 37318044390](https://github.com/zzttzzmyswy/myswyIm/actions/runs/37318044390)
六作业及隔离安装生命周期已确认通过；不能算作 preview20 的 CI 证据。
不能用模拟 TSF、旧 DLL 或声明能力类别替代新版真实控件验收。
后续代码审查 B–E 排期保持，优先普通学习 revision 同步与 writer 有界重试。
