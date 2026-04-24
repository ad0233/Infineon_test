"""
雷达综合控制面板 — 数据监控 + RGB 调光
用法: python radar_panel.py [COM口] [波特率]
示例: python radar_panel.py COM5 115200
"""
import sys
import re
import csv
import os
import time
import datetime
import tkinter as tk
from tkinter import ttk, filedialog
from collections import deque

try:
    import serial
except ImportError:
    print("需要 pyserial: pip install pyserial"); sys.exit(1)

try:
    import matplotlib
    matplotlib.use("TkAgg")
    # 中文字体
    matplotlib.rcParams["font.sans-serif"] = [
        "Microsoft YaHei", "SimHei", "Arial Unicode MS", "DejaVu Sans"
    ]
    matplotlib.rcParams["axes.unicode_minus"] = False
    from matplotlib.figure import Figure
    from matplotlib.backends.backend_tkagg import FigureCanvasTkAgg
except ImportError:
    print("需要 matplotlib: pip install matplotlib"); sys.exit(1)

PORT = sys.argv[1] if len(sys.argv) > 1 else "COM5"
BAUD = int(sys.argv[2]) if len(sys.argv) > 2 else 115200
WINDOW = 120  # 曲线保留最近 120 秒

# ---- 串口正则 ----
radar_re = re.compile(
    r"Radar: detected=(\w+) confidence=([\d.]+) distance=([\d.]+)cm "
    r"breath=([\d.]+)bpm heart=([\d.]+)bpm "
    r"rbm=(\w+) bodyMov=(\d+)%"
    r"(?: T=([-\d.]+) RH=([\d.]+) lux=([\d.]+))?"
)
wave_re = re.compile(r"Wave: breath=([-\d.]+) heart=([-\d.]+)")
aht_re = re.compile(r"温湿度 T=([-\d.]+)\S* RH=([\d.]+)")
veml_re = re.compile(r"VEML7700 环境光 ([\d.]+) lx")

WAVE_WINDOW = 200   # 波形显示最近 200 帧 (~20s @ 10Hz)


