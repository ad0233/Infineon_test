# flash_device_certs.py 使用文档

## 功能说明

用于烧录设备证书和 IoT 配置到 ESP32 设备。

## ⚠️ 重要提醒

### 1. 保证工程编译
- **使用前必须确保工程已成功编译**
- 脚本依赖编译后的工程环境
- 建议先执行 `idf.py build` 确保编译通过

### 2. 确定好串口
- **使用前必须确认正确的串口设备路径**
- Linux/Mac: 通常为 `/dev/ttyUSB0` 或 `/dev/ttyACM0`
- Windows: 通常为 `COM3`、`COM4` 等
- 可通过 `idf.py flash monitor` 或系统设备管理器确认串口
- 确保串口未被其他程序占用

## 使用方法

### 基本用法

```bash
python tools/flash_device_certs.py <设备目录> [选项]
```

### 参数说明

- `设备目录`: 包含证书和配置文件的目录路径
  - 必需文件：
    - `private_key.pem` - 设备私钥
    - `certificate.pem` - 设备证书
    - `AmazonRootCA1.pem` - CA 根证书
    - `iot_config.json` - IoT 配置文件

### 选项参数

- `-p, --port <串口路径>`: 指定串口路径（默认: `/dev/ttyUSB0`）
- `--target-chip <芯片类型>`: 目标芯片类型（默认: `esp32s3`）
  - 可选: `esp32`, `esp32s2`, `esp32c3`, `esp32s3`, `esp32c6`, `esp32h2`, `esp32p4`
- `--efuse-key-id <ID>`: eFuse key ID（默认: `2`）
- `--priv-key-algo <算法> <大小>`: 私钥算法和大小（默认: `RSA 2048`）
- `--skip-ds`: 跳过 DS 证书烧录
- `--skip-iot`: 跳过 IoT 配置烧录
- `--skip-flash`: 只生成文件，不烧录（仅对 DS 证书有效）

## 使用示例

### 示例 1: 完整烧录（默认串口）

```bash
python tools/flash_device_certs.py tools/LW25BQ7JFEAL
```

### 示例 2: 指定串口

```bash
python tools/flash_device_certs.py tools/LW25BQ7JFEAL -p /dev/ttyUSB0
```

### 示例 3: 只烧录 IoT 配置，跳过 DS 证书

```bash
python tools/flash_device_certs.py tools/LW25BQ7JFEAL -p /dev/ttyUSB0 --skip-ds
```

### 示例 4: 只烧录 DS 证书，跳过 IoT 配置

```bash
python tools/flash_device_certs.py tools/LW25BQ7JFEAL -p /dev/ttyUSB0 --skip-iot
```

### 示例 5: 指定芯片类型

```bash
python tools/flash_device_certs.py tools/LW25BQ7JFEAL -p /dev/ttyUSB0 --target-chip esp32
```

## 执行流程

1. **步骤 1: 烧录 DS 证书**
   - 使用 `configure_esp_secure_cert.py` 烧录设备证书、私钥和 CA 证书
   - 配置 eFuse 密钥

2. **步骤 2: 烧录 IoT 配置**
   - 使用 `gen_iot_config_nvs.py` 生成并烧录 IoT 配置到 NVS 分区

## 注意事项

- 确保设备已正确连接并进入下载模式
- 烧录过程中不要断开设备连接
- 如遇到权限问题，可能需要添加用户到 `dialout` 组（Linux）或使用管理员权限
- 建议在烧录前备份设备原有配置

