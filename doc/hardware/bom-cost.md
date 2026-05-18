# BOM & Cost — BY-SA-V001

> Status: 🟡 needs-review · Last reviewed: 2026-05-18
>
> BOM 从 [assets/netlist-BY-SA-V001-2026-05-06.enet](assets/netlist-BY-SA-V001-2026-05-06.enet) 自动抽取分组（脚本见末尾）。**网表更新后需重跑**。
>
> 成本数据 PM 视角：单价单位 ¥，料源 LCSC（量产可议价）。当前为 **EVT 单价**，**未含人工 / 测试 / 损耗 / PCB 板费**。

---

## 单台成本速览（EVT 估算）

| 类别 | 数量 | 估算单台成本 | 备注 |
|---|---|---|---|
| 主控（ESP32-P4 + Flash） | 2 | TBD | U5 + U60 |
| Wi-Fi/BT 模组（OA-W01） | 1 | TBD | U6，三方模组单点风险 |
| 雷达（BGT60TR13C） | 1 | TBD | 单点风险件，无国产替代 |
| 音频（双 codec + 功放） | 3 | TBD | U14 + U16 + U17 |
| 传感器 | 4 | TBD | AHT20 + VEML7700 + BH1750 + BMP580 |
| 麦克风 | 4（V1.1 → 2） | TBD | 双麦改板后省 2 颗 |
| 电源管理 | 3 | TBD | DC-DC + LDO + 保险 |
| 被动器件（R/C/L） | 122 | TBD | 大多 Basic Part，量产成本低 |
| 连接器 + USB + 按键 | 10 | TBD | |
| PCB 板费 | 1 | TBD | 4 层板估算 |
| **整机 BOM 合计** | — | **TBD** | 不含人工 / 损耗 |

> **TODO**：等供应商报价回来填表。先用单价 × 数量算粗值即可。

---

## 关键件（单点风险 / 不可替代）

| 位号 | 器件 | 供应商 | 替代方案 | 备注 |
|---|---|---|---|---|
| U5 | ESP32-P4 | Espressif | 无（IDF 锁定 P4） | 主控冻结，见 [decisions.md](../progress/decisions.md) |
| 雷达 IC | BGT60TR13C | Infineon | 无国产同类 60 GHz FMCW | 量产前需双源备货 |
| U6 | OA-W01 Wi-Fi 5G 模组 | Cross Air | 单点风险高，需备替代模组型号 | 含天线，更换需重测认证 |
| U60 | MKDV4GCL-ABB（4 Gb SLC NAND） | 美光 / 美光代理 | 可换其他 SLC NAND（需 IDF NAND 驱动适配） | |
| U14 / U16 | ES8311 codec | 周立功 / 国产替代多 | ES7148 等 | 量产替代灵活 |
| U17 | 2×10 W AB/D 类功放 | 待型号确认 | NS4225B / TPA3110 等 | EVT 网表无明确部件号 |

---

## BOM 详表（按类别）

### C — 电容（73 pcs / 18 型号）

| 位号 | 数量 | 规格 | 厂商 P/N | LCSC | 类别 |
|---|---|---|---|---|---|
| C1, C3, C64, C67 | 4 | 10 µF / 25 V X5R | CL10A106MA8NRNC | C96446 | Basic |
| C2, C4, C5, C65, C66, C68, C178 | 7 | 100 nF | CL05B104KB54PNC | C307331 | Basic |
| C6, C7, C345 | 3 | 10 µF | CL05A106MQ5NUNC | C15525 | 基础库 |
| C9, C12 | 2 | 10 µF | CL10A106MA8NRNC | C96446 | 基础库 |
| C10, C11, C48, C49, C54, C346 | 6 | 100 nF | CL05B104KB54PNC | C307331 | 基础库 |
| C13, C15, C16, C26, C27, C32, C33, C38, C39 | 9 | 100 nF | CL05B104KO5NNNC | C1525 | Basic |
| C14 | 1 | 2.2 µF | CL05A225MQ5NSNC | C12530 | Basic |
| C17, C18, C19, C20, C22, C23 | 6 | 22 µF | CL21A226MAQNNNE | C45783 | Basic |
| C21 | 1 | 220 pF | 0402B221K500NT | C1530 | Basic |
| C24, C25 | 2 | 10 µF | CL10A106KP8NNNC | C19702 | Basic |
| C28–C31, C34–C37, C40–C47, C50, C53, C56 | 19 | 1 µF | CL05A105KA5NQNC | C52923 | Basic |
| C51, C52 | 2 | 470 µF | MA16V470M8x10 | C46550462 | Extended |
| C55, C59 | 2 | 3.3 nF | CC0603KRX7R9BB332 | C107088 | Extended |
| C57 | 1 | 22 nF | 0402B223K500NT | C1532 | Basic |
| C60, C61, C62, C63 | 4 | 1 nF | 0402B102K500NT | C1523 | Basic |
| C74, C142 | 2 | 12 pF | 0402CG120J500NT | C1547 | Basic |
| C177 | 1 | 1 µF | CL05A105KA5NQNC | C52923 | Basic |
| C8 | 1 | NC（不贴） | CL05C100JB5NNNC | — | 基础库 |

