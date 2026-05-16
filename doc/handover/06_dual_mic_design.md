# BY-SA-V001 双麦克风方案（鼾声检测）

> 板号：BY-SA-V001 / 项目：BY001-ESP32（Lunawake 床头硬件）
> 用途：鼾声检测 + 双人床鼾声来源区分 + 环境噪声抑制
> 状态：V1.0 通过 BOM DNP 可直接验证；V1.1 建议改板落地

---

## 1. 设计目标

| 目标 | 要求 |
|------|------|
| 主功能 | 鼾声分类（100Hz–2kHz 频谱特征 + 周期包络） |
| 次功能 | 双人床鼾声来源区分（用户 vs 伴侣） |
| 环境降噪 | 利用双 MIC 相干性抑制空调/风扇/远场噪声 |
| 不做 | 声场定向、波束成形、远场语音唤醒（鼾声场景下过设计） |

---

## 2. 为什么是双 MIC 而不是四 MIC

鼾声识别的核心特征是**频谱与包络**，不依赖空间定位。
四 MIC 阵列的优势（波束成形、DOA、远场拾音）在床头鼾声场景下基本浪费。

| MIC 数 | 增量价值 | 鼾声场景必要性 |
|--------|----------|----------------|
| 1 MIC  | 基础鼾声分类 | ✅ 够用 |
| **2 MIC** | + 双人鼾声来源区分（左右 MIC 能量比 + 时延） | ✅ **推荐** |
|        | + 相干性降噪（3–6 dB SNR 提升） | |
|        | + 单只被遮挡时另一只兜底 | |
| 4 MIC  | + 波束成形 / DOA | ❌ 过设计 |

---

## 3. 麦克风选型

**LinkMems LCD3526B261-OFA03**（聆麦声学，PDM 数字硅麦）

| 参数 | 值 | 鼾声需求 | 评价 |
|------|----|----------|------|
| 输出格式 | 单 bit PDM | I2S/PDM RX | ✅ 适配 ESP32-P4 |
| AOP | 120 dB SPL @ 10% THD | 鼾声峰值 ~90 dB | ✅ 大余量不削波 |
| SNR | 65 dB(A) Std / 64 dB(A) LP | ≥ 60 dB(A) | ✅ 达标 |
| 灵敏度 | -26 dBFS @ 94dB SPL | — | ✅ 标准 |
| 频响 | 100Hz–3kHz 内 ±1 dB | 鼾声主能量 | ✅ 关键区平坦 |
| 指向性 | 全向 | 床头横放 | ✅ 不挑位姿 |
| 声孔朝向 | Bottom-port，ø0.32 mm | 面板背面贴装 | ✅ |
| 功耗 | 750 μA Std / 320 μA LP | 24h 常开 | ✅ |
| 工作电压 | 1.62–3.6V | VCC_3V3 | ✅ |
| L/R 通道选择脚 | 支持 | 两颗共享 CLK+DATA | ✅ **杀手锏** |
| 工作温度 | -40 ~ +100℃ | 室温 | ✅ |
| 封装 | 3.50 × 2.65 × 1.00 mm | — | ✅ |
| MSL | Class 1 | — | ✅ |

> **统一型号注意**：当前网表里 MIC1 是 `LHD3526B261-OFA03`，MIC4 是 `LCD3526B261-OFA03`，部件号差一个字母。V1.1 必须**统一改为 LCD3526B261-OFA03**，保证双 MIC 相干性。

---

## 4. 硬件电路设计

### 4.1 网表配置（基于现有 BY-SA-V001 4 MIC 设计裁剪）

V1.0 当前板的 4 MIC 配置：

| 位号 | 部件号 | DATA 网络 | L/R Pin2 | 通道 |
|------|--------|-----------|----------|------|
| MIC1 | LHD3526B261-OFA03 | GPIO10 | GND       | Left  |
| MIC4 | LCD3526B261-OFA03 | GPIO10 | VCC_3V3   | Right |
| MIC2 | LCD3526B261-OFA03 | GPIO54 | GND       | Left  |
| MIC3 | LHD3526B261-OFA03 | GPIO54 | VCC_3V3   | Right |

