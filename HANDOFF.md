# KAWAXP 交接：AX 已接入（2026-09-08 更新）

> **本轮最新：kind43 Scene 已接入原版胶片 UI，并修复 L/R 翻页不重绘。**
> 独立实现 `runtime/scene_ui.c/h`；`extras_ui.c` 按用户要求保持不动，kind44 未改。
> 8 页原图像素比对、40 项悬停 AX、SDL 肩键往返测试通过；Switch 编译通过，待实机确认。
> NRO MD5 `a56a453aaa3eacb4f04f1b9b5cbaa2d8`。详见 `reports/scene-ui-20260908.md`。

## 🎯 给 Codex 的当前任务清单（优先做）

### 0. Codex save_ui 拉取 + 回想 BGM 保留修复（2026-09-08）
- 拉取 Codex 新源码：save_ui.c/h（存读档新 UI：10 槽/页×4 页+メモ/決定/キャンセル/閉じる，kind38/45/46 改走 save_ui）、
  frontend/vm 对应接线、scene_ui 引用、meson 加 save_ui.c。**读档界面是否修好待实机测**。
- BGM 修复：回想シーン(EVENT)不 load ch0；离开回放不再无条件 audio_stop_all()——
  仅当回放期间 load 过 ch0(audio_bgm_dirty)才全停，否则 audio_stop_voice_se()（停 1..4 保 ch0 主界面曲）。
- NRO MD5 `c9a2ec68`，已同步 Codex。

### 0.1. 剧情选项显式选中（补丁：方向键接线漏写）（2026-09-08 修订）
- 上一版替换方向键→menu_move 的补丁因同脚本 assert 失败整体未写入 → 十字键未解除 need_sel，
  绘制全白且只能触摸。已补上键盘 4 向 + DPAD 4 向走 menu_move。
- NRO MD5 `727995c2`，已同步 Codex。待实机：分支首按方向出光标、A 确认、触摸即选。

### 0.1. 剧情选项需显式选中才可确认（2026-09-08）
- kind2 分支菜单打开时无默认选中（need_sel 武装态，绘制全白无金色）；A 在未武装时忽略（防误按跳过首项）；
  首次方向键（上=末项/下=首项/左右=首项）或触摸点选后解除武装，A 才确认。引擎菜单(41-44/37/45/46)不受影响。
- frontend.c：need_sel/menu_move、set_choices 按 kind2 武装、confirm/pointer/方向键接线、kind2 渲染门控。
- NRO MD5 `467c0f14`，已同步 Codex。待实机：首按方向落点（上=末/下=首）手感。

### 0.1. 回放离开即停音（改：不碰主界面 BGM）（2026-09-08 修订）
- 上一版在 util42/43/44 菜单打开时 audio_stop_all() → **误杀主界面/标题 BGM**（用户指正）。
- 已撤销 builder 内停止；改为 vm.c jump()/ret() **离开回放脚本瞬间**停音
  （is_replay_script = ALLPIC/EVENT0X/OVER/CREDITS；仅当跳/返回目标非回放族才停）。
- 效果：回想/结局回放结束回菜单/标题即停其语音+BGM；标题/菜单自身 BGM 不受影响。
- NRO MD5 `0f223c61`，已同步 Codex。待实机：回放结束回菜单无声残留、主界面 BGM 正常。

### 0.1. 回放回菜单即停声音（2026-09-08 第一版，已修订见上）
- audio.c 新增 audio_stop_all()（全 5 通道停播 + 取消 worker 待播 want_play/loading）；
  util42(エンディング)/util43(シーン, scene_menu_open)/util44(相册, vm_album_menu) 打开菜单时调用。
- 效果：回想(EVENT)/结局(OVER)播放结束或中断回到鉴赏菜单瞬间，语音/BGM/SE 立即停止。
- NRO MD5 `a335c836`，已同步 Codex。

### 0.1. 隐藏文字框：剧情暂停 + 不显示快进图标（2026-09-08 补充）
- 上条基础上：hide_msg 期间 ff_hold 的 ">>" 快进角标也不绘制（除非恢复显示）。
  NRO MD5 `726deed1`，已同步 Codex。

