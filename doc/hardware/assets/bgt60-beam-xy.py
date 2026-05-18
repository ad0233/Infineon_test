"""
BGT60TR13C 正视图：以雷达为观察对象，显示 X 轴和 Y 轴方向上的波束开射角。

数据来源: 数据手册第 12.1 节表 72 (typ 值)
封装: PG-WFWLB-119, L=6.5mm × W=5.0mm

注意: E-plane / H-plane 与物理 X/Y 的对应关系取决于 PCB 上芯片的安装朝向。
本图按 Infineon 标准参考设计约定: 长边水平 = X 轴 = E-plane(宽)，短边垂直 = Y 轴 = H-plane(窄)
"""
import numpy as np
import matplotlib.pyplot as plt
import matplotlib.patches as patches
from matplotlib.patches import Ellipse, FancyBboxPatch

plt.rcParams["font.sans-serif"] = ["Microsoft YaHei", "SimHei", "DejaVu Sans"]
plt.rcParams["axes.unicode_minus"] = False

# 数据手册典型值 (单根天线 typ HPBW)
HPBW_X = 65.0         # 长轴 = E-plane (宽)
HPBW_Y_AVG = 38.0     # 短轴 = H-plane (窄, RX 35° + TX 40°) / 2

# 1m 处覆盖范围 (双倍 tan(half))
def coverage_at_distance(angle_deg, distance_m):
    half = np.radians(angle_deg / 2)
    return 2 * distance_m * np.tan(half) * 100  # cm

cov_x_1m = coverage_at_distance(HPBW_X, 1.0)
cov_y_1m = coverage_at_distance(HPBW_Y_AVG, 1.0)

fig, ax = plt.subplots(figsize=(12, 10))
ax.set_aspect('equal')
ax.set_xlim(-1.6, 1.6)
ax.set_ylim(-1.4, 1.4)
ax.axis('off')

# 标题
ax.text(0, 1.30, "BGT60TR13C 正视波束图",
        ha='center', fontsize=18, fontweight='bold')
ax.text(0, 1.18, "(假设你正面对雷达，雷达正在朝你发射)",
        ha='center', fontsize=10, color='gray', style='italic')

# === 中心椭圆波束 (1m 距离处的截面) ===
# 椭圆半轴 = 1m 处覆盖宽度 / 2 / 总图宽度比例
half_x_m = cov_x_1m / 2 / 100  # 半宽 m
half_y_m = cov_y_1m / 2 / 100  # 半高 m

# 缩放到画布单位 (1m=1单位)
ellipse = Ellipse((0, 0), 2 * half_x_m, 2 * half_y_m,
                  facecolor='#2196F3', alpha=0.25,
                  edgecolor='#1565C0', linewidth=3)
ax.add_patch(ellipse)

# === 中心点 (boresight) ===
ax.plot(0, 0, 'r+', markersize=20, markeredgewidth=3)
ax.text(0.05, 0.05, 'boresight\n(中心轴)', fontsize=8, color='red')

# === X 轴角度标注 ===
# 水平双向箭头
ax.annotate('', xy=(half_x_m, -0.05), xytext=(-half_x_m, -0.05),
            arrowprops=dict(arrowstyle='<->', color='#1565C0', lw=2.5))
ax.text(0, -0.18, f'X 轴 (长边, E-plane)\n{HPBW_X:.0f}°  →  1m 处覆盖 {cov_x_1m:.0f} cm',
        ha='center', fontsize=12, color='#1565C0', fontweight='bold')

# === Y 轴角度标注 ===
# 垂直双向箭头
ax.annotate('', xy=(0.05, half_y_m), xytext=(0.05, -half_y_m),
            arrowprops=dict(arrowstyle='<->', color='#E65100', lw=2.5))
ax.text(0.18, 0, f'Y 轴 (短边, H-plane)\n{HPBW_Y_AVG:.0f}°\n1m 处覆盖 {cov_y_1m:.0f} cm',
        ha='left', va='center', fontsize=12, color='#E65100', fontweight='bold')

# === 雷达芯片示意 (右下角小图) ===
# 缩小的 chip 表示
chip_cx, chip_cy = -1.25, -0.95
chip_w, chip_h = 0.32, 0.25  # 6.5×5.0 比例
chip = FancyBboxPatch((chip_cx - chip_w/2, chip_cy - chip_h/2),
                       chip_w, chip_h,
                       boxstyle="round,pad=0.01",
                       facecolor='#212121', edgecolor='black', linewidth=1.5)
ax.add_patch(chip)
ax.text(chip_cx, chip_cy + 0.005, 'BGT60TR13C', color='white',
        ha='center', va='center', fontsize=7, fontweight='bold')
