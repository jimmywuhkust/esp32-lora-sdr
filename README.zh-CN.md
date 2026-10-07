<p align="center"><img src="docs/assets/hero.svg" alt="ESP32 LoRa SDR 原生收发库" width="100%"></p>

# ESP32 LoRa SDR

**用 ESP32-S3 自带的 2.4 GHz 射频收发完整 LoRa 包的 C++ 库。**

应用直接调用 `begin()`、设置参数，再调用 `transmit()` / `receive()`。
波形生成、解调、纠错和完整包 CRC 都在 ESP32 内完成，不需要外接 LoRa 芯片、
网站或电脑解包。空中通信需要另一台参数匹配的无线设备作为对端。

[英文主页](README.md) · [Arduino 收发入门](docs/arduino-rx.zh-CN.md) ·
[API](docs/api.md) · [Arduino 实测](docs/arduino-report.zh-CN.md) · [已有工作](docs/prior-art.md)

当前是实验性库，已验证 **Seeed XIAO ESP32-S3，8 MB flash + 8 MB OPI PSRAM**。
**Arduino 已支持收发**：使用随附的 PlatformIO ArduinoDuplex 工程，把
Arduino 2.0.17 作为 IDF 4.4.7 组件运行，应用仍是 `setup()` / `loop()`，
完整解包和 CRC 在 ESP32 上完成。仅把 ZIP 安装进普通 Arduino core 仍是 TX
路径，接收需要 duplex 工程。也提供纯 ESP-IDF 工程。底层使用未公开的射频
寄存器和 SDK PHY 函数，其他板型、SDK 不能直接视为兼容。

## 在自己的程序里调用

单位和调用习惯参考常见 LoRa 库：MHz、kHz、SF、编码率分母。检查返回状态。

```cpp
#include <LoRaRadio.h>
using namespace lora_sdr;

LoRaRadio radio;
LoRaSettings settings;
settings.frequencyMHz = 2440.125;
settings.bandwidthKHz = 203.125;
settings.spreadingFactor = 7;
settings.codingRate = 8;              // 4/8，可选 5、6、7、8
settings.transmitPowerPercent = 75;   // 相对幅度，尚未校准为 dBm

// 放在自己的应用函数内：
Error status = radio.begin(settings);
if (status != Error::Ok) return;
status = radio.transmit("Hello from ESP32-S3!");
RxPacket packet;
status = radio.receive(packet, 500);  // 采集 500 ms，再在芯片内解码
if (status == Error::Ok) {
    // packet.payload[0..packet.length) 是完整 CRC 有效载荷。
}
```

运行时可以用 `setSpreadingFactor()`、`setCodingRate()`、`setBandwidth()`、
`setFrequency()`、`setPreambleLength()`、`setSyncWord()` 和
`setTransmitPowerPercent()` 改参数。`configure(settings)` 先验证整个配置，
非法配置不会部分生效。二进制发包用 `transmit(bytes, length)`，1–255 字节；
`send()` 是等价别名。调用形式参考 RadioLib，但并非 SX1262 驱动的直接替换。
本地 TX 返回成功也不等于对端已收到。

## 编译独立接收或收包后回复的程序

安装 Python、Git 和 PlatformIO。下载 ZIP 或通过 GitHub 权限克隆；开发期仓库
为 private。Windows 建议放在 `C:\lora-sdr` 等纯英文路径。从仓库根目录运行：

```sh
python -m pip install platformio==6.1.19
python examples/NativeDuplex/setup_deps.py
pio run -d examples/ArduinoDuplex -e xiao-arduino-rx
pio device list
pio run -d examples/ArduinoDuplex -e xiao-arduino-rx -t upload --upload-port YOUR_PORT
```

