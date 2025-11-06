# LunaWake API 集成文档

本文档描述 LunaWake 使用 AWS IoT Core 的完整 API 调用流程。

https://ibza2n2iii.us-east-1.awsapprunner.com
测试接口:https://ibza2n2iii.us-east-1.awsapprunner.com/docs 

## 核心特性

- **mTLS 双向认证**：设备和云端相互验证身份
- **私钥不离设备**：私钥在设备端生成，永不上传
- **CSR 证书签发**：后端仅签发证书，无法获取私钥
- **细粒度权限控制**：通过 IoT Policy 精确控制设备权限

## 完整流程图

```mermaid
sequenceDiagram
    participant App as 移动 App
    participant BLE as 蓝牙
    participant Device as 硬件设备
    participant API as 后端 API
    participant AWS as AWS IoT Core

    Note over App: 步骤 1: 用户登录
    App->>API: POST /v1/auth/login
    API-->>App: access_token

    Note over App,Device: 步骤 2: 蓝牙配网 (Wi-Fi)
    App->>Device: 蓝牙连接，读取 device_id
    App->>Device: 发送 Wi-Fi 信息 (SSID, Password)
    Device->>Device: 连接 Wi-Fi
    Device-->>App: Wi-Fi 连接成功

    Note over App: 步骤 3: 绑定设备
    App->>API: POST /v1/user/device (device_id)
    API-->>App: 绑定成功

    Note over App: 步骤 4: 生成密钥对和 CSR
    App->>API: POST /v1/user/device/generate-keypair
    API-->>App: private_key_pem + csr_pem + device_id

    Note over App,Device: 步骤 5: 配置设备（蓝牙）
    App->>Device: 发送 private_key_pem (通过蓝牙)
    Device->>Device: 存储私钥（永不上传）
    Device-->>App: 私钥存储成功

    Note over App: 步骤 6: 签发证书并获取配置
    App->>API: POST /v1/user/device/iot-config {csr}
    API->>AWS: CreateCertificateFromCsr
    API->>AWS: CreateThing, AttachPolicy
    AWS-->>API: certificate_pem + thing_name
    API-->>App: certificate + iot_endpoint + root_ca

    Note over App,Device: 步骤 7: 配置 IoT（蓝牙）
    App->>Device: 发送 certificate + iot_endpoint + root_ca (通过蓝牙)
    Device-->>App: 配置接收成功

    Note over Device: 步骤 8: 连接并监听
    Device->>AWS: mTLS 连接 (private_key + certificate)
    Device->>AWS: 订阅 lunawake/{thing_name}/command
    AWS-->>Device: 订阅成功

    Note over App: 步骤 9: 发送指令
    App->>API: POST /v1/user/device/command
    API->>AWS: 发布指令到 topic
    AWS->>Device: 传递指令
    Device->>Device: 执行指令
    API-->>App: 指令已发送

    Note over Device: 步骤 10: 获取唤醒消息
    Device->>API: GET /v1/wakeup-message?device_id=xxx
    API-->>Device: audio_url + 消息内容
    Device->>Device: 播放音频
```

---

## 步骤 1: 用户登录

### API 调用

```http
POST /v1/auth/login
Content-Type: application/json

{
  "email": "test@example.com",
  "password": "password123"
}
```

### 成功响应 (200)

```json
{
  "access_token": "eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9...",
  "refresh_token": "eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9...",
  "user_id": "550e8400-e29b-41d4-a716-446655440000",
  "email": "test@example.com",
  "expires_in": 3600
}
```

### 错误响应

| 状态码 | 错误代码              | 说明           |
| ------ | --------------------- | -------------- |
| 401    | `INVALID_CREDENTIALS` | 邮箱或密码错误 |

---

## 步骤 2: 蓝牙配网 - Wi-Fi（App 端 + 设备端）

### 配网流程

通过蓝牙将 Wi-Fi 配置发送给设备：

1. **建立蓝牙连接**

   - App 扫描并连接设备的蓝牙（设备处于配网模式）
   - 设备蓝牙名称格式：`LunaWake_XXXX`（XXXX 为设备序列号后缀）

2. **获取 Device ID**

   - App 通过蓝牙服务读取设备的 device_id
   - 蓝牙特征值：Device Information Service
   - device_id 为字符串（例如：`c1bxlti57k1g`）

