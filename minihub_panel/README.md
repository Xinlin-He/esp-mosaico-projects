# Mosaico MiniHub

这是官方 **MiniHub Host** 在 ESP-Mosaico（ESP32-S31，480×480
触屏）上的产品适配。MiniHub 负责网络、协议、Web 和数据；Mosaico
只增加本机触屏 UI 与 Recovery。

界面默认 **English**，首次开机可选 **中文**。下文按中文界面写，英文标签写在括号里。

---

## 你会用到的东西

| | |
|---|---|
| 设备 | ESP-Mosaico 一块，USB 供电 |
| 网络 | 2.4 GHz Wi-Fi；触屏从扫描结果中选择网络 |
| 灯 / 开关 / 插座 | 当前 Zigbee 固件支持 Matter、Zigbee 和官方 MQTT |
| 电脑（可选） | 同一 Wi-Fi 下用浏览器打开屏的 IP，进完整控制台 |

当前固件是 **Zigbee 版**：可以同时用 Wi-Fi + Zigbee。Thread 是另一套固件，不能和 Zigbee 装在同一镜像里。

---

## 第一次开机

触屏首次设置依次完成：

1. **欢迎页**
   约 3 秒后自动进入语言页。
2. **语言 Language**
   默认选中 English（标 Default）。点 **中文** 再 **继续 Continue**。之后所有界面跟这次选择走，设置里还能改。
3. **Wi-Fi**
   面板异步扫描并显示最多 5 个 2.4 GHz AP，不允许手输 SSID。开放网络点选后直接提交；加密网络只需输入密码。
4. **模式 Mode**
   **Hub** 使用本机 `http://127.0.0.1`；**Panel** 连接用户填写的远程 MiniHub base URL，并使用 Bearer token。

Wi-Fi 扫描是 S31-Mosaico 适配层对官方 MiniHub 已初始化 `esp_wifi`
实例的只读使用；它不初始化、停止或接管 Wi-Fi。选定网络后，凭据仍只
通过官方 MiniHub `POST /api/net/wifi` 保存和应用，面板不自行保存
Wi-Fi 密码，也不直接连接 STA。

---

## 首页：四个页签

底栏从左到右：**设备** / **场景** / **自动化** / **设置**。

### 设备 Devices

- 每页最多 4 张卡片，左右滑翻页。
- 空白圆加号 = **添加设备**。
- 灯 / 开关 / 插座：点卡片进详情。底部大按钮执行官方已有的开关控制；离线设备只显示 **离线** 状态。
- 传感器只显示状态，不能开关。
- 设备名里如果有字库没有的汉字，会显示成 `设备 · <id>`，避免出现方块字。

点灯开关后，灯应该马上变。若卡片显示 **离线**，请检查设备供电、网络和官方 MiniHub 控制台状态，不要反复猛点。

### 添加设备

加号 → **选择设备类型**。只列出当前固件真正能用的协议。

| 类型 | 你要准备的 | 说明 |
|---|---|---|
| **Matter** | 11 或 21 位配对码，或 `MT:` 开头的二维码内容；名称；房间可选 | 通过官方 `ble_wifi` 首次配网；MiniHub 从自身配网存储复用当前 Wi-Fi 凭据并经 BLE 下发。 |
| **Zigbee** | 让设备进入配网；IEEE / 安装码可空；名称；房间可选 | 需要 Zigbee 版固件。 |
| **MQTT** | Broker、端口、Point ID、类型、主题等 | 使用官方 MiniHub MQTT 适配器。 |

「123」这种无效 Matter 码应当立刻失败。发现失败就返回，不会在首页留一张 PAIR 卡片。

### 场景 Scenes

场景 = **一组设备的开/关快照**，轻触执行一次。不是「房间」，也不是自动化。

1. 点 **创建**。
2. 给场景里的设备选 **跳过 / 关 / 开**。至少选一个动作。
3. 没有第二台设备时，**添加设备** 入口仍在，点了会提示 **无其它设备**。
4. 保存后，列表上轻触执行；右侧删除。

官方 MiniHub 就是这个模型：场景不负责「如果…那么…」。那是自动化。

### 自动化 Automation

自动化 = MiniHub 的 **规则**：某个设备变成开或关之后，去开/关另一台。

- 至少两台可控制设备（灯 / 开关 / 插座）才能创建。
- 第一步选触发设备及其当前状态，第二步选目标设备和要变成的开或关。
- 列表上轻触启用或停用，右侧删除。
- 更复杂的条件、多步、Store，去电脑上的 Web 控制台。

机载体感（IMU）**这版未启用**：晃动会把规则执行打进 Matter 加密栈并重启，判定也不够稳。

### 设置 Settings

- **Wi-Fi**：重新扫描并从列表切换网络。
- **模式**：切换本机 Hub 或远程 Panel。Panel 的 base URL 与 Bearer token 保存在 `app_hub` NVS；token 不写日志。
- **语言**：English / 中文。
- **重启 MiniHub**：设备会短暂离线。系统更新走 Mosaico Vibe Mode，不在这个按钮里。

---

## 电脑上的控制台

Hub 模式下，屏和电脑连同一 Wi-Fi 后，浏览器打开：

```text
http://<屏的 IP>/
```

这是完整 MiniHub Web：设备目录、规则、场景、适配器。屏上没有的高级编辑都在这里。Web 里的 **规则** 就是屏上的 **自动化**。

登录认证这版默认关掉。控制台文件在本机存储里；若页面提示未安装，需要重新写入工厂 `storage` 分区（会清掉已添加的设备）。

---

## 常见问题

**灯显示开了但没亮**
先看卡片是不是离线。在线仍不动时检查官方 MiniHub 控制台；Matter 灯刚配上网有时要等 CASE 会话起来。

**设备时不时离线**
检查物理设备供电、网络和官方 MiniHub 控制台。面板只显示官方 Host 报告的在线状态，不额外发起连接管理命令。

**场景点了没反应**
场景必须包含至少一台在线设备的开或关。只有名字、全是「跳过」等于什么都不做。

**为什么场景不能只绑一台灯**
可以绑一台，只是没意义。入口仍在，方便以后加第二台。

**关掉电脑 / 拔 USB 后设备没了**
正常运行时设备写在屏的 LittleFS 里，不断 USB 电、不整片擦除就不会丢。整包 `system-update` 如果带了工厂 `storage.bin`，会把已配设备清掉。日常应用更新不要带这块。

**中文变成问号**
动态名字超出附带字库。改成字库里有的字，或接受 `设备 · <id>`。

---

## 开发与预览

日常用户不用看这一节。

```sh
python mosaico.py project sim --project projects/minihub_panel --interactive
python mosaico.py recover
python mosaico.py iris system-update --project projects/minihub_panel
```

空白或未校验的板先 `recover`。改了 UI / 分区 / 资源用 `system-update`；只改 C 且分区表一致用 `app-update`。不要 `idf.py flash`。

工程结构：`main` 只组装官方 MiniHub 入口、Recovery 和 UI；`app_hub`
在 Hub 模式访问 localhost、在 Panel 模式访问持久化的远程地址，并仅
借用官方已启动的 Wi-Fi 实例进行扫描；`app_panel` 管 480×480 界面；`app_display`
接 BSP。不要在面板组件中重新实现 Wi-Fi、BLE、Matter 或 MiniHub 启动流程。