修改 [Arduino 示例](examples/ArduinoDuplex/main/main.cpp) 就可以直接调用库。
`xiao-arduino-rx` 默认接收；选择 `xiao-arduino-echo` 后，ESP32 在 CRC 有效的包到达后发送
`ACK:` 加原始载荷，全程无需电脑指令。刷好后 USB 日志可选。
[Arduino 中文入门](docs/arduino-rx.zh-CN.md) 包含 PSRAM/core 配置、对端参数、
收发时序和双 ESP32 ping/pong 示例。纯 `app_main()` 应用可用
[原生工程](docs/native-guide.zh-CN.md)。

普通 Arduino 发射可安装仓库 ZIP，选择 XIAO ESP32-S3 / core 2.0.17，打开
[SendOnce](examples/SendOnce/SendOnce.ino)。[Arduino 步骤](docs/quick-start.md)
还介绍了对应的 PlatformIO 构建。

## 真实验证到的能力

| 操作 | 已测范围与边界 |
|---|---|
| Arduino ↔ LR2021 | 两块 ESP32 分别收发，四条方向 **16/16**：SF7，CR4/5、4/8，8／32 字节。 |
| ESP32 ↔ ESP32 | 主机调度的单包 **6/8**，四个 CR4/8 样本全通过；交换发起角色的自主 ping/pong 也成功，独立会话次数见报告。 |
| Arduino 发射 | SF7、CR4/5–4/8、1–255 字节，严格 CRC＋逐字节 **18/20**；四种 CR 的 255 字节全部通过，漏包保留。 |
| Arduino 接收 | 203.125 kHz，SF7–12 各一个新 8 字节包，**6/6**；四项负例拒收。这是短包 smoke。 |
| 无电脑指令 Arduino echo | 板上收到 **8/8**；LR2021 严格收到完整 ACK **8/8**，没有 ESP32 串口指令。 |
| 参数 | 频率、SF、CR、带宽、前导码、sync、相对发射幅度和频偏修正；[支持范围](docs/native-guide.zh-CN.md)。 |

上述是固定室内三板的不同批次，不能合并成可靠率或距离保证。
[Arduino 报告](docs/arduino-report.zh-CN.md) 保留完整载荷、失败、IRQ/CRC 判据、
固件哈希和图表。中间版本曾输出一个 CRC 有效但字节错误的包，自主发起端
拒绝了对应回复；需要更强完整性的应用应另加 checksum 或认证封装。
[旧原生报告](docs/native-report.zh-CN.md) 和 [早期报告](docs/test-report.zh-CN.md) 另存 Arduino 发射及电脑 IQ
解码实验，不能当作原生收包结果。

接收是 50–900 ms 有限采集窗口，250 kcomplex samples/s，随后在 ESP32 内处理。
半双工，解码期间有盲区，高 SF 可能耗时数十秒。SF8/9 发射不可靠，SF10–12
发射未支持。Wi-Fi/BLE 共存、校准 dBm、连续接收、CAD 和 LoRaWAN 尚未实现。

## 可选调试工具

串口 bench 可以配合网页显示双板发包、完整字节和 CRC。
[网页实测证据](docs/native-report.zh-CN.md) 只作为调试展示；库本身、独立接收
和 echo 示例都不依赖网站。电脑仅用于安装、编译、刷机和可选查看日志。

## 原理与来源

编码器生成显式包头、payload CRC、白化、纠错、交织和 LoRa 符号；底层通过
ESP32-S3 内部 2.4 GHz 射频播放 I/Q 窗口，接收 IQ 存入 PSRAM，由 C++ 完整解码。
当前 DAC 发射符号间有静默间隙，不能宣称连续无损波形。

参考 [ESPARGOS](https://github.com/ESPARGOS/esp-sdr)、
[Jochen Hammes](https://github.com/jochenhammes/esp32-sdr-trx/tree/research/iq-tx)
和 [CNLohr](https://github.com/cnlohr/lolra)，不宣称世界首创。
[调研记录](docs/prior-art.md) 说明参考实现和失败方法。许可 GPL-3.0-only，
[第三方声明](THIRD_PARTY.md)。使用符合所在地要求与硬件条件的射频设置。