### 0.1. 隐藏文字框时剧情暂停推进（2026-09-08）
- B(hide_msg) 隐藏文字框期间：A 确认/快进(Y/X)/自动节拍(waiting==3 到时)一律不推进剧情，
  需再按 B 恢复显示后才继续。frontend.c confirm/ff/等待 gate 三处。NRO 见下。

### 0.1. util35 残留修复：mask==255 像素末步 reveal（2026-09-08 第三轮）
- 现象：转场平滑后仍有一列/簇菱形残留。
- 根因：reveal 条件 `mrow[x]<th`，末步 th=255 时 **==255 的像素永不 reveal**（06.msk 有 930px==255）。
- 修复：改 `<=`，末步全 reveal。NRO MD5 `85f54041`，已同步 Codex。

### 0.1. util35 转场不上屏根因修复（2026-09-08 第二轮）
- 真因：xfade_poll 每步写入 surfaces[0] 但**未置 gfx_dirty** → present 复用旧纹理，
  转场画面不上屏（实机"必须按键才刷新"）。
- 修复：① poll 每步重绘后 gfx_dirty=true；② waiting==3 放行条件加 `!frontend_xfade_active()`
  （util35 wipe 播完才恢复 VM，避免提前被后续绘制覆盖）；导出 frontend_xfade_active()。
- 保留：等像素分位步进（上一轮，06.msk 每步 reveal ~1/12）。NRO MD5 `477e705a`，已同步 Codex。
- util35 像素级仍属 Codex TASK-QUAKE 校准范围。

### 0.1. util35 masked 转场平滑化（2026-09-08 第一轮）
- 现象：回想(EVENT01 @56 util35(6))/转场只看到右侧残留块、"下一帧才刷掉"。
- 根因：mask 06.msk 数值分布不均（值>=128 占 2/3），原线性阈值(step*255/12)前半几乎不动、后半突变。
- 修复：frontend_xfade 步进阈值改为**等像素分位**（每步 reveal ~总/12，实测增量 24-27k/步），过渡平滑。
- 结局(OVER2)开头无 util35（仅 util34/黑场），エンディング→OVER 为直切，非本转场。
- util35 视觉精确性仍属 Codex 像素校准范畴（TASK-QUAKE）。NRO MD5 `53b24128`，已同步 Codex。

### 0.1. OVER 排除细化：仅禁エンディング鉴赏回放（2026-09-08 修订）
- vm.c：新增 `ending_replay_flag`——在 util42(エンディング)菜单选中具体结局（非戻る）时置位，
  vm_step 离开 OVER/CREDITS 脚本时清除；`in_replay_view()` 对 OVER 仅当 flag 为真才判为回放。
- 效果：**真实流程的结局(overXX)允许存读档**；只有从エンディング鉴赏进入的回放 OVER 仍禁止。
- 验证：编译干净、smoke PASS。NRO MD5 `58e716be`，已同步 Codex。

### 0.1. 存读档仅限实际游戏流程（2026-09-08）
- vm.c vm_slot_menu 增加 `in_replay_view()` 闸门：st.waiting==1（对白暂停）仍必须，且
  当前脚本为 ALLPIC(图片鉴赏)/EVENT0X(场景回想)/OVERXX(结局)/CREDITS 时**禁止调出**存/读档
  （L/R、F5/F9 等入口全部被挡）。标题ロード(util38)不经此函数，不受影响。
- 边界说明：OVERXX 也含真实结局演出，结局过程中现亦不可调出存档（如需要结局存档请告知再放开）。
- 验证：编译干净、smoke PASS。NRO MD5 `c2388831`，已同步 Codex。

### 0.1. CG 图片浏览 B 返回 + 导航方向修正（2026-09-08 第二轮）
- **CG 图片浏览**：ALLPIC.MES 播放中（waiting==1）B → 直接 `vm_jump_at("ALLPIC.MES",0x41)` 重开鉴赏菜单
  （不再只是 advance；任何 A/Y/触屏都只在组内翻图，B 才是退出）。vm.c 导出 vm_jump_at。
- **导航方向修正**：无激活项时的"就近跳跃"改为**沿原方向在区内环扫**——左键不再前跳到右侧远端
  （用户例：结局 1 右到 9 后，9 按左能扫回 1）。
