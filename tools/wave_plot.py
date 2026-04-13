"""
雷达 + 环境传感器实时数据可视化
用法: python wave_plot.py [COM口] [波特率]
示例: python wave_plot.py COM5 115200

串口数据格式:
  Radar: detected=yes confidence=1.00 distance=45.0cm breath=16.9bpm heart=87.2bpm rbm=N bodyMov=0%
  温湿度 T=23.5℃ RH=55.2%
  VEML7700 环境光 320.5 lx
"""
import sys
import re
import time
import serial
import matplotlib.pyplot as plt
from collections import deque

PORT = sys.argv[1] if len(sys.argv) > 1 else "COM5"
BAUD = int(sys.argv[2]) if len(sys.argv) > 2 else 115200
WINDOW = 120   # 显示最近 120 秒趋势

# ---- 正则 ----
radar_re = re.compile(
    r"Radar: detected=(\w+) confidence=([\d.]+) distance=([\d.]+)cm "
    r"breath=([\d.]+)bpm heart=([\d.]+)bpm "
    r"rbm=(\w+) bodyMov=(\d+)%"
)
ahtre = re.compile(r"温湿度 T=([-\d.]+)\S* RH=([\d.]+)")
veml_re = re.compile(r"VEML7700 环境光 ([\d.]+) lx")

# ---- 数据缓冲（按时间） ----
t_buf = deque(maxlen=WINDOW)
breath_buf = deque(maxlen=WINDOW)
heart_buf = deque(maxlen=WINDOW)
body_buf = deque(maxlen=WINDOW)
temp_buf = deque(maxlen=WINDOW)
rh_buf = deque(maxlen=WINDOW)
lux_buf = deque(maxlen=WINDOW)

state = {
    "detected": "no",
    "confidence": 0.0,
    "distance_cm": 0.0,
    "breath_bpm": 0.0,
    "heart_bpm": 0.0,
    "rbm": "N",
    "body_move": 0,
    "temp": 0.0,
    "rh": 0.0,
    "lux": 0.0,
}
t0 = None

# ---- 图表初始化 ----
plt.rcParams["font.sans-serif"] = ["SimHei", "Microsoft YaHei", "Arial"]
plt.rcParams["axes.unicode_minus"] = False

fig = plt.figure(figsize=(14, 9))
fig.suptitle("雷达 + 环境传感器实时数据", fontsize=14, fontweight="bold")
gs = fig.add_gridspec(3, 2, hspace=0.45, wspace=0.25, top=0.92, bottom=0.06,
                      left=0.08, right=0.96)

# 呼吸/心率
ax_vital = fig.add_subplot(gs[0, :])
line_breath, = ax_vital.plot([], [], "#2196F3", linewidth=1.5, label="呼吸 BPM")
line_heart,  = ax_vital.plot([], [], "#F44336", linewidth=1.5, label="心率 BPM")
ax_vital.set_ylabel("BPM", fontsize=10)
ax_vital.set_title("呼吸率 / 心率", fontsize=11, loc="left")
ax_vital.grid(True, alpha=0.2)
ax_vital.legend(loc="upper left", fontsize=9)
ax_vital.set_ylim(0, 150)

# 体动
ax_body = fig.add_subplot(gs[1, 0])
line_body, = ax_body.plot([], [], "#FF9800", linewidth=1.5)
ax_body.set_ylabel("体动 %", fontsize=10)
ax_body.set_title("体动强度", fontsize=11, loc="left")
ax_body.grid(True, alpha=0.2)
ax_body.set_ylim(0, 110)

# 环境光
ax_lux = fig.add_subplot(gs[1, 1])
line_lux, = ax_lux.plot([], [], "#FFC107", linewidth=1.5)
ax_lux.set_ylabel("lux", fontsize=10)
ax_lux.set_title("环境光 (VEML7700)", fontsize=11, loc="left")
ax_lux.grid(True, alpha=0.2)

# 温度
ax_temp = fig.add_subplot(gs[2, 0])
line_temp, = ax_temp.plot([], [], "#4CAF50", linewidth=1.5)
ax_temp.set_ylabel("℃", fontsize=10)
ax_temp.set_xlabel("时间 (秒)", fontsize=10)
ax_temp.set_title("温度 (AHT20)", fontsize=11, loc="left")
ax_temp.grid(True, alpha=0.2)

