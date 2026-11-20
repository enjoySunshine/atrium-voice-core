# 四空间场景混音路由（洞穴 / 木厅 / 院子 / 井口）

在现有 FluidSynth 合成器上增加“空间（space / room）”路由：每个空间有
自己的一对立体声干声、一份混响、一份合唱。一个正在发声的音（voice）
可以在不重新触发音头的前提下，按采样精度从一个空间移到另一个空间；
同一条 MIDI 通道上的不同音可以待在不同空间。

播放器不做改动，全部能力都在合成器内实现。

## 目录内容

| 文件 | 作用 |
| --- | --- |
| `scene_render.c` | 场景渲染与全部数值校验（只用公开 API） |
| `scene_report.html` | 本地报告页面，直接用浏览器打开即可，不发声、不起服务 |
| `scene_report_data.js` / `.json` | 合成器缓冲生成的波形与数字（页面读取 `.js`） |
| `run_scene.sh` | 编译并运行场景渲染，刷新报告数据 |

## 引擎改动（在 `src/` 内）

- `src/rvoice/fluid_rvoice.h` / `fluid_rvoice.c`
  - rvoice 增加多空间路由状态：当前空间、待切换空间、切换的绝对采样点。
  - 新增 `fluid_rvoice_set_route()`（rvoice 事件）与
    `fluid_rvoice_apply_route_pub()`，统一重算四个混音缓冲（左、右、
    混响发送、合唱发送）的目标 mapping。
- `src/rvoice/fluid_rvoice_mixer.c`
  - `fluid_rvoice_buffers_mix()` 重写为按绝对采样区间混音，并在待切换
    采样点把一个块精确切成两段：前半段进旧空间，后半段进新空间。
    幅度斜坡（amp ramp）按整条混音区间的绝对采样下标表达，因此切分
    不会重启斜坡，也不会再响一次音头。
  - 新增 `fluid_rvoice_mixer_set_render_tick()`，让混音器知道本次输出
    第一个采样对应的绝对 tick。
- `src/synth/fluid_synth.c` / `fluid_synth.h`
  - 新增按绝对 tick 调度的空间切换队列，在渲染线程进入包含该 tick 的
    块之前 flush 并 dispatch 对应 rvoice 事件。
  - 公开 API：`fluid_synth_count_spaces()`、
    `fluid_synth_voice_set_space()`（立即）、
    `fluid_synth_voice_set_space_at()`（按采样点）、
    `fluid_synth_voice_get_space()`，声明在
    `include/fluidsynth/synth.h`。
- `src/synth/fluid_voice.c` / `fluid_voice.h`
  - voice 增加 `space` 字段；`fluid_voice_init()` 在显式指定空间时按
    空间（而非 MIDI 通道）选干声组与效果单元；新增
    `fluid_voice_set_space()`。

### 为什么默认行为完全不变

- 不写空间时 `voice->space < 0`，mapping 仍按
  `chan % audio-groups` / `chan % effects-groups` 计算，和改动前一致。
- 混音循环在没有待切换（`pending_space < 0`）时走与原来等价的单循环，
  全部 46 个上游测试逐采样通过。
- 音的抢占（polyphony overflow）与 SoundFont 互斥类（exclusive class）
  逻辑只看通道/音色，不看空间，行为不变。

## 场景与校验（容差 0.0001，满幅按 1 算，采样率 44100）

主场景（2 秒）：

- 滴水：中央 C（MIDI 60），力度 100，第 0 采样在洞穴发声，第 22050
  采样（0.5 秒）整体改入木厅；该音色一个音产生两条硬声像立体声
  voice，两条一起移动。
- 脚步：低八度（MIDI 48），力度 100，共用同一 MIDI 通道，从头到尾
  留在洞穴。
- 院子、井口不写任何音。

参考渲染（1 秒，独立合成器实例）：

1. 滴水+脚步都在洞穴；
2. 只有脚步在洞穴；
3. 滴水从第 0 采样就在木厅（无脚步）。

校验项（共 83 项，全部通过，关键项绝对差为 0）：

- 院子/井口干声、混响、合唱全程静音，峰值 < 0.0001。
- 洞穴干声 `[0,22050)` 与参考 1 逐采样一致；`[22050,44100)` 与参考 2
  逐采样一致。
- 木厅干声在 22050 之前静音；`[22050,44100)` 与参考 3 逐采样一致，
  左右两条都比（对齐起点 = 22050，最大绝对差 = 0）。
- 切换点没有新音头；洞穴混响在滴水离开后仍保留已送入的尾巴；木厅
  混响/合唱在 22050 之前静音。
- 一次要 512 采样 vs 连续八次 64 采样：四组干声、四份混响、四份合唱
  全部逐采样一致（绝对差 0）。
- 混响/合唱发送全关时，八份效果缓冲全静音。
- 只开一组立体声/一组效果、不写空间时，干声与现在逐采样一致；
  四空间全开但不写空间时，音仍落在通道默认那一对。
- 通道音量（CC7）一改，洞穴脚步与已到木厅的滴水按同一系数一起变；
  通道声像（CC10）一改，两边的左右一起变，各自空间不变。
- 声部不足时的挤掉规则不变；音色里本会互掐的互斥类音，换了空间
  仍然互掐（Analog Drumset，互斥类 2，已验证跨空间切断）。

## 构建与运行

```sh
# 1) 构建 FluidSynth（仓库根目录）
cmake -S . -B build3 -DCMAKE_BUILD_TYPE=Release \
      -Denable-alsa=off -Denable-aufile=off -Denable-dbus=off \
      -Denable-jack=off -Denable-ladspa=off -Denable-libsndfile=off \
      -Denable-midishare=off -Denable-oss=off -Denable-readline=off \
      -Denable-network=off
cmake --build build3 -j

# 2) 跑场景并刷新报告
./scene_routing/run_scene.sh

# 3) 看结果：浏览器直接打开
#    scene_routing/scene_report.html
```

页面通过同目录的 `scene_report_data.js`（`window.SCENE_REPORT_DATA = …`）
取数，因此 `file://` 打开即可，无需本地服务，也不依赖网络。
`scene_report_data.json` 是同一份数据的纯 JSON 形式，便于其它工具读取。

## 备注：效果缓冲的读取布局

`fluid_synth_process()` 的 `fx[]` 按“每个效果单元 4 个缓冲”排列：
单元 k 对应 `fx[4k..4k+3]` = 混响左、混响右、合唱左、合唱右。
必须传入覆盖全部效果单元的 `nfx`（四空间时为 16），否则 FluidSynth
文档化的取模回绕会把后面的单元叠进前面的槽位——这是既有行为，与本
改动无关（在未改动的基线版本上同样可复现）。