- 验证：编译干净、smoke PASS；模拟部分解锁（仅 1/9 解锁）1→9→左回 1 正确。NRO MD5 `dfa328c4`，已同步 Codex。

### 0.1. CG 鉴赏按键 + 导航落点修正（2026-09-08 第一轮）
- **导航落点**：方向轴内无激活项时不再停在边界/原地，改跳**同区（网格区 vs 控制钮区）最近激活项**（向前环搜）。
- **CG 鉴赏播放**：vm_choose44 任何选择（含連続再生 EXTRA_PLAY）var32[9] 恒置 1 → 单张播放结束
  ALLPIC.MES 回菜单，**A/Y 快进都不再链跳到下一已解锁组**；
  B 键在播放中（waiting==1 && ALLPIC.MES）→ var9=1 + advance 返回相册菜单（album_back()），平时仍是 hide_msg。
- 验证：编译干净，smoke+roundtrip PASS，ALLPIC 可达（菜单 waiting=2）。NRO MD5 `5761ad37`，已同步 Codex。
- 待实机：导航跳最近激活手感、CG 播放 A/B/Y 行为。

### 0.1. 导航跳过未解锁项 + LR 翻页（2026-09-08）
- vm_menu_nav 移动后若落点 disabled（41 曲未解锁/42 结局/43 段/44 CG 未激活，extras_enabled=false）
  则继续同方向寻找可用项，整方向无可用则光标不动；几何层拆出 nav_step（行模型不变）。
- LR 肩键：引擎菜单 kind44(相册)/kind43(场景) 打开时 = 翻页（上一页/下一页，复用菜单内 EXTRA_PREV/NEXT 项）；
  其它场合保留原 L=存档菜单/R=读档菜单快捷。
- 验证：host 编译、smoke+roundtrip PASS、场景选段正常；nav 几何单元测试通过（44 sel15↓→19=play 钮、43 sel7↑→2 正确）。
- NRO MD5 `87f7ab6b`；已同步 Codex。待实机：跳过手感、LR 翻页。

### 0.1. 引擎菜单按键：网格导航 + B 返回（2026-09-08）
- frontend.c 新增 `vm_menu_nav(sel,dx,dy)`（按菜单视觉行模型移动，越界 clamp）与 `vm_menu_cancel()`。
  行模型：41=8行×2（14曲+停止/タイトル戻る）；42={4,5,5,5,1}；43={5,3}；44=5行×4（16格+4钮）；2/37/38/45/46=单列。
- 键盘/手柄：UP/DOWN=行内上下（经 nav），LEFT/RIGHT=水平移动（2/37 等单列无效），
  printed B（Switch=A 物理键；host=B）=引擎菜单(41..46/38)返回/关闭（=该菜单戻る/タイトル项），kind2/37 不响应。
- kind45/46/38 取消=~0u(戻る项)→waiting 回 1；41→タイトルに戻る(15)；42/43/44→戻る/タイトル(0)。
- 验证：编译干净，smoke+roundtrip PASS，场景选段正常。NRO MD5 `67378d51`，已同步 Codex。
- 待实机：41 两列/44 四列/42 卡片/43 名牌的左右上下手感、B 返回各界面。

### 0.1. 调试 flag 开关全部移除（2026-09-08 用户要求，含上条 log 关闭）
- frontend.c/audio.c/kawa.h：删除 noax/notext/noaudio/audiosync 的 flag 文件机制与 DBG 打印；
  逻辑还原为正常（AX 恒 tick、文本恒渲染、音频恒异步解码）。NRO MD5 `1b73830d`，已同步 Codex。
- 上条：Switch 不再生成 kawaxp.log（frontend.c 重定向块删除，恢复法见注释）。

### 0.1. 调试 .log 全部关闭（2026-09-08 用户要求）
- frontend.c：删除 Switch 下 stderr→save_dir/kawaxp.log 的重定向块（不再生成任何 .log）。
  stderr 在 Switch 上不接文件；恢复方法见注释（保留 DBG 行示例）。NRO MD5 `fbdb4412`，已同步 Codex。

