# KAWAXP 交接：AX 已接入（2026-09-08 更新）

## 最新进展（优先阅读）

**2026-09-08 深夜：EVENT 真结局线全自动打通（OVER18-HappyEnd3，var4[147]=1）**
- 修复 vm_choose 序号漏洞：var32[18] 原写「显示索引+1」，原引擎写「case 声明序号」
  （被条件隐藏的 case 也占位）。s14:16b5（case 顺序 俊介[cond 105==1]/音の部屋[cond 105==0]/眠る）
  在 105 确定时只显示 2 项，旧代码永远选不中「眠る」→ s16 章节不可达。修复后 s14 选眠る→s16。
- 真结局线旗标链（全实测）：s15 **无条件**置 var4[106]=1 → 需绕开 s15（s14 选「眠る」→s16）；
  s17:3136 两段式（先「かたづける」置 109=1，再「連れて帰る」→ 直跳 s19 不置 110）；
  s19 出口 (106==0 && 110==0) → s22（111 只切对白，不参与出口）；
  s22 置 116=1 → s38:ed2 选「美佐子」→ s39；s39:17b8 选「股間を蹴る」(置127) → over18。
- **var4[113] 是「美佐子配合事件计数」非死亡计数**（7 个 +=1 全在美佐子协作/庇护场景）；
  over18 @0d06 门：113>3 → 18A/HappyEnd3(147)；≤3 → 18B(148)。s22:1da2 选「さちこに注意する」
  提供第 4 计数 → 实测 113=4、147=1、credits.mes 播放 ✓（rt-s22f.log，KAWA_FLAGDUMP 行验证）。
- 完整 KAWA_BRANCH 参数与逐菜单去向见 work/route-progress.md 末节。

用户本轮明确要求完成 AX 视觉，覆盖下文旧交接中暂停 AX 工作的决定。
已新增 runtime/ax.{c,h}、ax_render.{c,h}，接入 20ms 播放、图层 8→1→0 合成、
透明绿/alpha、循环与续播、同步等待，以及 KWS v2 动画快照。详见 AX.md。
旧基类虚表追踪结论已纠正：具体派生类虚表为 0x493358，绘制函数位于
0x409da0 / 0x409e50 / 0x409f00，第四种绘制为原版空函数。

var32[14] **不是动画启用开关**：AX 装载成功时原程序写 0，失败写 1。
先前硬设为 1 导致脚本跳过 Anim.start，已修正。下文旧路线记录涉及该变量的解释需重验。

验证：117 AX 全槽运行共 28,173 次绘制、单元测试、序章暂停时眼睛动画、
槽 3 跨进程恢复、1,200 条对白主线经过 over2，均无 AX 错误。
普通蒙版复制复用原版 alpha 算法；修正保存菜单清空对白、声音选曲菜单消失、
KAWA_BRANCH 显式选项 0 被覆盖、KAWA_AUTOSAVE 忽略槽号的问题。

主机 work/runtime-host2、Switch work/runtime-switch 已重建，产物 outputs/KAWAXP-port/kawaxp.nro。
build-host.sh / build-switch.sh 现在默认包含游戏运行时。音频 F32 未改动。
本轮代码构建通过不代表 AX 已在 Switch 实机确认；请下一轮先实机验证再调整节奏。
接着可完善 PC 消息窗/菜单、MSK 转场/淡出、震屏、原版剧情存档与全路线回归。
详细当前能力与限制已统一到 README.md，避免旧状态相互矛盾。

## 以下为 HARNESS 原交接与路线笔记（保留；冲突处以上文为准）

本文档供 Codex 接手时快速对齐。项目根：`2026-09-07/ba/`（本目录已与主工作区同步）。

## 项目性质
《河原崎家の一族 For Windows XP》(KID AI5 系) → Switch。**复用 AI5-SDL2-SWITCH 的 libai5 仅作解析库**
（subprojects/libai5，vendored 原样）；游戏运行层（VM/画面/音频/存档/菜单）为自研 C runtime，
位于 `outputs/KAWAXP-port/runtime/`（vm.c/frontend.c/audio.c/text_{ttf,ft}.c/kawa.h）。

