# BY001 雷达系统 — APP 集成说明

> Status: 🟡 needs-review · Last reviewed: 2026-04-13 · 2026-05-10 算法翻新后参数表需重核

基于 60GHz 毫米波雷达的非接触式生命体征监测模组。

---

## 检测能力

| 指标 | 范围 | 更新频率 | 延迟 |
|---|---|---|---|
| 人存在 | 有人/无人 | 10 Hz | 进场 ~2s / 离场 ~4s |
| 距离 | 37.5 - 198.75 cm | 10 Hz | 实时 |
| 呼吸率 | 12 - 30 BPM | 每 20s | 需稳定静止 |
| 心率 | 51 - 132 BPM | 每 20s | 需稳定静止 |
| 体动 | 0 - 100% | 10 Hz | ~0.5s |
| 温湿度 | -40~85℃ / 0-100%RH | 1 Hz | — |
| 环境光 | 0 - 120000 lux | 0.67 Hz | — |

---

## 数据结构

```typescript
interface RadarResult {
  detected: 0 | 1;         // 人存在
  confidence: number;       // 0.0 - 1.0
  distance_cm: number;      // 距离（无人=0）
  breath_bpm: number;       // 呼吸率（0=未测到）
  heart_bpm: number;        // 心率（0=未测到）
  rbm: 0 | 1;              // 当前是否体动
  body_move: number;        // 体动强度 0-100%
}

interface Environment {
  temp_c: number;
  humidity: number;
  lux: number;
}
```

---

## 数据有效性

| 场景 | 处理 |
|---|---|
| `detected = 0` | 忽略 vitals，显示 "--" |
| `breath_bpm / heart_bpm = 0` | 显示 "测量中" |
| `confidence < 0.5` | 提示信号弱 |
| `rbm = 1` 或 `body_move > 30` | vitals 不可信，显示 "保持静止" |

---

## 能做 / 不能做

✅ 单人非接触监测、距离测量、心率呼吸（静止时）、体动幅度
❌ 多人区分、HRV、血压血氧、运动中精确心率、穿墙

---

## 串口数据格式

```
Radar: detected=yes confidence=1.00 distance=45.0cm breath=16.9bpm heart=87.2bpm rbm=N bodyMov=0%
温湿度 T=25.5℃ RH=50.0%
VEML7700 环境光 94.0 lx
```

---

## 使用边界

- 距离：最佳 **50 - 100 cm**
- 目标：单人，室内
- 避免：正对空调出风口、风扇等机械振动源