### 0.1. kind43 シーン：PC 语义分页选择器已实现（2026-09-08 本会话，逆向驱动）
- **PC 逆向结论（AI.exe 跳转表 0x43eefc/0x43ee94）**：util41=0x43eb94、util42=0x43ebe0、util43=0x43ec2c（vm.c 注释已修正）。
- util43(scene) 类 vtable 0x497658；**シーン回想 = 8 事件 × 每事件 5 段（=40 段）**：
  面板页=事件 0..7，页内 5 名牌=段 1..5；段解锁 flag=401+页×5+(段-1)（置位在 S 章节脚本，如 s1@1a0a var4[401]=1）。
  EVENT01..08.MES 开头按 var32[18]==1..5 分段跳转；SCENE.MES 按 var32[20]==0..7 call event0X、[20]==8 退出。
- **实现（runtime 引擎侧）**：util43 菜单改为分页（scene_page 0..7 全局，每页 5 段パート1..5 + 前の/次の/戻る）；
  vm_choose43 输出 var32[20]=页、var32[18]=段(1..5)；prev/next 翻页重建菜单；戻る→[20]=8,[18]=0。
  extras_rect/enabled 增 kind43（名牌 (24+120i,112,112,196)+底部 prev(64,432)/next(228,432)/戻る(452,432)）；
  engine_menu 43 铺 sc_bg 底+名牌+标题"イベント NN / 08"；触摸命中同步。
- **验证（host）**：菜单打开 SCENE page 0；KAWA_MENU43=3 → "SCENE ev0 part4" → event01.mes 段4 地址(@2341) ✓（语义链全通）。
- 名牌仍为**文字占位**（パートN）；PC 名牌部件=sc_pt01..08（2列×3行 每格约560×384，用法未完全解析）→ 后续像素轮替换。
- NRO MD5 `4c7b02df`；vm/frontend/extras_ui 已同步 Codex。KAWA_TEST_EXTRAS 现也置 flag401..440。

### 0.5. kind43 シーン：v1 猜测已撤销 + PC 逆向初步（2026-09-08 本会话）
- 曾做 v1 文字名牌面板（8 项シーン1..8）→ **用户指出需与 PC 一致 → 已撤销恢复 GitHub**（NRO 回 4db39364）。
- 逆向进展（AI.exe，跳转表 0x43eefc/0x43ee94 精确映射）：**util41=0x43eb94、42=0x43ebe0、43=0x43ec2c**（我方 vm.c 注释编号有误，待修）。
- util42(ending) 类 vtable 0x497568，方法 0x4488b0 查 19 项表（0x496b18 起）= 名字条几何 (x=88+120i, y=92+92r, 96×24)、戻る(256,416,128,32) —— **证实 Codex extras kind42 几何正确**。
- **util43(scene) 类 vtable 0x497658，方法 0x44a5a0：PC 版=8 页 × 每页 5 项**，项 flag=0x191(401)+页×5+项（401..440，每页 5），
  底部 prev(64,432)/next(228,432)/play(452,432,124×24)（条件：prev 页>0、next 页<7）；项几何/源表 0x4971b8 起（屏目标）与
  0x4971e0 起（sc_pt 源坐标，sc_pt = 2列×3行 每格约 560×384）。
- **结论**：PC シーン面板=分页 40 条目(flag401-440)，非我方 util43 的 8 项シーン1..8→EVENT01..08 直跳；完整还原需
  ①flag401..440 → EVENT 片段映射 ②翻页状态机 ③sc_pt 贴图；列为专门任务（后续轮次），vm.c util43 语义注释同步修正。

### 0.6. extras UI 误改已撤销（2026-09-08 本会话）
- 背景：kind43 原本走 engine_menu 中央文字 fallback（4 个鉴赏里唯一没面板的）。
- v1：SCENE.MES 已把 sc_bg 装到 surface1 → kind43 绘制改为铺 sc_bg 底 + 2×4 名牌(シーン1..8)+ 戻る，
  名牌文字暂用白/金色 text_at（选中 0xffe9a0），名牌底条深蓝。命中区 extras_rect(43) 同步（触摸/点击可用）。
- 名牌几何：x=90/330(220 宽), y=56+56r(48 高), 戻る (256,408,128,40)。
- **名牌部件图未做**：sc_pt01..08（每 1120×1152，2×8 格 560×144，文件间 ~90% 相同）网格用法未逆向，
  需原版画面参照/截图后再把文字名牌换成 sc_pt 部件。41/42/44 代码未动。
