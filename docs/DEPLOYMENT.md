# 部署清单

简体中文 | [English](DEPLOYMENT.en.md)

适用对象：执行部署的 AI，以及负责接线、配对和观察音箱的使用者。先读根目录 [AGENTS.md](../AGENTS.md)；本文件所有命令均从仓库根目录执行。本文不授权对未指定设备刷写或重置绑定。

## 1. 确认部署目标

**建议让 AI 按 AGENTS.md 和本部署清单直接部署。** AI 处理配置、编译、目标核验、上传和 HA 配置；人类准备硬件、提供本机私密参数，并完成下面的物理配对动作。

- [ ] 确认目标是用户指定的 ESP32-C3，Flash 为 4 MB；当前板型配置是 `esp32-c3-devkitm-1`。
- [ ] 用 USB 设备信息、物理插拔对应关系和板卡身份核对**一个**串口；记录最终目标。不得枚举所有串口逐个尝试刷写。
- [ ] 对 OTA，核对一个明确的目标 IP／主机名、现用 ESPHome 节点身份和设备信息；不要按网段尝试上传。
- [ ] 确认原遥控器、音箱各自的 **public MAC**。每套设备均需单独确认；不要复制旧部署地址，也不要把音箱的网络 MAC 当作 BLE MAC。
- [ ] 明确是首次部署还是保留现有绑定的升级；先保存现用源码、私密参数、工具链版本和可用回退固件。
- [ ] 在音箱实际操作前安排可控音量和可观察的输入指示灯。

ESP32-C3 4 MB 是已实测配置。其他 ESP32 变体、容量或另一套同型号音箱的兼容性仍需验证，不能以“芯片支持 BLE”替代本项目验证。

### 两端 MAC 如何确认

原遥控器是 BLE peripheral，主音箱是连接它的 **BLE central**。普通 BLE 扫描主要显示广播设备，不能据此保证获得音箱的中央角色身份；同名设备、Wi-Fi MAC、以太网 MAC 或相邻地址推算也不能替代证据。

如果还不知道音箱地址，应先做针对这套设备的身份研究，例如监听原遥控器与音箱的连接建立，核对发起方角色及地址类型，或取得可信的控制器／端点连接记录。遥控器地址也须通过物理设备对应关系确认。只得到无法确认身份或类型的地址时，先完成源码准备，等待补足证据后再生成部署配置；不要改为接受任意对端来绕过确认。

Nordic 抓包器可能用于首次地址确认或协议研究。**两端 public MAC 已确认后，正常部署、配对和日常控制均不需要 Nordic**；USB 首次上传后，开发电脑也不参与日常转发。

## 2. 创建独立构建环境

采用 Python 3.13；原开发环境为 Windows、Python 3.13.14。ESPHome 2026.9.1 的包元数据要求 Python `>=3.12,<3.15`。新检出仓库须自行创建虚拟环境，不能依赖作者机器上的 `.venv-esphome`。根目录 `requirements.txt` 用于抓包工具，固件构建依赖是 `requirements-build.txt`。

Windows PowerShell：

```powershell
py -3.13 -m venv .venv-build
.\.venv-build\Scripts\python.exe -m pip install -r requirements-build.txt
.\.venv-build\Scripts\python.exe -m esphome version
```

Linux：

```sh
python3.13 -m venv .venv-build
.venv-build/bin/python -m pip install -r requirements-build.txt
.venv-build/bin/python -m esphome version
```

输出应为 ESPHome **2026.9.1**。YAML 中 ESP-IDF 固定为 **5.5.5**。第一次编译需要联网下载构建工具链；不要为解决安装问题自行升级 ESPHome／IDF。Windows 建议在短、纯英文目录中检出仓库，避免工具链路径长度问题；不要恢复作者机器的绝对 `build_path`。

## 3. 填写本机私密参数

首次部署推荐运行 `scripts/prepare_config.py`：把下面两个占位符替换为已经确认的 public BLE MAC。脚本交互读取 SSID，使用 `getpass` 隐藏输入 Wi-Fi 密码，生成独有 API 密钥与 OTA 密码，直接写入被 Git 忽略的 `firmware/esphome/secrets.yaml`，不打印凭据，并拒绝覆盖已有文件。

Windows PowerShell：

```powershell
.\.venv-build\Scripts\python.exe scripts/prepare_config.py --remote-mac "REMOTE_PUBLIC_MAC" --speaker-mac "SPEAKER_PUBLIC_MAC"
```

Linux：

