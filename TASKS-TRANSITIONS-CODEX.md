# 过场/转场效果还原 — 反汇编任务单（2026-09-08，给 Codex）

> 背景：用户要求按 PC 原版还原所有过场效果。本单为 DeepSeek 侧反汇编 AI.exe 的结果，
> **代码未改**，由 Codex 按此实现/校准。当前实现参考 runtime/vm.c + frontend.c。
> 素材：/Users/rujian/Documents/DeepSeek/KAWAXP/AI.exe；反汇编文本 work/ai.asm、util.asm、util-core.asm；
> 工具 cd work && python3 asmrange.py <addr>；真实 mask sequence.ARC/%02u.MSK（640×480×1B）。

## util35 mask 转场（分析完成·完整版）

结构：util 分发 0x43e6a0（[0x50010]=util号→0x43eefc→跳表 0x43ee94）→ util35→case#10→**0x43eaec**（8 指令）
→ thunk 0x402eb4 → **核心 0x43a0a0**（7 参：0,0,0x280,0x1e0,mask号,0,0；0x43a0b5 只是其 sub esp）。
mask 号=脚本第 2 参 `Util.function[35](N,0)`，N=0..15。兄弟 0x439b60（6 参版，%02u.msk）只被 0x44b559（相册）调用，**不属于 util35**。
页面模型：[state+0x53154]+0x80=页 X 显示页、+0x84=页 Y 工作页（像素=Y+0x44、pitch=Y+0x8、高=Y+0xc）；
0x401aff→0x40a190=DDraw flip。

原版算法：
```
mask=load("%02u.msk",N); Y=page84; clr(Y,0,0,640,480);
if(arg6!=0) goto FINAL;                    // util35 恒 0 → 走相位
for(L=0x20; L<0x180; L+=0x20){             // 0x20..0x160，11 轮
  for y,x: m=mask[y*640+x];
    if(m<L){ v=L-m+0x10; if(v>0xff)v=0xff; Y[y][x]=v; }   // m>=L 保持原值(0)
  X.blit(Y); flip();                       // 每相位 1 次上屏+flip
  if(keyhit(0x200)&&this[0x53164]) goto FINAL;   // 按键跳过
  if(this[0x6df04]==1) goto FINAL;               // 跳过动画标志
}
FINAL: X.blit(新); [X+0x2a4]==1 时补 blit; [0x50c04]=1;
```
要点：等 **mask 值 0x20 步进**（非等像素分位）；**软波带**：m<L 像素写 v=L-m+0x10（首相位 ~0x11..0x30），
已揭像素每相位**重扫续增 +0x20**，约 7-8 相位爬满 0xff；m 小者先切（方向=现行一致）；
**无 sleep**（节拍=CPU+flip）；跳过=输入 bit0x200+[0x53164] 或 [0x6df04]==1，随时跳 FINAL（11 上屏+1 收尾≈12 次显示）。

当前差距（frontend_xfade）：① 分位阈值 vs 等值 0x20；② 硬切 vs 软波带（已揭像素续增）；③ 纯黑特判 hack
（frontend surface1 采样跳过）无原版依据；④ 完成/跳过结构（现行 12 帧+600ms）；⑤ Y 页 8bpp→调色板映射静态未定。

修复要点（C 伪码）：
```c
static void xf_apply(unsigned L){  /* L=0x20*step */
  for(y<480)for(x<640){ int m=msk[y*640+x];
    if(m>=L) continue;
    int v=L-m+0x10; if(v>255)v=255;
    px(x,y)=lerp(from,to, v/255.0); } }
/* poll: step1..11 → xf_apply(0x20*step); step12 → FINAL dst=to 全量、清态;
 * skip(按键/快进)任意 step → FINAL；移除纯黑分支；20ms/步仅作帧率稳定 */
```
待 Codex 实机核验：① 收尾时 m∈[0xe0..0xff] 像素是否由 util35 返回后的正常页面切换补齐
（补齐则 FINAL 全新可复刻）；② Y 值→old/new 调色板换算方向（v 高=新?）；③ 12 次显示动作是否吻合 PC 帧数。
真实 .msk 值域 0..255 全满（16 个，distinct=256），支持"高值像素靠收尾补齐"。

## util34 quake/震屏（分析完成；先纠正旧记录）