- host 验证：SCENE.MES 截图正常；KAWA_MENU43=3 → event04.mes ✓。NRO MD5 `d77019a6`。

### 0.5. extras UI 误改已撤销（2026-09-08 本会话）
- 曾把 extras_ui.c kind42 命中区 y 按底图实测改为 58/150/242/334（NRO baa461ef）。
- **用户确认该 UI 本身已是校准过的正确版本 → 已 git checkout 恢复 GitHub HEAD 版本**，NRO 回 `4db39364`。
- 教训：extras_ui.c（含 41/42/44 命中区）以 Codex/GitHub 版本为准，勿再凭部件图测量单方面改动。

### 0.5. Switch 固定闪退 — 根因已定位并修复（2026-09-08 本会话，已验证）
- **根因**：audio.c `switch_decode()` Ogg 分支按 `ov_pcm_total` 一次性 malloc、再按 4096 帧块循环读；
  voice.ARC 中 **25 个文件**的 pcm_total 低估实际可解帧数（+144 ~ +192272 帧；S083.OGG total=76744 实际 86744）。
  末块跨写越过 malloc 边界 → 堆损坏 → **固定地点崩溃**（S083 = s26 那句；其余 24 个 = 其它"固定闪退点"）。
  成因：原版语音文件尾部有静音 padding（0.6s 后 -180dB）且第 4 页起每页都错误置 EOS，导致 total 与真实流长不符。
  mac 不崩 = libsndfile 路径不同（host_decode），且它会停在首个 EOS 页（S083 只读 25304 帧=0.57s 人声部分，恰好正确）。
- **修复**：switch_decode 改为**读至 EOF + realloc 按需扩容**（不再信任 pcm_total 为上界），并加 channels 1..8 校验。
- **验证**：host ASan 复现程序（work/dbg_dec.c，镜像 switch_decode）全库扫描 voice.ARC 2170 个 ogg **零越界**；
  双端编译干净，NRO MD5 `4db39364`；host smoke + 存档往返 PASS。
- noax/notext/noaudio/audiosync flag 开关与日志开头 DBG 行保留（诊断用，不影响正常游玩）。
- 遗留观察（待办，不急）：mac host_decode 遇多 EOS 页文件只读首个 EOS 之前内容（S083=0.57s）——与 Switch 现行为(全文件)不一致，仅影响那 25 个文件的尾部静音长度，听感无差，暂不改。

### 1. 引擎存档覆盖修复（2026-09-08，已改）
- noax.flag/notext.flag 双开关实测**都仍崩** → 排除 AX 绘制与文本渲染路径。
- 最大嫌疑 = **Switch 异步音频解码 worker 线程**（audio.c adec_*）：mac 同步解码不崩，正好吻合"仅 Switch 特有"；
  崩点前日志 `AUDIO LOAD ch=4 s083.ogg` 后无 OK=，即崩溃发生在语音解码进行中。
- 新增 flag 开关（data_dir 下空文件）：`noaudio.flag`（完全不解码）、`audiosync.flag`（退同步解码、不起 worker）；
  日志开头新增 `DBG noax=.. notext=.. noaudio=.. audiosync=..` 行以确认开关生效。
- NRO MD5 `8e9d9414`（本会话最后）。等用户实机回报两开关结果后，再定位
  （若 noaudio 生效→查 worker 线程；若 audiosync 生效→查 vorbis/archive 读路径；两开关无效→查 present 其余路径）。

### 1. 引擎存档覆盖修复（2026-09-08，已改）
- **现象**：先生成的存档之后无法覆盖（SAVE FAIL）。原因：`save_state` 写临时文件后
  `rename(tmp,path)` 在 Switch fsdev/FAT（及 Windows）上遇已存在目标会失败。
- **修复**：frontend.c `save_state` —— rename 失败时先 `remove(path)` 再重试一次（原子性让步，旧档删除属预期）。
- 双端已编译，NRO MD5 `9af44d75`，host smoke 30 + SAVE ROUNDTRIP PASS。请 Codex 侧同步 frontend.c 并保持同 MD5。
- 待用户实机验证：同一槽位连续覆盖两次（第一次新建、第二次覆盖）均应显示 Saved。

