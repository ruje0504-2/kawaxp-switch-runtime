# KAWAXP 0.3.0 构建与验证

- 产物：kawaxp.nro，8,724,536 字节。
- SHA-256：21185982c68ed287312a0421e5c83f54e3e903a3148b4ca71e360cffd1e6567a
- NRO0 / ASET 标记：通过。
- macOS host 与 devkitA64/libnx Switch 构建：通过。
- AX 单测及全资源检查：ax-tests.log。
- 实际对白及眨眼序列：ax-dialogue.log。
- 1,200 条对白主线回归：ax-route.log（无 STOP，18 次已有震屏近似提示）。
- 槽位菜单保存：ax-save-menu.log。
- 跨进程恢复并续播：ax-restore.log。
- 旧 KWS v1 读取及 v2 再保存：ax-v1-load.log。
- 同一存档睁眼/闭眼：ax-open.png、ax-blink.png，515 个变化像素，边界 (290,80)–(364,94)。
- 本轮 NRO 的 Switch 实机 AX 验证：尚未进行。

主机测试使用原版游戏数据，保存到独立测试目录，没有改动用户游戏存档。
smoke 使用虚拟动画时钟并默认关闭音频；保留此前已在 Switch 实机确认的 F32 音频实现。
