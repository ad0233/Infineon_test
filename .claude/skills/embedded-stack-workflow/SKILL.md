---
name: embedded-stack-workflow
description: BY001 项目沉淀的"AI 辅助嵌入式产品"四层工作流方法论（硬件契约 / 驱动 / 功能需求 / AI 协作）。当用户要启动新硬件项目、复刻这套体系、讨论 PM 友好的硬件 demo 流程、设置多 AI（Claude/Codex/Cursor）协作规范、或在本仓库强化执行同一套纪律时触发。
---

# Embedded Stack Workflow — AI 辅助嵌入式产品的四层工作流

本 skill 提炼自 BY001-ESP32 雷达睡眠监测项目的实践，回答一个核心问题：
**"如何让 PM 改 markdown 就能调 demo，同时 AI 不会破坏底层硬件契约？"**

---

## 一、四层架构（必须守住的边界）

```
┌────────────────────────────────────────────────────────────────┐
│ L4 ── AI 协作层（开发期，给 AI 看）                             │
│   CLAUDE.md / AGENTS.md / .cursorrules / .claude/skills/*      │
│   .codex/skills/* / mempalace.yaml（分布式上下文索引）          │
├────────────────────────────────────────────────────────────────┤
│ L3 ── 功能需求层（PM/上层开发可动）                             │
│   doc/hardware/<module>.md（每模块输出契约 + 数据结构 + 边界）  │
│   doc/hardware/luna-panel-bridge.md（信号链 + 状态机 + 验收）   │
│   doc/progress/evt-radar-bringup.md（调参实验日志）            │
├────────────────────────────────────────────────────────────────┤
│ L2 ── 驱动/组件层（工程师维护，PM 只读）                        │
│   components/my_<area>_<func>/（snake_case + my_ 前缀）        │
│   doc/standards/coding-standard.md（C 句柄式不透明指针规范）    │
├────────────────────────────────────────────────────────────────┤
│ L1 ── 硬件契约层（一次性产出，几乎不变）                        │
│   reference/hardware/（datasheet PDF）                         │
│   doc/netlist_*.tel（原理图网表）                              │
│   CLAUDE.md 内嵌的"硬件参数表 + 关键约束 + 期望日志"            │
└────────────────────────────────────────────────────────────────┘
```

**关键判断**：动一行代码前，先回答"这属于哪一层？" L1/L2 改动需要工程师评审，L3/L4 是日常协作主战场。

---

## 二、L4 AI 协作层：多 AI 并存的三层规范

### 2.1 三套规范文件，分工明确

| 文件 | 受众 | 长度 | 内容侧重 |
|------|------|------|---------|
| `CLAUDE.md` | Claude Code | 中（~150 行） | 项目定位 + 主工作路径 + 硬件约束 + 期望日志 + MemPalace 调用规则 |
| `AGENTS.md` | Codex / 通用 agent | 中（~135 行） | 与 CLAUDE.md 同源，开头声明"是 CLAUDE.md 的 Codex 适配版本" |
| `.cursorrules` | Cursor IDE | 极短（~7 行） | 只保留风格偏好（中文、简洁、不主动写 md） |

**反模式**：三份内容互相矛盾。规则是 **CLAUDE.md = 真相，AGENTS.md 同步翻译，.cursorrules 只放风格**。

### 2.2 双 AI Skill 体系（领域专家拆分）

- `.claude/skills/<domain>/SKILL.md` — 高层调优式专家（参数表 + 算法分支 + 调参入口）
- `.codex/skills/<chip-or-bringup>/SKILL.md` — 硬件 bring-up 式专家（寄存器 + 时序 + SPI 模式）

**两套 Skill 内容不重复**。Claude Skill 写"怎么调好"，Codex Skill 写"怎么调通"。

### 2.3 Skill 的 frontmatter 必须明示触发场景

```yaml
---
name: <kebab-case-name>
description: <做什么>。当用户讨论 <场景 1>、<场景 2>、<场景 3> 时触发。
---
```

description 不写"什么时候触发"= 永远不会自动触发。

### 2.4 .claude/settings.json 用于沉淀"反复授权的命令"

把已经手动批准过的 curl、pip、grep 模式存到 `permissions.allow`，下次免确认。

---

## 三、L4 分布式 mempalace.yaml（这是本项目独有的设计）

### 3.1 设计原则

每个语义子目录放一份 `mempalace.yaml`，声明该目录下的"局部记忆视图"。