3. **发送 Wi-Fi 配置**

**Wi-Fi 配置：**

```json
{
  "type": "wifi_config",
  "data": {
    "ssid": "Your_WiFi_SSID",
    "password": "your_wifi_password"
  }
}
```

4. **设备连接 Wi-Fi**
   - 设备收到配置后连接到指定的 Wi-Fi 网络
   - 连接成功后通过蓝牙返回状态

### 配网成功响应

```json
{
  "type": "provision_result",
  "data": {
    "status": "success",
    "wifi_connected": true,
    "ip_address": "192.168.1.100"
  }
}
```

---

## 步骤 3: 绑定设备

### API 调用

```http
POST /v1/user/device
Authorization: Bearer {access_token}
Content-Type: application/json

{
  "device_id": "c1bxlti57k1g"
}
```

### 成功响应 (200)

```json
{
  "device_id": "c1bxlti57k1g",
  "name": "Lunawake",
  "status": "offline",
  "bound_at": "2024-11-03T12:00:00Z"
}
```

### 错误响应

| 状态码 | 错误代码               | 说明                 |
| ------ | ---------------------- | -------------------- |
| 401    | `UNAUTHORIZED`         | Token 无效或过期     |
| 404    | `DEVICE_NOT_FOUND`     | 设备不存在           |
| 409    | `DEVICE_ALREADY_BOUND` | 设备已被其他用户绑定 |

---

## 步骤 4: 生成密钥对和 CSR（App 端）

### API 调用

```http
POST /v1/user/device/generate-keypair
Authorization: Bearer {access_token}
```

### 成功响应 (200)

```json
{
  "device_id": "c1bxlti57k1g",
  "private_key_pem": "-----BEGIN PRIVATE KEY-----\nMIIEvgIBADANBgkqhkiG9w0BAQEFA...",
  "csr_pem": "-----BEGIN CERTIFICATE REQUEST-----\nMIICvDCCAaQCAQAwdzELMAkGA1UEBh..."
}
```

**重要说明：**

- `private_key_pem`：RSA 2048 位私钥，**必须安全存储在设备本地，永不上传**
- `csr_pem`：证书签名请求，将在步骤 6 发送给后端签发证书

---

## 步骤 5: 配置设备私钥（App 端 + 设备端，蓝牙）

### 通过蓝牙发送私钥

App 将步骤 4 获取的私钥通过蓝牙发送给设备：

```json
{
  "type": "private_key_config",
  "data": {
    "private_key_pem": "-----BEGIN PRIVATE KEY-----\n..."
  }
}
```

### 设备确认

设备收到并存储私钥后返回确认：

```json
{
  "type": "private_key_result",
  "data": {
    "status": "success"
  }
}
```

**安全提示：**

- 私钥仅在设备本地存储
- 通过蓝牙传输后立即删除 App 中的副本
- 私钥永不上传到服务器

---

## 步骤 6: 签发证书并获取 IoT 配置（App 端）

### API 调用

```http
POST /v1/user/device/iot-config
Authorization: Bearer {access_token}
Content-Type: application/json

{
  "csr": "-----BEGIN CERTIFICATE REQUEST-----\nMIICvDCCAaQCAQAwdzELMAkGA1UEBh..."
}
```

### 成功响应 (200)

```json
{
  "iot_endpoint": "a1b2c3d4e5f6g7-ats.iot.us-east-1.amazonaws.com",
  "iot_port": 8883,
  "thing_name": "c1bxlti57k1g",
  "root_ca": "-----BEGIN CERTIFICATE-----\nMIIDQTCCAimgAwIBAgITBmyfz5m/jAo...",
  "certificate_status": "ACTIVE",
  "certificate_pem": "-----BEGIN CERTIFICATE-----\nMIIDWTCCAkGgAwIBAgIUXYZ...",
  "certificate_id": "abc123def456..."
}
```

**响应字段说明：**

- `iot_endpoint`：AWS IoT Core 端点
- `iot_port`：MQTT over TLS 端口（默认 8883）
- `thing_name`：IoT Thing 名称（通常与 device_id 相同）
- `root_ca`：AWS IoT Root CA 证书
- `certificate_pem`：设备证书（由 CSR 签发）
- `certificate_status`：证书状态（ACTIVE/INACTIVE/REVOKED）