### R — 电阻（47 pcs / 13 型号）

| 位号 | 数量 | 规格 | 厂商 P/N | LCSC | 类别 |
|---|---|---|---|---|---|
| R1, R2, R36, R39 | 4 | 10 Ω | 0402WGF100JTCE | C25077 | Basic |
| R3, R4, R5, R16, R34, R37, R38, R76, R347-R352 | 14 | 10 kΩ | 0402WGF1002TCE | C25744 | Basic |
| R6, R7, R10, R11, R33, R361 | 6 | 0 Ω | 0402WGF0000TCE | C17168 | 基础库 |
| R8, R9, R14, R15 | 4 | 5.1 kΩ | 0402WGF5101TCE | C25905 | 基础库 |
| R12, R13 | 2 | 2.2 Ω | 0402WGJ022JTCE | C25158 | 扩展库 |
| R17 | 1 | 22 kΩ | 0402WGF2202TCE | C25768 | Basic |
| R18, R28 | 2 | 100 kΩ | 0402WGF1003TCE | C25741 | Basic |
| R19 | 1 | 1 kΩ | 0402WGF1001TCE | C11702 | Basic |
| R20, R21, R22, R23 | 4 | 100 Ω | 0402WGF1000TCE | C25076 | Basic |
| R24-R27, R29 | 5 | 20 kΩ | RC0402FR-0720KL | C93942 | Extended |
| R30 | 1 | 36 kΩ | 0402WGF3602TCE | C43676 | Extended |
| R31, R35 | 2 | 1 Ω | 0603WAF100KT5E | C22936 | Basic |
| R32 | 1 | 1 MΩ | 0402WGF1004TCE | C26083 | Basic |

### L — 电感（2 pcs）

| 位号 | 规格 | 厂商 P/N | LCSC |
|---|---|---|---|
| L1 | 1.5 µH | FXL0650-1R5-M | C475524 |
| L2 | 4.7 µH | FXL0650-4R7-M | C475520 |

### D — 二极管（5 pcs / 3 型号）

| 位号 | 数量 | 规格 | 厂商 P/N | LCSC |
|---|---|---|---|---|
| D1 | 1 | SMF5.0A（TVS） | SMF5.0A | C19077497 |
| D2, D3 | 2 | SS52F（肖特基） | SS52F | C41411778 |
| D6, D7 | 2 | 1N4148WS | 1N4148WS | C2128 |

### F — 保险（1 pcs）

| 位号 | 规格 | 厂商 P/N | LCSC |
|---|---|---|---|
| F1 | SMD0805-100-12（1 A / 12 V） | SMD0805-100-12 | C46640991 |

### U — IC（21 pcs / 17 型号）

| 位号 | 数量 | 器件 | 厂商 P/N | LCSC | 角色 |
|---|---|---|---|---|---|
| U1 | 1 | BH1750FVI | BH1750FVI-TR | C78960 | 光传感（备） |
| U2 | 1 | VEML7700 | VEML7700-TR | C504893 | 光传感（主） |
| U3 | 1 | AHT20 | AHT20 | C2757850 | 温湿度 |
| U4 | 1 | BMP580 | BMP580 | C22391138 | 气压（DVT 待决去留） |
| **U5** | 1 | **ESP32-P4** | （网表未填）| — | **主控** |
| **U6** | 1 | **Cross Air OA-W01** | （网表部分填）| — | **Wi-Fi 5G + BT 模组** |
| U7, U8, U10, U11 | 4 | RCLAMP0521T-ES | RCLAMP0521T-ES | C5180263 | ESD 保护 |
| U9 | 1 | 60 µH 共模电感 | PSTFAQ3416-600T020 | C3011570 | — |
| U12 | 1 | SY8368QNC | SY8368QNC | C125897 | DC-DC |
| U13 | 1 | PZ2.54-2×4 排针 | PZ2.54-2*4 | C5156676 | 调试口 |
| U14, U16 | 2 | ES8311 codec | ES8311 | C962342 | 立体声 codec |
| U15 | 1 | XC6219B332MR | XC6219B332MR | C347386 | LDO |
| **U17** | 1 | **2×10 W AB/D 类功放** | （网表未填）| — | **功放，待选型** |
| U20 | 1 | AiP8563 RTC | AiP8563 | C116087 | RTC |
| U39 | 1 | CR1220 座 | XDCR-1220-006 | C7498147 | RTC 备份电池座 |
| **U54** | 1 | （网表未填）| — | — | **未明，可能是电平转换 TXS0108E** |
| U60 | 1 | MKDV4GCL-ABB | MKDV4GCL-ABB | C7500178 | 4 Gb SLC NAND |