ESP32-P4 GPIO 占用：
- **GPIO11** = PDM CLK（4 颗共享）
- **GPIO10** = PDM DATA0（MIC1 + MIC4 立体声）
- **GPIO54** = PDM DATA1（MIC2 + MIC3 立体声）

### 4.2 V1.0 验证方案（BOM DNP，不改板）

| 位号 | 操作 | 备注 |
|------|------|------|
| MIC1 | ✅ 保留 | Left 通道 |
| MIC4 | ✅ 保留 | Right 通道 |
| MIC2 | ❌ DNP | 不贴片 |
| MIC3 | ❌ DNP | 不贴片 |
| MIC2/MIC3 对应 100nF 去耦电容 | ❌ DNP | 不贴片 |

软件侧只用 `GPIO10 (DATA) + GPIO11 (CLK)` 这一对 PDM 接口。

### 4.3 V1.1 改板方案（推荐）

**原理图改动**：
- 删除 MIC2、MIC3 及对应去耦电容网络
- 删除 GPIO54 → MIC DATA 走线
- 统一 MIC1 部件号为 **LCD3526B261-OFA03**（与 MIC4 一致）
- GPIO54 释放为通用 IO（可挪给按钮 / 状态 LED / 扩展）

**保留**：
- MIC1 + 100nF 去耦电容
- MIC4 + 100nF 去耦电容
- VCC_3V3 供电 + 100Ω 限流（R20）
- GPIO10（DATA）、GPIO11（CLK）共享走线

### 4.4 电源与去耦

```
VCC_3V3 ──┬── R20 (100Ω) ──┬── MIC1 VDD
          │                ├── MIC4 VDD
          │
          ├── C(MIC1) 100nF ── GND  (靠近 MIC1 VDD 脚)
          └── C(MIC4) 100nF ── GND  (靠近 MIC4 VDD 脚)
```

- **必须用专用 LDO 而非数字电源**（CODEC_3V3 或独立 LDO）
- VDD 脚去耦电容 0.1µF 紧贴 MIC 焊盘
- 双 MIC 总功耗：
  - LP Mode (768 kHz CLK)：2 × 320 μA = **640 μA**
  - Std Mode (2.4 MHz CLK)：2 × 750 μA = **1.5 mA**

---

## 5. PCB 布局规范

### 5.1 整体摆位（100×100 mm 板）

```
        ┌──────── 100 mm ────────┐
        │                        │
        │ ●MIC1          MIC4●   │   ← 沿床的"左右方向"
        │ ↑              ↑       │      间距 80 mm
        │ 距边10mm    距边10mm    │
        │                        │
        │      [BGT60 雷达]      │  ← 距 MIC ≥ 20 mm
        │                        │
        │      [其他模块]        │
        │                        │
        └────────────────────────┘
```

### 5.2 布局参数

| 项 | 数值 | 备注 |
|----|------|------|
| MIC1 ↔ MIC4 中心距 | **80 mm ±2 mm** | 鼾声 + 双人区分最优区间 |
| 摆位方向 | 沿床的左右方向 | 不是床头屏左右，是床的左右 |
| MIC 距板边 | ≥ 10 mm | 避免外壳干涉声道 |
| 距雷达模块 | ≥ 20 mm | 雷达 SPI 25 MHz 时钟可能耦合 |
| 距扬声器/振动器 | ≥ 30 mm | 防机械反馈 |
| 两颗 MIC 之间 PCB | **不开槽、不挖空** | 防止机械振动经 PCB 耦合形成相干噪声 |
| PCB 层 | Bottom Layer | 与现状一致 |

### 5.3 声学开孔（Bottom-port MIC 必备）

