# System Block Diagram

> Status: 🟢 current · Last reviewed: 2026-05-18
>
> 整机数据流 + 电源域。GitHub 自动渲染 mermaid。给未来的自己 + 给 Luna 团队解释最快的一张图。

---

## 1. 数据流（信号链）

```mermaid
flowchart LR
    %% 输入侧
    Radar["BGT60TR13C<br/>60 GHz 雷达<br/>1Tx/3Rx (RX1)"]
    MIC["4 麦阵列<br/>(V1.1 → 双麦)<br/>LCD3526B261"]
    AHT["AHT20<br/>温湿度"]
    VEML["VEML7700<br/>+ BH1750<br/>环境光"]
    BMP["BMP580<br/>气压 (待定)"]
    BTN["3× 轻触按键<br/>SW1/SW2/SW3"]

    %% 主控（P4 + C6 双芯）
    ESP["U5 主控模组<br/>JC-ESP32P4-M3<br/>(ESP32-P4 双核 RISC-V<br/>+16MB Flash+8MB PSRAM<br/>IDF v5.5)"]

    %% 输出侧
    LED["RGB LED<br/>GPIO15/16/17"]
    Codec["2× ES8311<br/>U14 / U16"]
    Amp["U17 功放<br/>2×10 W AB/D"]
    Spk["喇叭 L/R<br/>CN4 / CN18"]

    %% 通信
    Wifi["U6 协处理器模组<br/>Cross Air OA-W01<br/>(ESP32-C6 + 天线)<br/>Wi-Fi 2.4G + BT 5.0"]
    USBC["USB-C<br/>OTA + 数据"]

    %% 存储（板外，M3 模组内已含 16MB Flash + 8MB PSRAM）
    Flash["U60 板外存储<br/>MKDV4GCL-ABB<br/>4 Gb (512 MB) SLC NAND<br/>(音频素材 / 日志)"]

    Radar -- "SPI 25 MHz<br/>GPIO0/1/3/5+IRQ" --> ESP
    MIC -- "PDM<br/>GPIO10/11 + GPIO54(V1.0)" --> ESP
    AHT -- "I²C" --> ESP
    VEML -- "I²C" --> ESP
    BMP -- "I²C" --> ESP
    BTN -- "GPIO" --> ESP

    ESP -- "GPIO" --> LED
    ESP -- "I²S" --> Codec
    Codec -- "模拟" --> Amp
    Amp --> Spk

    ESP <-- "esp_hosted 透传<br/>SDIO/SPI" --> Wifi
    ESP <-- "USB" --> USBC

    ESP <--> Flash

    %% App 侧
    App["手机 App<br/>(Flutter)"]
    Wifi <-- "BluFi (配网)<br/>WS 8080/ws (数据)" --> App

    %% 样式
    classDef sensor fill:#dfd,stroke:#2a2;
    classDef output fill:#fdd,stroke:#a22;
    classDef core fill:#ddf,stroke:#22a,stroke-width:2px;
    classDef comm fill:#ffd,stroke:#aa2;

    class Radar,MIC,AHT,VEML,BMP,BTN sensor;
    class LED,Codec,Amp,Spk output;
    class ESP core;
    class Wifi,USBC,App comm;
```

## 2. 电源域

```mermaid
flowchart TD
    DC5V["5 V 主输入<br/>USB-C (USB1) / DC (H1)"]
    F1[F1 保险 1 A]
    D1[D1 TVS]
    U12["U12 SY8368<br/>DC-DC"]
    U15["U15 XC6219<br/>LDO (codec)"]
    USBA["USB-A 下游<br/>≤ 1.5 A passthrough"]

    V3V3D["3V3 数字域<br/>ESP32-P4 / 雷达数字侧<br/>麦克风 / 传感器 / LED"]
    V3V3A["3V3 模拟域<br/>ES8311 codec"]
    V1V8["1V8<br/>BGT60TR13C 内部 LDO 后"]
    VRTC["RTC 域<br/>CR1220 (U39)<br/>仅 AiP8563"]

    DC5V --> F1
    F1 --> D1
    D1 --> U12
    D1 --> USBA
    U12 -- "3V3" --> V3V3D
    V3V3D --> U15
    U15 --> V3V3A
    V3V3D -- "→ TXS0108E<br/>(电平转换)" --> V1V8
    CR1220["CR1220 纽扣"] --> VRTC

    classDef power fill:#ffe,stroke:#a82;
    classDef domain fill:#efe,stroke:#282;
    class DC5V,F1,D1,U12,U15,USBA,CR1220 power;
    class V3V3D,V3V3A,V1V8,VRTC domain;
```

## 3. 控制 / 状态机概览

```mermaid
stateDiagram-v2
    [*] --> Idle: 通电
    Idle --> Monitor: 单击 AUTO
    Monitor --> WindDown: 预设时间到 + 雷达检测到人
    WindDown --> WindDown_Voice: 助眠态单击 AUTO
    WindDown --> Asleep: 入睡判定 (呼吸稳定 + 体动衰减)
    Asleep --> Wake: wakeTime 到 / 智能窗口
    Wake --> Monitor: 起床
    
    Monitor --> Idle: 长按 AUTO
    WindDown --> Monitor: 长按 AUTO / 雷达持续无人
    
    note right of Asleep
        全本地闭环
        不依赖云
        断网整夜可跑
    end note
    
    note left of Idle
        隐私闸关
        雷达 VCC 物理切断
    end note
```

详细状态机映射见 [luna-panel-bridge.md §8](luna-panel-bridge.md)。

## 4. 模块归属图

```mermaid
flowchart TB
    subgraph "感知层"
      direction LR
      R[radar.md]
      M[microphone.md]
      E[environment.md]
    end
    
    subgraph "输出层"
      direction LR
      A[audio.md]
      I[indicator.md]
    end
    
    subgraph "基础层"
      direction LR
      P[power.md]
      N[networking.md]
    end
    
    subgraph "契约层 (对外)"
      direction LR
      AP[app-protocol.md]
      LP[luna-panel-bridge.md]
    end
    
    R --> AP
    R --> LP
    M --> LP
    E --> AP
    
    P -.供电.-> R
    P -.供电.-> M
    P -.供电.-> E
    P -.供电.-> A
    P -.供电.-> I
    
    N --- AP
    N --- LP
```

---

## 关键引脚速查

完整定义在 `components/my_lidar_inf/resource_map.h`（**权威**）。这里只列高频用到的。

| 信号 | GPIO | 模块 |
|---|---|---|
| Radar SPI SCLK | GPIO0 | radar |
| Radar SPI MOSI | GPIO1 | radar |
| Radar SPI MISO | GPIO3 | radar |
| Radar SPI CSN | GPIO5 | radar（软件控制） |
| Radar IRQ | GPIO2 | radar |
| Radar RSTN | GPIO13 | radar |
| RGB R / G / B | GPIO15 / 16 / 17 | indicator |
| PDM CLK | GPIO11 | microphone |
| PDM DATA0（MIC1+MIC4 立体声） | GPIO10 | microphone |
| PDM DATA1（MIC2+MIC3，V1.1 删） | GPIO54 | microphone |
| I²C SCL / SDA | （见 resource_map.h） | environment |
