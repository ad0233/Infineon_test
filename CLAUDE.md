# CLAUDE.md

本文件是项目面向 Claude Code 的指令文件，作用域为仓库根目录及全部子目录。

## 回复与协作风格

- 使用简体中文。
- 回复简洁直接，少解释，多落地。
- 默认直接执行任务，不先输出大段方案。
- 除非用户明确要求，否则不要主动新增 Markdown 文档。
- 编码风格优先遵循 `doc/standards/coding_standard.md`。

## 项目定位

- 基于 ESP-IDF / ESP-ADF 的嵌入式项目，根工程名为 `Lunawake`，目标芯片 `esp32p4`。
- `app_main()` 位于 `main/main.cpp`，主路径启动 `my_lidar_inf_init()`。
- 当前活跃雷达链路：`components/my_lidar_inf/`（Infineon BGT60TR13C SPI 雷达）。
- 旧串口雷达链路：`components/my_lidar/`，两条链路不要混改。

## 当前主工作路径

| 文件 | 说明 |
|------|------|
| `main/main.cpp` | 应用入口 |
| `components/my_lidar_inf/my_lidar_inf.c` | 雷达主任务：SPI/FIFO、距离检测、presence 检测 |
| `components/my_lidar_inf/radar_settings_tr13c.h` | 寄存器表（官方 dev board 导出） |
| `components/my_lidar_inf/cli_task.c` | CLI 控制入口，presence 参数调节 |
| `components/my_lidar_inf/resource_map.h` | 雷达引脚定义 |
| `dependencies/sensor-xensiv-bgt60trxx/` | Infineon 低层驱动 |
| `doc/netlist_BY-SA-V001.tel` | 主板原理图网表（Telia 格式，纯文本，含所有元器件和网络连接） |

## 雷达硬件配置（当前已验证）

| 参数 | 值 |
|------|----|
| 频率 | 59–63 GHz，BW = 4 GHz |
| ADC 采样率 | 1 MHz（ADC_DIV = 80） |
| 每 chirp 采样数 | 128 |
| 每帧 chirp 数 | 64 |
| RX 天线数 | 3 |
| 每帧原始样本 | 128 × 64 × 3 = **24576** |
| IF 增益 | 43 dB（VGA = 5） |
| 帧率 | ~10 Hz |
| 距离分辨率 | 3.75 cm/bin（c / 2BW） |
| 有效检测起点 | bin 8 = 30 cm |

## 关键约束（违反会崩溃或检测失效）

### FIFO 分片读取
- 24576 样本 > 硬件 FIFO 上限（16384），**必须**分 6 片 × 4096 读取。
- 所有 `set_fifo_limit()` 调用传 `FIFO_SLICE_SAMPLES = 4096`，**禁止**传 `NUM_SAMPLES_PER_FRAME = 24576`（断言崩溃）。
- 涉及函数：`init_sensor()`、`radar_rearm_next_measurement()`、`radar_restart_frame_generator()`。

### SPI 时钟
- **最低 25 MHz**。ADC 填充速率 ≈ 9.2 Mbps，10 MHz 经 GPIO matrix 实际不足，导致 FIFO 溢出 → FOF_ERR → 连续失败 → 重启崩溃。
- 配置位置：`my_lidar_inf.c` 的 `XENSIV_BGT60TRXX_SPI_FREQUENCY`（当前 25 MHz）。

### 初始化顺序（不能调换）
1. 释放雷达相关 GPIO hold（LP GPIO）
2. 初始化 SPI，mode 0
3. 拉高 `LDO_EN`
4. 调用 `xensiv_bgt60trxx_esp_init()`
5. 应用运行时 distance profile
6. 配置 IRQ 输入和 FIFO limit（`FIFO_SLICE_SAMPLES`）
7. 启动 frame generation

## 检测参数（调优入口）

- `RADAR_DISTANCE_THRESHOLD_DB`（当前 `-5.0f`）：距离检测 FFT 幅度阈值
- `RADAR_DISTANCE_FIRST_VALID_BIN`（当前 `8`，即 30 cm）：过滤近场杂波
- `RADAR_PRESENCE_MAX_RANGE_M`：presence 最大检测距离
- `macro_threshold` / `micro_threshold`：宏/微动检测门限

调参优先于改底层 SPI 或寄存器。

## 期望日志

**健康启动：**
```
Applied distance profile
Sensor initialized OK
Radar distance measurement started. bin_length=0.037m
```

**健康运行：**
```
Wave: breath=0.001234 heart=0.000567 frame=XXX           (每帧 10Hz)
Radar: detected=yes bin=XX level=X.XdB movement=X.XXX confidence=X.XX distance=XX.Xcm breath=XX.Xbpm heart=XX.Xbpm frame=XXX  (1Hz)
```

**可忽略的日志：**
- `SPI GSR0=0x8 CLK_NUM_ERR`：FIFO burst 遗留粘滞位，每帧后自动清除，不影响功能。（当前固件已关闭此日志）

**需要关注的日志：**
- `get_fifo_data failed`：FIFO 读取失败，检查 SPI 时钟是否 ≥ 25 MHz
- `IRQ wait high timeout`：帧生成异常，查看 FIFO 溢出状态
- `Restarting radar frame generator`：连续 3 次 FIFO 错误触发重启

## 构建与验证

```bash
# 构建
idf.py build

# 烧录并查看串口
idf.py -p COMx flash monitor

# 配置
idf.py menuconfig
```

- 改了 C/C++、CMake、组件依赖、引脚、驱动，必须执行 `idf.py build`。
- 不要编辑 `build/` 和 `managed_components/` 下的生成内容。
- 不要直接手改 `sdkconfig`，优先改 `sdkconfig.defaults*`。

## 改代码优先级

1. 优先改项目封装层（`components/my_lidar_inf/`）
2. 再改 `dependencies/sensor-xensiv-bgt60trxx/`（需说明封装层为何无法解决）
3. 保持 C 风格接口和 snake_case 命名
4. 不引入 class、继承、模板式重构

## ESP32 嵌入式约束

- ESP32-P4 FreeRTOS 任务栈有限，**禁止在函数内声明大数组（>512B）**，改用 static 或堆分配。
- SPI CS 由 sensor 库软件控制，SPI 总线配置必须 `spics_io_num = -1`，CS 引脚仍需 gpio_config 初始化为输出。
- 使用任何 SDK/库 API 前，**先读源码确认方法存在**，不要猜测方法名。

## 协作纪律

- 每次会话聚焦一个目标，完成验证后再切换下一个。
- 用户有自动编译+烧录脚本，代码改完不需要提醒编译或烧录。
- 用户说"同步"或"复制"文件时，先确认是 git 操作（merge/rebase）还是文件拷贝，不要自行假设。
- Python 代码中的中文标签、列名、UI 字符串一律使用**简体中文**，不要用繁体。