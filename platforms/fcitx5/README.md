# Fcitx 5 Linux 原型

目标框架版本 ≥5.1，C++17；实际本地验证版本见 STATUS。默认使用 98 条内置演示词条，也可在 Fcitx 配置工具里指定自定义 TSV。当前为开发预览，不要把它设为唯一的日常输入法。

## 构建与暂存安装

Debian/Ubuntu 示例，发行版包名可能不同：

```sh
sudo apt-get install build-essential cmake extra-cmake-modules libfcitx5core-dev fcitx5 fcitx5-config-qt
cargo build --release -p myswy-ffi --locked
cmake -S platforms/fcitx5 -B build/fcitx5 -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr
cmake --build build/fcitx5
ctest --test-dir build/fcitx5 --output-on-failure
DESTDIR="$PWD/build/stage" cmake --install build/fcitx5
```

最后一步只把文件放在 `build/stage`，不修改系统。默认动态插件目录取自发行版 Fcitx SDK 的 `FCITX_INSTALL_ADDONDIR`；Debian amd64 通常为 `/usr/lib/x86_64-linux-gnu/fcitx5`。可用 `-DMYSWY_ADDON_DIR=...` 指定，但必须匹配框架实际查找目录。交叉编译时通过 `-DMYSWY_RUST_LIBRARY=/path/to/libmyswy_ime.a` 指向同一目标架构的 Rust 静态库。

插件静态链接本项目 Rust 核心，动态依赖 Fcitx 框架；不需另装 `libmyswy_ime.so`。打包时保留 `LICENSE` 并列明平台依赖许可证。

## 在测试桌面安装

在准备好的测试桌面、检查暂存目录和 `build/fcitx5/install_manifest.txt` 后执行：

```sh
sudo cmake --install build/fcitx5
```

重新启动该桌面的 Fcitx 5 服务，在其配置工具中添加 **澄音输入法（全拼原型）**，使用框架配置的快捷键切换。X11/Wayland 的启动与应用模块配置遵循发行版和桌面说明，见 [兼容性矩阵](../../docs/COMPATIBILITY.md)。本轮开发没有替换当前系统输入法。

源码安装的卸载依据 `install_manifest.txt`：本项目模块、两个注册文件和两个文档文件，并在 Fcitx 配置工具移除该输入法。不批量删除整个 Fcitx 目录。使用下面的 Debian 包安装时，由包管理器跟踪和移除这些文件。

## 词典配置与更新

在 Fcitx 配置工具中打开澄音的设置，填写“词典 TSV 绝对路径”。留空恢复内置演示词典。自定义词典是**替换**演示词典，而非合并；格式见 [词典说明](../../data/README.md)。路径不展开 `~` 或环境变量。

也可编辑 `${XDG_CONFIG_HOME:-$HOME/.config}/fcitx5/conf/myswy.conf`，顶层字段如下：

```ini
DictionaryPath=/absolute/path/to/my-dictionary.tsv
```

配置工具保存会自动请求加载。手工编辑配置，或替换同一路径的 TSV 后，执行 `fcitx5-remote -r` 重新加载；当前不监视文件变更。建议先写新文件再原子重命名，避免读取到正在写入的半个词典。

加载只在一个后台线程中执行，多次修改只采用最后一次请求。成功后空闲会话共享新词典；正在输入的组合继续使用原词典，到上屏、取消或重置后切换。旧词典无人使用时在后台回收。工作线程仅在有旧会话待释放时每 250 ms 检查一次，其余空闲时间不轮询。

路径不存在、目录/FIFO、超过 64 MiB、非法 UTF-8/TSV/重复项等均拒绝加载。配置工具提交失败不会覆盖磁盘上的旧设置；错误记录在 Fcitx 日志和下一次组合的提示中。手工写坏的配置文件不会被自动改回，但运行中的旧词典仍可使用。程序重启后若配置加载失败，则保留内置演示词典。

## Debian 测试包

在目标发行版原生构建上述模块后，安装 `dpkg-dev` 与 Python ≥3.11，再执行：

```sh
python3 scripts/package_deb.py
python3 scripts/test_deb.py build/packages/*.deb
```

支持原生 Debian amd64/arm64 的打包路径；arm64 尚未实机验证。脚本从实际 ELF 链接关系生成运行依赖，缺少依赖元数据时停止。默认输出到 `build/packages`，已有同名文件不会覆盖；后续构建可传 `--version 0.1.0-2`。默认维护者地址是开发占位值，正式分发前应通过 `--maintainer 'Name <real-address>'` 配置真实维护者。

历史开发包：`build/packages/fcitx5-myswy_0.1.0-1_amd64.deb`，基于 **Debian 13 amd64 / Fcitx 5.1.12**。该包早于本轮共享核心更新；本轮重建和回归了模块，没有覆盖历史包。依赖至少 libc6 2.39、Fcitx Core/Config/Utils 5.1.12、libstdc++6 13.1；不是面向任意 Debian/Ubuntu 版本的通用包。

在匹配的测试桌面，用 `sudo apt install /absolute/path/to/fcitx5-myswy_0.1.0-1_amd64.deb` 安装；从配置工具移除该输入法后，用 `sudo apt remove fcitx5-myswy` 卸载。升级或回滚也通过安装对应版本包执行。包不自动重启 Fcitx，不更改当前输入法或全局环境变量，不删除用户词典和配置。

`test_deb.py` 在空临时根目录实际调用 dpkg，检查安装、重装、升级、回滚、移除和 purge；逐文件校验内容并确认用户配置保留。该文件布局测试有意跳过运行依赖，**不证明依赖安装成功或桌面输入可用**。

## 原型行为

- 每个 InputContext 独立会话；只读词典在同进程内共享。
- 字母全拼、`'` 音节约束、空格/1–9 选词、上下键、退格、Esc、Enter 原文上屏。
- 共享核心支持连续全拼/首拼/声母混输、前后翻页、中间编辑和分段选择；词条覆盖取决于配置的 TSV。默认仍为 98 条示例，常用词需配置本仓库 `data/daily.tsv`。
- 上屏后提供离线联想；Tab 或鼠标确认，普通空格/数字直接交给应用；重置和敏感输入清除上下文与临时偏好。
- 具备 Preedit 能力的应用使用 client preedit，其余使用输入面板；候选位置交给框架。
- 候选可鼠标选中；列表版本变化后旧回调不提交。
- Ctrl/Alt/Super/AltGr 透传；释放事件不重复上屏；ASCII 标点先提交候选，再透传。
- reset/deactivate 清空组合；Password/Sensitive 字段透传并清空状态。
- 达到长度/歧义上限时保留组合并显示提示。

无头测试使用真实 Fcitx InputContext、事件循环和输入面板对象验证事件转换、UTF-8 上屏、会话隔离、陈旧候选、重置/停用、敏感字段及异步词典热切换。它不启动 compositor、GTK/Qt 应用或真正候选窗，不验证焦点事件路由和光标定位。C++ 测试另以 AddressSanitizer/UndefinedBehaviorSanitizer 运行；Rust 静态库及系统库未做 sanitizer 插桩。

已知限制：Linux 配置界面当前只加载严格 TSV，尚未接入 Windows 的搜狗导入/追加界面；未持久保存用户词频，无中文标点转换；实际桌面焦点/导航/协议测试尚未完成。后台加载不等于无限容量：TSV 构建仍有临时内存峰值，磁盘/内核阻塞和正在执行的 Rust 构建不能瞬间取消，关闭插件需要等待工作线程结束。