## 已完成并验证（日志在 ../work/rt-*.log，README.md 有分节）
1. 标题/画廊菜单：Util 37/38/40–44（はじめから/ロード/アルバム/シーン/サウンド/エンディング），
   KAWA_MENU37..46/ROUTE/BRANCH 测试旋钮。
2. 存档：flag0100 原版字节格式（4336B，标记 4-bit @0x540，偶高奇低）；flag 访问器 0x40d9b0/0x40d9f0
   等 RE 出处见 README；Engine 槽位存/读菜单（F5/F9、kind45/46，槽0–39），跨进程恢复验证过。
3. Switch NRO：`kawaxp.nro`（HOMEBREW）可跑，实机已确认 BGM/语音正常（修了 RIFF tag 2字节 bug；
   混音保持 F32；勿改回 S16）。部署说明见 README「Switch 实机部署」。
4. 音频/文本解耦：AWF=RIFF PCM16 自解析(switch)/sndfile(host)；voice=OGG vorbisfile(switch)；
   文本 Switch 用裸 FreeType2（text_ft.c），host 用 SDL_ttf（text_ttf.c）。
5. AX 原先仅有结构解析，本轮已完成实际像素播放；MSK 转场仍待适配。旧分析已被 AX.md 纠正。
6. 结局矩阵：OVER1..18 直接驱动验证全部标记落盘 flag0100（见 ../work/endings-matrix.txt）。
   注意 OVER18 的 147 记录前置 = var4[113]<=3 且 162==0（真路线低重试走到 HappyEnd 18A）。

## 进行中（下一步重点）：EVENT 主线深走 → 真结局（OVER18 HappyEnd/147）
自动化工具：`KAWA_ROUTE=1,0,...`（按序选 kind2 菜单）；`KAWA_BRANCH=脚本[:地址]:选项`（分支，
同脚本多条按序，地址=运行时 MENU 日志里 `MENU sxx.mes:ADDR` 的 ADDR）；MENU 日志已内建。
章节全表见 ../work/route-progress.md。

已打通链：S1→…→S17(两段式: 0x3136 菜单先选3置109=1再选1)→S18→S19→[**需走 S22**]→S23→S26
(**第二菜单 0x1e81 选 2→S28**)→S29→S31(第一菜单0x5549 选1；第二菜单0x5b98 选1)→S35→
**S36→S37→S38**→[需 var4[116]==1]→S39→OVER18。
待逆推的旗标门：
- S19 出口（无菜单，旗标分流）：(106&110==1)→s20；(106&!110)→s21；(!106&110)→s20；
  **(!106 & !110 & 111==1)→s22**（s22 置 var4[116]=1）。
- S38 菜单 0xed2：opt1(var18==1) **且 var4[116]==1** → jump s39；116==0 选1 只置 126 并重试（死循环）。
- 106/110/111 = S1–S18 早期女主好感旗标：**下一步=在这些章节找置位/清零点，拼出 106=0,110=0,111=1**。
- var4[113] 死亡重试计数（S1/5/6/9/10/12/22 ++），到 OVER18 时需 <=3；162 需 0（首次）。

## 关键经验（避免返工）
- 本引擎 **jz 语义 = 表达式为假才跳转**（vm.c case16: if(!ev)跳）。多次反向解读造成返工。
- 分支键的“脚本”忽略大小写与扩展名匹配；地址必须用运行时 MENU 日志里的地址。
- 旧路线对 var32[14] 的解释不再有效：当前按原程序资源加载结果维护，成功为 0；
  不要为了改变分支硬设该变量。S30/S35 等涉及该值的路线说明需要重验。
- 主机构建目录 **work/runtime-host2**（旧的 runtime-host 指向 Codex 旧源路径，勿用）；
  Switch 构建 work/runtime-switch。改完双端都要 ninja + elf2nro 刷新 NRO。
- 未随源码仓库分发的：原版游戏数据（版权归原厂商，仓库内不含）、构建目录、体积大的日志；均可按需重建/拷回。

## 杂项
- 交互本地跑：`./runtime-host2/kawaxp --font reference/fonts/Kosugi-Regular.ttf /path/to/KAWAXP`
  （窗口 960x720，KAWA_W/KAWA_H 可调；smoke 无声音是设计，加 KAWA_AUDIO=1 强制音频）。