```sh
.venv-build/bin/python scripts/prepare_config.py --remote-mac "REMOTE_PUBLIC_MAC" --speaker-mac "SPEAKER_PUBLIC_MAC"
```

脚本只能校验地址格式，不能证明地址类型是 public；这一点必须由实际设备证据确认。已有部署应保留原秘密文件和凭据，避免 HA 连接和 OTA 访问失效；不要删除它来强行重新运行脚本，也不要用 `secrets.example.yaml` 覆盖它。示例文件用于了解下面六个配置键。

| 键 | 内容 |
| --- | --- |
| `wifi_ssid` | 本地 Wi-Fi SSID |
| `wifi_password` | Wi-Fi 密码 |
| `jbl4305p_api_key` | 32 字节随机值的 Base64 表示，用于 ESPHome API 加密 |
| `jbl4305p_ota_password` | 本机 OTA 密码 |
| `jbl4305p_remote_mac` | 本只原装遥控器的 public BLE MAC |
| `jbl4305p_speaker_mac` | 本只主音箱的 public BLE MAC |

两端 MAC 都必须配置，当前实现不会自动学习音箱地址。秘密文件、部署日志中的私密参数以及由真实参数编译的二进制均不得公开提交。AI 可以在本地使用参数，但不应在最终回复或提交说明中复述凭据。

首次安装由部署者在本地生成独有 API 密钥与 OTA 密码并直接保存到秘密文件，不输出到工具日志或公开记录；已有设备升级继续使用现有凭据。

## 4. 检查与编译

先完成 AGENTS.md 要求的静态／来源检查，再编译。以下命令不会上传固件：

Windows PowerShell：

```powershell
.\.venv-build\Scripts\python.exe firmware/esphome/check_jbl_config.py
.\.venv-build\Scripts\python.exe firmware/esphome/components/jbl_remote_relay/verify_received_keys.py
.\.venv-build\Scripts\python.exe -m esphome config firmware/esphome/jbl-4305p-bridge.yaml
.\.venv-build\Scripts\python.exe -m esphome compile firmware/esphome/jbl-4305p-bridge.yaml
```

Linux：

```sh
.venv-build/bin/python firmware/esphome/check_jbl_config.py
.venv-build/bin/python firmware/esphome/components/jbl_remote_relay/verify_received_keys.py
.venv-build/bin/python -m esphome config firmware/esphome/jbl-4305p-bridge.yaml
.venv-build/bin/python -m esphome compile firmware/esphome/jbl-4305p-bridge.yaml
```

- [ ] 配置引用六个本机 secret；ESPHome／IDF 版本正确。
- [ ] Report Map、Report Reference、13 个按键 payload 和中文事件表检查通过。
- [ ] C++ 编译成功；保存构建时间、版本、目标板和输出文件 SHA-256。
- [ ] 把这次自编译产物与 `releases/` 中的演示产物区分，上传前再次核对来源。

不要把配置成功或编译成功写成音箱已实测成功。

## 5. 上传：只针对已核实的一块板

首次部署使用 USB。以下 `COM_TARGET`／`/dev/serial/by-id/TARGET` 是占位符，必须换成步骤 1 中确认的真实端口。命令上传刚才本机编译的固件，不引用公开演示文件。

Windows PowerShell：

```powershell
.\.venv-build\Scripts\python.exe -m esphome upload firmware/esphome/jbl-4305p-bridge.yaml --device COM_TARGET
```

Linux：

```sh
.venv-build/bin/python -m esphome upload firmware/esphome/jbl-4305p-bridge.yaml --device /dev/serial/by-id/TARGET
```

对已有、已核实节点进行 OTA 时，同一命令的 `--device` 值改为该节点的准确 IP 或主机名，并使用它当前接受的 OTA 凭据。不要为了上传而改设备名、重建分区、全片擦除或清空 NVS；不要自动点击维护配对按钮。保留绑定是正常升级的目标，但跨工具链或分区升级不在本项目验证范围内。

`releases/2026.10.17/firmware.factory.bin` 和 `firmware.ota.bin` 使用假 SSID、示例密钥与文档 MAC。它们不是本机配置备份，**不得 OTA 覆盖现用设备**。演示固件也没有可供事后填写全部参数的配网门户；真实部署需填写私密参数并重新编译。

## 6. 加入 Home Assistant 并匹配实体