### 1. 像素级 UI 还原（核心）— 详见 TASKS-TITLE-UI-CODEX.md
标题菜单已按原版反汇编坐标改为部件图合成（见下）；读档/声音测试/结局/场景/相册/槽位仍是**中央纯文字列表占位**，需继续还原：
- 对照原版实机/截图确定各菜单项的**屏幕坐标与样式**；
- 用已解码部件图合成：menu.bmp(840×372 面板)、sl_pt.bmp(656×464 槽位面板)、
  setting.bmp(592×1096)、selparts.bmp(高亮条)、mwaku.bmp(消息窗)（PNG 在 work/cgout/）；
- 标题项已确认来自 title_pt.gcc，按钮 408×36、位置 (116,244+36×行号)，Logo (28,108)。
- ⚠️ **剧情分支(kind2)选项已是"底部消息窗内纯文字、无框"**（用户实机确认原版如此）——
  消息窗相关样式按此方向，勿再画中央框或给分支选项加 selparts 条。

### 2. quake/util35 特效精确还原 — 详见 TASK-QUAKE-UTIL34-CODEX.md
先核验原版像素模型，**不要直接按旧任务单实现 8bit 调色板**：新增逆向证据见 reports/title-ui-20260908.md。
引擎页模型([obj+0x80/84/88])；反汇编循环已给
(0x435c10 / 0x43a0b5)，建议逐行仿真后实机校准。⚠️ 补充：**util8**
(pixel_palette_crossfade 区域渐变，s10/s12 真实触发) 同属此类近似，请一并核验。

### 3. Switch 实机回归 — 用户确认已完成
用户在本轮明确回复“3已经完成”：257eedf / NRO MD5 6a32fcb1 基线的 AX 眼睛动画、
触屏、分支选项实机回归记为完成。这项确认不自动覆盖之后新增的标题 UI / 命中区改动。

### 4. PC 存档兼容 — 已封存待样本（TASKS-PC-SAVE-COMPAT.md）
骨架逆向已留档（SaveData = 30×0x40E 记录等），需真实游玩存档才能继续；当前暂停。

---

## 最新进展（优先阅读）

**2026-09-08 Codex：原版标题部件图接入，用户确认基线实机回归完成**
- 基线 NRO MD5 核对为 `6a32fcb1b106582fd7570f559a5d6072`；修改前备份在 `work/harness-257eedf/`。
- 标题 Logo 与正常/高亮按钮使用 `title_pt.gcc` 原图，不再用字体重绘。
  静态坐标、菜单顺序与解锁条件来自 `44b3a0` / `447430`，不是估算截图。
- 原版顺序：开始、读档（有存档）、声音、相册（flag200..319 有 1）、场景（flag400..439 有 1）、结局（flag152）。
  KWS 存档也可启用读档。`KAWA_MENU37` 仍按可见索引选择，旧额外菜单索引需随该顺序调整。
- 点击区域对应当前菜单；忽略触摸生成的重复鼠标点击和选项外点击。剧情分支仍为底部消息窗内纯文字无框。
- 新增 `KAWA_ENGINESHOT=37` 截标题测试钩子；截图 `work/title-original-parts.png`。
- 本轮未改 quake/util35/util8 的运行行为；已确认 util35 的 +0x44 是独立 alpha 平面，三字节颜色步长不能解释为索引页。
- 标题开场灰 Logo 渐变/按压过渡、其他引擎菜单与精确特效仍待继续；详见 `reports/title-ui-20260908.md`。

**2026-09-08 第五轮补充2：分支选项改为底部消息窗内纯文字（无框）**
- 用户实机确认原版分支选项**就在字幕(消息窗)位置、用同一 mwaku 背景、只有文字无框**。
- 改动：kind2 剧情分支现于底部消息窗内按行列出纯文字（选中行金色、其余白），
  行高随选项数自适应(14-26px)；**移除 selparts 紫框**（此前误用为屏幕中央大列表）。
- 引擎菜单(util37 标题/38 读档/41-44)仍为中央纯文字列表占位（像素面板待 Codex）。
- host 像素验证：两行选项文字在窗内(y~402/428)渲染、窗下半透明露出 CG、无紫框残留。
- NRO：outputs/KAWAXP-port/kawaxp.nro（MD5 6a32fcb1…），已同步 switch-package 与 Codex 镜像。

