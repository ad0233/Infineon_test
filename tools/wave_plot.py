"""
实时雷达呼吸/心率波形可视化
用法: python wave_plot.py COM5
"""
import sys
import re
import serial
import matplotlib.pyplot as plt
from collections import deque

PORT = sys.argv[1] if len(sys.argv) > 1 else "COM5"
BAUD = 115200
WINDOW = 200  # 显示最近 200 帧 (~20s)

# 正则匹配 Wave 和 Radar 行
wave_re = re.compile(r"Wave: breath=([-\d.]+) heart=([-\d.]+) frame=(\d+)")
radar_re = re.compile(r"breath=([\d.]+)bpm heart=([\d.]+)bpm")

breath_buf = deque(maxlen=WINDOW)
heart_buf = deque(maxlen=WINDOW)
frame_buf = deque(maxlen=WINDOW)
last_bpm = {"breath": 0.0, "heart": 0.0}

plt.ion()
fig, (ax1, ax2) = plt.subplots(2, 1, figsize=(12, 6), sharex=True)
fig.suptitle("Radar Vital Signs Waveform")

line_b, = ax1.plot([], [], "b-", linewidth=1)
ax1.set_ylabel("Breath Wave")
ax1.grid(True, alpha=0.3)
bpm_text_b = ax1.text(0.02, 0.95, "", transform=ax1.transAxes,
                       fontsize=12, verticalalignment="top",
                       bbox=dict(boxstyle="round", facecolor="lightblue"))

line_h, = ax2.plot([], [], "r-", linewidth=1)
ax2.set_ylabel("Heart Wave")
ax2.set_xlabel("Frame")
ax2.grid(True, alpha=0.3)
bpm_text_h = ax2.text(0.02, 0.95, "", transform=ax2.transAxes,
                       fontsize=12, verticalalignment="top",
                       bbox=dict(boxstyle="round", facecolor="lightyellow"))

plt.tight_layout()

print(f"Connecting to {PORT} @ {BAUD}...")
ser = serial.Serial(PORT, BAUD, timeout=0.1)
print("Connected. Waiting for data...")

update_counter = 0
try:
    while True:
        raw = ser.readline()
        if not raw:
            continue
        try:
            line = raw.decode("utf-8", errors="ignore").strip()
        except Exception:
            continue

        m = wave_re.search(line)
        if m:
            breath_buf.append(float(m.group(1)))
            heart_buf.append(float(m.group(2)))
            frame_buf.append(int(m.group(3)))
            update_counter += 1

        m2 = radar_re.search(line)
        if m2:
            last_bpm["breath"] = float(m2.group(1))
            last_bpm["heart"] = float(m2.group(2))

        # 每 5 帧刷新一次图表
        if update_counter >= 5 and len(frame_buf) > 1:
            update_counter = 0
            frames = list(frame_buf)
            line_b.set_data(frames, list(breath_buf))
            line_h.set_data(frames, list(heart_buf))

            ax1.set_xlim(frames[0], frames[-1])
            ax2.set_xlim(frames[0], frames[-1])

            if breath_buf:
                margin = max(abs(max(breath_buf)), abs(min(breath_buf)), 0.1) * 1.2
                ax1.set_ylim(-margin, margin)
            if heart_buf:
                margin = max(abs(max(heart_buf)), abs(min(heart_buf)), 0.1) * 1.2
                ax2.set_ylim(-margin, margin)

            bpm_text_b.set_text(f"Breath: {last_bpm['breath']:.1f} BPM")
            bpm_text_h.set_text(f"Heart: {last_bpm['heart']:.1f} BPM")

            fig.canvas.draw_idle()
            fig.canvas.flush_events()

except KeyboardInterrupt:
    print("\nStopped.")
finally:
    ser.close()
    plt.close()
