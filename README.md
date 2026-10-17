# JBL 4305P 蓝牙遥控桥接

简体中文 | [English](README.en.md)

用一块 ESP32-C3 同时接收 JBL 原装遥控器和 Home Assistant 的控制，再通过蓝牙发送给 JBL 4305P。日常使用时只需 ESP32 持续供电；Nordic 抓包器和开发电脑不参与中转。

```text
原装遥控器 ── BLE HID ──→ ESP32-C3 ── BLE HID ──→ JBL 4305P 主音箱
Home Assistant ── Wi-Fi / 加密 ESPHome API ──┘
```

**建议让 AI 按 [AGENTS.md](AGENTS.md) 和[部署清单](docs/DEPLOYMENT.md)直接部署。** 人类负责准备硬件、确认两端蓝牙地址、进行物理配对，并观察音箱是否实际执行。把仓库交给 AI 时，可使用以下指令：

> 先读 AGENTS.md、docs/DEPLOYMENT.md 和 docs/COMPATIBILITY.md。识别我指定的 ESP32，确认音箱与遥控器的 public MAC，使用我的私密参数编译。保留现有绑定与已验证版本，不要扫描串口后批量刷机，不要把演示固件 OTA 到现用设备。完成 HA 实体匹配、仪表盘导入和部署清单验证，并区分编译结果与现场实测结果。

## 当前能力

- 13 个音箱按键：待机／唤醒、音量增减、静音、上一首、播放／暂停、下一首，以及网络播放、蓝牙、USB、光纤、平衡、AUX 输入。
- 原装遥控器与 HA 共存；两条蓝牙连接分别显示状态。
- 中文原遥控接收事件、最近按键、独立 HA 发送结果和原生 24 小时活动卡。
- HA 的“状态与控制”页已合并收发历史；“配对说明”保持独立。
- 分别维护原遥控器和音箱的绑定，正常操作无需重新配对。

“收到”表示 ESP32 收到原遥控器报告；“已提交”表示蓝牙协议栈接受了 HA 按下报告。二者都不是音箱执行确认。连接状态也不等于音箱电源、音量或输入状态。

## 支持范围

| 项目 | 已有证据与限制 |
| --- | --- |
| 硬件 | 一套 JBL 4305P、配套原遥控器和 ESP32-C3 4 MB 已实测；不能据此保证其他板卡或另一套同型号即插即用 |
| 工具链 | ESPHome **2026.9.1**、ESP-IDF **5.5.5**；自定义组件依赖该版本，不要自动升级 |
| 按键 | 13 个 payload 均有真实抓包；音箱实际动作验证覆盖光纤、USB、音量增减、待机与唤醒，其他按键未逐项实测 |
| 重启 | 原套设备的 ESP32 重启及保留绑定恢复已验证；原遥控器首次按键可能只用于唤醒，约 5 秒后再按 |
| 可移植配置 | 发布配置要求分别填写遥控器和音箱的 public MAC；两端地址都不是通用常量，也不自动学习音箱地址 |
| 新发布版本 | 参数化代码与脱敏演示构建须按发布 manifest 核查；此前原套设备的实测不等于参数化版本已在新套设备实测 |

详细状态见[兼容性与验证边界](docs/COMPATIBILITY.md)。

## 你需要准备

1. JBL 4305P 主音箱及原装遥控器。
2. 已确认型号与容量的 ESP32-C3 4 MB、USB 数据线和稳定电源。
3. Home Assistant、可用 Wi-Fi，以及本套遥控器与音箱的 public MAC。
4. 私密 Wi-Fi SSID／密码、ESPHome API 加密密钥与 OTA 密码。不要把这些参数提交到 Git。

音箱在这条 BLE 链路中是 **central**，不能用 Wi-Fi MAC 或普通设备扫描结果猜它的 BLE 身份。地址未知时，需要原连接建立的抓取记录或可信端点证据，并确认地址类型为 public。首次地址确认可能需要 Nordic 等研究工具；两端 MAC 已知后，正常部署和控制不需要 Nordic。详见部署清单中的地址确认说明。

首次部署应按[完整部署流程](docs/DEPLOYMENT.md)从源码编译，并只上传到明确识别的目标板。运行中的设备升级以保留凭据、分区和绑定为前提。

## 文档与源码入口

| 入口 | 用途 |
| --- | --- |
| [AGENTS.md](AGENTS.md) | AI 执行约束、检查顺序与归档规则 |
| [docs/DEPLOYMENT.md](docs/DEPLOYMENT.md) | 人机分工、构建命令、首次部署、配对、HA 导入与验收清单 |
| [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) | 双 BLE 角色、组件职责、事件语义和绑定恢复 |
| [docs/COMPATIBILITY.md](docs/COMPATIBILITY.md) | 已测环境、协议证据与未验证项目 |
| [docs/ARCHIVE.md](docs/ARCHIVE.md) | 公开归档范围、两类固件版本、复现与回退边界 |
| [docs/REVERSE_ENGINEERING.md](docs/REVERSE_ENGINEERING.md) | 协议与身份研究过程、证据来源和研究脚本用途 |
| [NOTICE](NOTICE.md) | 许可与第三方声明 |
| [scripts/build_release.py](scripts/build_release.py) | 隔离构建公开演示产物的脚本；不上传设备 |
| [发布产物说明](releases/2026.10.17/README.md) | 此次 factory／OTA 示例文件、校验及使用限制 |
| [evidence/validation-summary.json](evidence/validation-summary.json) | 可供 AI 读取的脱敏验证摘要 |
| [docs/JBL-4305P-使用说明.txt](docs/JBL-4305P-使用说明.txt) | 日常操作及原部署的现场验证记录 |
| [firmware/esphome/jbl-4305p-bridge.yaml](firmware/esphome/jbl-4305p-bridge.yaml) | 固件入口；同目录 `components/` 是本地组件 |
| [firmware/home-assistant/jbl-4305p-dashboard.yaml](firmware/home-assistant/jbl-4305p-dashboard.yaml) | 原生 HA 仪表盘模板；必须按新实例实际实体 ID 调整 |
| [jbl_protocol.json](jbl_protocol.json) | 报告描述符、按键字节与抓包证据索引 |

公开仓库保留协议和脱敏验证摘要；原始抓包、现用固件备份与私密日志由项目拥有者本地保存，不随发布分发。

## 发布产物

`releases/2026.10.17/` 包含 `firmware.factory.bin`、`firmware.ota.bin`、`manifest.json` 和 `SHA256SUMS`。这些二进制由**公开脱敏演示参数**编译，含假 SSID、示例 API 密钥和文档用 MAC，不含可用于你家设备的配置。

演示产物仅用于首次 USB 烧录实验或构建复现；真实使用推荐填写本套设备参数后重新编译。项目未提供演示固件的运行时配网门户。**不要用演示 OTA 文件覆盖现用含凭据固件**，也不要把实际部署生成的含凭据二进制公开上传。发布文件名中的 `ota` 仅说明格式，不表示适合更新现用设备。
