# KAWAXP Switch 重构运行时

《河原崎家の一族 For Windows XP》的实验性 Switch / SDL2 运行时。当前可运行剧情、音频、选项、相册及存读档，**AX 人物动画已经实现**。仍未达到原版 PC 的完整重构程度。

游戏程序为 **kawaxp.nro（0.3.0）**。kawaxp-diagnostic.nro 仅检查资源，不运行游戏。本工程不包含原版游戏数据。

## 本轮更新（2026-09-08）

- 新增 AX 播放器：320 个程序槽，20ms 时钟，矩形绘制、延时、双层循环、停止/续播、同步等待。
- 对照 AI.exe 的实际派生类实现第 8 图层 → 第 1 图层 → 屏幕的合成。支持透明绿色和原版整数 alpha 计算；一般遮罩复制也使用相同规则。
- 修正 var32[14]：它是资源加载结果，AX 加载成功为 0；此前硬设 1 会使脚本跳过 Anim.start。
- KWS v2 保存 AX 文件内容、播放位置、延时、循环状态及图层。读档继续当前动画。仍可读取旧 KWS v1；旧存档没有动画状态，需等脚本下一次加载 AX 才恢复动画。旧程序不支持新 v2 存档。
- 保存/取消槽位菜单保留对白；对话暂停时才能打开槽位菜单。修复声音测试选曲后菜单消失，以及自动路线中显式选择 0 被覆盖的问题。
- 构建脚本现在会生成实际游戏运行时及 NRO，保留资源诊断程序。

逆向证据、格式和验证方法见 [AX.md](AX.md)，后续工作见 [HANDOFF.md](HANDOFF.md)。

## Switch 使用

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

从支持完整内存的 hbmenu 启动。升级时替换 kawaxp.nro，保留游戏数据和 kawaxp-saves 文件夹。字体可使用参考项目 fonts/Kosugi-Regular.ttf；随字体分发其许可。

| 操作 | Switch | 主机键盘 |
|---|---|---|
| 推进/确认 | A | Enter / Space |
| 选择 | 方向键 | ↑ / ↓ |
| 存档菜单（对白暂停时） | L | F5 |
| 读档菜单（对白暂停时） | R | F9 |
| 退出 | + | Esc |

保持 640×480 的 4:3 画面比例。存档默认写入游戏目录的 kawaxp-saves/，提供槽 0–39；标题菜单也能进入载入界面。

BGM/语音沿用此前用户实机确认可工作的 F32 混音。AX 动画（眼睛闪动等）已在 Switch 实机确认正常。

## 构建

主机需要 C 编译器、Meson、Ninja、pkg-config、SDL2、SDL2_ttf、libsndfile、libpng、zlib：

```sh
./build-host.sh
./build-host/kawaxp --font /path/to/Kosugi-Regular.ttf /path/to/KAWAXP
```

Switch 需要 devkitA64、libnx 和 Switch portlibs 中的 SDL2、FreeType、Vorbis、Ogg、libpng、zlib。路径配置在 switch-cross.txt；默认 /opt/devkitpro。Switch 不依赖 SDL2_ttf 或 sndfile。

```sh
./build-switch.sh
```

产物为项目根的 kawaxp.nro 和 kawaxp-diagnostic.nro。重复运行构建脚本会增量更新。

## 验证

```sh
meson test -C build-host --print-errorlogs
./build-host/ax-test /path/to/KAWAXP/sequence.ARC
./build-host/kawaxp-probe mes /path/to/KAWAXP/mes.ARC
./build-host/kawaxp-probe cg /path/to/KAWAXP/gcc.ARC
```

本轮记录：

- AX 单元测试：延时边界、续播、同步等待、有限/无限/嵌套循环、快照继续、损坏索引、透明绿、半透明与裁剪通过。
- 117/117 个 AX 文件，每文件启动全部 320 槽并运行 2,000 tick：28,173 次绘制，0 错误。此测试不等于原 PC 全部画面逐像素比较。
- 序章 60 条对白 + 停留等待：实际执行眼睛开合帧，VM + AX 存档往返通过。
- 主线 1,200 条对白：start → s1 → s4 → s5 → s6 → s7 → s11 → over2 后继续运行，无 STOP；18 次剩余震屏近似提示。
- 槽 3 跨进程恢复：对白保持，004.ax 从保存的延时继续播放，覆盖不同 srcY 的眼睛帧。
- 既有资源验证：90 MES、349 GCC 解码、8 归档共 3,087 条索引检查通过。

报告位于 reports/。smoke 默认静音；KAWA_AUDIO=1 可强制音频设备测试。

```sh
SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy \
KAWA_AX_TRACE=1 KAWA_SMOKE_HOLD=200 \
./build-host/kawaxp --smoke 60 --screenshot frame.bmp \
  --save-dir /tmp/kawaxp-test --font /path/to/Kosugi-Regular.ttf /path/to/KAWAXP
```

KAWA_SMOKE_HOLD 在目标对白处额外推进指定数量的 20ms 动画 tick。smoke 使用虚拟动画时间；交互模式使用实际时间。KAWA_ROUTE 指定脚本选项序列；KAWA_BRANCH 支持 脚本[:十六进制地址]:选项；选择索引从 0 开始。KAWA_AUTOSAVE=N:S 在第 N 条对白打开菜单保存到槽 S。KAWA_MENU37/38/41–46 用于引擎菜单自动化。

## 已有功能与未完成部分

已保留 HARNESS 适配的标题、CG 相册、声音/场景/结局回放菜单、全局标记、40 槽存读档、日文文本、语音、音效和 BGM。全局 flag0100 为原版 4,336 字节格式；5,000 个四位标记从 0x540 起，偶数索引使用高半字节。剧情槽使用本运行时 KWS 格式。

后续仍需完善原版菜单及消息窗布局、文字历史、MSK 转场、淡入淡出、震屏、其它专用 Util 语义、原 PC 剧情存档兼容、触屏体验以及全路线和 Switch 实机回归。当前通用菜单与消息窗不是原 PC UI 的像素复刻。不能将当前构建称为全部完成的 PC 重制版。

## 来源与许可

解析库来自 [AI5-SDL2-SWITCH](https://github.com/ruje0504-2/AI5-SDL2-SWITCH)，提交 94eba0c0009d154aeb13ab2c64f05f98f1f2636d。subprojects/libai5 保留原版权与许可，运行层为本项目自研 C 代码（GPL-2.0-or-later）。代码依据本地 AI.exe 的行为分析，不包含原程序代码或游戏资源。