**2026-09-08 第五轮补充：分支选择菜单换用原版 selparts 高亮条**（已废弃，见上）
- 用户指出分支选项是自绘黑块。已改为**原版部件图**：surfaces[5] 的 selparts.bmp
  （320×68 = 上下两帧 320×32 圆角高亮条，紫描边+镂空文字区）做每项背景，
  选中项用第 2 帧(y35-66)+金色文字，其余第 1 帧(y1-32)+白字；居中于 (640-320)/2，
  起点 y=120、每项 34px。新增 blit_keyed()（跳过 AI5 绿/品红键控色）。
- 鼠标/触摸命中区同步 (y-120)/34。host 截图验证框+文字已上屏（work/menu-selparts.bmp）。
- 工具：KAWA_MENUSHOT=<n> 在 smoke 第 n 个 kind2 菜单截图（仍可用）。
- **待 Codex 像素校准**：条目横排起点/间距、选中帧是否确为 frame2、文字基线、是否需
  标题条/页眉——仍属 TASKS-TITLE-UI-CODEX 像素对照范围。截图 work/menu-selparts.bmp。
- NRO：outputs/KAWAXP-port/kawaxp.nro（MD5 417ae164…），switch-package 与 Codex 镜像已同步。

**2026-09-08 第五轮：Switch 触屏 + crossfade 核查**
- **Switch 触屏**：SDL_FINGERDOWN 归一化坐标→640×480；普通对话点按=推进，选择菜单点按选项=选中+确认。
- **crossfade 核查结论**：0x2b/0x2c 语句（CROSSFADE/CROSSFADE2）在全部 90 脚本 **0 出现**
  （全脚本扫描 65801 语句），unsupported 分支不可达（实测多路线 unsupported=0）。
  mes-dump 里的 "pixel_palette_crossfade" 实为 **util 8**（区域像素渐变，参数 x,y,w,h），
  已有近似实现(case8 masked copy)，s10/s12 实测触发但无错；视觉精确性列入 quake/util35
  同族 Codex 像素核验范围（TASK-QUAKE 单内补充说明）。
- NRO：outputs/KAWAXP-port/kawaxp.nro（MD5 38d261be…），switch-package 与 Codex 镜像已同步。

**2026-09-08 备案：PC 存档兼容 → 已封存，待真实存档样本再续**
- 详见 **TASKS-PC-SAVE-COMPAT.md**（两个工作区均有）。方向=PC 原版存档→Switch 续玩。
- 骨架逆向已完成并留档：SaveData%04d = 30×0x40E 记录 + 尾部 4B；每条记录 = 一个已加载
  资源/场景登记项（rec0=CG "001b.gcc"、rec1=BGM "yokan.wav" 已从空壳破译）；flag%04d(0x10F0B)
  为另一套。I/O/管理类/记录集合架构代码路径已全部定位。
- **暂停原因**：本地样本（SaveData0001-40、flag0024-50）全是出厂空壳，无真实剧情进度；
  字段语义（type 全集/坐标/句柄/AX 状态）无法回验。待用户提供一份 PC 真实游玩存档再续。
- 本引擎自身存档（KWS v1/v2、40 槽、F5/F9、roundtrip 测试）**早已实现**，不受影响。

**2026-09-08 第四轮补充：FRAME 诊断日志默认关闭**
- 实机复测流畅度已达标（多核改造生效），用户要求关闭周期 FRAME 日志。
- 改动：frontend.c 的 FRAME avg/max 行改为默认不打印，仅设 **KAWA_FRAMELOG=1** 时输出
  （保留 frames 计数，KAWA_FRAMES 截图上限/smoke 帧数不受影响）。
- NRO：outputs/KAWAXP-port/kawaxp.nro（MD5 26527296…），switch-package 与 Codex 镜像已同步。

**2026-09-08 第四轮：多核优化（主线程绑核 + 音频解码工作线程 + 字形缓存）**
- 起因：Switch 实机观测到**引擎纯单线程挤在一个核**（游戏所在核满载、HOS 占少量、
  #1/#2 几乎全空）；FRAME 日志每个换行处 max=140-324ms（语音整段解码+消息文字整行
  FreeType 双次栅格化同步阻塞在 VM 线程），demo 段 avg 69-109ms。