```
仓库根/mempalace.yaml          ← 总索引：所有 rooms + 全局 exclude
doc/mempalace.yaml             ← 文档子集：documentation + radar-vitals
components/<comp>/mempalace.yaml ← 组件子集：source-radar
main/mempalace.yaml            ← 入口子集：source-main
tools/mempalace.yaml           ← 工具子集：tools
reference/hardware/mempalace.yaml ← 硬件子集：bgt60tr13c-datasheet
```

### 3.2 标准结构

```yaml
wing: <project_id>            # 全局 wing 名（跨子目录一致）
exclude:                      # 仅根 yaml 写，子目录继承
  - dependencies/
  - managed_components/
  - build/
rooms:
  - name: <room-id>
    description: <room 用途>
    keywords: [中英混合关键词列表]
```

### 3.3 必须执行的查询/写入触发

CLAUDE.md / AGENTS.md 内强制声明：
- 涉及硬件引脚 / 电源域 → 自动查 `room: hardware`
- 涉及调参 / 算法 → 自动查 `room: <feature>`
- 用户提到"上次/之前/历史" → 全局搜索
- 改 resource_map.h 或 SPI 配置前 → 必须先查

写入：硬件审计结论 / 调参实验确认 / 非显而易见的 bug 修复 → 强制存入对应 room。

**反模式**：临时调试数据、未确认实验结果、代码可读出的参数值——不存。

---

## 四、L1 硬件契约：把 datasheet 翻译成"AI 看得懂的约束"

不要假设 AI 会读 PDF。把硬件契约做成 **CLAUDE.md 内嵌表格**：

### 4.1 硬件配置表（已验证的运行真相）

```markdown
## 硬件配置（当前已验证）
| 参数 | 值 |
|------|----|
| 频率 | 59–63 GHz，BW = 4 GHz |
| ADC 采样率 | 1 MHz（ADC_DIV = 80） |
| ...
```

### 4.2 关键约束（违反会崩溃）

每条约束写明：**约束 → 涉及函数 → 后果 → 错误如何排查**。例：

> **FIFO 分片读取**：24576 样本 > 硬件 FIFO 上限（16384），必须分 6 片 × 4096 读取。
> 涉及函数：`init_sensor()`, `radar_rearm_next_measurement()`。
> 违反后果：断言崩溃。

### 4.3 期望日志四分类

| 类别 | 处理 |
|------|------|
| 健康启动日志 | 列出关键字，没出现 = 启动失败 |
| 健康运行日志 | 列出关键字 + 频率，无 = 链路断 |
| 可忽略日志 | 列出已知粘滞错误，AI 不要去"修复" |
| 需关注日志 | 列出 + 排查路径（先查什么再查什么） |

### 4.4 调参入口表

```markdown
| 参数 | 当前值 | 影响 |
|------|--------|------|
| RADAR_DISTANCE_THRESHOLD_DB | -5.0f | 距离检测幅度阈值 |
```

写明"当前值"+"调高/调低的影响"。AI 改参数前会优先在表里找。

### 4.5 改代码优先级

```
1. 优先改组件封装层
2. 再改 dependencies/（需说明封装层为何无法解决）
3. 保持 C 风格 + snake_case
4. 不引入 class、继承、模板式重构
```

---

## 五、L2 驱动层：组件命名与 C 句柄式接口

### 5.1 命名

- 目录：`components/my_<func>/`（统一前缀避免和官方组件冲突）
- 头文件：`my_<func>.h`
- 句柄类型：`my_<func>_handle_t`（不透明指针 forward declaration）

### 5.2 函数命名规范（强一致）

| 用途 | 命名 |
|------|------|
| 初始化 | `<comp>_init(handle_t *self_out, ...)` |
| 去初始化 | `<comp>_deinit(handle_t self)` |
| 注册回调 | `<comp>_reg_cb_<event>(handle_t, callback_t, void *ctx)` |
| 阻塞函数 | `<comp>_<func>_block(handle_t, ..., uint32_t timeout_ms)` |
| 非阻塞 | `<comp>_<func>(handle_t, ...)` |
| 查状态 | `<comp>_state(handle_t)` |
| 异步刷新 | `<comp>_flush(handle_t, uint32_t interval_ms)` |

### 5.3 硬约束

- 不得声明全局变量（临时验证可，交付不能）
- 嵌套 ≤ 3 层
- 错误码统一 `int` 返回（参考 `esp_err_t`）
- 内存申请失败必须返回错误
- 详见 [doc/standards/coding-standard.md](../../../doc/standards/coding-standard.md)

