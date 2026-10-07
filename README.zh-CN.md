# ESP32 LoRa SDR

**让 ESP32-S3 用自带的 2.4 GHz 射频，发送和解码完整 LoRa 包。**

采集、同步、解调、纠错、完整载荷和 CRC 都在 ESP32 上运行，电脑只显示结果。
完整双向库使用随附的 **PlatformIO / ESP-IDF 组件**；普通 Arduino 2.0.17
目前仍只支持发射，`receive()` 会明确返回 `Unsupported`。

接收端使用独立 LR2021，必须收到完整 payload、CRC 通过、逐字节相同，才算成功。
这是一项实验性实现，已经在 Seeed XIAO ESP32-S3 上做真实无线验证。

[英文主页](README.md) · [上手步骤](docs/quick-start.md) · [实测报告](docs/test-report.md) · [API](docs/api.md)

## 现在可以直接调用的接口

```cpp
#include <LoRaRadio.h>
lora_sdr::LoRaRadio radio;
// 在应用中检查每个调用返回的 Error：
radio.begin(2440.125);              // MHz
radio.setSpreadingFactor(7);
radio.setBandwidth(203.125);        // kHz
radio.setCodingRate(8);             // 4/8
radio.setTransmitPowerPercent(75);  // 幅度百分比，还不是校准后的 dBm
radio.send("Hello from ESP32!");
lora_sdr::RxPacket packet;
auto status = radio.receive(packet, 500);
// 返回 Ok 后，packet.payload / packet.length 是完整的 CRC 有效载荷。
```

完整收发入门见 [中文步骤](docs/native-guide.zh-CN.md) 和
[英文步骤](docs/native-guide.md)。最新原生收包矩阵27/31，覆盖SF7–12，高SF只有少量短包；发射以 SF7
作为已证明的档位，高 SF 发射仍不可靠。接口能设置参数不等于每个射频档位都已通过。
接收是有限窗口、半双工，采集后需要处理时间，期间会漏掉新到的信号。
[原生中文测试报告](docs/native-report.zh-CN.md) 单独保存了新测试、失败和处理延迟。

## 早期 Arduino 发射与电脑 IQ 解码记录

下面保留原来的批次，不能把其电脑解码结果当成新原生库的结果。

当前已验证的基准：2440.125 MHz，带宽 203.125 kHz，SF7，CR4/8，
前导码 16，显式包头，payload CRC，sync `0x12`。同一组 26 个载荷
（1–80 字节，含随机二进制、英文和中文）全部由 LR2021 收到且 CRC 正确。
更多长度和随机重复测试在报告中单独列出，不能把 26/26 当成所有环境都可靠。

已完成的随机参数矩阵共 **240 次，237 次成功（98.75%）**，覆盖四种纠错率和
1、8、32、80、128、250 字节，每格十个新载荷，失败不重发补成成功。

公开 RadioLib 接收器和标准 PlatformIO 刷机流程的另一个批次完成了
**239/240（99.583%）**，覆盖 1–255 字节，40 个 255 字节包全部成功。
两个批次的固件、载荷和接收程序不同，不能把它们当成受控性能对比。
仓库附有[独立 LR2021 接收器](companion/lr2021/README.md)，复现不需要私有 AeroLink 源码。
SF7 在 406.25 和 812.5 kHz 带宽下各完成了 **72/72** 个随机完整 CRC 包，
需要分别把窗口设成 7,500 和 3,750 点；这不代表更高 SF 已经可用。
另一个 48 档参数矩阵完成 **142/144**：三个频点、四种前导码、两个同步字和
两种匹配 IQ 极性。两次 CRC 错误保留，不能当成全部设置 100% 可靠。
[中文测试摘要](docs/test-report.zh-CN.md) 先列出结果和未完成项。

反向链路的新随机矩阵完成 **104/108**：公开 LR2021 发射，XIAO 自带射频采集，
**电脑**解出完整包和 CRC。覆盖SF7/8/9、四种CR、1/8/32字节；
各SF为33/36、35/36、36/36。独立接收固件源码、实时脚本和三档真实录音
已放进[接收说明](host/README.md)。这需要在XIAO上完整刷入另一个ESP-IDF应用，
不是Arduino内部完整解码。500ms窗口的IQ保留率99.18–100%，丢帧记录保留。

![两个独立实测批次](docs/assets/public-receiver-results.svg)

编码器支持 SF7–12、四种纠错率、1–255 字节，264 项板上编码交叉检查通过。
**编码正确不等于射频互通。** SF8/SF9 的当前 DAC 实测还没有接收成功。
另一条 PLL 路径已经有 SF8/SF9 的完整 CRC 收包，但小批次仅 1/10 和
4/10，仍不可靠，不能当作已支持档位。保持 DAC 幅度不变、改变模拟增益
的七档研究完成 70/70；这些寄存器码不单调，也没有校准成 dBm。
原生 Arduino 库目前没有实现完整包接收；XIAO 收包使用另一套 ESP-SDR 固件，
把 I/Q 交给电脑解码。不要把它宣传成已经完整替代普通 LoRa 收发芯片。
[电脑解码工具和真实 IQ 录音](host/README.md) 可直接下载复现完整 23 字节及 CRC；
这是一段录音回放，不能当成新的收包成功率或 ESP32 内部完整解码。

先安装 PlatformIO，在本仓库目录运行 `pio run -e xiao-s3`，再用
`pio device list` 找到 XIAO 的 USB 端口。按上手步骤刷写和打开串口。
串口输入 `DAC`，然后输入：

```text
TX 7 4 48656c6c6f2066726f6d205849414f21
```

这只发一个包。示例不会开机自动发射。LR2021 必须使用匹配的频率、带宽、SF、
sync、显式包头和 CRC。`TXEND` 仅表示发射操作结束，接收端的 CRC 与完整字节
才是成功证据。

默认 +15 kHz 是这两块测试板的测量修正值，不适用于所有设备。发射增益码和
DAC 幅度没有校准成 dBm；当前波形每个 chirp 有静默间隙，不能宣称连续无损。

我们参考了 LoLRa、ESPARGOS esp-sdr、Jochen Hammes 的 esp32-sdr-trx，
尤其是 DAC 引擎的失败记录。**不宣称世界首创。** 失败测试、限制、原始数据、
固件哈希和可复现步骤都应留在项目里。许可：GPL-3.0-only。

可选PSRAM缓冲在一个完整36次区组内保留全部输出IQ，完整CRC包34/36；长批次仍有边界中止。[早晨补测和真实IQ](docs/test-report.zh-CN.md)。

最新严格IRQ验收：224/240；全部240份payload字节完全一致。另16份含累积包头CRC错误事件，保守拒绝。旧API验收批次不能直接套用新规则，详见[接收器审计](docs/test-report.zh-CN.md)。