- 改动（runtime/）：
  1. **主线程绑核**（frontend.c，__SWITCH__）：默认 prefer core 1、可迁移
     （KAWA_CORE_MAIN=<0..3> 强制单核）；启动日志新增 `CORE main pref=.. mask=.. cur=..`
     可直接核对是否离开 HOS 核。
  2. **音频解码工作线程**（audio.c，__SWITCH__）：语音/BGM/SE 整段解码从 VM 线程
     移到钉在 core 2 的 worker（KAWA_CORE_AUDIO 可改；KAWA_AUDIOSYNC=1 回退同步）。
     audio_load 只入队（每声道 latest-wins + 代次防过期安装），解码完成在设备锁内
     装填；audio_play 若数据未就绪则记 want_play、解码落地即自动开播。锁序恒为
     SDL 设备锁 → adec_mu，避免死锁。host 端行为不变（仍同步，sndfile 快）。
  3. **FreeType 字形缓存**（text_ft.c）：按 codepoint 缓存灰度字形位图+度量，
     阴影/彩色两 pass 共享，重复汉字不再反复 FT_LOAD_RENDER。逐像素等价性已用
     host 单元测试验证（TEXT_CACHE_OK）。原版代码假设 FT pitch==width 的隐患
     已顺带修正（按 pitch 逐行拷贝）。
- 验证：host smoke 8/12/35 条消息 + 存档往返 PASS；双端 ninja 编译干净。
- NRO：outputs/KAWAXP-port/kawaxp.nro（MD5 2d11875a…），switch-package 与 Codex 镜像已同步。
- **待用户实机复测**：FRAME 行的 max 尖峰应大幅回落、demo 段 avg 应接近 16ms 级别；
  日志首行附近会多 `CORE` 与 `AUDIO worker started on core N` 两行，可确认多核生效。

**2026-09-08 交接：quake/mask 特效精确还原 → Codex**
- 详见 **TASK-QUAKE-UTIL34-CODEX.md**（两工作区均有）。原版特效跑在 8bit 索引缓冲+调色板、
  页模型未定；DeepSeek 已做多版 RGB 近似与完整反汇编证据整理，交付 Codex 实现并实机核验。
- 用户最新观察（PC 流程视频）：quake 点为“灰底 + 类似烟雾的缓慢移动、前画面 0% 可见”。

**2026-09-08 备案：标题/引擎菜单像素 UI → 移交 Codex 实现**
- 前置调研完成（部件图已解码 PNG、流程/坐标线索、引擎分发地址），任务单见
  **TASKS-TITLE-UI-CODEX.md**（两个工作区均有）。原因：像素级对照需目视原版，DeepSeek 模型无图像输入。
- 本轮工具：KAWA_FRAMES 非 smoke 也生效（截图帧上限）。

**2026-09-08 第三轮：震屏(Util 34) + 定时等待(Util 48) 实现；MSK 调研结论**
- Util 34 = 屏幕震动：参数全脚本仅 {0,1}，1 开启/0 停止。实现 frontend.c `frontend_quake()`：
  合成时 ±(3..5)px 伪随机偏移，3 秒自限防忘关。真结局线 RESULT unsupported 由 59 → **0**。
- Util 46/48 拆分：46=点击推进（CG 相册），48=定时等待（参数=1/20s ticks；CREDITS 25 处换页节奏）。
  CREDITS 非交互实测：end_staff00..12 每页 ~3s 自动推进 → 跳回 start.mes ✓。
- MSK：sequence.ARC 16 个 .msk = 640×480×1 逐像素「切换时刻」图（对角线/百叶窗/棋盘 wipe 素材）。
  AI.exe 仅在 0x439b75/0x43a0b5 两处 sprintf("%02u.msk")（vtable 间接调用的 wipe 合成器），
  **全部 90 个 MES 无任何 .msk/转场 util 引用** → 可达剧情不触发 msk wipe，暂不实现（详见 ../work/msk-note.md）。
- 运行时 util trace 旋钮 KAWA_UTILTRACE；震屏测试旋钮 KAWA_QUAKE=1。

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
