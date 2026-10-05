# 开发环境与复现

2026-10-05。本阶段最终交付为 Windows x64 preview16；67 项 Rust 测试、C ABI 和七项原生 CTest 已通过。
本机为 Windows 11 build 26200 / Ryzen 9 9950X 16C/32T，当前构建环境：

- VS 2022 Build Tools 17.14.41，MSVC 19.44.35229 / 14.44.35207；SDK 10.0.26100.0。
- Rust 1.99.0 x86_64-pc-windows-msvc，rustfmt/clippy/rust-docs；静态核心用 +crt-static。
- CMake/CTest 4.4.3；NSIS 3.11 portable + 仓库 prepare_windows_nsis.py 的固定 AMD64 组件。
- 官方 NSIS 3.11 ZIP SHA-256：c7d27f780ddb6cffb4730138cd1591e841f4b7edb155856901cdf5f214394fa1。
  来源 https://sourceforge.net/projects/nsis/files/NSIS%203/3.11/nsis-3.11.zip/download 。
- AMD64 组件来源、哈希和许可由准备助手写入 CHENGYIN_AMD64_COMPONENTS.json，随包收入许可 ZIP。
- Rustup、VS、CMake 通过官方 Windows 安装/winget 配置；Python 使用 Codex bundled runtime。

```powershell
$env:PATH="C:\Users\xhxez\.cargo\bin;C:\Program Files\CMake\bin;" + $env:PATH
$env:RUSTFLAGS='-C target-feature=+crt-static'
cargo build --release -p myswy-ffi --target x86_64-pc-windows-msvc --locked
cmake -S platforms/windows -B build/windows-msvc -G 'Visual Studio 17 2022' -A x64
cmake --build build/windows-msvc --config Release --parallel 3 --target myswy_tsf myswy_settings myswy_probe windows_ui_test windows_key_test windows_settings_test windows_update_test windows_live_test myswy_tsf_fixture
ctest --test-dir build/windows-msvc -C Release --output-on-failure
python scripts/package_windows.py --build-dir build/windows-msvc --toolchain msvc --makensis build/nsis-amd64/makensis.exe --nsis-notice packaging/windows/NSIS-LICENSE.txt
```

临时脚本、旧安装包和旧截图已归档到忽略的 build/archive/，最新安装包及 preview16 证据仍保留。
NSIS AMD64 与 portable 工具目录、最终 Rust release 和 Windows 原生构建目录保留，debug 缓存已由 cargo clean --profile dev 清理，可重新生成。
windows_desktop_test 是可选的开发者桌面驱动，仅显式指定该 target 时构建；不随默认 ALL 或安装包交付。
核心复查：cargo fmt --all -- --check；cargo clippy --workspace --all-targets --locked -- -D warnings；cargo test --workspace --locked。

已有安装和个人学习数据不参与回归 fixture。安装生命周期仅在隔离 runner 运行；
真实应用矩阵、物理混合 DPI 多屏和本批远程 CI 未完成，不能用模拟宿主测试代替。
历史云环境、NSIS 固定来源及历次工具链详见 [环境归档](history/ENVIRONMENT_THROUGH_PREVIEW16.md)。
清理保留项及本地分支状态见 [结项记录](WINDOWS_PHASE_WRAPUP.md)。
