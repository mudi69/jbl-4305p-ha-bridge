# AI deployment contract

简体中文 | [English](AGENTS.en.md)

本仓库建议由 AI 直接执行准备、编译、部署与验证。人类负责确认设备归属、接通电源、操作配对及观察音箱指示灯。先阅读 README.md、docs/DEPLOYMENT.md、docs/COMPATIBILITY.md，再操作。

## Authoritative files

- `firmware/esphome/jbl-4305p-bridge.yaml`：部署配置；组件固定到 ESPHome 2026.9.1、ESP-IDF 5.5.5。
- `firmware/esphome/components/`：必须一同部署的四个本地组件。
- `jbl_protocol.json`：13 个实测按键、HID Report Map、GATT 结构；原始抓包仅在项目拥有者本地保留。
- `evidence/validation-summary.json`：实测范围；不要把协议推断当成第二套硬件验证。
- `firmware/home-assistant/`：控制与收发历史合并页面、独立配对说明、连接图。
- `releases/2026.10.17/manifest.json`：公开示例产物的确切构建信息与限制。

## Required deployment inputs

确认目标 ESP32 型号、Flash 大小、唯一 USB 身份或已确认的 OTA IP；确认主音箱 BLE **public** MAC 和原遥控器 BLE **public** MAC；取得本次部署的 Wi-Fi 和 HA 接入信息。音箱 Wi-Fi MAC/IP 不可充当 BLE MAC。不要根据历史 COM 号或第一个串口猜测目标。

缺少输入时，先完成不依赖它的源码检查和说明，再仅询问缺失项。已有明确授权和足够设备证据后执行部署，不反复询问同一授权。刷写其他设备、清除 Flash、清除配对或扩大网络访问均不属于默认范围。

## Execution checklist

1. 读取当前状态，备份现有配置及当前固件；保存用户尚未提交的修改。不要把其他 ESPHome 设备的配置替换成此项目。
2. 使用隔离 Python 环境和 `requirements-build.txt`。Windows 建议 ASCII 路径工作副本/构建目录，避免工具链路径问题。
3. 按 `secrets.example.yaml` 创建忽略提交的 `secrets.yaml`；每台设备生成独有 API key 与 OTA 密码。凭据不打印、不上传 Git，不复制原开发者的绑定信息。
4. 填写两端 MAC。两端仍严格按身份过滤，不允许以关闭认证或接受任意对端来解决连接失败。
5. 运行配置映射检查、组件 provenance 检查、事件映射检查、名称 codegen 检查并完整编译。不能只验证 YAML 就宣称固件可用。
6. 只向已确认的 ESP32 刷写。首次使用 USB；已有正常设备优先使用匹配凭据的 OTA。不得把 `.factory.bin` 当成应用偏移文件，也不得把公开示例产物作为真实家居可用固件。
7. 按部署指南依次配对原遥控器与音箱；维护按钮各自只处理对应一侧。首次重连可能需要按一次唤醒、等待约 5 秒，再按实际操作键。
8. 在 HA 发现真实实体 ID 后适配页面 YAML。现有示例实体 ID 不是所有 HA 实例的保证值。先备份仪表盘，再通过 HA 支持的界面/API 保存，不直接修改运行中的 `.storage`。
9. 验证原遥控和 HA 的光纤→USB、音量、待机/唤醒，以及 ESP32 重启后的恢复。用户按键与观察无法由日志代替。
10. 核验中文收发事件分开记录、同键重复有独立事件。`已提交`仅为本地蓝牙协议栈接受按下报告，不是音箱执行 ACK。
11. 关闭临时监听，保留 HA/ESP32 正常运行。交付设备地址、版本、校验和、验证结果、未验证项及回退步骤；脱敏后更新档案。

## Release boundaries

- 现网实测版本与新增 MAC 参数化版本的验证范围必须分开表述；不要为“已上传”自动刷写现网。
- `captures/`、`tools/`、`.local/`、`secrets.yaml`、现网完整 Flash/NVS、日志、Wi-Fi/API/OTA 凭据及私有 `.bin` 禁止进入公开发布。
- 公开产物仅从明确公开的示例配置重新构建。提交前检查暂存区、二进制、压缩包和所有 Git 历史；同时扫描真实凭据的文本和 API key 解码形式，输出发现位置而非凭据。
- 保留上游许可、来源 commit 与修改文件哈希。更改组件后更新相应 provenance。
- 发布检查覆盖暂存内容及所有本地分支、标签、远程跟踪分支的历史。桌面应用的私有恢复引用（如 `refs/codex/`）仅留本地，不属于发布内容；只显式推送目标分支/标签，禁止 `push --mirror`。压缩包须展开单独审查；缺少私有凭据基线时不能声称完成真实凭据扫描。
- 单套音箱/遥控器为设计范围。新型号、不同 GATT、random 地址、多套并行、首次按键保留、长期稳定性均不能自行宣称支持。
