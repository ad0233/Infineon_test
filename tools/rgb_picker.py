"""
WS2812 RGB 调光控制（串口实时联动）
用法: python rgb_picker.py [COM口]
示例: python rgb_picker.py COM5

功能:
- 滑动条拖动实时发送 `rgb R G B BR` 到 ESP32
- 亮灯开关由 ESP32 端人存检测控制，本工具只改颜色
- 连接后自动按 Enter 进入 CLI 设置模式
"""
import sys
import time
import threading
import tkinter as tk
from tkinter import ttk, messagebox

try:
    import serial
except ImportError:
    print("需要 pyserial：pip install pyserial")
    sys.exit(1)

PORT = sys.argv[1] if len(sys.argv) > 1 else "COM5"
BAUD = 115200


class RGBController:
    def __init__(self, root):
        self.root = root
        root.title(f"RGB 调光 — {PORT}")
        root.resizable(False, False)

        self.r = tk.IntVar(value=255)
        self.g = tk.IntVar(value=255)
        self.b = tk.IntVar(value=255)
        self.brightness = tk.IntVar(value=100)
        self.auto_send = tk.BooleanVar(value=True)

        self.ser = None
        self.send_pending = False
        self.last_sent_time = 0

        main = ttk.Frame(root, padding=12)
        main.grid(row=0, column=0)

        # 连接状态
        conn_frame = ttk.Frame(main)
        conn_frame.grid(row=0, column=0, columnspan=2, sticky="ew", pady=(0, 8))
        self.conn_label = ttk.Label(conn_frame, text=f"● 未连接 {PORT}",
                                     foreground="#F44336", font=("Arial", 10, "bold"))
        self.conn_label.pack(side="left")
        ttk.Button(conn_frame, text="连接", width=8,
                   command=self._connect).pack(side="right", padx=2)
        ttk.Checkbutton(conn_frame, text="实时发送", variable=self.auto_send
                        ).pack(side="right", padx=8)

        # 左侧：预览
        preview_frame = ttk.LabelFrame(main, text="预览", padding=8)
        preview_frame.grid(row=1, column=0, rowspan=2, padx=(0, 12), sticky="ns")
        self.preview = tk.Canvas(preview_frame, width=180, height=180,
                                 highlightthickness=1, highlightbackground="#888")
        self.preview.pack(pady=4)
        self.hex_label = ttk.Label(preview_frame, text="#FFFFFF",
                                    font=("Consolas", 12, "bold"))
        self.hex_label.pack(pady=(8, 2))
        self.rgb_label = ttk.Label(preview_frame, text="R:255 G:255 B:255",
                                    font=("Consolas", 9))
        self.rgb_label.pack()
        self.out_label = ttk.Label(preview_frame, text="输出: 255,255,255",
                                    font=("Consolas", 9), foreground="#666")
        self.out_label.pack(pady=(4, 0))

        # 右侧：滑动条
        slider_frame = ttk.LabelFrame(main, text="RGB 通道 (0-255)", padding=8)
        slider_frame.grid(row=1, column=1, sticky="ew")
        self._build_slider(slider_frame, 0, "R", self.r, "#E53935")
        self._build_slider(slider_frame, 1, "G", self.g, "#43A047")
        self._build_slider(slider_frame, 2, "B", self.b, "#1E88E5")

        bright_frame = ttk.LabelFrame(main, text="亮度 (0-100%)", padding=8)
        bright_frame.grid(row=2, column=1, sticky="ew", pady=(8, 0))
        self._build_slider(bright_frame, 0, "%", self.brightness, "#757575", maxv=100)

        # 预设
        preset_frame = ttk.LabelFrame(main, text="预设颜色", padding=8)
        preset_frame.grid(row=3, column=0, columnspan=2, sticky="ew", pady=(12, 0))
        presets = [
            ("红",   255, 0,   0),
            ("橙",   255, 128, 0),
            ("黄",   255, 255, 0),
            ("绿",   0,   255, 0),
            ("青",   0,   255, 255),
            ("蓝",   0,   0,   255),
            ("紫",   128, 0,   255),
            ("粉",   255, 64,  128),
            ("白",   255, 255, 255),
            ("暖白", 255, 200, 140),
            ("冷白", 200, 220, 255),
            ("关",   0,   0,   0),
        ]
        for i, (name, r, g, b) in enumerate(presets):
            color = f"#{r:02X}{g:02X}{b:02X}"
            tk.Button(
                preset_frame, text=name, width=5,
                bg=color, fg="white" if (r + g + b) < 400 else "black",
                activebackground=color, relief="flat",
                command=lambda r=r, g=g, b=b: self._set_preset(r, g, b)
            ).grid(row=i // 6, column=i % 6, padx=2, pady=2, sticky="ew")

        # 发送状态 / 手动发送
        out_frame = ttk.Frame(main)
        out_frame.grid(row=4, column=0, columnspan=2, sticky="ew", pady=(12, 0))
        self.cmd_var = tk.StringVar(value="rgb 255 255 255 100")
        ttk.Entry(out_frame, textvariable=self.cmd_var,
                  font=("Consolas", 9)
                  ).grid(row=0, column=0, sticky="ew", padx=(0, 4))
        out_frame.columnconfigure(0, weight=1)
        ttk.Button(out_frame, text="发送", width=8,
                   command=self._send_now).grid(row=0, column=1)

        self.status_var = tk.StringVar(value="就绪")
        ttk.Label(main, textvariable=self.status_var, font=("Arial", 9),
                  foreground="#666"
                  ).grid(row=5, column=0, columnspan=2, sticky="w", pady=(8, 0))

        # 串口日志区
        log_frame = ttk.LabelFrame(main, text="串口日志 (TX=蓝 / RX=绿)", padding=4)
        log_frame.grid(row=6, column=0, columnspan=2, sticky="ew", pady=(8, 0))
        btn_bar = ttk.Frame(log_frame)
        btn_bar.pack(fill="x", pady=(0, 2))
        ttk.Button(btn_bar, text="清空", width=6,
                   command=self._clear_log).pack(side="left")
        ttk.Button(btn_bar, text="测试 rgb 255 0 0 100", width=22,
                   command=lambda: self._send("rgb 255 0 0 100")
                   ).pack(side="left", padx=4)
        self.log = tk.Text(log_frame, height=10, width=70,
                          font=("Consolas", 9), bg="#1E1E1E", fg="#DDD")
        self.log.pack(fill="both", expand=True)
        self.log.tag_config("tx", foreground="#64B5F6")
        self.log.tag_config("rx", foreground="#81C784")
        self.log.tag_config("sys", foreground="#FFB74D")

        self._refresh()

        # 自动连接
        root.after(100, self._connect)
        # 节流发送（10Hz）
        root.after(100, self._tick)
        # 串口接收轮询
        root.after(200, self._rx_poll)

    def _build_slider(self, parent, row, label, var, color, maxv=255):
        ttk.Label(parent, text=label, width=2, foreground=color,
                  font=("Arial", 11, "bold")
                  ).grid(row=row, column=0, padx=4)
        tk.Scale(parent, from_=0, to=maxv, orient="horizontal",
                 variable=var, length=280, showvalue=False,
                 troughcolor="#EEE", bg="white", activebackground=color,
                 highlightthickness=0,
                 command=lambda _e: self._on_change()
                 ).grid(row=row, column=1, padx=4, pady=2)
        ttk.Label(parent, textvariable=var, width=4,
                  font=("Consolas", 10)
                  ).grid(row=row, column=2, padx=4)

    def _on_change(self):
        self._refresh()
        self.send_pending = True

    def _set_preset(self, r, g, b):
        self.r.set(r); self.g.set(g); self.b.set(b)
        self._refresh()
        self.send_pending = True

    def _refresh(self):
        r = self.r.get(); g = self.g.get(); b = self.b.get(); br = self.brightness.get()
        ro = int(r * br / 100); go = int(g * br / 100); bo = int(b * br / 100)
        color = f"#{ro:02X}{go:02X}{bo:02X}"
        self.preview.configure(bg=color)
        self.hex_label.configure(text=f"#{r:02X}{g:02X}{b:02X}")
        self.rgb_label.configure(text=f"R:{r}  G:{g}  B:{b}")
        self.out_label.configure(text=f"输出: {ro},{go},{bo}")
        self.cmd_var.set(f"rgb {r} {g} {b} {br}")

    def _connect(self):
        if self.ser and self.ser.is_open:
            return
        try:
            self.ser = serial.Serial(PORT, BAUD, timeout=0.2)
            time.sleep(0.2)
            self.ser.reset_input_buffer()
            self.conn_label.configure(text=f"● 已连接 {PORT}", foreground="#4CAF50")
            self.status_var.set("已连接，可直接发送 rgb 命令")
        except Exception as e:
            messagebox.showerror("连接失败", f"{e}")
            self.conn_label.configure(text=f"● 连接失败 {PORT}", foreground="#F44336")

    def _send(self, cmd):
        if not self.ser or not self.ser.is_open:
            self.status_var.set(f"未连接，无法发送")
            self._log("未连接", "sys")
            return
        try:
            data = (cmd + "\n").encode("ascii")
            self.ser.write(data)
            self.ser.flush()
            self.status_var.set(f"→ {cmd}")
            self._log(f"TX: {cmd} ({len(data)}B)", "tx")
        except Exception as e:
            self.status_var.set(f"发送失败: {e}")
            self._log(f"TX ERR: {e}", "sys")

    def _log(self, msg, tag="sys"):
        if hasattr(self, "log"):
            self.log.insert("end", msg + "\n", tag)
            self.log.see("end")
            # 限制日志行数
            lines = int(self.log.index("end-1c").split(".")[0])
            if lines > 500:
                self.log.delete("1.0", "100.end")

    def _clear_log(self):
        self.log.delete("1.0", "end")

    def _rx_poll(self):
        """读取串口回显数据"""
        if self.ser and self.ser.is_open:
            try:
                n = self.ser.in_waiting
                if n > 0:
                    data = self.ser.read(n)
                    text = data.decode("utf-8", errors="ignore").strip()
                    if text:
                        # 只保留 rgb 相关日志，过滤 radar/温湿度刷屏
                        for line in text.splitlines():
                            line = line.strip()
                            if not line:
                                continue
                            if ("my_rgb" in line or "rgb set" in line or
                                    "USB-JTAG" in line or "rgb " in line.lower()):
                                self._log(f"RX: {line[:100]}", "rx")
            except Exception as e:
                pass
        self.root.after(100, self._rx_poll)

    def _send_now(self):
        self._send(self.cmd_var.get())

    def _tick(self):
        """节流：最快 10Hz（100ms）发送一次"""
        now = time.time() * 1000
        if (self.auto_send.get() and self.send_pending and
                (now - self.last_sent_time) >= 100):
            self._send(self.cmd_var.get())
            self.send_pending = False
            self.last_sent_time = now
        self.root.after(50, self._tick)


if __name__ == "__main__":
    root = tk.Tk()
    RGBController(root)
    root.mainloop()
