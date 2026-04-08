"""
雷达生命体征实时波形可视化
用法: python wave_plot.py [COM口] [波特率]
示例: python wave_plot.py COM5 115200
"""
import sys
import re
import time
import serial
import matplotlib.pyplot as plt
from collections import deque

PORT = sys.argv[1] if len(sys.argv) > 1 else "COM5"
BAUD = int(sys.argv[2]) if len(sys.argv) > 2 else 115200
WINDOW = 200  # 显示最近 200 帧 (~20s @ 10Hz)
FS = 10.0     # 帧率

# ---- 正则 ----
wave_re = re.compile(r"Wave: breath=([-\d.]+) heart=([-\d.]+) frame=(\d+)")
radar_re = re.compile(
    r"Radar: detected=(\w+) bin=(\d+) level=([-\d.]+)dB "
    r"movement=([\d.]+) confidence=([\d.]+) distance=([\d.]+)cm "
    r"breath=([\d.]+)bpm heart=([\d.]+)bpm frame=(\d+)"
)

# ---- 数据缓冲 ----
breath_buf = deque(maxlen=WINDOW)
heart_buf = deque(maxlen=WINDOW)
time_buf = deque(maxlen=WINDOW)   # 相对时间 (秒)

state = {
    "breath_bpm": 0.0,
    "heart_bpm": 0.0,
    "detected": "no",
    "distance_cm": 0.0,
    "confidence": 0.0,
    "level_db": 0.0,
    "movement": 0.0,
    "start_frame": None,
}

# ---- 图表初始化 ----
plt.rcParams["font.sans-serif"] = ["SimHei", "Microsoft YaHei", "Arial"]
plt.rcParams["axes.unicode_minus"] = False

fig, (ax1, ax2) = plt.subplots(2, 1, figsize=(14, 7))
fig.suptitle("雷达生命体征实时波形", fontsize=14, fontweight="bold")
fig.subplots_adjust(top=0.88, hspace=0.35)

# 呼吸波形
line_b, = ax1.plot([], [], "#2196F3", linewidth=1.2, label="呼吸波形")
zero_b = ax1.axhline(y=0, color="gray", linewidth=0.5, linestyle="--")
ax1.set_ylabel("呼吸信号幅度", fontsize=11)
ax1.set_title("呼吸波形 (0.1–0.6 Hz BPF)", fontsize=11, loc="left")
ax1.grid(True, alpha=0.2)
bpm_text_b = ax1.text(
    0.98, 0.95, "", transform=ax1.transAxes, fontsize=16,
    fontweight="bold", ha="right", va="top",
    bbox=dict(boxstyle="round,pad=0.4", facecolor="#E3F2FD", edgecolor="#2196F3")
)

# 心率波形
line_h, = ax2.plot([], [], "#F44336", linewidth=1.2, label="心率波形")
zero_h = ax2.axhline(y=0, color="gray", linewidth=0.5, linestyle="--")
ax2.set_ylabel("心率信号幅度", fontsize=11)
ax2.set_xlabel("时间 (秒)", fontsize=11)
ax2.set_title("心率波形 (0.85–2.2 Hz BPF)", fontsize=11, loc="left")
ax2.grid(True, alpha=0.2)
bpm_text_h = ax2.text(
    0.98, 0.95, "", transform=ax2.transAxes, fontsize=16,
    fontweight="bold", ha="right", va="top",
    bbox=dict(boxstyle="round,pad=0.4", facecolor="#FFEBEE", edgecolor="#F44336")
)

# 状态栏
status_text = fig.text(
    0.5, 0.95, "等待数据...", ha="center", fontsize=10,
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
print(f"已连接。等待 Wave/Radar 数据...")

# ---- 主循环 ----
update_counter = 0
frame_count = 0

try:
    while True:
        raw = ser.readline()
        if not raw:
            continue
        try:
            line = raw.decode("utf-8", errors="ignore").strip()
        except Exception:
            continue

        # 解析 Wave 行
        m = wave_re.search(line)
        if m:
            b_val = float(m.group(1))
            h_val = float(m.group(2))
            frame = int(m.group(3))

            if state["start_frame"] is None:
                state["start_frame"] = frame

            t = (frame - state["start_frame"]) / FS
            breath_buf.append(b_val)
            heart_buf.append(h_val)
            time_buf.append(t)
            update_counter += 1
            frame_count += 1

        # 解析 Radar 行
        m2 = radar_re.search(line)
        if m2:
            state["detected"] = m2.group(1)
            state["level_db"] = float(m2.group(3))
            state["movement"] = float(m2.group(4))
            state["confidence"] = float(m2.group(5))
            state["distance_cm"] = float(m2.group(6))
            state["breath_bpm"] = float(m2.group(7))
            state["heart_bpm"] = float(m2.group(8))

        # 每 5 帧刷新图表
        if update_counter >= 5 and len(time_buf) > 1:
            update_counter = 0
            t_list = list(time_buf)
            b_list = list(breath_buf)
            h_list = list(heart_buf)

            # 更新曲线
            line_b.set_data(t_list, b_list)
            line_h.set_data(t_list, h_list)

            # X 轴范围
            ax1.set_xlim(t_list[0], t_list[-1])
            ax2.set_xlim(t_list[0], t_list[-1])

            # Y 轴自适应
            if b_list:
                b_margin = max(abs(max(b_list)), abs(min(b_list)), 0.01) * 1.3
                ax1.set_ylim(-b_margin, b_margin)
            if h_list:
                h_margin = max(abs(max(h_list)), abs(min(h_list)), 0.01) * 1.3
                ax2.set_ylim(-h_margin, h_margin)

            # BPM 数字
            b_bpm = state["breath_bpm"]
            h_bpm = state["heart_bpm"]
            bpm_text_b.set_text(f"{b_bpm:.1f} BPM" if b_bpm > 0 else "-- BPM")
            bpm_text_h.set_text(f"{h_bpm:.1f} BPM" if h_bpm > 0 else "-- BPM")

            # 状态栏
            det = "有人" if state["detected"] == "yes" else "无人"
            status_text.set_text(
                f"{det} | 距离 {state['distance_cm']:.0f}cm | "
                f"置信度 {state['confidence']:.0%} | "
                f"信号 {state['level_db']:.1f}dB | "
                f"运动 {state['movement']:.3f} | "
                f"帧 {frame_count}"
            )

            fig.canvas.draw_idle()
            fig.canvas.flush_events()

except KeyboardInterrupt:
    print("\n已停止。")
finally:
    ser.close()
    plt.close()