| 项 | 数值 |
|----|------|
| PCB 通孔直径 | **ø0.6 mm** |
| 通孔位置 | 与 MIC ø0.32mm 声孔同心对齐 |
| 焊盘 | 内圈环形焊盘 + 焊膏全包封 |
| 外壳导音道 | 与 PCB 通孔对齐，孔径 ≥ 0.8 mm |
| 防尘 | 声学透气防尘网（可选） |

> ⚠️ **任何漏气都会让 100–500 Hz 低频灵敏度下降 3–6 dB**（鼾声主能量区），密封工艺是产线的关键检查点。

### 5.4 EMC / 屏蔽

- MIC 周边一圈 GND 过孔墙，抑制射频/电源耦合到模拟前端
- VCC_3V3 走线远离 SPI CLK（GPIO0）和 LED PWM（GPIO15–17）
- PDM CLK（GPIO11）走线避免长平行段，必要时加 22Ω 串阻

### 5.5 SMT 工艺要求（手册硬约束）

| 项 | 要求 |
|----|------|
| 回流焊峰值温度 | ≤ 260℃ @ 30s |
| 回流焊后 | **禁止水洗 / 超声清洗** |
| 气流测试 | 5 cm 内禁止 > 0.3 MPa 气流吹声孔 |
| 真空吸取位置 | MIC 顶面中心 0.5×0.5 mm 区域 |
| 储存条件 | -40 ~ +100℃，湿度 < 75%，MSL Class 1 |

---

## 6. 软件接口（硬件契约）

### 6.1 ESP32-P4 PDM RX 配置

```
PDM Dual-Microphone Bus
  CLK:    GPIO11    (Output, ESP32-P4 → MIC)
                    Frequency: 768 kHz (LP) or 2.4 MHz (Std)
  DATA:   GPIO10    (Input,  MIC → ESP32-P4)
                    Stereo PDM:
                      ├── Left  channel = MIC1 (床的左侧)
                      └── Right channel = MIC4 (床的右侧)
  Power:  VCC_3V3, always-on
  Sample Rate (after PDM→PCM downsampling):
          16 kHz, 16-bit, stereo
```

### 6.2 ESP-IDF 驱动示例

```c
#include "driver/i2s_pdm.h"

#define PDM_CLK_GPIO   GPIO_NUM_11
#define PDM_DIN_GPIO   GPIO_NUM_10
#define PDM_SAMPLE_HZ  16000

static i2s_chan_handle_t s_rx_handle;

void mic_init(void)
{
    i2s_chan_config_t chan_cfg =
        I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_AUTO, I2S_ROLE_MASTER);
    ESP_ERROR_CHECK(i2s_new_channel(&chan_cfg, NULL, &s_rx_handle));

    i2s_pdm_rx_config_t pdm_cfg = {
        .clk_cfg  = I2S_PDM_RX_CLK_DEFAULT_CONFIG(PDM_SAMPLE_HZ),
        .slot_cfg = I2S_PDM_RX_SLOT_DEFAULT_CONFIG(
                        I2S_DATA_BIT_WIDTH_16BIT,
                        I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {
            .clk = PDM_CLK_GPIO,
            .din = PDM_DIN_GPIO,
            .invert_flags = { 0 },
        },
    };
    ESP_ERROR_CHECK(i2s_channel_init_pdm_rx_mode(s_rx_handle, &pdm_cfg));
    ESP_ERROR_CHECK(i2s_channel_enable(s_rx_handle));
}

size_t mic_read(int16_t *stereo_buf, size_t frames, TickType_t timeout)
{
    size_t bytes_read = 0;
    i2s_channel_read(s_rx_handle, stereo_buf,
                     frames * 2 * sizeof(int16_t),
                     &bytes_read, timeout);
    return bytes_read / (2 * sizeof(int16_t));
}
```

### 6.3 立体声数据排布

```
stereo_buf[0] = MIC1 sample 0  (Left)
stereo_buf[1] = MIC4 sample 0  (Right)
stereo_buf[2] = MIC1 sample 1  (Left)
stereo_buf[3] = MIC4 sample 1  (Right)
...
```

