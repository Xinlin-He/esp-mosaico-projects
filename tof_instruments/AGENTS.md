# 乐器工坊

本目录单独放进 GitHub，里面没有 BSP。ToF 全部走 BSP 既有接口，不打补丁：`mosaico_tof_open()` 读硅片 ID 后自动区分 VL53L4CD 与 VL53L0X，随后 `mosaico_tof_set_address()` 给每路分配地址，`mosaico_tof_set_timing_budget()` 设 20 ms 预算，`mosaico_tof_start_continuous()` 起连续测距，`mosaico_tof_read_ready()` 轮询取数，`mosaico_tof_close()` 收尾。要求 `submodule/esp-mosaico-bsp` 不低于 `841f624`；更早的提交只有 `mosaico_tof_new()`，只能开一路 VL53L0X。

把本目录放到 Vibe 工作区的 `projects/tof_instruments`，从工作区根目录直接编译，不需要改子模块工作区。两路同时插入时，左槽打开后改到 `0x30`，右槽改到 `0x31`；未插入的一路由应用把 XSHUT 拉低，避免两片传感器抢默认地址 `0x29`。不要把 `mosaico_tof.c`、`mosaico_module_tof.h` 或子模块目录复制进本仓库，也不要为此再维护一份 BSP 补丁。
