# Networking 模块

> Status: 🟢 current · Last reviewed: 2026-05-18

Wi-Fi + 蓝牙双通道。ESP32-P4 主控**自身没有 Wi-Fi / BT 射频**，无线能力全部来自 **ESP32-C6 协处理器模组**（板上 U6 = Cross Air OA-W01），通过 `esp_hosted` SDIO/SPI 透传协议挂在 P4 侧。

主板：BY-SA-V001 / 项目：Lunawake (BY001)。

---

## 1. 主控方案：P4 + C6 双芯架构

```
┌─────────────────┐                ┌──────────────────────┐
│ U5 主控          │  esp_hosted    │ U6 协处理器模组       │
│ JC-ESP32P4-M3   │ ──透传 SDIO/── │ Cross Air OA-W01     │
│ (双核 RISC-V    │   SPI          │ (ESP32-C6 + 天线)    │
│  IDF v5.5)      │ ←───────────── │  Wi-Fi + BT          │
└─────────────────┘                └──────────────────────┘
       │                                       │
   雷达 SPI / I²C / I²S /                  Wi-Fi 数据链路（WS）
   PDM 麦 / GPIO / Flash                   BLE 配网（BluFi）
```

| 项 | 规格 |
|---|---|
| 主控模组 | **JC-ESP32P4-M3**（嘉立创 ESP32-P4 模组，含 16 MB Flash + 8 MB PSRAM） |
| 协处理器模组 | **Cross Air OA-W01**（含 ESP32-C6 + 板载天线） |
| Wi-Fi | **2.4 GHz Wi-Fi 6（802.11ax）** —— C6 仅原生 2.4 G 单频。网表 description 标 "2.4G/5G" 是把代际名 "Wi-Fi 6" 误写成了频段 "5G"，实际 **不支持 5 GHz 频段** |
| 蓝牙 | **Bluetooth 5（LE）** —— C6 提供。P4 自身无 BT 射频 |
| 802.15.4 | **支持** —— C6 原生 Thread / Zigbee / Matter。**对 Luna Matter Controller 有直接价值**，未来若做 Thread 邻边路由不用外加芯片 |
| 主控接口 | `esp_hosted` 透传（Wi-Fi + BLE 共用一条命令链路，SDIO 或 SPI） |
| 软件栈 | `esp_hosted` + `esp_wifi_remote`（managed_components） |
| 天线 | OA-W01 模组板载天线，无外置天线接口 |

### C6 内部规格（来自乐鑫数据手册）

| 项 | 规格 |
|---|---|
| 高性能 CPU | RISC-V 32-bit，最高 160 MHz |
| 低功耗 CPU | RISC-V 32-bit，最高 20 MHz |
| 内存 | 512 KB SRAM + 320 KB ROM；支持外接 flash |
| GPIO | 30 (QFN40) / 22 (QFN32) 可编程 |
| 接口 | SPI、UART、I²C、I²S、RMT、TWAI、PWM、MCPWM、SDIO |
| ADC | 12-bit + 温度传感器 |
| 协议 | Wi-Fi 6、BLE 5、802.15.4（Thread / Zigbee） |

---

## 2. 软件栈

## 2. 软件栈

| 功能 | 组件 | 来源 |
|---|---|---|
| Wi-Fi/BLE 透传 | `esp_hosted` + `esp_wifi_remote` | `managed_components/` |
| Wi-Fi STA | `esp_wifi` | IDF 内置 |
| BluFi 配网 | `esp_blufi`（`CONFIG_BT_BLE_BLUFI_ENABLE=y`） | IDF 内置 |
| mDNS 服务发现 | `mdns` | IDF 内置 |
| WebSocket 服务端 | `esp_http_server`（支持 WS frame） | IDF 内置 |
| JSON | `cJSON` | IDF 内置 |
| NVS 凭据存储 | `nvs_flash` | IDF 内置 |

## 3. 链路用途

| 链路 | 协议 | 路径 | 端口 | 角色 |
|---|---|---|---|---|
| 配网 | BLE / BluFi | — | — | App 一次性发送 SSID + PWD |
| 数据 | Wi-Fi / WebSocket | `/ws` | 8080 | ESP 服务端，App 客户端 |
| 发现 | mDNS | `_lunawake._tcp.local` | — | App 解析 IP；失败兜底手动输 |
| OTA | Wi-Fi / HTTP | 待定 | 待定 | 固件升级（未实现） |
| Matter / Thread | 802.15.4（C6 原生） | — | — | 未启用，硬件能力已就绪。详见 [matter.md](matter.md) 前瞻文档 + [luna-panel-bridge.md §5](luna-panel-bridge.md) A 域 Matter |

详细消息字典与时序见 [app-protocol.md](app-protocol.md)。

## 4. 蓝牙音频

C6 协处理器**理论支持**蓝牙音频（A2DP），P4 通过 `esp_hosted` 拿到 PCM 流再走 ES8311 codec 输出。但能否与 Wi-Fi + 雷达 SPI 25 MHz + 4 麦 PDM + 双 codec **同时运行**需要固件资源预算确认（瓶颈在 `esp_hosted` 透传带宽和 C6 内部并发能力）。

- 见 [lunawake-hardware.md §5](lunawake-hardware.md) 待澄清问题 8

## 5. 关键约束

- 配网失败回退：连续 retry 5 次失败 → 重开 BluFi 广播等待重配，NVS 凭据保留作 fallback
- WS 客户端上限：4 个并发，超出拒绝
- WS 单帧上限：64 KB（超过需分片协议）
- mDNS 失败兜底：App 手动输 IP

## 6. 待办（DVT 遗留）

| # | 项 |
|---|---|
| 1 | OTA 链路立项（HTTP / MQTT 路径与签名校验） |
| 2 | 蓝牙音频路径与 Wi-Fi + 雷达 + 麦的资源预算验证 |
| 3 | WS 鉴权机制（`protocol_version=2` 起启用 token） |

## 7. 配套资料

- [app-protocol.md](app-protocol.md) — 完整 App 通信协议（BluFi + WS + 消息字典）
- [luna-panel-bridge.md](luna-panel-bridge.md) — 与 Luna 软件契约（含 ConfigCard 摄取链路待定）
- [lunawake-hardware.md §5](lunawake-hardware.md) — 待澄清问题（蓝牙音频、ConfigCard 通信协议）
- [../../../reference/hardware/espressif-esp32c6-datasheet-cn.pdf](../../../reference/hardware/espressif-esp32c6-datasheet-cn.pdf) — **ESP32-C6 完整数据手册（中文）** — 引脚 / 内部框图 / Wi-Fi 6 + BLE 5 + 802.15.4 详细规格
