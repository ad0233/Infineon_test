# EVT — 雷达点亮 + 算法基线实验记录

> Status: 🟢 archived · Last reviewed: 2026-04-13 · EVT 阶段归档
>
> 项目：ESP32-P4 + BGT60TR13C 60GHz 雷达 + AHT20 + VEML7700 + WS2812
> 周期：2026-04-06 ~ 2026-04-13

---

## 一、实验路径：从底层到产品化

### 阶段 1：硬件点亮与 SPI 通信（起点）

| 环节 | 关键问题 | 解决方案 |
|---|---|---|
| 初始化顺序 | BGT60 需严格顺序上电 | 固定：GPIO hold释放 → SPI init → sensor init → 寄存器配置 |
| SPI 时钟 | 10MHz 导致 FIFO 溢出 | 提到 25MHz（后因 XTAL 限制降到 20MHz） |
| FIFO 容量 | 24576 样本超过 8192 上限 | 分 6 片 × 4096 读取 |
| FIFO burst | MOSI=0x00 读不到数据 | 改为 MOSI=0xFF（存入 MemPalace） |
| 连续重启 | 偶发 FIFO 卡死无限循环 | 断电 3 秒冷启动（硬件偶发，非代码） |

### 阶段 2：算法移植（Python → ESP32 C）

参考 `vital_signs_detector.py`，移植到 `my_lidar_inf.c`：

```
相位提取 → 循环缓冲区 → 双路径处理
   ├─ 呼吸: 去均值 → diff-clip → 7点平滑 → filtfilt BPF → FFT 1024 → ROI
   └─ 心率: 去均值 → diff-clip(不平滑) → filtfilt BPF → 去呼吸谐波 → FFT 1024 → ROI
```

**关键坑点**：
- `dsps_fft2r_init_fc32()` 是全局一次性，`sensor-dsp` 先以 128 初始化 → 1024-FFT 越界
- **修复**：`dsps_fft2r_deinit_fc32()` + 重新 `init(1024)` 强制重分配 twiddle 表

### 阶段 3：Presence 检测校准（迭代最多的环节）

---

## 二、校准方法论

### 1. 子指标可观测化

把 presence 4 个子指标全部打印到 1Hz 日志：

```
Radar: detected=yes confidence=1.00 distance=45cm
  phExc=1.2mm(Y) ampCV=0.21(Y) binSpan=1(Y) breathPk=Y
```

没有这一步，所有调参都是盲调。

### 2. CLI 运行时调参（后续改回内部校准）

`set_peak_height`, `set_phase_th`, `set_confidence_th` 等命令运行时修改阈值，避免每次重编译。

### 3. 分场景数据采集（最终流程）

按顺序采集每个阶段 **15 秒** 数据：

| Step | 场景 | 关键指标 |
|---|---|---|
| 1 | 空场 | 确认零误报 |
| 2 | 静坐（正常距离） | ampCV1s 最大值 = 体动阈值下界 |
| 3 | 轻微动作 | 边界过渡 |
| 4 | 中等摆动 | ampCV1s 最小值 = 体动阈值上界 |
| 5 | 大动作 | 验证最大响应 |
| 6 | 恢复测试 | 测量归零延迟 |

**阈值 = (Step2 最大值 + Step4 最小值) / 2**

实测结果：静坐 max=0.353, 运动 min=0.413 → 阈值取 **0.38**

---

## 三、用到的技术栈

### 硬件
| 器件 | 作用 | 状态 |
|---|---|---|
| ESP32-P4 (JC-ESP32P4-M3) | 主控 | SPI 25MHz 雷达通信 |
| BGT60TR13C | 60GHz FMCW 雷达 | 59-63GHz, 3RX（仅用 RX1）|
| TXS0108EPWR | 1.8V↔3.3V 电平转换 | 5V→3V3→1V8 供电 |
| AHT20 | 温湿度 | I2C, 1s 周期 |
| VEML7700 | 环境光 | I2C, 1.5s 周期 |
| WS2812 | RGB 灯 | GPIO20, RMT 驱动 |

