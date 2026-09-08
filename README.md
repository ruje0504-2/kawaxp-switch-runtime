# 河原崎家の一族 — Switch 移植运行时（kawa by elf）

《河原崎家の一族 For Windows XP》的自研 C 运行时移植，运行于 Nintendo Switch（直装 NSP 或
hbmenu NRO）与桌面主机（SDL2）。**已基本实现原 PC 的全部游戏功能与演出**：完整剧情/分支、
语音/BGM/音效、AX 人物动画、CG 相册、场景回想、声音鉴赏、结局回放、全图解锁、40 槽存读档、
标题/引擎菜单原版 UI，以及 util 系列的转场演出（mask 过渡/震屏/扫掠/渐显等）。

游戏程序为 **kawaxp.nro（0.3.0）**（homebrew，名 `kawa`，作者 `elf`）或直装
**NSP（titleid `0100E6B2B3E50000`，名「河原崎家の一族」，作者 `elf`）**。
kawaxp-diagnostic.nro 仅检查资源，不运行游戏。本工程**不包含原版游戏数据**。

## 部署方式

### 方式 A：NSP 直装（推荐）

完整 NSP 由 `make-nsp.sh` 打包：游戏数据内置在标题 RomFS，存档由系统（HOS SaveData）管理。
将 `0100E6B2B3E50000.nsp` 通过安装器（DBI / Awoo / Tinleaf 等）安装后，直接从主菜单启动。
存档与全图档位于「設定 → データ管理」中该游戏的存档区，无需 SD 卡目录。

- Title ID：`0100E6B2B3E50000`（`0100` + “河原崎家” UTF-8 前 4 字节 `E6B2B3E5` + `0000`）
- 重打包：改代码后 `./make-nsp.sh`（需要 `~/.switch/prod.keys` 与 `~/bin/hacbrewpack`）

### 方式 B：NRO + SD 卡（hbmenu）

将以下文件放到 SD 卡：

```text
switch/
  kawaxp.nro
  KAWAXP/
    mes.ARC
    gcc.ARC
    sequence.ARC
    bgm.AWF
    effect.AWF
    effect2.AWF
    effect3.AWF
    voice.ARC
    Kosugi-Regular.ttf
```

从支持完整内存的 hbmenu 启动。升级时替换 kawaxp.nro，保留游戏数据和 kawaxp-saves 文件夹。

**全图解锁（PC 存档）**：把 PC 原版的全图旗标放到
`sdmc:/switch/KAWAXP/kawaxp-saves/FLAG0100`（旧小写 `flag0100` 也兼容），
启动后标题菜单的 アルバム / シーン / サウンド / エンディング 即全部解锁。

| 操作 | Switch | 主机键盘 |
|---|---|---|
| 推进/确认 | A | Enter / Space |
| 选择 | 方向键 | ↑ / ↓ |
| 存档菜单（对白暂停时） | L | F5 |
| 读档菜单（对白暂停时） | R | F9 |
| 退出 | + | Esc |

画面保持 640×480 的 4:3 比例。BGM/语音/触屏/AX 动画均已在 Switch 实机确认工作。

## 已实现（对应原 PC 功能与演出）

- **完整剧情引擎**：START.MES → 全 90 条 MES 脚本、分支选择、结局 OVER/CREDITS 全通。
- **AX 人物动画**：320 个程序槽、20ms 时钟、矩形绘制、延时、双层/嵌套循环、停止/续播、
  同步等待；第 8 图层 → 第 1 图层 → 屏幕合成，透明绿与原版整数 alpha。
- **演出 util 系列**（对照 AI.exe 反汇编实现）：
  - util24：页面显示/渐显（title_bg 等"黑→亮"）
  - util34：震屏（脚本启停、逐行余弦位移）
  - util35：mask 遮罩转场（原版 .msk 分位 reveal，11 软 alpha 相位 + 终帧）
  - util8：条带扫掠转场（scratch + 上下交错条带）
  - util3/5/6：黑场/白闪/恢复；util48：定时等待；util40：滞留页
- **引擎菜单原版 UI**（原图部件合成 + 正确热区/解锁判定）：
  - util37 标题（はじめから/ロード/サウンド/アルバム/シーン/エンディング，菜单按钮 alpha 淡入）
  - util38 读档、util41 声音鉴赏（14 曲）、util42 结局回想、util43 场景回想（8 页胶片）、
    util44 CG 相册（123 张，含连续再生）、util45/46 存读档槽位面板
  - L/R 翻页、B 返回、十字键网格导航、触屏点选
- **文本/UI 布局**：正文起点 (32,400)、消息窗 (0,376,640,104)、分支选项在窗内无框、
  四行一列超四双列，日文字体渲染。
- **存档**：40 槽 KWS v1/v2（含 AX 现场、全表面画面恢复），对话暂停时才能存读；
  隐藏文字框暂停剧情；标题ロード入口；回放（回想/结局）期间禁止存读。
- **全局旗标**：FLAG0100 原版 4,336 字节格式（5,000 个 4-bit @0x540，偶高奇低），
  与 PC 存档直接兼容（大写命名，读取兼容旧小写）。
- **音频**：BGM ch0 + 语音/SE，Ogg Vorbis Switch 解码线程，F32 混音；回想结束回主界面
  BGM 快照恢复。
- **交互细节**：快进（Y/X）、A 确认、方向键、触屏、隐藏文字框（B）、smoke/自动测试环境变量。

## 构建

主机需要 C 编译器、Meson、Ninja、pkg-config、SDL2、SDL2_ttf、libsndfile、libpng、zlib：

```sh
./build-host.sh
./build-host/kawaxp --font /path/to/Kosugi-Regular.ttf /path/to/KAWAXP
```

Switch 需要 devkitA64、libnx 和 Switch portlibs 中的 SDL2、FreeType、Vorbis、Ogg、libpng、zlib：

```sh
./build-switch.sh        # 产出 kawaxp.nro（名称 kawa、作者 elf、带图标）
./make-nsp.sh            # 产出 0100E6B2B3E50000.nsp（数据进 RomFS，需 prod.keys）
```

## 验证

```sh
meson test -C build-host --print-errorlogs
```

- AX 单元测试、117/117 个 AX 文件全槽运行、序章与主线（1,200 条对白）VM+AX 存档往返通过。
- 演出状态机像素测试（util35 mask 相位 / util34 震屏 / util8 条带）与 16 张原版 .msk 全像素比对通过。
- 90 MES 脚本、349 GCC 解码、8 归档 3,087 项资源索引检查通过。

报告与逆向证据位于 reports/ 与 *-CODEX.md / HANDOFF.md。

## 来源与许可

解析库来自 [AI5-SDL2-SWITCH](https://github.com/ruje0504-2/AI5-SDL2-SWITCH)（提交
94eba0c0009d154aeb13ab2c64f05f98f1f2636d）。subprojects/libai5 保留原版权与许可；
运行层为本项目自研 C 代码（GPL-2.0-or-later）。代码依据本地 AI.exe 的行为分析编写，
不包含原程序代码或游戏资源；游戏数据需自行提供。
