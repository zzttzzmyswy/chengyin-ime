# preview23：准确整句、句末助词与前缀保护

分支 codex/exact-sentence-prefix，基于 main 316d7f3。问题 xianzaikaishiba 在打开键盘纠错后
首选“想再开时报”，实际准确前缀“现在开始”排后；无匹配规则时准确前缀已可用。

- [x] 空历史、87540 条生产词库、四种 flags 复现用户问题及相邻句型。
- [x] 多字词＋句末吧/吗/呢/啊/呀的准确连接；仅终止位置，保留跨词约束与单弱兜底。
- [x] 整句纠错规范拼音重新对齐、全局累计成本 <=2，避免每个词重开预算。
- [x] 任何准确完整句压制非准确生成句；分段模式保护可准确匹配的多音节词组前缀。
- [x] 完整词语召回保留优先级，准确前缀只提前到猜测生成句之前；上下文排序也校验。
- [x] 七条生产句型/四 flags/默认与立即分段，五个自造助词结构、缺少整句词库、错误助词位置、全局预算回归。
- [x] “现在开始”只消费13字节，剩余ba再匹配；前缀学习须确认提交。
- [x] Windows TSF fixture 直接核验“现在开始吧”首位、“现在开始”分段上屏、保留ba组合及取消后不删已上屏部分。
- [x] 102 Rust Release、fmt/Clippy、零按键分配/64KiB、MSVC /W4 /WX C ABI、8/8原生CTest（38.61s）通过。
- [x] 独立488条集旧/新立即分段同口径对照：Top1 92.0%、Top9 97.7%、200页可达98.8%，分类/分档/半集指标一致。
  该集整体首选/首页/可达不退化；不修改评测语料，不将本批修复例子混入独立test半集。
- [x] version.json preview23，生成头文件、模拟bump与动态CI包名通过。
- [x] 本机安装包与全负载校验。
- [x] 推送、远程CI与隔离安装生命周期。
  源码 `2fc0fa3` 的 [Actions 37453639077](https://github.com/zzttzzmyswy/myswyIm/actions/runs/37453639077) 六作业成功，含 Windows 隔离安装、升级、回滚、卸载。

## 交付

本机交付 chengyin-windows-x64-0.1.0-preview23-msvc.exe，5135063 bytes，SHA256 `628a33c65c765c74a11edd6c9870cb6ff3ca16c9068111f6b2fceeb58f44e126`。
PE/7-Zip/全负载SHA/COM/生产字节一致/22项许可与插画署名CRC通过，已复制主目录。

核心六串9900样本与旧源码同配置顺序对照，P50/P95/P99从3727.7/19076.9/23955.8到222.9/11334.1/16782.3µs；
不含TSF/UI，非专用机，口径与日志见PERFORMANCE，长串预算仍须后续优化。

## 结果与证据

四 flags 空历史下：xianzaikaishiba → 现在开始吧；mingtiankaishiba → 明天开始吧；
womenxianzaikaishiba → 我们现在开始吧。“现在开始”仍在第一页，选中后仅留下ba。
源码说明见 DEVELOPMENT。原始日志：build/sentence-before-preview23.txt、sentence-after-preview23.txt、
tests-release-preview23.txt、clippy-preview23.txt、abi-test-preview23.txt、native-test-preview23.txt、
quality-preview22-incremental.txt、quality-preview23.txt、quality-preview23-incremental.txt。

真实升级宿主与物理混合DPI仍按E单独验收；不修改个人安装、设置和学习。
词库/语法模型覆盖不足时优先逐段选准确词，D其余模糊召回和性能优化继续排期。

交付证据：build/package-build-preview23.txt、package-verify-preview23.txt、delivery-preview23.json；性能证据见PERFORMANCE。
