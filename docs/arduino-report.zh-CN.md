# Arduino 板上 LoRa 收发：三板实测报告

2026 年 10 月 7 日，香港时间。[完整英文报告](arduino-report.md) ·
[编译与使用](arduino-rx.zh-CN.md) · [API](api.md)

**Arduino RX 已实现，并在真实硬件上成功收发。** 程序使用 `setup()`、
`loop()` 和 `LoRaRadio`，IQ 采集、同步、解调、FEC、完整 payload 和 CRC
全部在 ESP32-S3 内执行。PlatformIO 把 Arduino 2.0.17 作为 IDF 4.4.7
组件编译；普通 Arduino IDE 仅安装 ZIP 的路径仍是 TX，RX 必须用随附工程。

两块 ESP32 已分别向 LR2021 发包、从 LR2021 收包，并成功自主 ping/pong。
电脑观察自主对传时向两块 ESP32 写入 **零个串口字节**，没有电脑编码、IQ
上传或电脑解包。有限窗口有盲区，收包仍是实验性功能，不保证可靠率。

## 硬件与判据

| 设备 | 实际识别的硬件 |
|---|---|
| A / COM3 | XIAO ESP32-S3 rev0.2，40 MHz，8 MB flash + 8 MB OPI PSRAM |
| B / COM5 | ESP32-S3 rev0.2，40 MHz，内置 8 MB OPI PSRAM + 8 MB flash；具体板型未验证 |
| COM4 | AeroLink S3 + LR2021，HF 2.4 GHz 路径，公开 RadioLib 7.7.0 companion |

2440.125 MHz、BW 203.125 kHz、preamble 16、sync `0x12`、标准 IQ、显式
header、payload CRC。ESP32 相对发射幅度 75%，频偏修正 +15 kHz；LR2021
请求 −12 dBm。实际辐射功率、距离、环境干扰和衰减没有测量。结果属于固定
室内小样本，不能当作灵敏度、距离或可靠率保证。

每个成功包都要求 **完整字节与独立生成的原始数据一致**，并且 CRC 通过。
ESP32 解码器不接受预期载荷作为输入。LR2021 还要求 raw IRQ 的 RX_DONE、
HEADER_VALID、CRC_OK 位成立，header/payload CRC error 位不得成立；混合
错误 IRQ 仍判失败。主机调度测试、自主对传、录波回放和纯软件编码测试分开计数。

## 修复后的三板结果

使用修复栈占用后的同一 bench 固件，seed `202610071709`，每方向测试
SF7 × CR4/5、CR4/8 × 8、32 字节，共四次单次发射，没有 RF 重试。

| 方向 | 完整 CRC＋字节一致 |
|---|---|
| A → LR2021 | 4/4 |
| B → LR2021 | 4/4 |
| LR2021 → A | 4/4 |
| LR2021 → B | 4/4 |
| A → B | 3/4 |
| B → A | 3/4 |

总计 **22/24**，LR2021 相关方向 **16/16**，ESP32 对传 **6/8**。两个失败为
A→B 的 CR4/5 32 字节与 B→A 的 CR4/5 8 字节；四个 CR4/8 对传样本全部成功。
16 个接收窗口的 capture status、drops、abandoned 都为零，三个成功包使用
软判决 FEC。两个失败窗口解包耗时 2.74、2.32 秒；采集无丢样不代表没有解码
失败或接收盲区。[全部原始记录](../evaluation/data/arduino-three-radios-final.json)。

另一轮发射矩阵 seed `202610071720`，CR4/5–4/8 × 1、8、32、80、255
字节，严格通过 **18/20**。四种编码率的 255 字节全通过；CR4/5、CR4/6 的
80 字节失败。[20 次原始记录](../evaluation/data/arduino-tx-final.json)。

独立 RX smoke seed `202610071721`，SF7–12 各一个新 8 字节 LR2021 包，
**6/6** 完整接收。SF7 用 CR4/8，其余用 CR4/5；SF10–12 用 900 ms 窗口。
SF12 解包耗时 **18.93 秒**。无 RF、无 payload CRC、错 sync、截断窗口
四项负例都正确拒收；10 个窗口采集状态／丢样／abandoned 均为零。这不能
证明高 SF 长包或灵敏度。[全部 smoke 记录](../evaluation/data/arduino-rx-smoke-final.json)。

最终独立 Arduino echo 测试 seed `202610071727`，CR4/5–4/8 × 8／32 字节，
板上接收 **8/8**，返回 `ACK:` 在 LR2021 严格判据下 **8/8** 全部字节一致。
主机向 ESP32 写零字节；LR2021 自己发射后明确重启 RX。
[全部双端日志](../evaluation/data/arduino-echo-final.json)。

## 自主对传和必须保留的失败

请求为 20 字节，包含 `PING`、随机 session、序号及随机数据。responder
CRC 通过后把 `PING` 改成 `PONG`；initiator 比较整个返回包。每个独立请求
发送 8 个有日志的 RF 副本，收到请求后发 8 个回复副本。等待 6.5 秒后回复，
500 ms 接收窗口，四个独立请求后停止发射。副本不能算作多个独立成功包。

