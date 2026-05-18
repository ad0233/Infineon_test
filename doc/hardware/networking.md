# Networking 模块

Wi-Fi + 蓝牙双通道。Wi-Fi 走 App 数据链路，BLE 走配网（BluFi）+ 蓝牙音频（可选）。

主板：BY-SA-V001 / 项目：Lunawake (BY001)。

---

## 1. 硬件

| 项 | 规格 |
|---|---|
| 模组 | Cross Air OA-W01 |
| Wi-Fi | 2.4 GHz / 5 GHz |
| 蓝牙 | ESP32-P4 内置 + 协处理器（esp_hosted） |
| 天线 | 模组板载天线 |
| 主控接口 | esp_hosted 透传（Wi-Fi + BLE 共用） |

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

详细消息字典与时序见 [app-protocol.md](app-protocol.md)。

## 4. 蓝牙音频

ESP32-P4 + 协处理器**理论支持**蓝牙音频，但能否与 Wi-Fi + 雷达 SPI 25 MHz + 4 麦 PDM + 双 codec **同时运行**需要固件资源预算确认。

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