# 湿度
ax_rh = fig.add_subplot(gs[2, 1])
line_rh, = ax_rh.plot([], [], "#009688", linewidth=1.5)
ax_rh.set_ylabel("%", fontsize=10)
ax_rh.set_xlabel("时间 (秒)", fontsize=10)
ax_rh.set_title("相对湿度 (AHT20)", fontsize=11, loc="left")
ax_rh.grid(True, alpha=0.2)
ax_rh.set_ylim(0, 100)

# 状态栏
status_text = fig.text(
    0.5, 0.965, "等待数据...", ha="center", fontsize=10,
    bbox=dict(boxstyle="round,pad=0.3", facecolor="#F5F5F5", edgecolor="#BDBDBD")
)

plt.ion()
plt.show(block=False)

# ---- 串口连接 ----
print(f"正在连接 {PORT} @ {BAUD}...")
try:
    ser = serial.Serial(PORT, BAUD, timeout=0.1)
except serial.SerialException as e:
    print(f"串口打开失败: {e}")
    sys.exit(1)
print(f"已连接。")


def now_t():
    global t0
    if t0 is None:
        t0 = time.time()
    return time.time() - t0


def push_vital():
    """Radar 行到达时推送一次所有值到缓冲区（时间对齐）"""
    t = now_t()
    t_buf.append(t)
    breath_buf.append(state["breath_bpm"])
    heart_buf.append(state["heart_bpm"])
    body_buf.append(state["body_move"])
    temp_buf.append(state["temp"])
    rh_buf.append(state["rh"])
    lux_buf.append(state["lux"])


def refresh():
    if len(t_buf) < 2:
        return
    ts = list(t_buf)

    line_breath.set_data(ts, list(breath_buf))
    line_heart.set_data(ts, list(heart_buf))
    line_body.set_data(ts, list(body_buf))
    line_lux.set_data(ts, list(lux_buf))
    line_temp.set_data(ts, list(temp_buf))
    line_rh.set_data(ts, list(rh_buf))

    xlim = (max(0, ts[-1] - WINDOW), ts[-1] + 1)
    for ax in (ax_vital, ax_body, ax_lux, ax_temp, ax_rh):
        ax.set_xlim(*xlim)

    # 自适应
    if lux_buf:
        m = max(lux_buf) + 50
        ax_lux.set_ylim(0, max(m, 100))
    if temp_buf:
        tmin, tmax = min(temp_buf), max(temp_buf)
        span = max(tmax - tmin, 2.0)
        ax_temp.set_ylim(tmin - span * 0.2, tmax + span * 0.2)

    det_icon = "●" if state["detected"] == "yes" else "○"
    rbm_icon = "⚡" if state["rbm"] == "Y" else "·"
    status_text.set_text(
        f"{det_icon} {'有人' if state['detected']=='yes' else '无人'} | "
        f"距离 {state['distance_cm']:.0f}cm | "
        f"置信度 {state['confidence']:.0%} | "
        f"呼吸 {state['breath_bpm']:.1f} BPM | "
        f"心率 {state['heart_bpm']:.1f} BPM | "
        f"{rbm_icon} 体动 {state['body_move']}% | "
        f"{state['temp']:.1f}℃ {state['rh']:.0f}%RH | "
        f"{state['lux']:.0f} lx"
    )

    fig.canvas.draw_idle()
    fig.canvas.flush_events()


# ---- 主循环 ----
try:
    while True:
        raw = ser.readline()
        if not raw:
            continue
        try:
            line = raw.decode("utf-8", errors="ignore").strip()
        except Exception:
            continue

        m = radar_re.search(line)
        if m:
            state["detected"]    = m.group(1)
            state["confidence"]  = float(m.group(2))
            state["distance_cm"] = float(m.group(3))
            state["breath_bpm"]  = float(m.group(4))
            state["heart_bpm"]   = float(m.group(5))
            state["rbm"]         = m.group(6)
            state["body_move"]   = int(m.group(7))
            push_vital()
            refresh()
            continue

        m = ahtre.search(line)
        if m:
            state["temp"] = float(m.group(1))
            state["rh"]   = float(m.group(2))
            continue

        m = veml_re.search(line)
        if m:
            state["lux"] = float(m.group(1))
            continue

except KeyboardInterrupt:
    print("\n已停止。")
finally:
    ser.close()
    plt.close()