1. ESP32 上线后，在 HA 的 ESPHome 集成中添加该节点，使用本机 API 加密密钥。
2. 检查实际出现的 21 个实体：15 个按钮（13 个控制＋2 个维护）、2 个连接传感器、2 个文本传感器、2 个事件实体。
3. 以实际 HA 注册结果建立对应表。仓库保留的模板实体 ID 来自原实例；新实例可能不同，不能假定中文转写、设备前缀或后缀完全一致。这里的固件实体键与 HA `entity_id` 也不是同一概念。
4. 新建独立 JBL 仪表盘，导入 `firmware/home-assistant/jbl-4305p-dashboard.yaml`，把其中全部实体引用替换为本实例对应值；不要接管或覆盖用户原仪表盘。
5. 将 `firmware/home-assistant/jbl-connection.svg` 放到 HA 的 `/config/www/jbl-4305p/connection.svg`。模板使用 `/local/jbl-4305p/connection.svg`；首次创建 `www` 目录需要重启 HA，安排后再执行。
6. 核对只有两个视图：`control`（状态、控制和收发历史）与 `pairing`（配对说明）。不再使用独立 `history` 视图。仪表盘 URL 前缀由创建时实际路径决定。

全部卡片均为 HA 原生卡片，无需 HACS 或自定义卡。设备详情页自动展示实体；任意说明与关系图放在专用仪表盘。关系图是结构说明，两个连接实体提供实时状态。

## 7. 首次配对与维护配对

现有绑定正常时跳过本节。新板或确需修复的一侧，分别按下面流程操作。两个维护按钮只重置各自目标，不应连续反复点击。

**首次安装先关闭主音箱后背 POWER，再把原遥控器配对到 ESP32，避免原音箱抢先连回遥控器。** 遥控器连接完成后，再打开音箱并执行音箱配对流程。对于已部署设备的原遥控器维护配对，已验证可保持音箱通电；不要把这项维护验证当成首次安装步骤。

### 原遥控器 → ESP32 配对

1. 将遥控器放在 ESP32 附近，ESP32 持续供电。首次安装时主音箱后背 POWER 保持关闭；已部署设备的维护配对可保持音箱通电。
2. 点击“重新配对原装遥控器（维护）”。
3. 人类同时按住遥控器 **Streaming（网络播放）＋播放／暂停／OK** 约 **3 秒**后松开。
4. 等待“原装遥控器 → ESP32”连接，检查接收事件；音箱一侧也完成配对后，再按光纤、USB 观察音箱实际切换。

该动作只处理原遥控器一侧，保留音箱绑定；无需操作音箱旋钮。日常不要再把原遥控器直接配对回音箱。重新使用睡眠中的遥控器时，首次按键可能仅唤醒并重连，等约 5 秒再按。

### 音箱 → ESP32 配对

1. ESP32 持续供电，打开主音箱背面 POWER。等待启动、白色电源灯及输入灯亮起。
2. 在“配对说明”页点击“重新配对音箱（维护）”。
3. 人类按住音箱正面左侧 VOL/MUTE 旋钮约 **4 秒**后松开。
4. 等待“ESP32 → 音箱”连接，再从 HA 依次选择光纤、USB，观察输入指示灯。

该动作处理音箱一侧的绑定与订阅，保留原遥控器绑定。若迟迟不连接，可关闭音箱 POWER 约 10 秒再打开，待启动后重试；ESP32 全程保持供电。是否有闪灯或提示音不能单独作为成败依据。

## 8. 验收与交付

- [ ] 两侧连接正常；原遥控器就绪意味着认证和三个输入报告订阅完成。
- [ ] HA 光纤→USB、原遥控器光纤→USB 均由人类确认音箱实际切换。
- [ ] 原遥控器同键松开再按两次产生两个独立接收事件。
- [ ] HA 同键发送两次产生两个发送事件，且不增加原遥控器接收历史。
- [ ] HA 显示中文类型与秒级时间，两个历史卡片可见，无缺失实体。
- [ ] 适当验证音量增减、待机／唤醒，并记录哪些按键没有实测。
- [ ] 在明确安排的重启验证后确认绑定保留、音箱恢复，原遥控器唤醒重连后仍能控制。
- [ ] 交付版本、构建哈希、目标身份、实际 HA 实体映射和验证记录；不公开凭据。

“已提交”只证明本地协议栈接受按下报告，不证明音箱执行或后续松开报告送达。HA 离线、未到达 ESP32、超出队列容量的请求没有完整历史保证。长按过滤、拥塞和释放失败等项目若只完成源码／编译检查，交付时必须明确注明。
