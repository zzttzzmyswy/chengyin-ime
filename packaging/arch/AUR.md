# AUR 发布说明

AUR 包名 `fcitx5-chengyin`（稳定/预览版源码包，从发行 tag 构建），维护者账号 `zzttzzmyswy` 在 AUR 上为 `zzttzz`。
包为**预览版**：`pkgdesc` 如实标注 preview；Fcitx 侧尚未做真实桌面验收（见 `docs/STATUS.md`）。

## 每次发版后同步 AUR

1. 先让对应 tag 存在（`gh release create` 或 `git tag` 推送），再取其归档校验和，连续下载两次必须一致：
   `curl -fsSL --retry 5 https://github.com/zzttzzmyswy/chengyin-ime/archive/refs/tags/v<版本>.tar.gz | sha256sum`
2. 在 `packaging/arch/PKGBUILD` 更新 `pkgver`（与 `version.json` 一致，`python3 scripts/version.py --check` 通过）和 `sha256sums`；`pkgrel` 随新 `pkgver` 重置为 1。
3. 在干净 Arch 环境只装声明的依赖，`makepkg -f` 通过（`check()` 会跑 Fcitx 的 CTest），`namcap PKGBUILD` 与 `namcap <pkg>` 无新增告警。
4. `makepkg --printsrcinfo > .SRCINFO`。
5. 把 `PKGBUILD` 与 `.SRCINFO`（只此两个文件）提交到 `ssh://aur@aur.archlinux.org/fcitx5-chengyin.git` 的 `master` 分支并推送。AUR 提交身份使用 GitHub noreply 地址，不要使用工作邮箱。

## 注意

- `.SRCINFO` 必须与 `PKGBUILD` 同步，AUR 以 `.SRCINFO` 为准显示元数据。
- 词库许可：包内含 Apache-2.0（Rime pinyin-simp）与 MIT（jieba、THUOCL、phrase-pinyin-data）数据，`license` 数组已覆盖；MIT 文本随包装在 `/usr/share/licenses/fcitx5-chengyin/`。