- 纠正：旧记 "0x43ead9→0x435c10 quake 核心 / 8×8 瓦片灰度" **错**。util34 handler 0x43ead9 只调
  thunk 0x402c9d→**0x43e590**；0x435c10（8px 瓦片拷贝）属 util6(0x43e8f5)，与 quake 无关。无灰度/调色板。
- 原版：`util34(arg0)`，arg=1 启动 / 0 停（脚本 204 停 + 196 启配对，无幅度参数——全游戏同一组参数）。
  启动：填 480 项波表 `wave[c]=lround(100*cos(2π/240 * c))`（双余弦波，值域 ±100）；置 q_act=1；注册 80ms 周期 timer。
  tick（每 80ms）：门控位(0x6ebc0..cc 其它效果重绘中)则跳过；否则**把 B 页逐行水平位移拷到 A 页**：
  相位 idx（每 tick +479 ≡ −1 mod 480 → 慢漂移），dx=wave[idx]/6（±16px），dx>=0 右移回绕、dx<0 左移；
  479 行后 present 上屏。结束=脚本 util34(0)（撤 timer+清 act）；复位流程也停；无超时。
- 当前差距（frontend_quake/quake_render 灰度滚动近似）：① 灰度化（原版无）；② 恒定滚动 vs 余弦逐行形变
  （波长 240px、±16px、80ms/相位漂 1 采样）；③ 3 秒硬自限 vs 脚本启停；④ 页轮换 A/B 缺失。
- 修复要点：见下伪码（frontend 替换灰度近似；页映射与观感两点需实机定）。
```c
/* 状态 wave[480], quake_act, quake_idx; 每帧驱动≈80ms */
void frontend_quake(unsigned level){ /* vm case34 已传 arg0 */
  if(level==1){ if(!quake_act) for(c=0..479) wave[c]=lround(100.0*cos(0.02617993833*c));
                 quake_idx=0; quake_act=1; }
  else quake_act=0;
}
void quake_frame(void){
  if(!quake_act) return;
  /* 转场/整屏效果进行中则 return（端口用 xf.on/mf_on 同门） */
  dst=A页, src=B页;            /* 32bpp 自行换算；A/B=surfaces 双页映射实机定 */
  for(r=0;r<479;r++){ if(quake_idx>=480)quake_idx=0; int dx=wave[quake_idx++]/6;
    逐行: dx>=0 右移回绕; dx<0 左移; }
  present;
}
```
- 待实机：cos 形变观感是否为原版"抖动"；页 A/B 与 surfaces[0]/[1] 映射。

## util24 anim_wait（分析完成·完整版）

分发：util号24 → 0x43e6a0（表0x43eefc→跳表0x43ee94）→ case **0x43eacd** → thunk 0x4017a8 →
**0x43e010**（mode 分派，arg1=this+0x50054；mode==7 特例）→ 0x401659 → **0x43d3e0**（9 参 ret 0x24，跳表 0x43d590）。
参数槽：argN 在 this+0x50010+0x44*N（string 就地 0 结尾，解析器 0x40e8c0）。
mode1 = 等到单位空闲（0x44fbd0）；mode4 = 构造 0x40e 记录 {unit u16; file[0x400]; 6×u16 @+0x402 = arg4..9}
下发到任务容器 this+0x56100（vector<0x40e>，索引=unit）。
页面/单位语义（语料+实跑）：unit 0/1/2=.gcc CG 槽（flag 1 渐显）；8=.ax 特效；9=NNNan.gcc 叠加层；
10/19=""=纯等待 AX 播完（CREDITS end_staff）；14=voice；15/16/17=SE。
渐显常量（0x43e788）：每帧亮度 +[this+0x6df00]/2，循环到 0x7f 转终帧 0xff；跳过=输入 bit0x200 && [0x53164]==1
或 [0x6df04]==1；0x40134d(period 1/20s, 0x40eed0)=按键询问。

当前差距：① mode1 被做成 no-op（应阻塞等待单位空闲——影响节律）；
② 渐显触发过宽：现 mode4&&par4==1 一律 fadein，纯等待(4,10/19,"",0/1)与 unit8/9 flag=0 不应渐显；
③ "12 步 20ms"近似 vs 原版每帧 +[0x6df00]/2、0→127→255。
未最终确认：mode1 阻塞是解释器重入还是单次调用；[0x6df00] 默认值/真实时长（查单位槽逐帧 tick
0x43f6c0/0x440078/0x44fbd0 区）+实机定标。

