# ESP-Mosaico Projects

个人 ESP-Mosaico 应用集合。每个应用独立保存在仓库根目录的 `<应用名>/` 中，
本仓库不复制 Vibe 主仓库、BSP、ESP-Iris、GSP 或其他公共依赖。

## 应用

- [`basketball`](basketball/README.md)：Arcade Hoops 街机投篮机，支持触摸上划投篮、
  球架、连击与 FEVER。
- [`mosaico_chess`](mosaico_chess/README.md)：支持人机对战和
  双设备对战的国际象棋应用。
- [`snake`](snake/README.md)：面向 480×480 触摸屏的经典贪吃蛇。

## 开发方式

应用需要复制到官方
[`esp-mosaico-vibe`](https://github.com/esp-mosaico/esp-mosaico-vibe)
工作区的 `projects/<应用名>/`，再从 Vibe 工作区根目录执行构建、模拟器和
设备更新命令。

AI 开发和集成约束见 [`AGENTS.md`](AGENTS.md)；应用目录存在 `AGENTS.md` 时还需
遵循其补充规则。

## 许可

程序源代码采用 [Apache-2.0](LICENSE)。图片素材来源见
[ASSETS_LICENSE.md](ASSETS_LICENSE.md)。