class RadarPanel:
    def __init__(self, root):
        self.root = root
        root.title(f"雷达综合控制面板 — {PORT}")
        root.geometry("1300x820")

        # ---- 状态变量 ----
        self.state = {
            "detected": "no", "confidence": 0.0, "distance_cm": 0.0,
            "breath_bpm": 0.0, "heart_bpm": 0.0,
            "rbm": "N", "body_move": 0,
            "temp": 0.0, "rh": 0.0, "lux": 0.0,
        }
        self.t0 = None
        self.wave_idx = 0
        self.wave_x = deque(maxlen=WAVE_WINDOW)
        self.wave_breath = deque(maxlen=WAVE_WINDOW)
        self.wave_heart = deque(maxlen=WAVE_WINDOW)
        # 体动曲线：1Hz 采样，保留最近 300 秒（5 分钟）
        self.body_t = deque(maxlen=300)
        self.body_v = deque(maxlen=300)

        # RGB 状态
        self.r = tk.IntVar(value=255)
        self.g = tk.IntVar(value=255)
        self.b = tk.IntVar(value=255)
        self.brightness = tk.IntVar(value=100)
        self.auto_send = tk.BooleanVar(value=True)

        # 串口
        self.ser = None
        self.send_pending = False
        self.last_sent_time = 0

        # CSV 录制
        self.csv_file = None
        self.csv_writer = None
        self.csv_path = None
        self.csv_row_count = 0

        # Wave 高频 (10Hz) 波形录制，与主 CSV 同步启停，独立文件
        self.wave_csv_file = None
        self.wave_csv_writer = None
        self.wave_csv_path = None
        self.wave_row_count = 0

        # ---- 布局 ----
        self._build_status_bar()
        body = ttk.Frame(root)
        body.pack(fill="both", expand=True, padx=8)
        body.columnconfigure(0, weight=3)
        body.columnconfigure(1, weight=1)
        body.rowconfigure(0, weight=1)

        self._build_chart(body)
        self._build_rgb_panel(body)
        self._build_log(root)

        self._refresh_rgb()
        root.after(100, self._connect)
        root.after(100, self._rx_poll)
        root.after(100, self._tx_tick)
        root.after(500, self._chart_refresh)

    # ---------- 状态栏 ----------
    def _build_status_bar(self):
        top = ttk.Frame(self.root, padding=4)
        top.pack(fill="x")
        self.conn_label = ttk.Label(top, text=f"● 未连接 {PORT}",
                                     foreground="#F44336", font=("Arial", 10, "bold"))
        self.conn_label.pack(side="left", padx=(0, 8))
        ttk.Button(top, text="连接", width=6, command=self._connect).pack(side="left")

        # 录制按钮
        self.rec_btn = tk.Button(top, text="● 开始录制", width=10,
                                  bg="#F44336", fg="white", relief="flat",
                                  activebackground="#D32F2F",
                                  command=self._toggle_recording)
        self.rec_btn.pack(side="left", padx=(12, 4))
        self.rec_info = ttk.Label(top, text="未录制", font=("Consolas", 9),
                                   foreground="#666")
        self.rec_info.pack(side="left")

        # 睡眠分析按钮
        self.analyze_btn = tk.Button(top, text="分析睡眠", width=10,
                                      bg="#2196F3", fg="white", relief="flat",
                                      activebackground="#1976D2",
                                      command=self._analyze_sleep)
        self.analyze_btn.pack(side="left", padx=(8, 4))

        self.status_var = tk.StringVar(value="等待数据...")
        ttk.Label(top, textvariable=self.status_var, font=("Arial", 10),
                  foreground="#333"
                  ).pack(side="left", padx=12)

    # ---------- 曲线区 ----------
    def _build_chart(self, parent):
        frame = ttk.LabelFrame(parent, text="波形", padding=4)
        frame.grid(row=0, column=0, sticky="nsew", padx=(0, 4))

        self.fig = Figure(figsize=(8, 7), dpi=90)
        self.fig.subplots_adjust(hspace=0.45, top=0.95, bottom=0.06,
                                  left=0.08, right=0.96)
        self.ax_breath = self.fig.add_subplot(3, 1, 1)
        self.line_breath, = self.ax_breath.plot([], [], "#2196F3", linewidth=1.5)
        self.ax_breath.set_title("呼吸波形", fontsize=11, loc="left")
        self.ax_breath.set_ylabel("相位")
        self.ax_breath.grid(True, alpha=0.2)

        self.ax_heart = self.fig.add_subplot(3, 1, 2)
        self.line_heart, = self.ax_heart.plot([], [], "#F44336", linewidth=1.5)
        self.ax_heart.set_title("心率波形", fontsize=11, loc="left")
        self.ax_heart.set_ylabel("相位")
        self.ax_heart.grid(True, alpha=0.2)

        self.ax_body = self.fig.add_subplot(3, 1, 3)
        self.line_body, = self.ax_body.plot([], [], "#FF9800", linewidth=1.5)
        self.ax_body.fill_between([], [], 0, color="#FF9800", alpha=0.25)
        self.ax_body.set_title("体动强度", fontsize=11, loc="left")
        self.ax_body.set_ylabel("%")
        self.ax_body.set_xlabel("时间 (秒)")
        self.ax_body.set_ylim(0, 110)
        self.ax_body.grid(True, alpha=0.2)

        canvas = FigureCanvasTkAgg(self.fig, master=frame)
        canvas.draw()
        canvas.get_tk_widget().pack(fill="both", expand=True)
        self.canvas = canvas

    # ---------- RGB 控制 ----------
    def _build_rgb_panel(self, parent):
        frame = ttk.LabelFrame(parent, text="RGB 调光", padding=8)
        frame.grid(row=0, column=1, sticky="nsew")

        preview_box = ttk.Frame(frame)
        preview_box.pack(fill="x", pady=(0, 8))
        self.preview = tk.Canvas(preview_box, width=260, height=90,
                                 highlightthickness=1, highlightbackground="#888")
        self.preview.pack()
        self.hex_label = ttk.Label(preview_box, text="#FFFFFF",
                                    font=("Consolas", 11, "bold"))
        self.hex_label.pack(pady=(4, 0))
        self.out_label = ttk.Label(preview_box, text="输出: 255,255,255",
                                    font=("Consolas", 8), foreground="#666")
        self.out_label.pack()

        ttk.Checkbutton(frame, text="实时发送到设备", variable=self.auto_send
                        ).pack(anchor="w", pady=(0, 4))

        slider_frame = ttk.LabelFrame(frame, text="RGB", padding=4)
        slider_frame.pack(fill="x", pady=2)
        self._build_slider(slider_frame, 0, "R", self.r, "#E53935")
        self._build_slider(slider_frame, 1, "G", self.g, "#43A047")
        self._build_slider(slider_frame, 2, "B", self.b, "#1E88E5")

        bright_frame = ttk.LabelFrame(frame, text="亮度 %", padding=4)
        bright_frame.pack(fill="x", pady=2)
        self._build_slider(bright_frame, 0, "%", self.brightness, "#757575", maxv=100)

        preset_frame = ttk.LabelFrame(frame, text="预设", padding=4)
        preset_frame.pack(fill="x", pady=2)
        presets = [
            ("红",   255, 0,   0),  ("橙",   255, 128, 0),
            ("黄",   255, 255, 0),  ("绿",   0,   255, 0),
            ("青",   0,   255, 255),("蓝",   0,   0,   255),
            ("紫",   128, 0,   255),("粉",   255, 64,  128),
            ("白",   255, 255, 255),("暖白", 255, 200, 140),
            ("冷白", 200, 220, 255),("关",   0,   0,   0),
        ]
        for i, (name, rr, gg, bb) in enumerate(presets):
            color = f"#{rr:02X}{gg:02X}{bb:02X}"
            tk.Button(
                preset_frame, text=name, width=4,
                bg=color, fg="white" if (rr + gg + bb) < 400 else "black",
                activebackground=color, relief="flat",
                command=lambda r=rr, g=gg, b=bb: self._set_preset(r, g, b)
            ).grid(row=i // 4, column=i % 4, padx=1, pady=1, sticky="ew")
        for c in range(4):
            preset_frame.columnconfigure(c, weight=1)

        cmd_frame = ttk.Frame(frame)
        cmd_frame.pack(fill="x", pady=(8, 0))
        self.cmd_var = tk.StringVar(value="rgb 255 255 255 100")
        ttk.Entry(cmd_frame, textvariable=self.cmd_var,
                  font=("Consolas", 9)).pack(side="left", fill="x", expand=True)
        ttk.Button(cmd_frame, text="发送", width=6,
                   command=self._send_now).pack(side="right", padx=(4, 0))

    def _build_slider(self, parent, row, label, var, color, maxv=255):
        ttk.Label(parent, text=label, width=2, foreground=color,
                  font=("Arial", 10, "bold")
                  ).grid(row=row, column=0, padx=2)
        tk.Scale(parent, from_=0, to=maxv, orient="horizontal",
                 variable=var, length=180, showvalue=False,
                 troughcolor="#EEE", bg="white", activebackground=color,
                 highlightthickness=0,
                 command=lambda _e: self._on_rgb_change()
                 ).grid(row=row, column=1, padx=2, pady=1, sticky="ew")
        ttk.Label(parent, textvariable=var, width=4,
                  font=("Consolas", 9)).grid(row=row, column=2, padx=2)
        parent.columnconfigure(1, weight=1)

    # ---------- 日志 ----------
    def _build_log(self, parent):
        frame = ttk.LabelFrame(parent, text="串口日志", padding=2)
        frame.pack(fill="x", padx=8, pady=(0, 4))
        bar = ttk.Frame(frame)
        bar.pack(fill="x")
        ttk.Button(bar, text="清空", width=6,
                   command=lambda: self.log.delete("1.0", "end")).pack(side="left")
        self.filter_rgb = tk.BooleanVar(value=False)
        ttk.Checkbutton(bar, text="只显示 RGB 相关", variable=self.filter_rgb
                        ).pack(side="left", padx=8)

        self.log = tk.Text(frame, height=8, font=("Consolas", 8),
                           bg="#1E1E1E", fg="#DDD")
        self.log.pack(fill="both", expand=True)
        self.log.tag_config("tx", foreground="#64B5F6")
        self.log.tag_config("rx", foreground="#81C784")
        self.log.tag_config("sys", foreground="#FFB74D")

    def _log(self, msg, tag="sys"):
        self.log.insert("end", msg + "\n", tag)
        self.log.see("end")
        lines = int(self.log.index("end-1c").split(".")[0])
        if lines > 500:
            self.log.delete("1.0", "100.end")

    # ---------- RGB 处理 ----------
    def _on_rgb_change(self):
        self._refresh_rgb()
        self.send_pending = True

    def _set_preset(self, r, g, b):
        self.r.set(r); self.g.set(g); self.b.set(b)
        self._refresh_rgb()
        self.send_pending = True

    def _refresh_rgb(self):
        r = self.r.get(); g = self.g.get(); b = self.b.get(); br = self.brightness.get()
        ro = int(r * br / 100); go = int(g * br / 100); bo = int(b * br / 100)
        color = f"#{ro:02X}{go:02X}{bo:02X}"
        self.preview.configure(bg=color)
        self.hex_label.configure(text=f"#{r:02X}{g:02X}{b:02X}")
        self.out_label.configure(text=f"输出: {ro},{go},{bo}")
        self.cmd_var.set(f"rgb {r} {g} {b} {br}")

    def _send_now(self):
        self._send(self.cmd_var.get())

    def _tx_tick(self):
        """节流发送，最快 10Hz"""
        now = time.time() * 1000
        if (self.auto_send.get() and self.send_pending and
                (now - self.last_sent_time) >= 100):
            self._send(self.cmd_var.get())
            self.send_pending = False
            self.last_sent_time = now
        self.root.after(50, self._tx_tick)

    def _send(self, cmd):
        if not self.ser or not self.ser.is_open:
            self._log("未连接", "sys"); return
        try:
            self.ser.write((cmd + "\n").encode("ascii"))
            self.ser.flush()
            self._log(f"TX: {cmd}", "tx")
        except Exception as e:
            self._log(f"TX ERR: {e}", "sys")

    # ---------- 串口接收 ----------
    def _connect(self):
        if self.ser and self.ser.is_open: return
        try:
            # 打开前关掉 DTR/RTS，避免触发 ESP 板子的自动复位 / 进 bootloader
            ser = serial.Serial()
            ser.port = PORT
            ser.baudrate = BAUD
            ser.timeout = 0.1
            ser.dtr = False
            ser.rts = False
            ser.open()
            self.ser = ser
            time.sleep(0.2)
            self.ser.reset_input_buffer()
            self.conn_label.configure(text=f"● 已连接 {PORT}", foreground="#4CAF50")
            self._log(f"Connected {PORT} @ {BAUD} (DTR/RTS off)", "sys")
        except Exception as e:
            self._log(f"连接失败: {e}", "sys")
            self.conn_label.configure(text=f"● 连接失败 {PORT}", foreground="#F44336")
            self.root.after(2000, self._connect)

    def _now_t(self):
        if self.t0 is None: self.t0 = time.time()
        return time.time() - self.t0

    def _rx_poll(self):
        """定时读串口，解析多种数据"""
        if self.ser and self.ser.is_open:
            try:
                n = self.ser.in_waiting
                if n > 0:
                    data = self.ser.read(n).decode("utf-8", errors="ignore")
                    for line in data.splitlines():
                        line = line.strip()
                        if not line: continue
                        self._parse_line(line)
            except Exception as e:
                # 不再静默吞异常：让问题可见
                self._log(f"串口读错误: {e}", "sys")
        self.root.after(50, self._rx_poll)

    # ---------- CSV 录制 ----------
    def _toggle_recording(self):
        if self.csv_file is None:
            self._start_recording()
        else:
            self._stop_recording()

    def _start_recording(self):
        # 默认路径: tools/records/radar_YYYYMMDD_HHMMSS.csv
        default_dir = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                    "records")
        os.makedirs(default_dir, exist_ok=True)
        default_name = "radar_" + datetime.datetime.now().strftime("%Y%m%d_%H%M%S") + ".csv"
        path = filedialog.asksaveasfilename(
            defaultextension=".csv",
            filetypes=[("CSV 文件", "*.csv"), ("所有文件", "*.*")],
            initialdir=default_dir,
            initialfile=default_name,
            title="选择录制保存位置"
        )
        if not path:
            return
        try:
            self.csv_file = open(path, "w", newline="", encoding="utf-8-sig")
            self.csv_writer = csv.writer(self.csv_file)
            self.csv_writer.writerow([
                "时间", "帧号",
                "有人", "置信度", "距离_cm",
                "呼吸_BPM", "心率_BPM",
                "体动", "体动强度_%",
                "温度_℃", "湿度_%", "环境光_lux"
            ])
            self.csv_file.flush()
            self.csv_path = path
            self.csv_row_count = 0

            # 同步打开波形 CSV（10Hz 原始相位 BPF 输出）
            if path.lower().endswith(".csv"):
                wave_path = path[:-4] + "_wave.csv"
            else:
                wave_path = path + "_wave.csv"
            self.wave_csv_file = open(wave_path, "w", newline="", encoding="utf-8-sig")
            self.wave_csv_writer = csv.writer(self.wave_csv_file)
            self.wave_csv_writer.writerow(["时间", "帧号", "呼吸_相位", "心率_相位"])
            self.wave_csv_file.flush()
            self.wave_csv_path = wave_path
            self.wave_row_count = 0

            self.rec_btn.configure(text="■ 停止录制", bg="#4CAF50",
                                    activebackground="#388E3C")
            self.rec_info.configure(text=f"录制中: {os.path.basename(path)}",
                                     foreground="#4CAF50")
            self._log(f"开始录制: {path}", "sys")
            self._log(f"  + 波形: {os.path.basename(wave_path)}", "sys")
        except Exception as e:
            self._log(f"录制失败: {e}", "sys")
            self.csv_file = None
            self.wave_csv_file = None

    def _stop_recording(self):
        if self.csv_file:
            try:
                self.csv_file.close()
            except Exception:
                pass
            path = self.csv_path
            rows = self.csv_row_count
            self.csv_file = None
            self.csv_writer = None
            self.csv_path = None
            # 同时关闭波形文件
            wave_rows = self.wave_row_count
            wave_path = self.wave_csv_path
            if self.wave_csv_file:
                try:
                    self.wave_csv_file.close()
                except Exception:
                    pass
            self.wave_csv_file = None
            self.wave_csv_writer = None
            self.wave_csv_path = None
            self.rec_btn.configure(text="● 开始录制", bg="#F44336",
                                    activebackground="#D32F2F")
            self.rec_info.configure(text=f"已保存 {rows} 行 + 波形 {wave_rows} 帧",
                                     foreground="#666")
            self._log(f"停止录制，共 {rows} 行 → {path}", "sys")
            if wave_path:
                self._log(f"  + 波形 {wave_rows} 帧 → {wave_path}", "sys")

    # ---------- 睡眠分析 ----------
    def _analyze_sleep(self):
        """选一个已录制的 CSV，后台跑 sleep_analyze.py 产出分期报告。"""
        here = os.path.dirname(os.path.abspath(__file__))
        default_dir = os.path.join(here, "records")
        if not os.path.isdir(default_dir):
            default_dir = here

        initial_dir = default_dir
        initial_name = None
        if self.csv_path and os.path.isfile(self.csv_path):
            initial_dir = os.path.dirname(self.csv_path)
            initial_name = os.path.basename(self.csv_path)

        path = filedialog.askopenfilename(
            initialdir=initial_dir,
            initialfile=initial_name,
            filetypes=[("CSV 文件", "*.csv"), ("所有文件", "*.*")],
            title="选择要分析的睡眠录制 CSV",
        )
        if not path:
            return

        analyze_script = os.path.join(here, "sleep_analyze.py")
        if not os.path.isfile(analyze_script):
            self._log(f"找不到 sleep_analyze.py: {analyze_script}", "sys")
            return

        self.analyze_btn.configure(state="disabled", text="分析中...")
        self._log(f"开始分析: {path}", "sys")

        import threading
        import subprocess

        def worker():
            try:
                proc = subprocess.run(
                    [sys.executable, analyze_script, path],
                    capture_output=True, text=True,
                    encoding="utf-8", errors="replace",
                )
                output = (proc.stdout or "") + (proc.stderr or "")
                self.root.after(
                    0, lambda: self._analyze_done(proc.returncode, output, path)
                )
            except Exception as e:
                err = f"[EXC] {e}"
                self.root.after(0, lambda: self._analyze_done(-1, err, path))

        threading.Thread(target=worker, daemon=True).start()

    def _analyze_done(self, rc, output, csv_path):
        self.analyze_btn.configure(state="normal", text="分析睡眠")
        tail = "\n".join(output.splitlines()[-30:]) if output else ""
        if tail:
            self._log(tail, "sys")
        if rc != 0:
            self._log(f"分析失败 rc={rc}", "sys")
            return
        stem = os.path.splitext(os.path.basename(csv_path))[0]
        out_dir = os.path.join(os.path.dirname(csv_path), stem)
        png_path = os.path.join(out_dir, "hypnogram.png")
        if os.path.isfile(png_path):
            try:
                os.startfile(png_path)
                self._log(f"结果: {out_dir}", "sys")
            except Exception as e:
                self._log(f"无法打开 PNG: {e}", "sys")
        else:
            self._log(f"未找到 {png_path}", "sys")

    def _record_row(self, frame_num):
        if not self.csv_writer:
            return
        try:
            s = self.state
            self.csv_writer.writerow([
                datetime.datetime.now().strftime("%Y-%m-%d %H:%M:%S"),
                frame_num,
                1 if s["detected"] == "yes" else 0,
                f"{s['confidence']:.2f}",
                f"{s['distance_cm']:.1f}",
                f"{s['breath_bpm']:.1f}",
                f"{s['heart_bpm']:.1f}",
                1 if s["rbm"] == "Y" else 0,
                s["body_move"],
                f"{s['temp']:.1f}",
                f"{s['rh']:.1f}",
                f"{s['lux']:.1f}",
            ])
            self.csv_file.flush()
            self.csv_row_count += 1
            # 每 60 行更新一次显示（避免频繁刷新）
            if self.csv_row_count % 60 == 0:
                self.rec_info.configure(
                    text=f"录制中: {self.csv_row_count} 行"
                )
        except Exception as e:
            self._log(f"写入失败: {e}", "sys")

    def _record_wave_row(self, breath, heart, frame_idx):
        """10Hz 原始相位 BPF 输出写入独立 CSV。"""
        if not self.wave_csv_writer:
            return
        try:
            self.wave_csv_writer.writerow([
                datetime.datetime.now().strftime("%Y-%m-%d %H:%M:%S.%f")[:-3],
                frame_idx,
                f"{breath:.4f}",
                f"{heart:.4f}",
            ])
            self.wave_row_count += 1
            # 不每行 flush（性能考虑），每 100 行 flush 一次
            if self.wave_row_count % 100 == 0:
                self.wave_csv_file.flush()
        except Exception:
            pass

    def _parse_line(self, line):
        m = wave_re.search(line)
        if m:
            b = float(m.group(1))
            h = float(m.group(2))
            self.wave_breath.append(b)
            self.wave_heart.append(h)
            self.wave_x.append(self.wave_idx / 10.0)  # 10Hz → 秒
            self.wave_idx += 1
            self._record_wave_row(b, h, self.wave_idx)
            return

        m = radar_re.search(line)
        if m:
            self.state["detected"]    = m.group(1)
            self.state["confidence"]  = float(m.group(2))
            self.state["distance_cm"] = float(m.group(3))
            self.state["breath_bpm"]  = float(m.group(4))
            self.state["heart_bpm"]   = float(m.group(5))
            self.state["rbm"]         = m.group(6)
            self.state["body_move"]   = int(m.group(7))
            # 可选环境字段（Radar 行尾的 T/RH/lux），无则保留旧值
            if m.group(8) is not None:
                self.state["temp"] = float(m.group(8))
                self.state["rh"]   = float(m.group(9))
                self.state["lux"]  = float(m.group(10))
            # 推入体动曲线（1Hz）
            self.body_t.append(self._now_t())
            self.body_v.append(self.state["body_move"])
            self._update_status()
            # 每次 Radar 行（1Hz）写一条 CSV
            self._record_row(self.wave_idx)
            if not self.filter_rgb.get():
                self._log(f"RX: {line[:100]}", "rx")
            return

        m = aht_re.search(line)
        if m:
            self.state["temp"] = float(m.group(1))
            self.state["rh"]   = float(m.group(2))
            self._update_status()
            if not self.filter_rgb.get():
                self._log(f"RX: {line[:100]}", "rx")
            return

        m = veml_re.search(line)
        if m:
            self.state["lux"] = float(m.group(1))
            self._update_status()
            if not self.filter_rgb.get():
                self._log(f"RX: {line[:100]}", "rx")
            return

        # 只显示 RGB 相关日志（如果过滤器开启）
        if "my_rgb" in line or "rgb " in line.lower():
            self._log(f"RX: {line[:100]}", "rx")

    def _update_status(self):
        s = self.state
        det_icon = "●" if s["detected"] == "yes" else "○"
        rbm_icon = "⚡" if s["rbm"] == "Y" else "·"
        self.status_var.set(
            f"{det_icon} {'有人' if s['detected']=='yes' else '无人'} | "
            f"{s['distance_cm']:.0f}cm | "
            f"{s['breath_bpm']:.1f}bpm ♡{s['heart_bpm']:.1f}bpm | "
            f"{rbm_icon} 体动 {s['body_move']}% | "
            f"{s['temp']:.1f}℃ {s['rh']:.0f}%RH | "
            f"{s['lux']:.0f} lx"
        )

    def _chart_refresh(self):
        redraw = False
        if len(self.wave_x) >= 2:
            xs = list(self.wave_x)
            bs = list(self.wave_breath)
            hs = list(self.wave_heart)

            self.line_breath.set_data(xs, bs)
            self.line_heart.set_data(xs, hs)

            xlim = (xs[0], xs[-1] + 0.1)
            self.ax_breath.set_xlim(*xlim)
            self.ax_heart.set_xlim(*xlim)

            # Y 轴仅按最近 5 秒（50 帧 @ 10Hz）自动缩放
            # 避免几分钟前的大幅体动压扁当前的弱小波形
            recent_n = 50
            bs_recent = bs[-recent_n:] if len(bs) > recent_n else bs
            hs_recent = hs[-recent_n:] if len(hs) > recent_n else hs
            if bs_recent:
                m = max(abs(max(bs_recent)), abs(min(bs_recent)), 0.001) * 1.3
                self.ax_breath.set_ylim(-m, m)
            if hs_recent:
                m = max(abs(max(hs_recent)), abs(min(hs_recent)), 0.001) * 1.3
                self.ax_heart.set_ylim(-m, m)

            self.ax_breath.set_title(
                f"呼吸波形  {self.state['breath_bpm']:.1f} BPM",
                fontsize=11, loc="left")
            self.ax_heart.set_title(
                f"心率波形  {self.state['heart_bpm']:.1f} BPM",
                fontsize=11, loc="left")
            redraw = True

        if len(self.body_t) >= 2:
            bt = list(self.body_t)
            bv = list(self.body_v)
            self.line_body.set_data(bt, bv)
            # 清除旧填充后重绘（matplotlib 3.x ArtistList 无 clear，逐个 remove）
            for coll in list(self.ax_body.collections):
                coll.remove()
            self.ax_body.fill_between(bt, bv, 0, color="#FF9800", alpha=0.25)
            # 窗口: 最近 300 秒
            xlim_body = (max(0, bt[-1] - 300), bt[-1] + 1)
            self.ax_body.set_xlim(*xlim_body)
            self.ax_body.set_title(
                f"体动强度  当前 {self.state['body_move']}%",
                fontsize=11, loc="left")
            redraw = True

        if redraw:
            self.canvas.draw_idle()
        self.root.after(200, self._chart_refresh)


if __name__ == "__main__":
    root = tk.Tk()
    try:
        from tkinter import font as tkfont
        default = tkfont.nametofont("TkDefaultFont")
        default.configure(family="Microsoft YaHei", size=9)
    except Exception:
        pass
    panel = RadarPanel(root)

    def on_close():
        try:
            panel._stop_recording()
        except Exception:
            pass
        root.destroy()

    root.protocol("WM_DELETE_WINDOW", on_close)
    root.mainloop()
