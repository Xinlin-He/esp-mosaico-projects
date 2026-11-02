# 乐器工坊

本目录单独放进 GitHub，里面没有 BSP。编译前必须先打补丁，否则没有 `mosaico_tof_open_pair()`，工程编不过。BSP 基线是 `7cbfb390638dcb93522fc3cde094b2d43cf907c2`，其中 `mosaico_tof_new()` 只能打开一路 VL53L0X，地址是 `0x29`。保存下来的是 `patches/0001-mosaico-module-tof-dual.patch`。

把本目录放到 Vibe 工作区的 `projects/tof_instruments`，在工作区根目录执行：

```sh
git -C submodule/esp-mosaico-bsp apply --check ../../projects/tof_instruments/patches/0001-mosaico-module-tof-dual.patch
git -C submodule/esp-mosaico-bsp apply ../../projects/tof_instruments/patches/0001-mosaico-module-tof-dual.patch
```

第一条只检查能否打上，第二条才写入 BSP 工作区。打上之后调用 `mosaico_tof_open_pair()`。两路同时打开时，左槽改为 `0x30`，右槽保持 `0x29`。空着的槽传 `NULL`。不要在应用里再写一套 XSHUT 和改地址，也不要把 `mosaico_tof.c`、`mosaico_module_tof.h` 或子模块工作区当成已经入库的 BSP。
