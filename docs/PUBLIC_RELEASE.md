# Cardputer Codex Companion 公共发布

## 1.2.3 制品

`release/product-release.json` 是机器可读清单。公共发布包含：

- `cardputer_codex_companion-full.bin`：从 `0x0` 刷写的通用完整镜像；
- `cardputer_codex_companion.bin`：仅升级应用分区，地址 `0x20000`；
- `CardputerCompanion-mac-installer/`：macOS App、Agent、HAL 和 AudioBridge；
- `CardputerCompanion-1.2.3-windows-x64-setup.exe`；
- Windows amd64/ARM64 可移植 ZIP；
- `1.2.3-SHA256SUMS`。

## 安全边界

公共完整镜像不包含 Wi‑Fi SSID/密码、设备 PIN、Agent pairing、私有 NVS 或本机
证书。首次启动由 Cardputer 本地向导配置 Wi‑Fi，HTTPS 身份和 PIN 在设备首次
启动时生成。Machine Agent 只在局域网内连接，产品不提供离开局域网的远程控制。

发布门禁扫描所有 refs、reflog commits、保留的不可达对象、当前源码和最终
制品；报告只显示计数和位置，不打印候选 secret。以下内容禁止进入公共制品：

- `build/private/`、`dist/private/`；
- Wi‑Fi NVS blob、私有 full image；
- PIN、LAN 地址、内部域名或用户目录；
- 音频采样内容。

## 构建

需要 ESP-IDF 5.5、Swift 6、Go、NSIS、CMake、Python/uv：

```bash
scripts/verify_product_release.sh
shasum -a 256 -c dist/1.2.3-SHA256SUMS
```

门禁覆盖 Python、正常与 sanitizer host、ESP-IDF clean build、内存门槛、Web
嵌入资源、Swift 产品测试、HAL/签名、Go/race、Windows 双架构、NSIS、
通用固件无私有 provisioning、版本一致性、制品 allowlist 和 SHA-256。

## 刷写与验证

首次安装：

```bash
python -m esptool --chip esp32s3 -b 460800 \
  --before default_reset --after hard_reset \
  write_flash 0x0 dist/cardputer_codex_companion-full.bin
```

保留已配置设备状态的升级只写 `0x20000` 应用镜像。自动构建不会擅自刷写设备。
macOS 真机 HIL 与 Windows 真机安装/HIL 是独立运行门禁；没有对应硬件证据时，
发布报告必须明确标记 pending，不得由交叉编译或静态测试替代。
