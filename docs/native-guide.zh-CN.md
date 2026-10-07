# 一个 ESP32-S3 原生收发 LoRa

这套库让 ESP32 自己完成 LoRa 的采集、解调、包头、纠错、去白化、完整字节和
CRC，再用自带射频发射。应用调用 `send()` / `receive()`，不需要电脑解包。

目前完整双向版本走 **PlatformIO 的 ESP-IDF 组件**。普通 Arduino 库版本可以
发送，完整接收仍返回 `Unsupported`。实测板是 XIAO ESP32-S3，8 MB flash
和 8 MB OPI PSRAM，已连接 2.4 GHz 天线。不同板型不能直接套用未验证的配置。

安装 Git、Python 和 PlatformIO，把仓库下载到文件夹。Windows 建议用
`C:\lora-sdr` 这样的纯英文路径。在仓库根目录运行：

```sh
python examples/NativeDuplex/setup_deps.py
pio run -d examples/NativeDuplex -e xiao-native
pio device list
pio run -d examples/NativeDuplex -e xiao-native -t upload --upload-port YOUR_PORT
pio device monitor --port YOUR_PORT --baud 115200
```

把 `YOUR_PORT` 换成板子的端口，例如 `COM3`。项目锁定工具链和 DSP 依赖，
提供 PSRAM、射频 SRAM 保留与采集核的配置。照随附例程修改自己的应用即可。
默认例程只接收，开机不自动发包。

`LoRaRadio` 提供频率 MHz、带宽 kHz、SF、编码率分母 5–8（代表 4/5–4/8）、
前导码、同步字、频偏修正和发射幅度 1–100% 的设置。每个调用返回 `Error`。
幅度百分比可以重复设置，但还没有校准成天线输出 dBm。

选择 `xiao-echo` 环境可以运行明确启用的回包例程：收到 CRC 有效的完整包后，
ESP32 自己加上 `ACK:` 发回。测试程序只给 LR2021 发命令，不给 XIAO 写入
载荷或解码提示。公开接收器只适用于已验证的 AeroLink 板引脚，其他板需正确适配。

```sh
pio run -d examples/NativeDuplex -e xiao-echo -t upload --upload-port YOUR_XIAO_PORT
pio run -d companion/lr2021 -e aerolink-hf-tx -t upload --upload-port YOUR_LR2021_PORT
python evaluation/verify_native_echo.py --xiao YOUR_XIAO_PORT --lr2021 YOUR_LR2021_PORT --output echo-result.json
```

当前原生射频接收带宽为 203.125 kHz，已有 SF7–12 完整包实测，高SF仅少量短包；编码器能处理
SF7–12，不能据此宣称所有 SF 的射频收发都可靠。SF7 发射已证明，高 SF 发射
仍属实验。发送支持 1–255 字节，接收的完整包必须落在 50–900 ms 的采集窗口内。

接收以 250 kcomplex samples/s、8-bit I 和 8-bit Q 保存连续窗口，随后在 ESP32
解包。处理期间会漏掉新到的信号，尤其高 SF 耗时更长。`lastReceive()` 保留
采集状态、样本数、丢失和解码耗时。它目前不是全时连续监听的专用 LoRa 芯片替代品。

参见[完整 API 与边界](native-guide.md)、[新的原生实测](native-report.md)及
[历史报告](test-report.zh-CN.md)。失败、严格 CRC 拒绝和各批次分母都保留。