### 6.4 鼾声算法输入建议

| 项 | 推荐 |
|----|------|
| 采样率 | 16 kHz |
| 位深 | 16-bit |
| 帧长 | 30 ms（480 samples） |
| 帧移 | 15 ms（240 samples） |
| 特征 | MFCC / log-mel + 包络周期性 |
| 双人区分 | 左右 MIC 能量比 + 互相关时延（GCC-PHAT） |
| 降噪 | 相干性 NS（双通道一致性加权） |

---

## 7. 验证清单

### 7.1 EVT（V1.0 + DNP）

- [ ] BOM 标注 MIC2、MIC3 及对应去耦电容为 DNP
- [ ] 烧录 PDM 驱动，确认 GPIO10/11 可正常收到立体声数据
- [ ] 单 MIC 通道幅度差 < 3 dB（敲击靠近 MIC1 vs MIC4 验证 L/R 对应）
- [ ] 静音环境下噪声底 ≥ -65 dBFS
- [ ] 鼾声样本采集，频谱可见 100Hz–2kHz 能量集中
- [ ] LP Mode（768 kHz CLK）下 SNR ≥ 60 dB(A)

### 7.2 DVT（V1.1 改板）

- [ ] MIC1/MIC4 部件号统一为 LCD3526B261-OFA03
- [ ] MIC1 ↔ MIC4 间距实测 80 mm ±2 mm
- [ ] PCB 声孔与 MIC 声孔同心，无漏气（用洗耳球贴测密封）
- [ ] 整机装配后双 MIC 一致性测试（同声源等距测试，幅度差 < 2 dB）
- [ ] 鼾声算法在双人床场景下来源区分准确率 > 90%
- [ ] 24h 常开功耗 ≤ 1 mA @ LP Mode

---

## 8. 决策依据与权衡

| 决策点 | 选择 | 备选 | 理由 |
|--------|------|------|------|
| MIC 数量 | 2 | 1 / 4 | 1 不能做双人区分；4 在鼾声场景过设计 |
| 间距 | 80 mm | 30–60 mm | ≥ 60 mm 才有显著左右分离能量差 |
| MIC 类型 | PDM 数字 | 模拟 + ADC | 抗噪、PCB 走线简单、ESP32-P4 原生支持 |
| 共享 CLK+DATA | 是 | 独立 2 路 PDM | L/R 脚特性，省 GPIO，省 BOM |
| CLK 频率 | 768 kHz (LP) | 2.4 MHz (Std) | 鼾声 6 kHz 带宽够用，省功耗 60% |
| 砍掉的 MIC | MIC2, MIC3 | MIC1, MIC4 | MIC1+MIC4 已配置在同一条 DATA 上，零软件改动 |

---

## 9. 变更影响

| 影响项 | V1.0 (DNP) | V1.1 (改板) |
|--------|-----------|-------------|
| PCB | 不改 | 重排 MIC 位置 + 删除 MIC2/3 网络 |
| BOM | -2 MIC -2 电容 | -2 MIC -2 电容 |
| GPIO 释放 | GPIO54 释放（软件不用） | GPIO54 释放（原理图删除） |
| 软件 | 单一 PDM RX 通道，无需改动 | 同 V1.0 |
| 成本 | -2 × MIC 单价 ≈ -¥0.6/台 | 同左 |
| 风险 | LHD/LCD 部件号不一致 → 双 MIC 一致性差 | ✅ 已解决（统一型号） |

---

## 10. 参考资料

- LinkMems LCD3526B261-OFA03 数据手册（Rev 1.1，2026-01-17）
- 当前网表：`doc/Netlist_BY-SA-V001_2026-05-06.enet`
- 资源映射：`components/my_lidar_inf/resource_map.h`
- ESP-IDF PDM RX 文档：`esp-idf/components/driver/i2s/include/driver/i2s_pdm.h`
