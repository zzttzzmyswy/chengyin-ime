# 许可说明

本文说明澄音输入法（Chengyin IME）各部分的许可，以及它们之间的关系。许可正文以各
目录下的文件为准；本文只做索引和边界说明，不替代原文。

## 1. 代码：GPL-3.0-or-later

本项目自有代码（`crates/`、`include/`、`platforms/`、`scripts/`、`tests/`）自
**preview29 起**采用 **GNU General Public License v3.0 or later**
（SPDX：`GPL-3.0-or-later`），全文见仓库根目录 [`LICENSE`](../LICENSE)。
首方源码文件头部均带 `SPDX-License-Identifier: GPL-3.0-or-later`。

Cargo workspace 的 `license` 字段、Arch `PKGBUILD` 的 `license=`、Windows 安装器
`LegalCopyright`、Debian/Arch 包内 `copyright`、设置「关于」页与
[`platforms/windows/THIRD_PARTY.md`](../platforms/windows/THIRD_PARTY.md) 均与此一致。

选择 "or-later" 而非 "-only"：便于与要研读的 Apache-2.0 / LGPL / GPL 来源保持兼容，
也让使用者可按 GPLv3 或后续版本任选其一。

## 2. 历史版本不追溯

许可变更**不追溯**已发布版本：

| 版本 | 代码许可 |
| --- | --- |
| preview28 及更早 | MIT（按发布时的 `LICENSE` 授予，持续有效） |
| preview29 起 | GPL-3.0-or-later |

任何人已经按 MIT 取得的 preview28 及更早版本副本，其 MIT 授权继续有效，不受本次变更
影响。GPL-3.0-or-later 只适用于 preview29 及之后的版本。

## 3. 词库与美术素材：独立于代码

**词库和美术素材的许可独立于代码许可**，不随代码改为 GPL 而变化。GPL 只覆盖代码；
这些数据按各自来源的许可分发，其原文保存在 `data/sources/*/LICENSE`。

| 素材 | 许可 | 许可原文 / 署名位置 |
| --- | --- | --- |
| Rime pinyin-simp（词库与音节表） | Apache-2.0 | `data/sources/rime-pinyin-simp/` |
| jieba（常用词与词频） | MIT | `data/sources/jieba/` |
| THUOCL（领域词） | MIT | `data/sources/thuocl/` |
| phrase-pinyin-data（自带读音的词） | MIT | `data/sources/phrase-pinyin-data/` |
| 手写演示词典 `demo.tsv` | MIT | 本仓库，见 `data/README.md` |
| 自写搭配 `associations.tsv` | MIT | 本仓库，见 `data/README.md` |
| TreapGoGo「Q 版大肥鱼」角色形象 | 上游未给出独立图像许可声明 | [`assets/skins/README.md`](../assets/skins/README.md) |

各来源的固定修订、原始文件哈希与转换规则见 [`data/README.md`](../data/README.md)。
Windows 安装包的 `RUNTIME_LICENSES.zip` 同时收录四项词库的 `LICENSE`、`README` 与
`SOURCE.json`，以及角色形象的来源说明。

角色形象不在代码许可范围内：不把上游网站代码的 MIT 许可套用为图片许可，也不把本项目的
GPL 套用到该形象上。详见 `assets/skins/README.md` 与 `THIRD_PARTY.md`。

## 4. 随包第三方组件（Windows）

### 4.1 Rust 标准库与运行时

Rust 标准库静态链接进输入服务。Rust 采用 MIT / Apache-2.0 双许可，两者都与 GPLv3
兼容；`RUNTIME_LICENSES.zip` 保留实际构建工具链的 `COPYRIGHT-library.html` 与
`licenses/`。**结论：兼容，无未决点。**

### 4.2 NSIS 安装器 stub（zlib / LZMA）

安装 EXE 内嵌 NSIS 3.11 的原生 amd64 Unicode stub、System 插件与 LZMA 压缩代码。
NSIS 只是构建期工具和**安装时运行**的代码，正常输入不加载安装器，也不与澄音的
输入服务链接成同一程序。

