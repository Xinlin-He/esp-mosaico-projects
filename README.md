# ESP-Mosaico Projects

个人 ESP-Mosaico 应用集合。每个应用独立保存在仓库根目录的 `<应用名>/` 中，
本仓库不复制 Vibe 主仓库、BSP、ESP-Iris、GSP 或其他公共依赖。

## 应用

- [`mosaico_chess`](mosaico_chess/README.md)：支持人机对战和
  双设备对战的国际象棋应用。

## 开发方式

应用需要复制到官方
[`esp-mosaico-vibe`](https://github.com/esp-mosaico/esp-mosaico-vibe)
工作区的 `projects/<应用名>/`，再从 Vibe 工作区根目录执行构建、模拟器和
设备更新命令。

AI 开发和集成约束见 [`AGENTS.md`](AGENTS.md)，各应用的补充规则见其目录内的
`AGENTS.md`。