---

## 步骤 7: 配置 IoT（App 端 + 设备端，蓝牙）

### 通过蓝牙发送 IoT 配置

App 将步骤 6 获取的配置通过蓝牙发送给设备：

```json
{
  "type": "iot_config",
  "data": {
    "iot_endpoint": "a1b2c3d4e5f6g7-ats.iot.us-east-1.amazonaws.com",
    "iot_port": 8883,
    "thing_name": "c1bxlti57k1g",
    "certificate_pem": "-----BEGIN CERTIFICATE-----\n...",
    "root_ca": "-----BEGIN CERTIFICATE-----\n..."
  }
}
```

### 设备确认

设备收到配置后返回确认：

```json
{
  "type": "iot_config_result",
  "data": {
    "status": "success"
  }
}
```

**此时 App 可以断开蓝牙连接。**

---

## 步骤 8: 设备连接 AWS IoT（设备端）

### 连接说明

设备使用步骤 5 和步骤 7 获取的凭证连接 AWS IoT Core：

1. **mTLS 认证**

   - 私钥：步骤 5 存储的 `private_key_pem`
   - 证书：步骤 7 存储的 `certificate_pem`
   - Root CA：步骤 7 存储的 `root_ca`

2. **连接参数**

   - Endpoint：`iot_endpoint`
   - Port：`iot_port`（8883）
   - Client ID：`thing_name`

3. **订阅 Topic**
   - 订阅：`lunawake/{thing_name}/command`
   - QoS：1（至少一次传递）

### 接收的指令格式

设备订阅 topic 后会收到 JSON 格式的指令：

```json
{
  "type": "server.command",
  "timestamp": "2024-11-03T12:00:00Z",
  "data": {
    "command_id": "cmd_a1B2c3D4",
    "action": "wake_alarm",
    "params": {
      "wakeup_time": "07:30"
    }
  }
}
```

### 支持的指令类型

**1. 设置唤醒闹钟 (wake_alarm)**

```json
{
  "type": "server.command",
  "timestamp": "2024-11-03T12:00:00Z",
  "data": {
    "command_id": "cmd_a1B2c3D4",
    "action": "wake_alarm",
    "params": {
      "wakeup_time": "07:30"
    }
  }
}
```

**2. OTA 固件更新 (ota_update)**

```json
{
  "type": "server.command",
  "timestamp": "2024-11-03T12:00:00Z",
  "data": {
    "command_id": "cmd_x9Y8z7W6",
    "action": "ota_update",
    "params": {
      "firmware_url": "https://example.com/firmware/v1.2.0.bin",
      "version": "1.2.0"
    }
  }
}
```

---

## 步骤 9: 发送设备指令（App 端）

### API 调用

```http
POST /v1/user/device/command
Authorization: Bearer {access_token}
Content-Type: application/json

{
  "action": "wake_alarm",
  "params": {
    "wakeup_time": "07:30"
  }
}
```

### 成功响应 (200)

```json
{
  "success": true,
  "message": "Command 'wake_alarm' sent to device",
  "command_id": "cmd_a1B2c3D4"
}
```

### 错误响应

| 状态码 | 错误代码           | 说明               |
| ------ | ------------------ | ------------------ |
| 400    | `INVALID_REQUEST`  | 参数格式错误       |
| 404    | `DEVICE_NOT_FOUND` | 设备未配置或未绑定 |
| 500    | `COMMAND_FAILED`   | 发送指令失败       |

---

## 步骤 10: 获取唤醒消息（设备端）

### 调用时机

在闹钟时间**前 15 分钟**调用（例如：闹钟 07:30，在 07:15 获取）

### API 调用

```http
GET /v1/wakeup-message?device_id=c1bxlti57k1g&date_param=2024-11-03
```

**参数:**

- `device_id` (必填): 设备标识符
- `date_param` (可选): 日期 YYYY-MM-DD，默认今天

### 成功响应 (200)

```json
{
  "date": "2024-11-03",
  "emoji": "🌅",
  "greeting": "早上好，小明！",
  "wakeup_message": "新的一天开始了！今天下午 2 点有重要演讲，加油！",
  "audio_url": "https://xxx.supabase.co/storage/v1/object/public/audios/xxx/wakeup/2024-11-03.mp3",
  "created_at": "2024-11-03T07:15:00Z"
}
```