ax.text(chip_cx, chip_cy - 0.08, '6.5 × 5.0 mm', color='gray',
        ha='center', va='top', fontsize=7)

# 4 个天线小点 (示意位置)
ant_positions = [
    (chip_cx - 0.10, chip_cy + 0.06, 'Tx'),
    (chip_cx + 0.05, chip_cy + 0.06, 'Rx1'),
    (chip_cx + 0.10, chip_cy + 0.06, 'Rx2'),
    (chip_cx + 0.10, chip_cy - 0.04, 'Rx3'),
]
for ax_x, ax_y, label in ant_positions:
    ax.plot(ax_x, ax_y, 'o', color='gold', markersize=4, markeredgecolor='orange')

ax.text(chip_cx, chip_cy - 0.18, '↑ 雷达本体（示意）', fontsize=8,
        ha='center', color='gray', style='italic')

# === 比例参考 ===
# 左侧标尺
scale_x = -1.45
ax.plot([scale_x, scale_x], [-half_y_m, half_y_m], 'k-', lw=1)
ax.plot([scale_x - 0.02, scale_x + 0.02], [half_y_m, half_y_m], 'k-', lw=1)
ax.plot([scale_x - 0.02, scale_x + 0.02], [-half_y_m, -half_y_m], 'k-', lw=1)
ax.text(scale_x - 0.05, 0, f'{cov_y_1m:.0f} cm\n@ 1m',
        rotation=90, ha='right', va='center', fontsize=8, color='dimgray')

# 底部标尺
scale_y = -0.50
ax.plot([-half_x_m, half_x_m], [scale_y, scale_y], 'k-', lw=1)
ax.plot([-half_x_m, -half_x_m], [scale_y - 0.02, scale_y + 0.02], 'k-', lw=1)
ax.plot([half_x_m, half_x_m], [scale_y - 0.02, scale_y + 0.02], 'k-', lw=1)
ax.text(0, scale_y - 0.06, f'{cov_x_1m:.0f} cm @ 1m',
        ha='center', va='top', fontsize=8, color='dimgray')

# === 距离覆盖速查表 ===
table_x = 0.85
table_y = -0.55
ax.text(table_x, table_y + 0.42, '不同距离的覆盖范围',
        ha='center', fontsize=9, fontweight='bold')

table_data = [
    ('距离', 'X 轴 (65°)', 'Y 轴 (38°)'),
    ('0.5 m', f'{coverage_at_distance(HPBW_X, 0.5):.0f} cm',
              f'{coverage_at_distance(HPBW_Y_AVG, 0.5):.0f} cm'),
    ('1.0 m', f'{coverage_at_distance(HPBW_X, 1.0):.0f} cm',
              f'{coverage_at_distance(HPBW_Y_AVG, 1.0):.0f} cm'),
    ('1.5 m', f'{coverage_at_distance(HPBW_X, 1.5):.0f} cm',
              f'{coverage_at_distance(HPBW_Y_AVG, 1.5):.0f} cm'),
    ('2.0 m', f'{coverage_at_distance(HPBW_X, 2.0):.0f} cm',
              f'{coverage_at_distance(HPBW_Y_AVG, 2.0):.0f} cm'),
]
row_h = 0.08
for i, row in enumerate(table_data):
    y = table_y + 0.28 - i * row_h
    fw = 'bold' if i == 0 else 'normal'
    bg = '#E3F2FD' if i == 0 else 'white'
    if i > 0:
        ax.add_patch(patches.Rectangle((table_x - 0.45, y - 0.025), 0.9, row_h,
                                         facecolor=bg, edgecolor='lightgray', lw=0.5))
    ax.text(table_x - 0.35, y, row[0], fontsize=8, ha='left', fontweight=fw)
    ax.text(table_x - 0.05, y, row[1], fontsize=8, ha='left',
            fontweight=fw, color='#1565C0' if i > 0 else 'black')
    ax.text(table_x + 0.20, y, row[2], fontsize=8, ha='left',
            fontweight=fw, color='#E65100' if i > 0 else 'black')

# === 底部说明 ===
fig.text(0.5, 0.04,
         "• 椭圆 = 1m 距离处的半功率 (-3dB) 主瓣截面 (单根天线)\n"
         "• 数值来自数据手册第 12.1 节，假设芯片以『长边=X轴』标准朝向安装",
         ha='center', fontsize=9, color='dimgray')

plt.tight_layout()
out_path = __file__.rsplit('.', 1)[0] + '.png'
plt.savefig(out_path, dpi=140, bbox_inches='tight', facecolor='white')
print(f"saved: {out_path}")
