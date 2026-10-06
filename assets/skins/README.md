# 澄音皮肤插画来源

用户于 2026-10-06 指定 [TreapGoGo 鲸鱼娘资产库](https://treapgogo.github.io/deepseek-whale-girl/)，
并明确授权本地裁切、去背景。preview22 的“Q 版大肥鱼”采用首页设定图左上角正面全身形象。
保留发箍、围裙、鲸尾、鞋袜和姿态，不重新生成或重新设计角色。

- 原图：https://treapgogo.github.io/deepseek-whale-girl/assets/dafeiyu-chibi-character-sheet.png
- 上游：[TreapGoGo/deepseek-whale-girl](https://github.com/TreapGoGo/deepseek-whale-girl)
- 获取日期：2026-10-06
- 原图 SHA-256：`539548dbdf78a6488b9baca3af07521fe422c62bd8dee6e5664a1880754325b6`
- 处理：原图左上 `(0,0,543,622)`，连通背景去白底、去地面阴影；仅背景邻接边缘去白色衬底，内部白色服饰保留。
- `whale-girl-source.png`：526 × 600，RGBA，透明裁切结果，SHA-256 `7982b10823fc624fa90b36c933189a4831d8beb06eb27325ad6766aaf56df700`。
- `whale-girl.png`：449 × 512，RGBA，Lanczos 缩放部署图，SHA-256 `f98a79d4c063c94ca3a4279fd687a62051dccaa8c30d6c14ef3c9febab946364`。

本地处理脚本与深色背景核对图分别保存在本批构建证据 `build/extract_q_dafeiyu.py`、
`build/q-dafeiyu-matte-preview.png`。内置 image_gen 多次网络失败，没有生成图作为最终资源。

上游 README 区分网站 MIT 代码、CC BY-SA 4.0 文字/提示词及图片的单独许可。
读取的设定图位置未给出独立图像许可声明，不将网站代码 MIT 许可误标为图片许可。
本项目保留来源署名，不主张原始素材权利。DeepSeek 名称说明社区二创形象，非官方认可；
第三方名称/商标不适用项目代码许可。青瓷枝叶、夜航星图及波纹为项目内几何绘制。

preview21 使用的网站透明头像已由本批全身形象替换，主题 ID 12 保持兼容。