### 说明

设备应该：

1. 提前 15 分钟调用此 API
2. 下载 `audio_url` 并缓存到本地
3. 闹钟时间播放音频

---

## 步骤 11: 查询设备状态（App 端）

### API 调用

```http
GET /v1/user/device/status
Authorization: Bearer {access_token}
```

### 成功响应 (200)

**1. 设备在线 (已配置 + 已连接)**

```json
{
  "device_id": "c1bxlti57k1g",
  "status": "online",
  "connected": true,
  "last_update": "2025-11-04T10:30:45.123Z",
  "shadow_version": 123
}
```

**2. 设备离线 (已配置 + 未连接)**

```json
{
  "device_id": "c1bxlti57k1g",
  "status": "offline",
  "connected": false,
  "last_update": "2025-11-04T09:15:20.456Z",
  "shadow_version": 122
}
```

**3. 设备未配置 (未完成 IoT 配置)**

```json
{
  "device_id": "c1bxlti57k1g",
  "status": "not_provisioned",
  "connected": false,
  "last_update": null,
  "shadow_version": null
}
```

### 工作原理

此接口通过 **AWS IoT Device Shadow** 实时查询设备状态：

```mermaid
sequenceDiagram
    participant App as App 端
    participant API as LunaWake API
    participant Shadow as AWS IoT Shadow
    participant Device as 设备端

    Note over Device: 设备通过 MQTT 持续上报状态
    Device->>Shadow: Publish to $aws/things/{id}/shadow/update<br/>{"state": {"reported": {"connected": true}}}

    App->>API: GET /v1/user/device/status
    API->>Shadow: get_thing_shadow(thing_name)
    Shadow-->>API: 返回 Shadow 文档
    API-->>App: {"status": "online", "connected": true, ...}
```

### 设备端实现

设备需要通过 MQTT 客户端定期上报状态到 AWS IoT Shadow：

**上报主题:**

```
$aws/things/{thingName}/shadow/update
```

**Payload 示例:**

```json
{
  "state": {
    "reported": {
      "connected": true
    }
  }
}
```

**离线检测:**

- 设备需配置 **Last Will Testament (LWT)** 遗嘱消息
- 设备异常断开时，AWS IoT 自动发送 LWT 更新 Shadow 为 `connected: false`
- 检测延迟约为 `keepalive × 1.5`（例如 keepalive=60s，延迟约 90s）

详细的设备端实现指南请参考 [DEVICE_SHADOW_GUIDE.md](./DEVICE_SHADOW_GUIDE.md)。

### 错误响应

| 状态码 | 错误代码           | 说明               |
| ------ | ------------------ | ------------------ |
| 404    | `DEVICE_NOT_FOUND` | 设备不存在或未绑定 |
| 500    | `INTERNAL_ERROR`   | 查询 Shadow 失败   |

---

## 证书重新配置

如果设备丢失证书或需要重置，可以重新执行步骤 4-7：

1. App 调用 `/v1/user/device/generate-keypair` 生成新的密钥对和 CSR
2. 通过蓝牙发送新私钥到设备
3. App 调用 `/v1/user/device/iot-config` 并传递新 CSR
4. 后端会自动删除旧的 Thing 和证书，创建新的
5. 通过蓝牙发送新证书到设备

---

## 错误处理

### 错误格式

```json
{
  "error": {
    "code": "ERROR_CODE",
    "message": "错误描述"
  }
}
```

### 常见错误

| 错误代码               | 原因                 | 解决方案           |
| ---------------------- | -------------------- | ------------------ |
| `INVALID_CREDENTIALS`  | 登录凭据错误         | 检查邮箱密码       |
| `UNAUTHORIZED`         | Token 无效/过期      | 重新登录           |
| `DEVICE_NOT_FOUND`     | 设备不存在或未绑定   | 检查设备 ID        |
| `DEVICE_ALREADY_BOUND` | 设备已被其他用户绑定 | 联系设备拥有者解绑 |
| `INVALID_REQUEST`      | 请求参数错误         | 检查参数格式       |
| `INTERNAL_ERROR`       | 服务器内部错误       | 联系技术支持       |

---

**文档版本:** 2.0.0  
**最后更新:** 2025-11-04
