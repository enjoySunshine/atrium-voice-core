# 空间混音（每声部独立空间）— 验证与结果页

在 FluidSynth 上给每个声部加了独立的"空间"（房间）输出：四个空间
**洞穴 / 木厅 / 院子 / 井口**，每个空间各有一对立体声干声、一份独立混响、
一份独立合唱（`synth.audio-groups = synth.effects-groups = 4`）。

## 新增的合成器 API（`include/fluidsynth/synth.h`）

- `fluid_synth_noteon_space(synth, chan, key, vel, space)`
  起音时指定空间。不写空间时仍按通道落组（行为和现在完全一致）。
- `fluid_synth_voice_set_space(synth, voice, space, when)`
  把某个正在响的声部在第 `when` 个采样处改到别的空间，不重新触发音头。
- `fluid_synth_key_set_space(synth, chan, key, space, when)`
  把某个音（一条通道 + 一个键，可能含多层声部）整体改到别的空间。

空间 `s` 落在第 `s % audio-groups` 对立体声、第 `s % effects-groups` 份效果上。
声部仍属于它原来的 MIDI 通道：通道音量、声像照常在两个空间里一起生效；
抢声部、互斥组（exclusive class）行为不变。

## 场景

滴水（中央 C，力度 100）第 0 个采样在洞穴响起，第 22050 个采样（0.5 s）
改去木厅；脚步（低八度，同一通道）全程留在洞穴。院子和井口没有音。

## 文件

- `space_test.c` — 验证程序：渲染各场景、逐采样比对、写出 `report_data.js`。
- `render_baseline.c` — 用**未改动**的合成器渲染"现在"的参考缓冲
  （`baseline_b1.bin` / `baseline_b2.bin`），供"和现在逐采样一致"比对。
- `index.html` + `report_data.js` — 结果页，浏览器直接打开即可（不发声、不起服务）。
- `run.sh` — 一键重编译并重跑全部验证。

## 跑一遍

```sh
./run.sh
```

24 项检查全部通过，所有逐采样比对的绝对差都是 0（远小于 0.0001 的阈值）。
