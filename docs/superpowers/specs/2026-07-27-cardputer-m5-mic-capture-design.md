# Cardputer M5.Mic 采集后端设计

## 背景与根因

当前固件直接使用 ESP-IDF `i2s_pdm_rx`，其 mono 默认配置选择
`I2S_PDM_SLOT_LEFT`。M5Unified 对 Cardputer 的板级配置将 GPIO46 设为数据、
GPIO43 设为 PDM 时钟，并使用 `input_only_right`。因此现状可以稳定生成
16 kHz 音频帧，却持续读取未接麦克风的 PDM 槽，Mac 侧表现为无输入电平。

直接 I2S 路径还绕过了 M5Unified 的板级音频处理，包括自动零点校正、
oversampling 和 `magnification`。用户选择不继续维护这条自定义硬件路径，
而是完全改用 M5Unified 的 `M5.Mic`。

## 设计

保留产品层 `IAudioCapture`、麦克风状态机、IMA-ADPCM、BLE Audio GATT 和
Mac HAL 协议不变，只替换 `AudioCaptureBackend` 的 ESP32-S3 实现。

新后端通过 `M5.Mic.config()` 明确设置：

- `pin_data_in = GPIO_NUM_46`
- `pin_ws = GPIO_NUM_43`
- `pin_bck = I2S_PIN_NO_CHANGE`，选择 PDM 模式
- `input_channel = input_only_right`
- `over_sampling = 1`
- `magnification = 16`
- `sample_rate = 24000` 或 `16000`

`enable()` 调用 `M5.Mic.begin()` 并预先排入两个固定 PCM 缓冲区。
`read()` 等待最旧缓冲区完成，将其复制到调用方后立即重新排队，从而让
M5Unified 自带任务持续采集，避免逐帧 begin/end 或采集间隙。等待上限仍为
100 ms；超时返回既有 `AudioCaptureResult::timeout`。`disable()` 调用
`M5.Mic.end()` 并清空队列状态。Speaker 互斥仍由现有产品层保证。

所有缓冲区在对象构造时固定分配；启动后不进行堆分配。不会记录、落盘或输出
PCM/ADPCM 内容。

## 失败处理

- M5.Mic 初始化或排队失败：返回 `backend_error`，UI/Web 显示既有
  `MIC_INIT_FAILED`。
- 等待录音完成超过 100 ms：返回 `timeout`，沿用现有可恢复路径。
- BLE 压力、24→16 kHz 降级、断连停止和 G0 本地启停语义保持不变。
- 停止操作幂等；再次以不同采样率启动时重新应用 M5.Mic 配置。

## 验证边界

先用 host 测试固定采样率、帧长、生命周期和双缓冲完成顺序；再执行
ESP-IDF 构建。仅刷写 `0x20000` 应用分区，保留 NVS、Wi-Fi、PIN、Profile、
宠物和 BLE bonds。

实机门禁包含：

- 串口模拟 G0 启停成功；
- 16 kHz 连续帧、无重启、无 source overrun；
- Mac 虚拟输入设备在近场发声时出现非零峰值/RMS；
- 静音与发声窗口有可区分的电平；
- HID、HTTPS、堆、栈和 BLE 重连回归通过。

电平验证只保留统计量，不保存音频内容。