修复栈之前的 reference 版本，A 发起为 **2/4**，B 发起为 **3/4**；分别
观察到 32＋24、32＋32 个请求／回复副本。原始日志全部保留。A 发起那轮还
出现一个 CRC 有效但字节错误的输出：原包末尾 `ac0b`，接收日志及回复为
`cc0b`。initiator 的逐字节判据正确拒绝它，不能算成功。这次输出走 hard
路径，没有证据把原因归到软判决列表。[原始会话](../evaluation/data/arduino-ping-pong-reference-ab.json)。

这说明 CRC 不能代替完整字节验证，更不能作为认证。需要更强完整性的应用应
加自己的 checksum 或认证封装。本报告不掩盖该事件，也不把不同版本的结果
合并成成功率。

## 发射和栈问题如何定位

原来的 15,000 样本 DAC 窗口给下一次复制留下约 255 微秒，但实际 Arduino
复制最多 72,324 CPU cycles，约 301 微秒。一个包 85 段中 **81 段迟到**，
四个请求全失败。重新取得 DAC clock 的另一轮仍是 0/4，clock 前后相同，
不能宣称是 clock 或屏蔽中断单独解决了问题。

现在 SF7 发射前测三次 SRAM 复制，按耗时加 8,192 cycles（约 34 微秒）
余量缩短播放窗口，符号周期保持不变。实测常见 13,500–13,600 样本；修复后的
自主发射没有观察到迟到段。波形仍有空隙，不能宣称连续无损 DAC 发射。

连续 bench 测试又发现 8 KiB Arduino `loopTask` 栈溢出，崩溃和未完成
测试记录保留。把 2.2 KiB 符号 workspace 改为检查分配结果的动态内存后，
重新编译、刷机并完成上述 24 项测试。每个阶段对应自己的固件哈希。

## 软件与重现

捕获数据是 250 kcomplex samples/s 的 signed 16-bit FIR 输出，存入 OPI
PSRAM；物理 ADC 仍是 10-bit。SF7 支持 FFT bit confidence 软判决、最多六个
弱 FEC 字的第二选择、最多 63 个列表候选，固定收到的 CRC 与最后两字节，
只接受唯一的完整 CRC 有效结果。`softDecoded`、`crcAided` 标明路径。
多假设也会增加漏检错误的机会。

C++ 测试通过 144 项 codec round trip、截断检查、24 项损坏 CRC 负例、
24 项软恢复、4 项 soft CRC 负例、4 项列表恢复；真实 LR2021 SF7/8/9 和
两段 ESP32 RF 录波回放通过，另有零输入／噪声／截断负例。7 项 host 测试
通过。这些不是新增的在线成功包。

五个 Arduino、三个 native、两个 stock Arduino profile 都编译通过。
52 个组件源码／配置输入逐项与 Windows ASCII staging checkout 比较，统一
CRLF/LF 后记录指纹。编译 manifest 包含 bootloader、partition 和 application
哈希；它证明本次构建身份，不保证跨电脑逐字节相同的 binary。

最终源码指纹为 `ee15d804513597eec83b887fd944680db1f1f8a48c35192a912aad2b1cd077ec`。
[构建 manifest](../evaluation/data/arduino-build-final.json) 含 52 个完整源码哈希
及五个 profile 的完整 image 哈希。bench application 为 655120 字节，SHA-256
`f8646ed4265826c6ca851c9c7ba1cc2a36e6f8a82ed17779ac70e5a76d9a5bf7`；
LR2021 application 为 320928 字节，SHA-256
`a46cbeacaf9a5dcd3d49e40d09996ef9d0791517f9e96a84548ae474596ad6ca`。
esptool 刷写哈希验证通过；记录是编译的应用，不是包含 NVS 的私有 flash 备份。

按 [指南](arduino-rx.zh-CN.md) 编译，测试用 bench，独立运行用 rx、echo 或
ping/pong。每次测量用新输出文件；不要覆盖失败。网站必须先释放串口。
自主对传先刷 responder，再刷 initiator，在开机 10 秒等待内启动观察器。
刷好后两端只用电源就能工作，不依赖网站或 Python。

## 尚未完成的能力与 GPL

接收为 50–900 ms 窗口后解码，半双工，有盲区。SF7–12 接收已实现，高 SF
实测仅是短包；SF7 发射可互通，SF8/9 发射不可靠，SF10–12 发射未支持。
连续 RX、CAD、Wi-Fi/BLE 共存、校准 dBm、LoRaWAN、弱信号提高 SF 恢复实验
尚未完成。不宣称世界首创或已达到论文验证水平。

库、链接后端及示例保持 **GPL-3.0-only**，保留上游源码、修改来源、构建文件
和声明。[LICENSE](../LICENSE) · [来源](../SOURCE_LICENSES.md) ·
[第三方声明](../THIRD_PARTY.md)。私有 AeroLink 代码、NVS 和设备备份不上传。
旧 native SDK 的测量另见 [历史原生报告](native-report.zh-CN.md)。