> **U5 / U6 / U17 / U54** 网表条目缺型号 → 抽 BOM 前需在 EDA 工具里补完厂商 P/N。

### MIC — 麦克风（V1.0 4 颗 / V1.1 → 2 颗）

| 位号 | 数量 | 器件 | LCSC | 备注 |
|---|---|---|---|---|
| MIC1, MIC3 | 2 | LHD3526B261-OFA03 | C53446629 | V1.1 改板：MIC1 改 LCD..., MIC3 删 |
| MIC2, MIC4 | 2 | LCD3526B261-OFA03 | C53446628 | V1.1 改板：MIC2 删；MIC4 保留 |

V1.1 决策见 [microphone.md](microphone.md) + [decisions.md](../progress/decisions.md) 2026-05-13。

### CN / USB / H / SW / X — 接口与机械

| 位号 | 数量 | 器件 | LCSC |
|---|---|---|---|
| CN2 | 1 | HC-1.25-8PLT（PicoBlade 8P，调试） | C2845394 |
| CN4, CN18 | 2 | HC-1.25-6PLT（PicoBlade 6P，喇叭 L/R） | C2845392 |
| USB1 | 1 | TYPE-C 16PIN 卧贴 | C2765186 |
| USB2 | 1 | USB-A 180° 直插 | C456015 |
| H1, H2 | 2 | PH2.0 2P 卧贴（DC 电源） | C3029440 |
| SW1, SW2, SW3 | 3 | SKRPACE010 立贴轻触开关 | C139797 |
| X2 | 1 | 32.768 kHz 晶振（RTC） | C7500573 |

---

## 元件数量汇总

| 类别 | 数量 | 型号数 |
|---|---|---|
| 电容 C | 73 | 18 |
| 电阻 R | 47 | 13 |
| 电感 L | 2 | 2 |
| 二极管 D | 5 | 3 |
| 保险 F | 1 | 1 |
| IC U | 21 | 17 |
| 麦克风 MIC | 4 → 2（V1.1） | 2 → 1（V1.1 统一型号） |
| 连接器 CN/USB/H | 7 | 5 |
| 按键 SW | 3 | 1 |
| 晶振 X | 1 | 1 |
| **合计** | **164 → 162** | **63** |

---

## 替代方案 / 国产化备份

| 类别 | 当前选型 | 国产 / 替代选项 | 备注 |
|---|---|---|---|
| 主控 | ESP32-P4 | 无（IDF 锁定） | 不可替代 |
| 雷达 | BGT60TR13C | 无国产同类 60 GHz FMCW | 单点风险件 |
| Wi-Fi 模组 | OA-W01 | 安信可 ESP32-C6 模组、矽昌 5G 模组 | 替换需重测认证 |
| 光传感 | VEML7700 / BH1750 | OPT3001、SI1133 | 双源备份充足 |
| 温湿度 | AHT20 | SHT41、HDC2080 | 双源备份充足 |
| 气压 | BMP580 | BMP390、ICP-10125 | 若 DVT 删则不需要 |
| codec | ES8311 | TLV320AIC3104、WM8978 | 量产替代灵活 |
| 功放 | 2×10 W AB/D | TPA3110D2、NS4225B | EVT 网表未填，DVT 选型 |
| DC-DC | SY8368QNC | TPS54302、MP2143 | 兼容封装直接换 |
| LDO | XC6219B332MR | AMS1117、LDL1117 | 国产替代多 |
| MCU 闪存 | MKDV4GCL-ABB | 兆易 GD5F4GQ4UC、长江存储 SLC NAND | 需 NAND 驱动适配 |

---

## 维护

- 网表更新后重跑下面脚本：
  ```bash
  python tools/extract_bom.py doc/hardware/assets/netlist-BY-SA-V001-2026-05-06.enet > /tmp/bom.md
  # 手动合并 /tmp/bom.md 中"### C — ..."等表段到本文件
  ```
  （脚本待写。当前手动跑 inline Python，见会话 2026-05-18 记录）
- 单价 / 单台成本估算等供应商报价后填
- V1.1 改板后重抽 BOM，对比删除 / 替换条目
