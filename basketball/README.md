# Arcade Hoops（街机投篮机）

基于 Mosaico Game SDK（Raylib Lite Engine）的第一视角**街机投篮机**：单人限时刷分。
以投篮者第一人称视角面对球场与篮筐，在 60 秒内尽量多投进球，冲击最高分（BEST）。
玩法与绘制均为可移植 C 代码，Host 仿真器与设备固件共用同一套 `bball_game.*`（模型）
和 `bball_view.*`（绘制）。画面直接写入 480×480 RGB565 帧缓冲，优先使用
`assets_src/` 生成的球、篮筐和球场图集；资源不可用时退回图元绘制。

## 玩法

- 目标：60 秒限时内连续快速投篮，得分越高越好；最高分跨局保留。
- 球架：开局有 5 颗球，出手后手中立即补上下一颗；球架约每秒回一颗。已经出手的球继续飞行，场上最多同时保留 8 颗飞行球。
- 瞄准：上划时的横向偏移决定方向；Host 键盘也可用 `A`/`D` 或方向键。侧风会把球吹偏。
- 力度：上划的距离决定力度。大约向上划 175 像素落在篮筐上，太短偏软、太长偏冲。画面上不再画力度条。
- 出手：在屏幕上向上划并松手投篮；划得太短（轻点）不会出手。键盘 `F`（或 `空格`/`↑`）仍按内部力度值开火。
- 篮筐/球值：当前版本固定为中距离篮筐，基础命中得 2 分。
- 连击加成：连续命中 `COMBO`，连中 3 个及以上每球 +1 基础分。
- 疯狂模式 FEVER：最后 20 秒进入，篮筐左右滑动、所有进球得分翻倍（x2），画面/计时
  高亮，考验抓时机的手速。
- 时间到后不再接受新投篮；在途球结算完毕后显示最终分、最高分、命中率与最长连击。
  Host 可用 `Enter` 重开和 `P` 暂停，设备端点击结算卡片中的按钮重开。

## 代码结构

| 文件 | 职责 |
| --- | --- |
| `main/bball_game.c/.h` | 模型：60s 计时、滑动瞄准/力度、投篮物理、球架、命中判定、连击和 FEVER |
| `main/bball_view.c/.h` | 共享绘制：图集/图元回退、球场、篮筐、风向、飘分、街机 HUD、FEVER 和卡片 |
| `main/game_module.c` | Host 仿真生命周期与输入映射（仅 Host 编译） |
| `main/bball_app.c/.h`、`main/main.c` | 设备端应用：GSP 画布、触摸、NVS 最高分、音频和震动反馈 |
| `assets_src/` | 生成设备 `game_assets` 分区和 Host 图集的源图片及描述 |
| `game.sim.json` | 声明 Host 仿真编译的源文件 |
| `replays/*.json` | 可复现的 headless 回放（单次投篮、整场连投至 FEVER 与结算） |

## 输入映射（Host 动作码）

`0=左 1=右 2=上(=开火) 3=暂停 4=开始/重开 5=下 6=开火`。触摸与鼠标：向上划投篮，
左右偏移瞄准，松手出手；划动太短不出手。标题/结束界面点按开始。设备端不用 IMU。

## 在工作区中运行

从工作区根目录运行：

```sh
python mosaico.py game sim --project projects/basketball                       # 交互仿真
python mosaico.py game sim --project projects/basketball --headless --frames 300
python mosaico.py game sim --project projects/basketball --headless --frames 40 \
    --replay projects/basketball/replays/one_shot.json                         # 回放验证一次命中/飘分
python mosaico.py game sim --project projects/basketball --headless --frames 1810 \
    --replay projects/basketball/replays/full_match.json                       # 回放验证连投/FEVER/结算
python mosaico.py game build --project projects/basketball
python mosaico.py iris system-update --project projects/basketball
```

空白或未验证设备先执行 `python mosaico.py recover`。首次安装、分区或资源变化使用
`iris system-update`；分区表完全一致且仅修改代码时可用 `iris app-update`。生成应用
显式复用工作区固定的 BSP、引擎和 utils，不自动从相邻目录猜测依赖。

真机阶段再验证物理触摸、屏幕与音频；Host 仿真结果不能代替硬件验证。

## 已知构建问题

归档时使用 Vibe `v26.0.2` 的固定依赖重新验证，Host 与设备构建都会在
`main/bball_view.c` 调用 `MosaicoFastFramebuffer()` 处失败；该符号不在固定版本的
Raylib Lite Engine 公共接口中。修复前不能把此归档快照视为可构建发布版本。