- **zlib 许可**：宽松许可，与 GPLv3 兼容。
- **LZMA 代码（CPL-1.0）**：NSIS 的 copyright 明确附加例外——"expressly permit you
  to statically or dynamically link your code (or bind by name) to the files from the
  LZMA compression module for NSIS without subjecting your linked code to the terms of
  the Common Public license version 1.0"。
- 即便不依赖上述例外，安装器把 GPL 的输入法与 NSIS stub 放在同一个分发介质里，属于
  GPLv3 **第 5 段**末段定义的 **aggregate**（"Inclusion of a covered work in an aggregate
  does not cause this License to apply to the other parts of the aggregate"）。

完整署名、许可与例外逐字保留在 `packaging/windows/NSIS-LICENSE.txt`，随包为
`INSTALLER_LICENSE.txt`。**结论：兼容，无未决点。**

### 4.3 MSVC C/C++ 静态运行库 —— ⚠ 未决点

MSVC 包把 Microsoft C/C++ 运行库静态链接进二进制（`CMAKE_MSVC_RUNTIME_LIBRARY =
MultiThreaded`，Rust 侧 `-C target-feature=+crt-static`），不随包分发运行库 DLL。
这部分**不由 Microsoft 以自由软件许可提供**，需要单独判断，本任务**不给出确定结论**。

支持"兼容"的论据（GPLv3 文义）：

- 第 1 段把 **System Libraries** 定义为"包含在某个 Major Component 的正常打包形式中、
  但不是该 Major Component 的一部分，且只用于让作品能配合该 Major Component 使用，
  或实现某个已有公开源码实现的标准接口"；"Major Component" 明确包含
  **a compiler used to produce the work**。编译器运行库正落在"随编译器正常打包、只用于
  让作品配合该编译器使用"。
- 第 6 段："A separable portion of the object code, whose source code is excluded
  from the Corresponding Source as a System Library, need not be included in conveying
  the object code work."
- 因此按 GPLv3 文义，MSVC 运行库可作为 System Library 排除在 Corresponding Source 之外，
  静态链接本身不使本项目代码违反 GPL。

**未决点（不臆断，交由法务/维护者决定）：**

1. 上述 System Library 例外是否覆盖**非系统自带**的编译器运行库，FSF 未就澄音这种
   "Windows 上静态链接 MSVC CRT" 的具体情形给出明确答复；该例外通常被理解为面向
   操作系统自带的编译器运行库。
2. Microsoft 自身的再分发条款不是自由软件许可，其条款与 GPLv3 的兼容性没有权威结论。
3. 若要彻底消除该不确定性，可选做法：MSVC 包改为动态链接系统提供的运行库
   （`MultiThreadedDLL`），或改用 MinGW 工具链（GCC 侧有明确的 Runtime Library
   Exception，见 4.4）。**本任务未做该改动**，因为会改变发布产物的构建方式，超出任务卡范围。

**交付结论**：本项按任务卡要求**标记为未决**，不作兼容性断言；其余各项均已确定。

### 4.4 GNU / MinGW 包（如构建）

MinGW 包静态链接 GCC/libstdc++/libgcc、MinGW CRT 与线程运行时。GCC 使用 GPL 与
**GCC Runtime Library Exception**，该例外明确允许符合条件的独立程序使用运行库，并不把
本项目代码改为 GPL。工具链的 copyright 文件（`--runtime-notice` 传入）随包保留。
**结论：兼容，无未决点。**

### 4.5 Windows 系统组件

GDI+、系统 DLL、系统字体只通过系统 API 使用，包内不附带。属于操作系统的一部分，
非本项目分发物。

## 5. 贡献约定

向本项目提交贡献即表示同意该贡献以 **GPL-3.0-or-later** 授权，并确认有权这样做
（例如贡献由本人创作，或已获原权利人许可）。请勿提交与 GPL-3.0-or-later 不兼容的来源代码。

新增或导入任何词库、语料、美术素材时，须同时记录来源与许可，并按第 3 节的表格形式
登记；词库不得混入用户个人词库或个人学习数据。
