# 雷达罩首件验证流程

DVT 第一版结构件回来后必须跑的对照测试。判定外壳/雷达罩材料、厚度、装饰条、筋位是否合格。

源：[../../hardware/product-spec.md §5.4](../../hardware/product-spec.md)（雷达罩硬约束清单 §5.1–5.3 见同文档）。

---

## 1. 前置条件

- [ ] PCB 已完整组装（雷达正常工作，可输出 vitals）
- [ ] DVT 第一版外壳到件，含雷达罩 + 装饰条 + 完整结构件
- [ ] 测试环境：室内、空场、远离空调出风口和风扇
- [ ] 测试者一人，仰卧、平静呼吸、不大动
- [ ] 测距 80 cm 标记到位
- [ ] 工具就绪：`tools/sleep_analyze.py`（unified panel GUI）+ CSV 录制

## 2. 测试步骤

### A. 裸板基线（不带壳）

1. PCB 裸板按摆放姿态放在床头柜上（仰卧用户胸口距雷达 80 cm，雷达高于胸口 15 cm，俯仰 −15°）
2. 启动 panel GUI，打开 CSV 录制
3. 用户仰卧静止 **60 s**
4. 关闭录制 → 文件命名 `<date>_baseline_naked.csv`

### B. 带 DVT 外壳

1. 装上 DVT 外壳（含罩 + 装饰条 + 内部所有结构件）
2. **同位置、同姿势** 重测 60 s
3. 文件命名 `<date>_dvt_<rev>.csv`

### C. 数据对比

跑 `tools/sleep_analyze.py` 对比 A / B：

| 指标 | 阈值 |
|---|---|
| `distance_cm` 曲线最大偏差 | ≤ 5 cm |
| `breath_bpm` 稳态偏差 | ≤ 2 BPM |
| `heart_bpm` 稳态偏差 | ≤ 5 BPM |
| `confidence` 均值下降 | ≤ 0.1 |

## 3. 判定

**任一项不达标 → 不通过**。按下面顺序拆罩定位：

1. 罩面材料：是否含玻纤 / 金属漆 / IMD 膜（违反 [product-spec.md §5.1](../../hardware/product-spec.md)）
2. 罩面厚度：是否在 1.0 ± 0.1 mm（最大 2.0 mm）
3. 装饰条距 IC ≥ 25 mm，且不在 ±45° 锥内
4. 内壁加强筋 / 螺柱在雷达正面 ±20 mm 范围内是否被引入
5. 喇叭磁铁距雷达 IC ≥ 50 mm

修正后**回到步骤 B 重测**。

## 4. 已知坑（实验室历史教训）

- 第一版打样罩面贴了 IMD 金属膜 → 信号完全消失
- 罩内壁加强筋距 IC 太近 → distance 曲线出现 30 cm 处假目标
- 喇叭磁铁正对雷达 → confidence 永远 < 0.5

## 5. 通过后归档

- [ ] CSV 原始数据归档到 `tools/records/<date>/radome-validation/`
- [ ] `progress/dvt-radome-firstpiece.md` 写一份简报（pass / fail + 偏差数值 + 走过的 iteration）
- [ ] 把 DVT rev 号写进 [../changelog.md](../changelog.md)

## 6. 参考

- [../../hardware/product-spec.md §5](../../hardware/product-spec.md) — 雷达罩硬约束完整清单
- [../../hardware/radar.md](../../hardware/radar.md) — 雷达模块对外契约
- [../../hardware/assets/bgt60-beam-pattern.png](../../hardware/assets/bgt60-beam-pattern.png) — 波束开角参考
