# Arduino 收发

`ArduinoDuplex` 使用真正的 Arduino `setup()`、`loop()`、`Serial` 和
`LoRaRadio` API。PlatformIO 将 Arduino 2.0.17 作为 ESP-IDF 4.4.7 组件编译，
以便配置接收所需的 CPU、RF SRAM 和 OPI PSRAM。同步、解调、纠错、完整包
恢复以及 payload CRC 都在 ESP32-S3 上执行，不需要电脑解包或网站。

**接收必须使用这个构建 profile。** 普通 Arduino IDE 安装 ZIP 不会重新编译
SDK，也不会改变双核配置；旧的 stock Arduino 路线仍只支持 TX，RX 返回
`Unsupported`。不要手工定义 `LORA_SDR_NATIVE_BACKEND` 假装启用 RX；这个宏
必须对应已链接的接收后端。

## 编译与刷机

使用带 8 MB flash、8 MB **OPI PSRAM** 的 XIAO ESP32-S3，接好 2.4 GHz
天线和 USB 数据线。Windows 请使用 ASCII 路径，例如 `C:\lora-sdr`。
从仓库根目录执行：

```sh
python -m pip install platformio==6.1.19
python examples/NativeDuplex/setup_deps.py
pio run -d examples/ArduinoDuplex -e xiao-arduino-rx
pio device list
pio run -d examples/ArduinoDuplex -e xiao-arduino-rx -t upload --upload-port YOUR_PORT
pio device monitor --port YOUR_PORT --baud 115200
```

把 `YOUR_PORT` 换成实际串口，并先关闭占用它的软件。首次编译会下载锁定版本
的 Arduino 和 IDF。PlatformIO 可能显示使用默认 variant 的通用警告，但本
工程通过 SDK 配置明确选择了 `XIAO_ESP32S3`。

修改 [Arduino 程序](../examples/ArduinoDuplex/main/main.cpp) 即可使用库。
`xiao-arduino-rx` 默认只接收；`xiao-arduino-echo` 明确启用收到 CRC 有效包
之后发送 `ACK:` 加原始 payload，原始 payload 最大 251 字节。开机不发包。
刷好后电脑只用于可选日志，射频处理由板子独立完成。

## 两块 ESP32 对传

先编译两种角色，先刷 responder，再刷 initiator：

```sh
pio run -d examples/ArduinoDuplex -e xiao-arduino-pong -e xiao-arduino-ping
pio run -d examples/ArduinoDuplex -e xiao-arduino-pong -t upload --upload-port RESPONDER_PORT
pio run -d examples/ArduinoDuplex -e xiao-arduino-ping -t upload --upload-port INITIATOR_PORT
```

initiator 开机等待 10 秒，然后尝试四个 20 字节请求，包含随机 session、
序号和随机数据。responder 完整解包并通过 CRC 后，将 `PING` 改成 `PONG`
返回；initiator 要求整个返回包与请求内容匹配且 CRC 通过。为覆盖采集/解包的
盲区，请求发送 8 个副本，两次调用之间等待 350 ms；每次发射后可选 USB 日志
另外等待 200 ms，实际间隔还包含波形生成和 airtime。responder 解包后等待
6.5 秒，再用相同延迟发送 8 个回复副本。initiator 以 500 ms 窗口监听，最多等待 18 秒，
下一请求之前等待 5.5 秒。副本是同一包的独立 RF 发射，分别记录；库本身不自动
重试。四个独立请求后停止发射，
再次启动需要给 initiator 重新上电。两端仅接 USB 电源即可运行，日志可选。
成功率以实际 RF 报告为准。`xiao-arduino-bench` 只是另外的主机调度测试
程序，解包仍在 ESP32 上，不是应用或 ping/pong 的必需依赖。

核心调用如下；完整 `setup()`/`loop()` 示例见 [英文指南](arduino-rx.md)：

```cpp
RxPacket packet;
Error status = radio.receive(packet, 500);
if (status == Error::Ok) {
    Serial.write(packet.payload, packet.length);
    // 可选：radio.transmit(packet.payload, packet.length);
}
```

对端采用 2440.125 MHz、BW 203.125 kHz、SF7、preamble 16、sync `0x12`、
标准 IQ、显式 header 和 payload CRC。显式 header 包含收到的 CR 和长度。
独立对端测试使用 [LR2021 HF companion](../companion/lr2021/README.md)。

RX 后端支持 203.125 kHz、SF7–12 和 50–900 ms 有限采集窗口。采集结束后
才解包，解包和发射期间不能接收；高 SF 可能需要数秒至数十秒。Arduino
4.4.7 profile 的实测结果必须单独记录，不能直接套用较新 native SDK 的结果。

这个 SDK profile 把 RTOS/Arduino 放在 core 0，将 core 1 留给采集，并保留
三个 RF SRAM bank；IQ 存在 OPI PSRAM。有限采集期间屏蔽中断，因此关闭了
interrupt/task watchdog。不要同时启用 Wi-Fi/BLE 或在 core 1 建应用任务。
示例固定 Arduino loop stack 为 8 KiB，并缩小不用的 Wi-Fi buffer pool，
为采集 worker 留出连续内存；改写 sketch 时请保留这个 stack override。
这些是实验 SDR 的要求。参数、错误与诊断见 [API 文档](api.md)。网站是另外
可选的 serial bench 程序，不是使用库的前提。
