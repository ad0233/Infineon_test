# Environment Sensing 模块

室内环境传感 — 温湿度 / 环境光 / 气压。始终在线，不受隐私闸控制。

主板：BY-SA-V001 / 项目：Lunawake (BY001)。

---

## 1. 板上器件

| 位号 | 器件 | 功能 | 接口 | 采样率 |
|------|------|------|------|--------|
| U3 | AHT20 | 温湿度 | I²C | 1 Hz |
| U2 | VEML7700 | 环境光（主） | I²C | 0.67 Hz；0 – 120 000 lux |
| U1 | BH1750FVI | 环境光（次） | I²C | 可配 |
| U4 | BMP580 | 气压 | I²C / SPI | 软件文档当前未启用，可选保留或 DVT 删（见 §5） |

## 2. 输出契约

```typescript
interface Environment {
  temp_c    : number;   // °C, AHT20
  humidity  : number;   // %RH, AHT20
  lux       : number;   // VEML7700 主，BH1750 校验
  pressure  : number;   // hPa, BMP580（若启用）
}
```

| 字段 | 范围 | 精度 | 备注 |
|---|---|---|---|
| `temp_c` | −40 ~ 85 ℃ | ±0.3 ℃ | 数据手册标称 |
| `humidity` | 0 – 100 %RH | ±2 %RH | |
| `lux` | 0 – 120 000 | — | 双传感器交叉校验 |
| `pressure` | — | — | DVT 决策 |

## 3. 物理 / 结构约束

### VEML7700 镜片

VEML7700 的光学窗口对镜片透光率、波长选择、贴装位置敏感。供应商规格见
[assets/OPT-LENS-001_VEML7700镜片供应商规格要求书.md](assets/OPT-LENS-001_VEML7700镜片供应商规格要求书.md)。

结构组开孔时需保证：

- 镜片正前方无遮挡（按规格书定开孔直径、深度、贴合面）
- VEML7700 与 BH1750 物理位置错开但视场覆盖同一区域，便于交叉校验

### AHT20 / BMP580

- AHT20 不可贴近发热器件（功放 U17、DC-DC U12），否则温度读数会偏高。距离主热源 ≥ 30 mm
- BMP580 同样避开发热器件

## 4. 与隐私闸的关系

环境感知 **始终在线**，不受隐私闸切断（见 [lunawake-hardware.md §1.5](lunawake-hardware.md) §6）。
软件团队侧 [luna-panel-bridge.md §10](luna-panel-bridge.md) 也是该约束。

## 5. 待办

- [ ] BMP580 保留 / 删除 决策（DVT，见 [product-spec.md §10](product-spec.md) 遗留项中已对齐项）
- [ ] BH1750 与 VEML7700 双光传感数据融合策略（软件层）
- [ ] AHT20 长时间满载温升对读数偏移的标定

## 6. 配套资料

- [assets/OPT-LENS-001_VEML7700镜片供应商规格要求书.md](assets/OPT-LENS-001_VEML7700镜片供应商规格要求书.md) — VEML7700 镜片元器件规格要求书
- [assets/netlist-BY-SA-V001-2026-05-06.enet](assets/netlist-BY-SA-V001-2026-05-06.enet) — 网表（U1/U2/U3/U4 完整连接）
- [lunawake-hardware.md](lunawake-hardware.md) — 整机硬件总览