修复要点（C 伪码）：
```c
/* case24 mode1: 阻塞直到对应单位空闲(以 1/20s 节拍驱动任务槽) —— 若槽在忙则 st.waiting=1/3 挂 VM，
 * 槽完成信号由逐帧 tick(0x43f6c0 系)置位后放行 */
/* case24 mode4: 按 unit 分发：
 *   unit0/1/2(.gcc) flag=1 → fadein 帧步进模型 v += [0x6df00]/2; v<0x7f 循环、终帧 0xff；
 *        flag=0 → 直接显示(整帧 blit)；
 *   unit8(.ax)/9(叠加层) → 装载/叠加，不渐显；
 *   unit10/19("") → 纯等 AX 播完(waiting 等动画)；
 *   unit14/15/16/17 → voice/SE(audio) */
/* fadein 跳过=输入或 [0x6df04]==1 → 直接 0xff 收尾 */
```
待实机：mode1 等待时序观感、[0x6df00] 初值定标（估 16-32 → 每帧 +8..16、全程 ~15-30 帧）。

---

## util8 pixel_palette_crossfade（分析完成·完整版）

派发：op=8 → LUT[0x43eefc+7] → 跳表 0x43ee94[7]=**0x43e933**（0x43e933..0x43eac8，出口 0x43e819=0x401aff 全屏 invalidate）。
实参槽（0x40e8c0，槽距 0x44）：x=dword[0x50054]、y=dword[0x50098]、w=dword[0x500dc]、h=dword[0x50120]。
页面：eng=[interp+0x53154]；eng+0x80+4i 表面表：surf0=显示页、surf1=暂存新帧、surf7=scratch。

原版算法（一次调用内同步执行，内部用 0x40134d 让步）：
```
A: surf7 ← surf0 全屏快照   (vt+0x1c, 0,0,0,0, 640,480)
B: surf7 ← surf1 区域       (vt+0x3c, x,y,x,y, w,h)
if(h/2==0) goto FINAL
rowT=y; loopX=y+h-1;
for(i=0;i<h/2;i++){
  t0=tick();
  C: surf0 ← surf7 (x,rowT, w,1)          // 顶行
  D: surf0 ← surf7 (x,loopX, w,1)         // 底行
  notify_dirty(x,rowT,x+w,rowT+1); notify_dirty(x,loopX,x+w,loopX+1);
  // 时间闸: target=t0+1; while(now<target){ if(0x40134d(0x200,0)==1)break; if([0x6df04]==1)break; }
  rowT+=2; loopX-=2;
}
FINAL: E: surf0 ← surf1 (x,y,x,y,w,h) 全区域提交
```
要点：**从上下边缘向中央、每步各 2px 高的条带收敛**（步数 h/2）；每步两行宽 w 高 1；
快进仅 [0x6df04]==1（去掉帧间延时但仍跑完 h/2 步，无点按跳尾）；
h==0（(0,0,0,0)）→ 无扫掠仅 A/B/E（0 尺寸 vt+0x3c 语义待实机）。

当前差距（vm case8=copy_rect 单次掩码拷贝≈E-only）：无 surf7 快照/scratch(A/B)、无 h/2 步
上下条带扫掠、无每步脏区/帧等待/快进、h==0 未区分。

修复要点（C 伪码，仿 frontend_xfade/fadein 挂法）：
```c
/* vm.c case8: x,y,w,h; frontend_ppf_start() 真 → st.waiting=3 逐帧 poll */
/* 每帧(≈原版每步): 已 A/B 后, i 步: 拷 surf7 行 y+2i 与 y+h-1-2i(各 w×1) 到 surf0, 标脏;
 * i++ 至 n=h/2 或 h==0 → E: surf1 全区域提交; 结束清态 */
/* 快进/输入: 每帧照做 i++ 直至完成(去延时), 即 [0x6df04] 路径 */
```
待 Codex 校准：① surface 类真实 vtable（寻路：surface_copy/masked 语句处理器 0x410e90/0x410f80/0x4110b0
分别调 vt+0x1c/+0x30/+0x3c；vt+0x1c=行/区域直拷、vt+0x3c=掩码/alpha 提交为推断，p1=src/this=dst 假定）；
② 0x40134d 帧等待的绝对时长单位；③ (0,0,0,0) 时 E 是否整页。实机对拍：总帧数 h/2+1、条带由上下向中心、每帧 2 行。

