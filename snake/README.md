# Mosaico 贪吃蛇

经典街机贪吃蛇，为 ESP-Mosaico 设备打造。指挥小蛇吃苹果、不断变长，别撞到墙壁或自己的身体。分数越高，蛇跑得越快，考验你的反应和路线规划。

## 需要的硬件

- 一台 ESP-Mosaico 设备（480×480 触摸屏，内置扬声器）。
- 刷入本固件后即可离线游玩，无需联网或额外配件。

## 怎么玩（触摸操作）

- **开始 / 重新开始**：在标题或结束画面，点按屏幕任意位置。
- **控制方向**：以屏幕中心为界，点按屏幕的上 / 下 / 左 / 右区域，小蛇就朝那个方向转向。
- **不能直接掉头**：例如正在向右时点左侧无效，需要先转上或下再折返（避免瞬间撞到自己）。
- 蛇按固定节奏自动前进，你只需在拐弯前点一下目标方向即可，不用一直按住。

## 游戏规则

- 吃到红苹果得分并让蛇变长。
- 每得 40 分提速一档，屏幕右上角的 **SPEED** 速度条会点亮更多格，最快时移动非常迅速。
- 撞到四周墙壁或自己的身体即游戏结束，点按屏幕可重新开始。
- 顶部信息栏：**SCORE** 当前分 · **BEST** 本次上电期间的最高分 · **LEN** 蛇身长度 · **SPEED** 当前速度。
- 吃果与结束都有音效反馈。

## 在工作区中运行

将本目录复制到 Vibe 工作区的 `projects/snake/`，从工作区根目录运行：

```sh
python mosaico.py game sim --project projects/snake
python mosaico.py game sim --project projects/snake --headless --frames 300
python mosaico.py game sim --project projects/snake --headless --frames 500 \
    --replay projects/snake/replays/eat_first_food.json
python mosaico.py game build --target iris --project projects/snake --idf-path <ESP-IDF路径>
# 使用上一条命令打印的 snake-system-update.irisfw：
python mosaico.py iris system-update --bundle <snake-system-update.irisfw>
```

空白、未验证或 0.1 合同设备先执行 `python mosaico.py recover`。首次安装、分区或
资源变化使用 `iris system-update`；项目已迁移到 Vibe `v26.0.3`、Recovery ABI 2
和 `mosaico-retained-test-2m-v2` 合同。真机结果以长期归档文档为准。

## 已知限制

- 目前为纯触摸操作，设备端暂未提供暂停功能。
- `BEST` 只保存在 RAM 中，设备重启后归零。

## 许可

程序源代码与项目专用封面采用 Apache-2.0；完整条款及封面来源说明见仓库根目录
`LICENSE` 和 `ASSETS_LICENSE.md`。
