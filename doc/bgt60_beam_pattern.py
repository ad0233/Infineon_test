"""
BGT60TR13C 天线波束方向图可视化
数据来源: 数据手册第 12.1 节表 72（单根天线 HPBW typ 值）
"""
import numpy as np
import matplotlib.pyplot as plt
import matplotlib.patches as patches
from matplotlib.patches import Wedge

# 中文字体
plt.rcParams["font.sans-serif"] = ["Microsoft YaHei", "SimHei", "DejaVu Sans"]
plt.rcParams["axes.unicode_minus"] = False

# 数据手册典型值
HPBW_E = 65.0   # E-plane (宽) typ
HPBW_H_RX = 35.0  # H-plane RX typ
HPBW_H_TX = 40.0  # H-plane TX typ
HPBW_H_AVG = (HPBW_H_RX + HPBW_H_TX) / 2  # 平均

fig, axes = plt.subplots(1, 2, figsize=(14, 7))
fig.suptitle("BGT60TR13C 天线波束方向图 (数据手册 12.1 节 typ 值)",
             fontsize=14, fontweight='bold')

def draw_beam(ax, angle_deg, title, color):
    """画扇形波束图，带角度标注"""
    ax.set_xlim(-1.2, 1.2)
    ax.set_ylim(-1.0, 1.0)
    ax.set_aspect('equal')
    ax.axis('off')

    # 芯片位置（左侧中心）
    chip_x, chip_y = -1.0, 0.0

    # 主瓣扇形（半功率波束）
    half_angle = angle_deg / 2
    radius = 2.0
    wedge = Wedge((chip_x, chip_y), radius, -half_angle, half_angle,
                  facecolor=color, alpha=0.3, edgecolor=color, linewidth=2)
    ax.add_patch(wedge)

    # 边界线（虚线）
    for sign in [-1, 1]:
        rad = np.radians(sign * half_angle)
        ax.plot([chip_x, chip_x + radius * np.cos(rad)],
                [chip_y, chip_y + radius * np.sin(rad)],
                color=color, linestyle='--', linewidth=1.5, alpha=0.7)

    # 中心轴 (boresight)
    ax.plot([chip_x, chip_x + radius], [chip_y, chip_y],
            color='red', linewidth=2, alpha=0.8, label='boresight')
    ax.annotate('', xy=(chip_x + radius * 0.95, 0), xytext=(chip_x + 0.5, 0),
                arrowprops=dict(arrowstyle='->', color='red', lw=2))

    # 芯片标记
    chip_box = patches.FancyBboxPatch((chip_x - 0.15, chip_y - 0.08), 0.3, 0.16,
                                       boxstyle="round,pad=0.02",
                                       facecolor='black', edgecolor='black')
    ax.add_patch(chip_box)
    ax.text(chip_x, chip_y, 'BGT60TR13C', color='white',
            ha='center', va='center', fontsize=8, fontweight='bold')

    # 角度标注（弧形）
    arc_radius = 0.6
    arc = patches.Arc((chip_x, chip_y), arc_radius * 2, arc_radius * 2,
                      angle=0, theta1=-half_angle, theta2=half_angle,
                      color='darkblue', linewidth=2)
    ax.add_patch(arc)
    ax.annotate(f'{angle_deg:.0f}°',
                xy=(chip_x + arc_radius + 0.1, 0),
                fontsize=22, fontweight='bold', color='darkblue',
                ha='left', va='center')

    # 距离参考圈
    for r, label in [(0.5, '0.5m'), (1.0, '1m'), (1.5, '1.5m')]:
        circle = plt.Circle((chip_x, chip_y), r, fill=False,
                            color='gray', linestyle=':', alpha=0.5)
        ax.add_patch(circle)
        ax.text(chip_x + r, -0.05, label, fontsize=7, color='gray',
                ha='center', va='top')

    ax.set_title(title, fontsize=12, fontweight='bold', pad=15)

# 左图: E-plane (宽方向)
draw_beam(axes[0], HPBW_E,
          f"E-plane（宽方向）\nHPBW typ = {HPBW_E:.0f}°  (范围 50°–80°)",
          color='#2196F3')

# 右图: H-plane (窄方向)
draw_beam(axes[1], HPBW_H_AVG,
          f"H-plane（窄方向）\nHPBW typ ≈ {HPBW_H_AVG:.0f}°  (RX 35°, TX 40°)",
          color='#FF9800')

# 底部说明
fig.text(0.5, 0.04,
         "● 实线扇形 = 半功率（-3dB）主瓣区域   "
         "● 红箭头 = boresight 中心轴   "
         "● 灰圆 = 距离参考",
         ha='center', fontsize=9, color='dimgray')
fig.text(0.5, 0.01,
         "注: 此为单根天线的方向图。双程 (TX×RX) 等效波束约为单程 × 0.71",
         ha='center', fontsize=9, color='dimgray', style='italic')

plt.tight_layout(rect=[0, 0.06, 1, 0.96])

out_path = __file__.rsplit('.', 1)[0] + '.png'
plt.savefig(out_path, dpi=140, bbox_inches='tight', facecolor='white')
print(f"saved: {out_path}")