---

## util3 update_schedule / util5 load_stored_palette / util6 photo_slide（分析完成）

共享前提：eng=[interp+0x53154]；canvas=[eng+0x80]、blt/buf84=[eng+0x84]、页B=[eng+0x88]；
[this+0x6df00]=渐变步长（设定点 0x438910→0x10、0x438c87→0x40 或计算值）；
[this+0x6df04]==1=跳过渐变；[this+0x50c04]=调度状态；canvas+0x2a4=页面忙碌；
区域 [this+0x50bd0..dc]=(x,y,w,h)，由 0x411450 写入。
原语：wait 0x40134d→0x40eed0(limit,mask：mask&vt+0x28 && [0x53164]→中止；否则让出 1 帧；>limit*20ms 返回)；
present 0x401aff→0x40a190；渐变循环统一 (limit=0,mask=0x200) 即每步让出 1 帧。

### util3 update_schedule @0x43e71e（mode=arg0: 0/1/2；脚本 0×337、1×267、2×3(OVER6/10/15 白闪)）
- mode0 @0x435780（新画面合成前黑场）：canvas 忙碌置位；for(lvl=255;lvl>0;lvl-=fade_step){wait/跳过检查;
  c.vt[0x34](0,0,0,lvl);present();} 尾 lvl=0 present；状态清。
- mode2 @0x435870（白闪，OVER6/10/15）：同构，vt[0x34](255,255,255,lvl) 白。
- mode1 @0x4355f0（合成后恢复显示，按 canvas[0x2a4] 分支）：
  busy 分支：for(lvl=0;lvl<0x7f;lvl+=fade_step/2){wait/跳;c.vt[0x58](lvl);present();} 尾 0xff+双 present；busy 清；
  否则：for 同上 {c.vt[0x2c](buf84,0,0,640,480,lvl);present();} 尾 0xff；状态=1。
- mode3/4（0x435ac0/0x435970，脚本不调用，占位即可）。
- 当前差距：case3 仅 a==1 时 frontend_present——缺 0/1/2 三模式渐变引擎（每步画帧+present+让出 1 帧、
  fade_step 与跳过、busy/状态位）。

### util5 load_stored_palette @0x43e83d（无参，246 处调用）
区域来自 [0x50bd0..dc]；for(lvl=0;lvl<0x7f;lvl+=fade_step){wait/跳过检查;c.vt[0x2c](buf84,x,y,w,h,lvl);present();}
尾 vt[0x2c](...,0xff);present() → **区域内内容从黑逐帧刷到全亮**。
当前差距：case5=全屏 copy_rect——缺区域源、0→0x7f→0xff 分级 + 每步 present + 一帧同步 + [0x6df04] 中断。
与 util4 差别：util4 固定整屏、step=fade_step/2；util5 区域化、step=fade_step。

### util6 photo_slide @0x43e8f5（脚本未用）
一次 0x40121c→0x435c10：1920(=0x780)×480 大块从 [eng+0x88] 静默拷到 [eng+0x80]（无渐变无 present）。
当前差距：case6=全屏 copy_rect 近似，应改为 1920×480 静默拷贝。

### 修复要点（C 伪码）
```c
/* util3(a): a==0 黑场/ a==2 白闪: for(lvl=255;lvl>0;lvl-=step){if(poll_skip())break;
 *   tint_rect(canvas,0,0,640,480, a==0?0:255, ...,lvl); present;} tint lvl=0; present; state=0;
 * a==1: for(lvl=0;lvl<127;lvl+=step/2){if(poll_skip())break; draw_buf(canvas,buf84,0,0,640,480,lvl);present;}
 *   draw_buf(...,0xff); present; state=1;  // poll_skip=检查跳过并让出 1 帧; step=[0x6df00] */
/* util5: r={0x50bd0..}; for(lvl=0;lvl<127;lvl+=step){...draw_buf(canvas,buf84,r,lvl);present;} ...0xff;present; */
/* util6: memcpy_rect(dst=[g+0x80],src=[g+0x88],0,0,1920,480); */
```
注：vt[0x2c]/0x34/0x58 是 256 色 palette 亮度合成原语（0=黑、0xff=全亮；vt[0x34] 另有 r,g,b 色调）——
颜色方向建议对照 kuro.bmp/白闪实机帧；控制流/矩形/步长/中断/每帧 present 已逐字还原，可直接照抄。