### 软件栈
| 组件 | 作用 |
|---|---|
| ESP-IDF v5.5 | 固件基础 |
| esp-dsp | Biquad、FFT |
| sensor-xensiv-bgt60trxx | 雷达底层驱动 |
| espressif/led_strip | WS2812 RMT 驱动 |
| FreeRTOS_CLI | 参数调节命令行 |

### 自建组件
| 组件 | 功能 |
|---|---|
| `my_lidar_inf` | 雷达主任务 + vitals + presence + RBM |
| `my_rgb` | WS2812 控制 + 独立串口命令监听 |
| `cli_task` | 阈值调节 CLI |

### 调试工具
| 工具 | 用途 |
|---|---|
| `tools/radar_panel.py` | 综合 GUI：波形 + RGB 控制 + 串口日志 |
| `tools/wave_plot.py` | 旧的波形工具（matplotlib） |
| `tools/rgb_picker.py` | 独立 RGB 调光工具 |

---

## 四、问题 ↔ 解决方案对照表

### A. 硬件/通信层

| 问题 | 根因 | 解决方案 |
|---|---|---|
| FIFO 溢出卡死 | SPI 10MHz 不够 | 提到 20MHz（25MHz 受 XTAL 限制） |
| FIFO 分片断言崩溃 | 传 24576 超过 8192 | 传 `FIFO_SLICE_SAMPLES=4096` |
| 启动时 FIFO 崩溃 | 硬件偶发 | 冷启动 3 秒 |
| SPI CS 异常 | 配置冲突 | `spics_io_num=-1`，由库软件控制 |

### B. Vitals 算法层

| 问题 | 根因 | 解决方案 |
|---|---|---|
| 心率卡死 52.1 BPM | FFT twiddle 表被 sensor-dsp 以 128 抢占初始化 | deinit + reinit(1024) |
| 呼吸 8.8 BPM 锁死 | FFT ROI 下限 0.15Hz 落到噪声 bin | 改为 0.20-0.50Hz（12-30 BPM） |
| 心率离群值全拒绝 | 首次值不准 → 后续全被 ±12BPM 拒绝 | 前 3 次不拒绝，之后放宽到 ±25BPM |
| 心率被 7 点平滑过滤 | 平滑 1.4Hz 截止切掉心率带 | 心率路径跳过 7 点平滑 |
| BPM 人走后不归零 | 等 20s 估算周期 | `presence=no` 立即清零 |

### C. Presence 检测层

| 问题 | 根因 | 解决方案 |
|---|---|---|
| 人走后 4-9 秒才判 no | phase/breath 窗口 6s 太长 | 缩到 3s |
| bin 卡在近场 bin 8 | 启动时全局搜索锁定近场杂波 | FIRST_VALID_BIN 8→10 (37.5cm起) |
| bin 跟踪太慢 | SWITCH_RATIO 1.35 + step 1 | 改 1.25 + step 2 |
| 人走后 bin 跳到 58（2米远） | global energy 直接跳 | 加距离限制 ≤10 bin |
| 人走后 presence 不归零 | breathPk 从历史残留中触发 | `breathPk` 不单独触发，改为 `breath AND phase` |
| 静坐时误判无人 | confidence 门限 0.35 太低 | 提到 0.80 |
| 人离开后 bin 乱飘 | tracker 继续搜索 | no 时冻结 bin，yes 时清空 history 重新锁定 |

### D. 体动检测层（经历 4 次方案迭代）

