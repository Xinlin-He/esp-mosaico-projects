# 乐器工坊

在 ESP-Mosaico 上用 ToF 隔空演奏吉他，或打一首原创太鼓。界面为中英双语。曲目、鼓声和界面均为本应用自有资源，不含商业节奏游戏的名称、角色、谱面或美术。

![首页](website/home.png)

## 能做什么

首页进入吉他或太鼓。

吉他用扩展槽上的 ToF 识别上扫和下扫，屏幕上选择和弦并微调音高，扬声器播放对应琴弦采样。

太鼓包含选曲、演奏和结算。音符沿音轨走向鼓面，击中鼓心或鼓边后给出判定、连击和分数，一曲结束停在结算页。内置三首原创曲：

- 《山海疾风》120 BPM
- 《朱雀夜奔》138 BPM
- 《破阵新声》156 BPM

伴奏在演奏过程中播放。鼓声只在触摸或 ToF 击打时发出，漏击不会自动补鼓。

![吉他](website/guitar.png)

![太鼓演奏](website/play.png)

## 硬件与操作

- 一块 ESP-Mosaico（ESP32-S31，480×480 触控屏）。
- 太鼓的鼓心、鼓边各使用一路 ToF，模块插入对应扩展槽。只插一路时，设置页会显示当前可用的那一路，触屏仍可打鼓。
- 吉他扫弦使用 ToF 手势。没有模块时，屏幕上的和弦和调音仍可查看。
- 触屏用于首页切换、选曲、开始、设置和备用击打。

## 双路 ToF

两片传感器复位后都在 `0x29`，因此左槽打开后立即改到 `0x30`，右槽再上电打开并改到 `0x31`。未插入的一路始终保持 XSHUT 拉低，既不占用默认地址，也不影响热插拔。

驱动全部来自 BSP，应用不打补丁。`mosaico_tof_open()` 读硅片 ID 后自动区分模组型号：当前硬件是 VL53L4CD（VL53L4CDV0DH/1），旧版 VL53L0X 模组仍可识别并使用。连续测距由 `mosaico_tof_start_continuous()` 启动，取数用 `mosaico_tof_read_ready()` 轮询，不会阻塞主循环。要求 `submodule/esp-mosaico-bsp` 不低于 `841f624`。

测距预算取 20 ms，两路轮询周期 2 ms。鼓心与鼓边的判定、扫弦方向都在应用侧完成，驱动只负责出距离。

`mosaico_tof.c` 和 `mosaico_module_tof.h` 不要提交进本仓库。

## 安装

在 ESP-Mosaico 工作区根目录执行：

```sh
python mosaico.py iris system-update --project projects/tof_instruments
```

资源已经打进应用镜像。分区表里的 `ui_apps` 只是预留数据区，本应用不往那里写入预置文件。

## 许可

应用代码为 Apache-2.0。DejaVu Sans 的许可见 `ui/fonts/LICENSE-DejaVuSans`。界面还附带 Ma Shan Zheng 与 Instrument Sans。
