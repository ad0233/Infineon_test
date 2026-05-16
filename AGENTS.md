# AGENTS.md

这份文件是本仓库面向 Codex 的项目指令文件，可视为适配到 Codex 的 `CLAUDE.md` 模板。
作用域为仓库根目录及其全部子目录。

## 回复与协作风格

- 使用简体中文。
- 回复尽量简洁直接，少解释，多落地。
- 默认直接执行任务，不先输出大段方案。
- 除非用户明确要求，否则不要主动新增 Markdown 文档。
- 编码风格优先遵循 `doc/coding_standard.md`。
- 额外的表达偏好可参考 `.cursorrules`。

## 项目定位

- 这是一个基于 ESP-IDF / ESP-ADF 的嵌入式项目，根工程名为 `Lunawake`。
- 当前 `sdkconfig` 目标为 `esp32p4`，不要随意切换 target。
- 根 `CMakeLists.txt` 依赖 `$ADF_PATH` 和 `$IDF_PATH`。
- 当前 `app_main()` 位于 `main/main.cpp`，现阶段主路径是启动 `my_lidar_inf_init()` 并打印内存信息。

## 当前主工作路径

- `main/main.cpp`
  应用入口与主流程拼装点。
- `components/my_lidar_inf/my_lidar_inf.c`
  Infineon 雷达主任务、SPI/FIFO、运行时 profile、距离检测、presence 检测主路径。
- `components/my_lidar_inf/cli_task.c`
  presence 参数调节、CLI 控制入口。
- `components/my_lidar_inf/xensiv_radar_presence_impl.c`
  RISC-V 路径下使用的 presence 实现。
- `components/my_lidar_inf/resource_map.h`
  雷达相关引脚定义。
- `dependencies/sensor-xensiv-bgt60trxx/`
  Infineon 低层驱动。
- `doc/coding_standard.md`
  代码规范。

## 重要约束

- 当前活跃雷达路径是 `components/my_lidar_inf/`，不是旧的 `components/my_lidar/`。
- 不要把旧串口雷达协议栈和 Infineon SPI 雷达链路混在一起改，除非用户明确要求。
- 心率、呼吸、睡眠等旧功能仍主要属于 `components/my_lidar/` 旧链路，不要默认往 `my_lidar_inf` 里补这些能力。
- `my_lidar_inf` 当前使用 **3 路 RX、128 点/chirp、64 chirp/frame**，每帧原始样本 = 128×64×3 = 24576。
- 运行时 profile 的真实来源在 `components/my_lidar_inf/my_lidar_inf.c`，不能只看 `radar_settings_tr13c.h`。
- 雷达频率配置：59–63 GHz，BW=4 GHz，ADC 采样率 1 MHz，IF 增益 43 dB（VGA=5）。

## 构建与验证

- 只要改了 C/C++、CMake、组件依赖、引脚、驱动、配置，默认都应至少执行一次 `idf.py build`。
- 烧录与串口日志查看使用：
  `idf.py -p COMx flash monitor`
- 若只改注释或纯文档，可不编译，但最终必须明确说明未运行验证。
- 不要编辑 `build/` 下生成内容。
- 不要编辑 `managed_components/` 下自动管理内容，除非用户明确要求处理依赖缓存。
- 除非用户明确要求提交配置变更，否则不要直接手改 `sdkconfig`；优先改 `sdkconfig.defaults*` 或通过 `menuconfig` 导出预期变更。
- 根 `CMakeLists.txt` 会创建 `spiffs_data` 分区镜像；如果改了 `spiffs/` 内容，要注意镜像随构建更新。

## Infineon 雷达专项规则

- 初始化顺序不要乱改：
  1. 释放雷达相关 GPIO hold
  2. 初始化 SPI，总线模式保持 mode 0
  3. 调用 `xensiv_bgt60trxx_esp_init(...)`
  4. 应用运行时 distance profile
  5. 配置 IRQ 输入和 FIFO limit
  6. 启动 frame generation
- 不要轻易把 IRQ 轮询改回 GPIO ISR；当前轮询是为了规避 WDT 和时序问题。
- 不要默认把未知 chip id 当成致命错误；这个板级路径可能存在兼容兜底。
- 如果距离日志不稳定，优先检查和调这几个量，而不是先动底层 SPI：
  - `RADAR_DISTANCE_THRESHOLD_DB`
  - `RADAR_DISTANCE_FIRST_VALID_BIN`
  - `RADAR_PRESENCE_MAX_RANGE_M`
  - `macro_threshold`
  - `micro_threshold`
- 没有明确证据时，不要先改 SPI 模式、帧大小、基础寄存器表。
- **FIFO 读取必须分片**：每帧 24576 样本超出硬件 FIFO 上限（16384），已拆分为 6 片 × 4096。
  所有 `set_fifo_limit()` 调用必须传 `FIFO_SLICE_SAMPLES=4096`，**不能**传 `NUM_SAMPLES_PER_FRAME=24576`（会触发断言崩溃）。
- **SPI 时钟不能低于 25 MHz**：ADC 填充速率 ≈ 768k 样本/秒 × 12 bit = 9.2 Mbps，
  10 MHz 经 GPIO matrix 实际吞吐不足，会导致 FIFO 溢出 → FOF_ERR → 连续读取失败 → 重启崩溃。

## 期望日志

- 健康启动通常会看到：
  - `Applied distance profile`
  - `Profile registers`
  - `Range bin length=...`
  - `Sensor initialized OK`
  - `Radar presence detection started`
- 健康运行通常会看到：
  - `Radar frames processed: XXXX`（每 32 帧打印一次）
  - `Distance export: detected=yes distance=XXcm bin=XX level=XXdB`
  - `Presence export: detected=yes confidence=0.80 ... flags(breath=1 phase=1 ...)`
- `CLK_NUM_ERR`（GSR0=0x8）是上一帧 FIFO burst 遗留的粘滞位，**每帧重置后自动清除**，可忽略。
- `FOU_ERR`、`SPI_BURST_ERR` 若与反复 FIFO 失败同时出现，优先检查 SPI 时钟是否 ≥ 25 MHz。

## 改代码时的优先级

- 优先改项目封装层和组件层，再考虑改 `dependencies/` 里的第三方驱动。
- 若必须修改 `dependencies/sensor-xensiv-bgt60trxx/`，要先确认封装层无法解决，并在结果里说明原因。
- 新增逻辑时优先复用现有组件命名和调用风格，不引入无必要的新抽象。
- 保持 C 风格接口和 snake_case 命名。
- 尽量避免引入 class、继承、模板式重构；这个仓库更偏 C 风格组件化。

## 常用命令

- 构建：
  `idf.py build`
- 烧录并查看串口：
  `idf.py -p COMx flash monitor`
- 配置：
  `idf.py menuconfig`
- 生成 IoT 配置 NVS：
  `python tools/gen_iot_config_nvs.py ...`
- 烧录设备证书：
  `python tools/flash_device_certs.py --port COMx ...`

## 修改前的默认思路

- 先确认问题属于哪条链路：
  - `my_lidar_inf` 的 Infineon SPI 雷达链路
  - `my_lidar` 的旧串口雷达链路
  - `main/` 的应用层拼装
  - 传感器/显示/UI/网络等外围组件
- 如果任务与雷达有关，优先阅读：
  - `components/my_lidar_inf/my_lidar_inf.c`
  - `components/my_lidar_inf/cli_task.c`
  - `components/my_lidar_inf/resource_map.h`
- 改动后优先给出：
  - 改了什么
  - 为什么这么改
  - 是否已编译验证
  - 若未验证，缺的是什么
