# Lunawake App 通信协议 v1

本文档定义 **ESP32-P4 设备 ↔ 手机 App** 之间的配网与数据通信契约。交付给 App 开发方作为对接规范。

- **配网通道**：BluFi over BLE（Espressif 标准协议）
- **数据通道**：WebSocket over WiFi（ESP 做服务端）
- **传输格式**：UTF-8 JSON，每帧一个对象
- **字符编码**：简体中文（所有 `warnings` / `insights` 等文本字段统一为简体）

---

## 1. 传输与发现

| 项 | 值 |
|---|---|
| 配网协议 | BluFi (ESP-IDF 官方) + `esp_blufi` Flutter 包 |
| 数据协议 | WebSocket (ws://，MVP 不启用 TLS) |
| 服务端 | ESP32-P4（`esp_http_server`） |
| 端口 | `8080` |
| 路径 | `/ws` |
| 发现方式 | mDNS 服务类型 `_lunawake._tcp.local`，主机名 `lunawake-<mac6>.local` |
| mDNS 失败兜底 | App 提供手动输入 IP 的 UI；IP 也可由 BluFi 配网成功回调中的自定义数据域带回 |

### 1.1 通用字段

所有消息**必含**：

| 字段 | 类型 | 说明 |
|---|---|---|
| `type` | string | 消息种类（见各节） |
| `ts` | int | 消息生成时刻，Unix 时间戳**毫秒** |

### 1.2 连接生命周期

1. **握手**：WS 建连后 ESP 主动推送一条 `hello`，App 根据 `protocol_version` 判断兼容性（见 §3.0）。
2. **心跳**：ESP 每 **25s** 发 WS PING 帧；App 回 PONG。App 侧可主动 PING。任一侧连续 2 次 PING 无响应即视为死连接，主动关闭并重连。
3. **重连**：App 侧指数退避 `1s → 2s → 4s → 8s → 16s` 封顶，**无限重试**。每次重连成功后必须重新等 `hello`。
4. **单帧上限**：文本帧 ≤ **64 KB**。超过的消息（未来高分辨率波形回放等）另行协商分片协议。
5. **并发客户端**：ESP 端默认允许 **4** 个 WS 客户端同时连接，超出拒绝新连接。

---

## 2. 字段命名与单位约定

1. **全部 `snake_case`**；单位作为后缀进字段名：`_cm` / `_pct` / `_c` / `_bpm` / `_min`。
2. **时间戳**：
   - 实时消息 `ts`：Unix 毫秒整数
   - 睡眠报告内部日期时间：本地时区的 `YYYY-MM-DD HH:MM:SS` 字符串
   - ESP 启动后通过 NTP 同步时间；ESP 本地时区与手机时区不一致时以 ESP 为准
3. **布尔**：JSON 原生 `true` / `false`，**不使用** `0/1/"yes"/"no"`。
4. **未知/缺失**：JSON `null` 或直接不带字段，**禁止** `-1` / `0` / `"N/A"` 魔法值。
5. **浮点**：生理量保留 1 位小数（`distance_cm`、`breath_bpm` 等），百分比保留 1 位。
6. **枚举**：睡眠阶段固定 `0/1/2/3`（见 §3.3）。App 自行做本地化映射。

---

## 3. 消息字典

### 3.0 `hello`（ESP → App）

WS 建连后 ESP 第一帧。App 必须先处理这条才视为"连接就绪"。

```json
{
  "type":             "hello",
  "ts":               1713546789123,
  "device_id":        "lunawake-a1b2c3",
  "fw_version":       "v0.3.1",
  "protocol_version": 1,
  "capabilities":     ["radar", "sleep_report", "rgb"]
}
```

| 字段 | 说明 |
|---|---|
| `device_id` | `lunawake-<mac 后 6 位小写>` |
| `fw_version` | 固件版本串 |
| `protocol_version` | 本协议版本号，**不兼容升级时递增** |
| `capabilities` | 该固件实际实现的消息/命令种类；App 按此隐藏未支持的 UI |

### 3.1 `radar`（ESP → App，默认订阅，1 Hz）

```json
{
  "type":         "radar",
  "ts":           1713546789123,
  "detected":     true,
  "distance_cm":  48.7,
  "breath_bpm":   14.2,
  "heart_bpm":    72.3,
  "body_mov":     3,
  "temp_c":       30.7,
  "humidity_pct": 62.6,
  "lux":          29.0
}
```

| 字段 | 类型 | 单位 / 值域 | 备注 |
|---|---|---|---|
| `detected` | bool | true / false | 对应固件 `Radar: detected=yes/no` |
| `distance_cm` | float | 厘米，`detected=false` 时为 `0` | 保留 1 位小数 |
| `breath_bpm` | float | 次/分；未收敛或无人时为 `0` | |
| `heart_bpm` | float | 次/分；未收敛或无人时为 `0` | |
| `body_mov` | int | 0–100 % | 对应固件 `bodyMov` |
| `temp_c` | float | 摄氏度 | |
| `humidity_pct` | float | 相对湿度 % | |
| `lux` | float | 环境光照度 | |

**无人时的推送语义**：`detected=false` 时**仍然** 1Hz 推送本消息，生理字段 `breath_bpm` / `heart_bpm` / `body_mov` 置 `0`；温湿光继续有效。App 据此明确区分"无人在场"与"链路已断"。

### 3.2 `sleep_report`（ESP → App，按请求返回）

数据结构与 [tools/sleep_staging.py](../tools/sleep_staging.py) 的 `save_json_report` + `save_csv_report` 输出对齐。

```json
{
  "type": "sleep_report",
  "ts":   1713546789123,
  "date": "2026-04-20",

  "onset_time": "2026-04-19 22:45:00",
  "wake_time":  "2026-04-20 07:15:00",

  "total_sleep_min": 510,

  "stages": {
    "deep_min":   95, "deep_pct":  18.6,
    "light_min": 280, "light_pct": 54.9,
    "rem_min":    95, "rem_pct":   18.6,
    "awake_min":  40, "awake_pct":  7.8
  },

  "hypnogram": [
    { "t": "2026-04-19 22:45:00", "stage": 3 },
    { "t": "2026-04-19 22:46:00", "stage": 1 }
  ],

  "warnings": ["数据空白 12 min (03:15 -> 03:27) - interpolated"],
  "insights": ["深睡比例偏高..."]
}
```

| 字段 | 类型 | 说明 |
|---|---|---|
| `date` | string `YYYY-MM-DD` | 报告归属日（以 onset 日期计） |
| `onset_time` / `wake_time` | string `YYYY-MM-DD HH:MM:SS` | 本地时区 |
| `total_sleep_min` | int | `wake_time - onset_time` 之间的分钟数 |
| `stages.*_min` / `*_pct` | int / float | 四期时长 + 百分比 |
| `hypnogram[].t` | string | 分钟粒度时间戳 |
| `hypnogram[].stage` | int 0..3 | 见 §3.3 |
| `warnings` / `insights` | string[] | 数据质量警告与专家洞察；直接转发算法输出 |

### 3.3 睡眠阶段编码

| 值 | 英文 | 中文 |
|---|---|---|
| `0` | Deep | 深睡 |
| `1` | Light | 浅睡 |
| `2` | REM | 快速眼动 |
| `3` | Awake | 清醒 |

**编码固定不变**，对齐 [sleep_staging.py L82-86](../tools/sleep_staging.py#L82-L86)。App 侧自行做本地化映射。以后若细分（如 N1/N2）会使用新编码值，不破坏旧客户端。

### 3.4 `sleep_report_list`（ESP → App）

```json
{
  "type":  "sleep_report_list",
  "ts":    1713546789123,
  "dates": ["2026-04-18", "2026-04-19", "2026-04-20"]
}
```

### 3.5 命令（App → ESP）

| `cmd` | 载荷 | 触发回包 |
|---|---|---|
| `get_status` | `{"cmd":"get_status"}` | 立刻推一条 `radar`（不等 1Hz 节拍） |
| `list_sleep_reports` | `{"cmd":"list_sleep_reports"}` | 回 `sleep_report_list` |
| `get_sleep_report` | `{"cmd":"get_sleep_report","date":"2026-04-20"}` | 回 `sleep_report`；无数据则 `ack` 失败 |
| `rgb` | `{"cmd":"rgb","r":255,"g":255,"b":255,"br":80}` | `ack` |

所有命令 ESP 统一回 `ack`：

```json
{ "type":"ack", "ts":..., "cmd":"<原命令名>", "ok":true }
{ "type":"ack", "ts":..., "cmd":"<原命令名>", "ok":false, "err":"no_data" }
```

### 3.6 `ack.err` 标准错误码

| 值 | 含义 |
|---|---|
| `no_data` | 请求的数据不存在（如对应日期无睡眠报告） |
| `invalid_args` | 参数格式错误或越界 |
| `busy` | 设备正在执行冲突任务（如正在分析睡眠） |
| `not_implemented` | 命令该固件版本未实现 |
| `internal` | 内部错误（建议附加自由文本调试） |

App 侧按这 5 个值做 i18n，未知错误码退化展示 `err` 原文。

---

## 4. 完整交互流程

```
┌───────────┐                      ┌────────────┐                     ┌──────────┐
│  手机 App │                      │ ESP32-P4   │                     │ ESP32-C6 │
│ (Flutter) │                      │ (主 MCU)   │                     │(协处理器)│
└─────┬─────┘                      └─────┬──────┘                     └────┬─────┘
      │                                   │                                 │
      │                                   │ ① 上电: esp_hosted 初始化        │
      │                                   │────────────────────────────────►│
      │                                   │   WiFi+BLE 透传链路建立          │
      │                                   │                                 │
      │                                   │ ② 查 NVS 中的 WiFi 凭据          │
      │                                   │    无 → 启动 BluFi 广播          │
      │                                   │                                 │
      │ ③ BLE 扫描发现 "Lunawake-XXXX"    │                                 │
      │◄──────────────────────────────────┤                                 │
      │                                   │                                 │
      │ ④ BluFi 连接 + 发送 SSID+PWD     │                                 │
      │──────────────────────────────────►│                                 │
      │                                   │                                 │
      │                                   │ ⑤ esp_wifi_set_config+connect   │
      │                                   │────────────────────────────────►│
      │                                   │                                 │
      │                                   │ ⑥ GOT_IP                        │
      │                                   │◄────────────────────────────────│
      │                                   │   保存凭据到 NVS                 │
      │                                   │                                 │
      │ ⑦ BluFi 回报 "conn success" + IP │                                 │
      │◄──────────────────────────────────┤                                 │
      │   (App 可主动断 BLE)              │                                 │
      │                                   │                                 │
      │                                   │ ⑧ mDNS 广播 lunawake-xxx.local │
      │                                   │ ⑨ 启动 WS 服务 :8080/ws         │
      │                                   │                                 │
      │ ⑩ mDNS 解析 → IP (或手动输入)     │                                 │
      │                                   │                                 │
      │ ⑪ WS 建连 ws://<ip>:8080/ws      │                                 │
      │◄─────────────────────────────────►│                                 │
      │                                   │                                 │
      │ ⑫ ESP 推送 hello                 │                                 │
      │◄──────────────────────────────────┤                                 │
      │                                   │                                 │
      │                                   │ ⑬ 默认推送 radar @1Hz           │
      │◄──────────────────────────────────┤                                 │
      │                                   │                                 │
      │ ⑭ 命令 (rgb / get_sleep_report)  │                                 │
      │──────────────────────────────────►│                                 │
      │   ← ack / sleep_report           │                                 │
      │                                   │                                 │
      │ ↻ 心跳: 每 25s WS PING/PONG      │                                 │
      │◄─────────────────────────────────►│                                 │
```

### 4.1 异常恢复

| 情景 | 处理 |
|---|---|
| WiFi 断开 | ESP 连续 retry 5 次失败 → 重开 BluFi 广播等待重配网，NVS 凭据保留作为 fallback 继续用 |
| WS 客户端断开 | 服务端清理该 client 即可，不影响其他连接 |
| mDNS 解析失败 | App 3s 内无响应 → 切换手动输入 IP |
| WS 心跳超时 | 任一侧连续 2 次 PING 无响应 → 关闭连接 + App 进入指数退避重连 |
| 并发上限 | ESP 拒绝第 5 个客户端；App 侧不需要特殊处理 |

---

## 5. 鉴权（MVP 暂不实现）

MVP 阶段在同一局域网信任，**无鉴权**。后续版本预留字段：

- 配网时 BluFi 随机生成 16 字节 token，App 侧保存
- WS 建连时 App 在 `Sec-WebSocket-Protocol` 子协议字段中带 `token.xxx`，ESP 校验后放行

协议版本 `protocol_version=2` 起启用。

---

## 6. 使用库清单

### 设备端（ESP-IDF）

| 功能 | 组件 | 来源 |
|---|---|---|
| WiFi/BLE 透传 | `esp_hosted` + `esp_wifi_remote` | `managed_components/` |
| WiFi STA | `esp_wifi` | IDF 内置 |
| BluFi | `esp_blufi` (`CONFIG_BT_BLE_BLUFI_ENABLE=y`) | IDF 内置 |
| mDNS | `mdns` | IDF 内置 |
| WS 服务端 | `esp_http_server` (支持 WS frame) | IDF 内置 |
| JSON | `cJSON` | IDF 内置 |
| NVS 凭据存储 | `nvs_flash` | IDF 内置 |

### App 端（Flutter）

| 功能 | 包 |
|---|---|
| BluFi 配网 | [`esp_blufi`](https://pub.dev/packages/esp_blufi) |
| mDNS 发现 | `nsd` 或 `multicast_dns` |
| WebSocket | `web_socket_channel` |
| JSON | `dart:convert`（内置）|

---

## 7. App 对接最小契约（TL;DR）

App 开发只需要知道这三件事：

1. **配网**：用 `esp_blufi` 包连设备名匹配 `Lunawake-*` 的 BLE，发 SSID+PWD；成功回调后拿到 `conn_state=connected` 以及 ESP 回传的 IP。
2. **发现**：mDNS 查 `_lunawake._tcp.local` 拿到 `IP:8080`；失败兜底用 BluFi 回传的 IP 或手动输入。
3. **数据**：WebSocket 连 `ws://<ip>:8080/ws`，先处理 `hello`，之后默认收 `radar` 流；命令格式见 §3.5。心跳、重连、错误码见 §1.2 / §3.6。

---

## 8. 版本历史

| 版本 | 日期 | 变更 |
|---|---|---|
| 1 | 2026-04-20 | 初版定稿；BluFi + WebSocket；`radar` + `sleep_report` + `rgb` 四组消息 |
