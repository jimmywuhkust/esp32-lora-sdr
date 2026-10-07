# 用 Arduino 收发 LoRa

先用随附的 PlatformIO 工程刷入接收程序，让另一台电台发一个包过来。
确认收到后，再把示例改成自己的程序。

**Arduino IDE 安装 ZIP 目前不能接收。** 只需要发射时，用
[SendOnce.ino](quick-start.md)；需要收发时按下面步骤操作。

## 1. 下载工程

准备带 8 MB flash、8 MB OPI PSRAM 的 XIAO ESP32-S3，接好 2.4 GHz 天线，
使用 USB 数据线。还需要一个发包的对端：另一块运行本项目的 ESP32，或参数
匹配的 2.4 GHz LoRa 电台。

安装 Python，下载仓库 ZIP 并解压，也可以用自己的 GitHub 账号克隆。
Windows 建议放在 `C:\lora-sdr`。在仓库目录打开终端：

```sh
python -m pip install platformio==6.1.19
python examples/NativeDuplex/setup_deps.py
```

setup 命令下载 DSP 依赖，第一次编译会下载编译器和 Arduino 包。

## 2. 刷入接收程序

```sh
pio run -d examples/ArduinoDuplex -e xiao-arduino-rx
pio device list
```

从列表找到板子的串口，用它替换下面的 `YOUR_PORT`，例如 Windows 的 `COM3`。
先关闭占用该串口的其他软件，再运行：

```sh
pio run -d examples/ArduinoDuplex -e xiao-arduino-rx -t upload --upload-port YOUR_PORT
pio device monitor --port YOUR_PORT --baud 115200
```

开机应出现 `ARDUINO_BEGIN status=ok`，随后
持续打印 `LISTEN arduino sf=7 window=500`。

## 3. 发一个包过来

把对端设为：

| 参数 | 值 |
|---|---|
| 频率 | 2440.125 MHz |
| 带宽 | 203.125 kHz |
| SF | 7 |
| 编码率 | 4/8 |
| Preamble | 16 symbols |
| Sync word | `0x12` |
| 包头 | 显式 |
| Payload CRC | 开启 |
| IQ | 标准 |

先发一个短包。接收端会打印 `ARDUINO_RX crc_ok=1`、长度和完整 payload 的
hex，下一行是采集诊断。[LR2021 companion](../companion/lr2021/README.md)
可以作为已测试的对端。

想让这块板收到包后回复，改刷 echo：

```sh
pio run -d examples/ArduinoDuplex -e xiao-arduino-echo -t upload --upload-port YOUR_PORT
```

它回复 `ACK:` 加收到的字节。输入最多 251 字节，给四字节前缀留出空间。
在对端检查完整回复及 CRC。刷好后可只接 USB 电源运行，串口日志可选。

## 两块 ESP32 对传

这个测试先保留原来的示例文件。接上两块板，用 `pio device list` 找到端口，
先刷 responder，再刷 initiator：

```sh
pio run -d examples/ArduinoDuplex -e xiao-arduino-pong -t upload --upload-port RESPONDER_PORT
pio run -d examples/ArduinoDuplex -e xiao-arduino-ping -t upload --upload-port INITIATOR_PORT
```

initiator 等待十秒，发出四个不同的请求。responder 解包后把 `PING` 改成
`PONG`，返回其余原始字节。打开 initiator 的日志：

```sh
pio device monitor --port INITIATOR_PORT --baud 115200
```

`PING_RESULT ... crc_exact_pong=1` 表示收到完整匹配的回复并通过 CRC。
`PING_FINISHED` 显示成功次数。给 initiator 重新上电可再跑一轮，交换两块板
的程序可以检查另一方向。

每个请求／回复各发八个副本，用来覆盖接收盲区，副本逐个记录；四个独立请求
后停止发射。[两轮实测](arduino-report.zh-CN.md) 各完成 3/4 往返，并非每包必达。

## 写自己的程序

把 [`main/main.cpp`](../examples/ArduinoDuplex/main/main.cpp) 换成
[英文指南中的完整接收程序](arduino-rx.md#write-your-own-program)，保留其余
工程文件，继续用 `xiao-arduino-rx` 编译和刷机。

在 `setup()` 里 `radio.begin(settings)` 成功后可以发文字：

```cpp
Error status = radio.transmit("Hello from ESP32!");
```

在 `loop()` 收取完整字节：

```cpp
RxPacket packet;
Error status = radio.receive(packet, 500);
if (status == Error::Ok) {
    Serial.write(packet.payload, packet.length);
}
```

二进制发包用 `radio.transmit(bytes, length)`。检查返回状态，并在对端确认送达。
`receive()` 先采集，再解码，成功时返回完整 CRC 有效 payload。其余参数和错误
码见 [API](api.md)。

## 没有收到时

- 刷机打不开端口：关闭其他串口监视器和 web bench。
- 初始化不是 `ok`：看错误码，确认 OPI PSRAM，保留随附工程设置。
- 一直监听但没包：核对两端天线和表格中的全部参数，先用 SF7、CR4/8 短包。
- 本机 TX 返回成功：还要在另一端检查完整字节和 CRC，才能确认送达。

解码和发射期间不能接收。当前支持 203.125 kHz、SF7–12 接收，高 SF 解码可能
耗时数秒。已演示的发射是 SF7，SF8/9 发射不可靠，SF10–12 未支持；功率不是
校准的 dBm。[测试报告](arduino-report.zh-CN.md) 保留漏包及一个中间版本输出
CRC 有效但字节错误的样本。

## 构建说明

工程使用 Arduino 2.0.17 和 ESP-IDF 4.4.7。RX 需要预留 RF 内存、OPI PSRAM
和专用采集核，安装一个库 ZIP 不会改变普通 Arduino core 的构建配置。

随附文件把 Arduino 放在 core 0，采集放在 core 1，预留 SRAM，关闭 watchdog，
把 loop 栈设为 8 KiB。改写示例时保留这些文件和 stack override；不要同时启用
Wi-Fi/BLE，也不要在 core 1 建任务。PlatformIO 的默认 variant 通用警告可以
出现，SDK 配置实际选择 `XIAO_ESP32S3`。

`xiao-arduino-bench` 是独立的串口命令测试程序，用于
[测量脚本](../evaluation/README.md) 或可选网页；自己的程序调用 `LoRaRadio`
不需要它。

对传示例的具体时序：副本间等待 350 ms，每次还包含波形生成、airtime 和
200 ms 日志等待。responder 等待 6.5 秒再回复。initiator 用 500 ms 窗口接收，
最多等 18 秒，下一请求之前等 5.5 秒。