---

## 六、L3 功能需求层：PM/APP 视角的契约

### 6.1 模块输出契约写法（核心入口）

四块必备：
1. **检测能力表**：指标 / 范围 / 更新频率 / 延迟
2. **数据结构**（TypeScript interface 形式，APP 直接抄）
3. **数据有效性矩阵**（什么场景显示 "--" / "测量中" / "信号弱"）
4. **能做 / 不能做**（边界声明，避免过度承诺）

### 6.2 实施逻辑文档（PM 需求 → 硬件落地版）

把上层 PM/UI 需求**重排为接口契约 + 信号链 + 状态机**，让硬件工程师"看完能动手"。

必备章节：
- 顶层分工图（谁做什么）
- 信号 → 决策 → 执行主链路
- 接口契约（schema 形式）
- 状态机命名对齐表
- 降级树
- 验收红线

### 6.3 实验日志习惯

每次调参留两件套：
- `doc/progress/evt-<topic>-bringup.md` — 实验结论（按 EVT/DVT 里程碑命名）
- `tools/records/<date>/` — 真实采集数据 + hypnogram + summary.json

校准值都写日期：`(2026-04-12)`。

---

## 七、协作纪律（每次会话都要遵守）

1. **每次会话聚焦一个目标**，完成验证后再切换。
2. **用户有自动编译+烧录脚本**，代码改完不要提醒编译/烧录。
3. **"同步"或"复制"文件**：先确认是 git 操作（merge/rebase）还是文件拷贝。
4. **Python 中文一律简体**，不用繁体。
5. **运行时真相在 .c 文件里**，不要只看 settings.h 注释。
6. **文档冲突时以代码为准**，并立即修文档。

---

## 八、开新项目落地 Checklist

按这个顺序做，最快 1 天搭好骨架：

- [ ] 1. 创建 `reference/hardware/` 放 datasheet PDF
- [ ] 2. 导出原理图网表到 `doc/netlist_*.tel`
- [ ] 3. 写 `CLAUDE.md`：项目定位 + 主工作路径表 + 硬件参数表 + 关键约束 + 期望日志
- [ ] 4. 复制 `CLAUDE.md` 为 `AGENTS.md`，开头改成"Codex 适配版本"
- [ ] 5. 写 `.cursorrules`：只放 5-7 行风格偏好
- [ ] 6. 写 `doc/standards/coding-standard.md`（可直接复用本项目的 C 句柄式规范）
- [ ] 7. 根目录写 `mempalace.yaml`（声明所有 rooms + exclude）
- [ ] 8. 各语义子目录补 `mempalace.yaml`（声明该目录的局部 rooms）
- [ ] 9. 第一个组件按 `components/my_<func>/` 结构搭起来
- [ ] 10. 每个核心领域补一份 `.claude/skills/<domain>/SKILL.md`
- [ ] 11. 写 `doc/hardware/lunawake-hardware.md` + 每模块 md（哪怕只有占位接口）
- [ ] 12. 设置 `.claude/settings.json` 沉淀常用授权命令

---

## 九、何时调用本 skill 之外的 skill

| 场景 | 调用 |
|------|------|
| 调雷达 vitals 参数 / BPM 异常 | `radar-vitals` |
| 雷达 SPI/FIFO bring-up / 寄存器 | Codex 端 `infineon-bgt60tr13c-radar` |
| 启动新硬件项目 / 复刻工作流 | 本 skill |
| 强化本项目执行规范 | 本 skill + `radar-vitals` |

---

## 十、本方法论的核心论断

1. **AI 协作的杠杆来自"连接处"**，不是单点速度。普通工程师在某层比 AI 强，但 AI 把"硬件层 ↔ 算法层 ↔ 上位机层 ↔ 文档层"的协作成本压到 0。
2. **文档同步产出**而非事后补，避免 30% 额外工作量。
3. **L1 硬件契约入 CLAUDE.md** 是关键——AI 看不懂 PDF，但能严格遵守 markdown 表格里的约束。
4. **mempalace 分布式上下文** 解决"AI 进哪个目录就该懂哪个目录"的局部相关性问题。
5. **多 AI 兼容**（Claude/Codex/Cursor）让团队不被单一工具锁定，三套规范同源同步。

—— 这套方法论在 BY001 项目上的实测：8 天独立完成 3-4 个中级雷达工程师 1 个月的工作量。
