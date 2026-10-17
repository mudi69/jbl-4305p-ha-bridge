# 2026.10.17 公开示例固件 / Public example firmware

**真实部署建议让 AI 按根目录 AGENTS.md 和部署指南，用你自己的私有参数重新编译。** 本目录固件采用公开演示 Wi-Fi、API/OTA 密钥和两端 MAC，不含原家庭配置，不能直接用于控制你的音箱。没有运行时配置门户；不要 OTA 覆盖正在工作的设备。

**For real use, ask an AI to follow AGENTS.en.md and the deployment guide, and rebuild with your private settings.** These binaries contain public demonstration Wi-Fi, API/OTA credentials and MAC addresses. They cannot control your installation as-is. There is no runtime provisioning portal. Do not OTA them over a working device.

| 文件 / File | 用途 / Purpose |
| --- | --- |
| `firmware.factory.bin` | ESP32-C3 4 MB，首次 USB 合并镜像，起始偏移 `0x0` / merged first-flash image at `0x0` |
| `firmware.ota.bin` | 应用镜像格式，仅供构建核验 / application image format, for build verification |
| `partitions.csv` | 分区表 / partition layout |
| `sdkconfig.example` | 本次构建实际 ESP-IDF 配置 / actual ESP-IDF build configuration |
| `dependencies.resolved.json` | 依赖解析摘要，缓存路径已脱敏，不是可直接恢复的 lockfile / resolved metadata with cache paths removed, not a restorable lockfile |
| `public-example.secrets.yaml` | 可公开的示例参数 / intentionally public example settings |
| `manifest.json` | 输入与产物 SHA-256、版本、偏移及验证边界 / input and artifact hashes, versions, offsets and validation scope |
| `SHA256SUMS` | 产物及 manifest 校验和 / checksums of artifacts and manifest |

## 重建 / Rebuild

先按 [中文部署指南](../../docs/DEPLOYMENT.md) / [English deployment guide](../../docs/DEPLOYMENT.en.md) 创建 Python 3.13 环境并安装 `requirements-build.txt`。从仓库根目录运行 / From the repository root:

```powershell
.\.venv-build\Scripts\python.exe scripts/build_release.py --build-root C:\jbl-public-build
```

```sh
.venv-build/bin/python scripts/build_release.py --build-root /tmp/jbl-public-build
```

脚本每次建立独立目录，复制公开参数，完整编译并校验源文件未改变，再生成 manifest。不会读取真实 secrets.yaml，也不会刷写设备。构建目录保留供诊断。构建包含时间戳，不承诺不同机器或时间逐字节一致。

The script creates a fresh isolated directory, copies only public settings, compiles, rejects source changes during the build, and generates the manifest. It neither reads the private secrets file nor uploads firmware. Build directories remain for diagnostics. Build timestamps mean bit-for-bit identity across machines or times is not promised.

## 验证 / Validation

原固定地址版本已实测；本次双 MAC 参数化版本经独立源码审查和完整编译，未刷写现用设备，也未在第二套同型号硬件实测。详见 [验证摘要](../../evidence/validation-summary.json) 与 manifest。

The original fixed-address version was physically tested. This parameterized release received independent source review and a complete compile; it was not uploaded to the current device or tested on a second hardware pair. See the [validation summary](../../evidence/validation-summary.json) and manifest.

Linux 可运行 `sha256sum -c SHA256SUMS`；Windows 可用 `Get-FileHash <file> -Algorithm SHA256` 对照校验。发布前在仓库根目录暂存完整公开文件，再运行 `python scripts/check_archive.py`，检查暂存内容与构建输入的对应关系。

On Linux run `sha256sum -c SHA256SUMS`; on Windows compare `Get-FileHash <file> -Algorithm SHA256`. Before publishing, stage the complete public tree and run `python scripts/check_archive.py` from the repository root to verify staged content against the build manifest.
