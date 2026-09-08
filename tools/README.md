# kawaxp 汉化工具集（无任何游戏资源）

本目录是《河原崎家の一族》Switch 移植（KAWAXP）**汉化辅助工具**。
**不含任何游戏数据（CG/脚本/语音/字体等原件或成品位图）**，
只含：解析/制作/校验脚本、术语与坐标元数据、翻译层说明。

## text/ —— 文本汉化工具

| 文件 | 用途 |
|---|---|
| `extract_zh.py` | 从 mes.ARC(日)/汉化补丁 ARC 按字节偏移抽 每脚本 TSV(offset<TAB>原文<TAB>补丁)，供人工/批量译。输出 `work/zh-work/*.tsv`。 |
| `asmrange.py` | 小助手：按区间打印 MES/AX 反汇编行，定位某段 op 地址范围。 |
| `flagdump.py` | 输出运行时旗标(flag)快照，帮助梳理路线/解锁条件文本对应。 |

说明：对白译文的成品（`translations/zh_CN.txt` + `kept-jp-zh_CN.txt`）
与运行时翻译层(`runtime/zh.c` / `zh.h`)在本仓库根与 `translations/` —— 属操作文本，
不带原游戏字节。

## image/ —— 图片汉化工具（无成品位图）

`build_images.py` 依据 `制作坐标.json` 在**固定坐标**从 codex 的原始图上合成简体
UI 底板/按钮；`scan-text*.swift` 用系统 Vision OCR 定位图中文字框；
`verify_pixels.py`/`verify-ocr.swift` 做像素级越界与 OCR 修订自检。

元数据（纯文本，无像素）：
- `统一译名.csv`：标题/菜单等术语建议简体。
- `图片汉化清单.csv`：每张图(名/尺寸/优先级/是否需改/校对要点)。
- `ocr-review.json` `ocr-boxes.json`：OCR 校对与文本框。
- `制作坐标.json` `像素验收.json`：合成坐标与像素验收记录。

> 汉化过的 `.png`（成品）与各原图一律**不入库**。

## 其他

根上另有移植本身工具：`tools/inventory.py`(只读校验 ARC/AWF 索引合法性)、
`tools/probe.c`、`tools/switch_main.c`。
运行时图标 `assets/*.png` 为移植程序(nro)图标，非游戏内容。