| 方案 | 问题 | 最终方案 |
|---|---|---|
| 帧间相位跳变 | 相位差受 bin 跳动污染（不同 bin 的相位无意义） | 弃用 |
| 自适应频谱差分 + 噪声底 | noise 死锁（运动时冻结→永远无法解锁） | 弃用 |
| 全频段能量差分 | 信噪比差，静坐/运动重叠 | 弃用 |
| **1s 滑窗 ampCV + binSpan** | ✅ 信噪比好，校准点明确 | **采用** |

最终阈值：ampCV1s > 0.38 OR binSpan1s ≥ 4 → rbm=Y

### E. 交互/工具层

| 问题 | 根因 | 解决方案 |
|---|---|---|
| CLI 输入 `getchar()` 不工作 | UART0 和 USB-JTAG 竞争 | 独立 `rgb_cmd_task` 同时监听两者 |
| GUI 只发不收 | COM5 = USB-UART 桥接到 UART0，但读的是 USB-JTAG | UART 和 USB-JTAG 都读，哪个有用哪个 |
| RGB 命令格式错 | CLI 需要先按 Enter 进入设置模式 | 独立任务直接解析，无需进入模式 |
| 波形图中文乱码 | matplotlib 默认字体 | 指定 Microsoft YaHei |

---

## 五、当前系统状态

### 功能达成情况

| 功能 | 延迟 | 精度 | 状态 |
|---|---|---|---|
| 人存检测 | 进场 ~2s / 离场 ~4s | 可靠 | ✅ |
| 距离测量 | 实时 | 3.75cm 分辨率 | ✅ |
| 呼吸率 | 每 20s 更新 | 12-30 BPM 范围 | ✅ |
| 心率 | 每 20s 更新 | 51-132 BPM 范围 | ✅ |
| 体动检测 | ~0.5s 检出 / ~1s 归零 | 0-100% | ✅ |
| WS2812 灯控 | 实时 | 人存联动 + 远程调色 | ✅ |
| 环境传感器 | 1-1.5s | 温湿度 + 环境光 | ✅ |

### 主循环逻辑

```
radar_task (10Hz):
  └─ 读 FIFO (6片) → Range FFT → 目标 bin 跟踪
       → 相位提取 → 循环缓冲区
       → Presence 判定（4 指标融合）
       → RBM 判定（1s 滑窗）
       → 每 20s: filtfilt + FFT → 呼吸/心率 BPM
       → 输出 Wave (10Hz) + Radar (1Hz) 日志

aht20_task (1Hz):  读温湿度
veml7700_task (0.67Hz):  读环境光

rgb_cmd_task (100Hz poll):
  └─ 监听 UART + USB-JTAG → 解析 "rgb R G B BR" → 设置颜色

rgb_ctrl_task (5Hz):
  └─ 读 presence_detected → 控制 my_rgb_enable()
```

### 待优化（TODO）

| 优先级 | 项目 | 说明 |
|---|---|---|
| 高 | 心率下限扩展 | 51→42 BPM，覆盖深睡/运动员 |
| 高 | 侧向运动检测 | 启用 3RX 相位差（Phase 2）|
| 中 | 信号质量指数 SQI | 判断 vitals 可信度 |
| 中 | LMS 自适应谐波消除 | 替代固定 Notch |
| 低 | HRV | 需帧率 ≥50Hz，当前硬件不可行 |

---

## 六、关键经验教训

1. **先观测再调参**：presence 4 个子指标不暴露，所有调参都是盲人摸象
2. **避免死锁设计**：自适应噪声底死锁耗了多轮迭代，最终用**固定阈值 + 校准流程**更稳
3. **信噪比优先**：频谱差分 SNR 差，不如直接用已有的 ampCV/binSpan（信噪比高 3-5 倍）
4. **场景化校准**：空场/静坐/轻微/中等/大动作分档采集，阈值取中点
5. **硬件认知补全**：USB-JTAG / USB-UART / UART0 的区别导致多次调试偏离，查网表确认才定位
6. **记录不是选项**：evt-vitals-baseline.md + SKILL.md + MemPalace 的实时更新是持续迭代的基础
