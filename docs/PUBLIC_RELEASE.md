# Cardputer Codex Companion 公共发布

## 1.3.4 制品

`release/product-release.json` 是机器可读清单。公共发布包含：

- `Cardputer-Codex-Companion-1.3.4-factory.bin`：从 `0x0` 刷写的
  Factory 完整镜像；
- `Cardputer-Codex-Companion-1.3.4-app.bin`：仅升级 Factory 应用分区，
  地址 `0x20000`；
- `Cardputer-Codex-Companion-1.3.4l-launcher.bin`：M5Launcher 2.8.0+
  使用的同源兼容镜像；
- `CardputerCompanion-mac-installer/`：macOS App、Agent、HAL 和 AudioBridge；
- `CardputerCompanion-1.3.4-windows-x64-setup.exe`；
- Windows amd64/ARM64 可移植 ZIP；
- `CardputerCompanion-1.3.4-web-installer.zip`；
- `1.3.4-SHA256SUMS`。

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
cd dist && shasum -a 256 -c 1.3.4-SHA256SUMS
```

门禁覆盖 Python、正常与 sanitizer host、ESP-IDF clean build、内存门槛、Web
嵌入资源、Swift 产品测试、HAL/签名、Go/race、Windows 双架构、NSIS、
通用固件无私有 provisioning、版本一致性、制品 allowlist 和 SHA-256。

## 刷写与验证

Factory 安装会替换 M5Launcher 及设备配置：

```bash
python -m esptool --chip esp32s3 -b 460800 \
  --before default_reset --after hard_reset \
  write_flash 0x0 dist/Cardputer-Codex-Companion-1.3.4-factory.bin
```

保留 M5Launcher 时，必须先升级到 2.8.0 或更高版本，再从 Launcher 安装
`Cardputer-Codex-Companion-1.3.4l-launcher.bin`。该制品长度为
`0x621000`，最后一个 4 KiB 扇区保持擦除，用于让 Launcher 创建从
`0x620000` 开始、大小为 `0x1e0000` 的 `assets` SPIFFS 分区；该扇区不含
用户数据。保留已配置 Factory 设备状态的升级只写 `0x20000` 应用镜像，不能
用于 Launcher 或未知分区布局。`PARTITION ERROR` 表示当前运行版本所需的存储
分区缺失、类型错误或容量不足，需要通过 Launcher 重新分区或改刷 Factory
镜像。

自动构建不会擅自刷写设备。
macOS 真机 HIL 与 Windows 真机安装/HIL 是独立运行门禁；没有对应硬件证据时，
发布报告必须明确标记 pending，不得由交叉编译或静态测试替代。
