# UdiskTools UI 功能状态说明

> 适用版本：软件 v1.2.0（2026-10-25 工作树；配置文件版本见 ini [TOOLSINFO] VERSION）　窗口主类：`UDK_MAINCLASS`（`src/gui.h:11`）、配置窗口类 `UDK_CFGCLASS`（`src/gui_config.c`）
> 依据代码：`src/gui.c`（主窗口）、`src/gui_config.c`（配置窗口）、`src/gui_shared.h`、`src/gui.h`、`src/ops.c`、`src/ops.h`、`src/device.c/.h`、`src/util.c/.h`、`src/main.c`、`ui/resource.h`（53 个 `#define`）
> 校验方式：以工作树源码逐函数核对；控件 ID 与 `ui/resource.h` 一一比对（见附录 A）；行为与《配置文件使用手册》《UdiskTools软件规划书》《插拔全流程图.html》交叉核对
> 说明：文中 `gui.c:127` 形式均指当前工作树行号，后续代码变动时行号可能漂移，以函数名 + 控件 ID 定位为准。
> 版本约定：窗口标题为 `<TITLE>（软件 v<软件版本>）（配置 v<配置版本>）`；软件版本 `APP_VERSION=1.2.0`（`--version`/启动日志/标题“（软件 v1.2.0）”），配置文件版本=ini `[TOOLSINFO] VERSION`（标题“（配置 v…）”、`--check-ini` 显示），两者独立。

## 0. 阅读约定与用户约束

**状态标记**

| 标记 | 含义 |
|---|---|
| ✅ 已实现 | 功能可用，行为与本文一致 |
| ⚙️ 按配置/状态 | 由 `ENABLE_*`、监控开关、`AUTORUN` 或运行状态决定可用/禁用 |
| 👁 只读展示 | 程序写入、操作员不可编辑 |
| ⏸ 预留 | 控件存在但**无入口、不参与任务**；v1.2.0 不承诺该功能 |
| 🚫 废弃 | 保留 ID/消息号但不再使用 |

**两条硬约束（用户明确要求）**
1. **不做“密码连错 3 次锁定”**：`ADMINPASS` 错误只弹提示并写日志（不含密码内容），**允许无限重试**；未解锁时配置窗口只读，[保存修改]/[恢复默认设置] 不可用。当前代码 `src/gui.c` **不存在** `g_cfg_failcount` 或任何次数锁定逻辑（见 `gui.c:1557-1591`）。
2. **`IDC_EDIT_IDENT 1402`（CMD-B）为以后功能扩展保留**：无入口、从不写入、不参与任务（见 §1.6 与 §7）。旧文档中“CMD-B 演示输出/标识生成已实现”的说法已作废。

---

## 1. 主窗口（`WND_MAIN`，`UDK_MAINCLASS`）控件状态

全部 35 个主窗口控件 ID 均来自 `ui/resource.h`。创建代码集中在 `gui.c:1767-1820`（`WM_CREATE`），布局在 `gui.c:943-1053`（`layout()`）。

### 1.1 机型与操作按钮（PANE_TOP）

| 控件 ID | 名称/默认文字 | 类型 | 状态 | 行为与刷新规则 | 代码依据 |
|---|---|---|---|---|---|
| `IDC_COMBO_MODEL` 1001 | 机型下拉（显示 `DISPLAYNAME`） | COMBOBOX `CBS_DROPDOWNLIST` | ✅ | 启动选中 `DEFAULTMODEL`；`CBN_SELCHANGE` 立即从磁盘重载配置并定位所选机型；重载失败保持原机型并记 ERR（不假报成功） | `gui.c:1776`、`1888-1919`、`2374-2381` |
| `IDC_BTN_START` 1002 | 开始（可被 `BTN_START` 覆盖） | BUTTON | ✅ | 触发**整任务**（`task_run_async(g_cur_model, TRIGGER_MANUAL, 0)` → 取第一个可移动盘符） | `gui.c:1777`、`1920-1922`、`724-780` |
| `IDC_BTN_STOP` 1003 | 停止（可被 `BTN_STOP` 覆盖） | BUTTON | ⚙️ 仅任务执行中可用 | 置停止请求（`ops_request_stop()`，含取消 CopyFileEx）；空闲时禁用 | `gui.c:240`、`1923-1926` |
| `IDC_BTN_FORMAT` 1004 | 格式化（`BTN_FORMAT`） | BUTTON | ⚙️ `ENABLE_FORMAT=1` 且空闲 | 单操作格式化（强制确认 + 身份复核；自动流程永不格式化） | `gui.c:1779`、`1927-1929`、`ops.c:798-967` |
| `IDC_BTN_READLABEL` 1005 | 读取标签（`BTN_READLABEL`） | BUTTON | ⚙️ 空闲 | 单操作读取卷标/文件系统/序列号/容量/可用空间并刷新标签行 | `gui.c:1780`、`1930-1932`、`ops.c:1029-1060` |
| `IDC_BTN_WRITELABEL` 1006 | **复制**（`BTN_WRITELABEL`，如“升级”） | BUTTON | ⚙️ `ENABLE_WRITE=1` 且空闲 | 单操作复制：`SRCPATH→DSTPATH` + MD5（不一致重试一次）；结果徽标 `Copy Complete!/Copy Failure!` | `gui.c:1781`、`1933-1935`、`ops.c:241-500` |
| `IDC_BTN_CONFIG` 1007 | 配置 | BUTTON | ✅ | 打开配置窗口（模态风格独立顶层窗口）。注意：**`BTN_CONFIG` 不被读取**，文字固定为“配置” | `gui.c:1784`、`1942-1944`、`227-227` |
| `IDC_BTN_READCHECK` 1015 | 读取校验（`BTN_READCHECK`） | BUTTON | ⚙️ `ENABLE_READ=1` 且空闲 | 单操作读取 + `READRULE` 判定（PASS/FAIL 徽标与双窗格内容） | `gui.c:1782`、`1936-1938`、`ops.c:545-750` |
| `IDC_BTN_RWTEST` 1016 | 读写测试（`BTN_RWTEST`） | BUTTON | ⚙️ 空闲 | 单操作 8MB 写→回读→MD5 比对+速率（NO_BUFFERING 直读介质，回退缓冲时告警） | `gui.c:1783`、`1939-1941`、`ops.c:1091-1290` |

### 1.2 复选框

| 控件 ID | 文字 | 状态 | 行为 | 代码依据 |
|---|---|---|---|---|
| `IDC_CHK_TOPMOST` 1008 | 窗口置顶 | ✅ | **初始状态由全局键 `TOPMOST` 决定（缺省 1=默认勾选并置顶）**；勾选 `HWND_TOPMOST` / 取消 `HWND_NOTOPMOST` 并写 CMD-A 日志；配置保存/重载时按 TOPMOST 同步 | `gui.c`、`config.c TOPMOST` |
| `IDC_CHK_AUTO_COPY` 1009 | 自动复制(AUTO_COPY) | CHECKBOX `BS_AUTOCHECKBOX \| WS_DISABLED`（**灰只读**） | 👁 | 显示当前机型 `AUTO_COPY`（缺省 1）的勾选状态；`update_auto_flags_ui()` 同步勾选并强制禁用，不可操作；不参与 AUTO/MANUAL 判定 | `gui.c:1787-1788`、`gui.c:420-436`、`gui.c:243` |
| `IDC_CHK_AUTO_TEST` 1018 | 自动测试(AUTO_TEST) | CHECKBOX `BS_AUTOCHECKBOX \| WS_DISABLED`（**灰只读**） | 👁 | 显示当前机型 `AUTO_TEST`（缺省 0）的勾选状态；规则同上；`AUTO_TEST=1` 时同时决定 READRULE 标签 1109 是否显示 | `gui.c:1789-1790`、`gui.c:420-436`、`gui.c:244` |
| `IDC_CHK_HOTKEY` 1013 | 快捷键使能（Ctrl+Alt+T/C） | ✅ | 勾选时注册两个全局热键（完整语义见 §2）；任一注册失败则回滚并取消勾选+提示；取消勾选/退出注销 | `gui.c:1791-1792`、`1981-1994`、`1726-1758` |

### 1.3 标签/状态行（PANE_LABEL / PANE_STATUS）

| 控件 ID | 内容 | 类型 | 状态 | 行为 | 代码依据 |
|---|---|---|---|---|---|
| `IDC_STC_LABEL` 1101 | 标签：各盘卷标 | STATIC | 👁 | 随插拔刷新（`update_device_text`）；多盘逐个列出；无盘“（无磁盘）”。**USB VID/PID/SN 不在界面显示**（仅任务线程用于身份复核/格式化确认）；[读取标签] 可刷新为详细文本 | `gui.c:1793`、`510-554`；`ops.c:1029-1060` |
| `IDC_STC_AUTOMODE` 1104 | AUTO / MANUAL | RichEdit（回退 STATIC）只读 | 👁 | 唯一判定点：监控开启 **或** 当前机型 `AUTORUN=1` → AUTO（绿）；否则 MANUAL（蓝）；变化时写 CMD-A 日志 | `gui.c:1794`、`364-403` |
| `IDC_STC_SRCPATH` 1105 | `SRCPATH：<当前机型值>` | STATIC `SS_ENDELLIPSIS` | 👁 | 随机型切换/保存重载刷新；未配置显示“（本机型未配置）” | `gui.c:1795`、`405-416` |
| `IDC_STC_READRULE` 1109 | `判定规则：<READRULE>` | STATIC `SS_LEFT \| SS_ENDELLIPSIS` | 👁 | `AUTO_TEST=1`：显示 `判定规则：<READRULE>`；READRULE 为空显示 `判定规则：（未配置 READRULE）`；`AUTO_TEST=0`：内容为空；`update_readrule_text()` 在 refresh_buttons/机型切换时刷新 | `gui.c:1797-1798`、`gui.c:440-460`、`gui.c:244` |
| `IDC_STC_INSERTCOUNT` 1017 | 计数:0000 | STATIC（粗体） | 👁 | **仅监控开启时**每次插入 +1（拔出不计）；4 位循环 9999→0000；启动 0、退出不保存 | `gui.c:1796`、`488-495`、`2061-2064` |
| `IDC_STC_DEVICE` 1201 | 设备状态：盘符/卷标/容量 | STATIC（粗体） | 👁 | 随插拔刷新；无盘“未检测到可移动磁盘（等待插入…）”；8192 字符有界追加，26 盘不越界 | `gui.c:1799`、`510-554`、`497-508` |
| `IDC_STC_ERROR` 1202 | ✓ 正常 / ✕ 出错：原因 | RichEdit（回退 STATIC）只读 | 👁 | 出错=红字+标题追加 `[!]`；插盘/成功时熄灭；任务失败由 ops `post_error` 置红 | `gui.c:1800`、`257-307` |

### 1.4 任务进度（PANE_TASK）

| 控件 ID | 内容 | 类型 | 状态 | 行为 | 代码依据 |
|---|---|---|---|---|---|
| `IDC_PBAR_TASK` 1301 | 任务进度条 | PROGRESS_CLASS | 👁 | **唯一百分比入口** `post_progress_pct()`：任务开始/结束/停止/拔出统一归零；复制按字节、格式化按 fmifs 回调换算 0..100 | `gui.c:1801`、`337-348`、`2025-2028` |
| `IDC_STC_PROGTITLE` 1106 | 任务进度： | STATIC | 👁 | 固定提示文字 | `gui.c:1802`、`1006-1006` |
| `IDC_STC_PROGPCT` 1107 | 0%…100% | STATIC `SS_RIGHT` | 👁 | 与进度条同源更新 | `gui.c:1803`、`337-348` |

### 1.5 控制台（PANE_CMDS）

| 控件 ID | 内容 | 类型 | 状态 | 行为 | 代码依据 |
|---|---|---|---|---|---|
| `IDC_EDIT_ENGINE` 1401 | CMD-A 操作日志 | RichEdit 只读（回退 STATIC） | 👁 | 所有 `applog(CH_ENGINE,…)` 同步显示，按级别配色（OK 绿/WARN 黄/ERR 红/UI 蓝）；>120000 字符自动清空；清屏按钮可清 | `gui.c:1813`、`169-209`、`1963-1968`、`2018-2024`；`util.h:19` |
| `IDC_EDIT_IDENT` 1402 | **CMD-B 输出区（预留）** | RichEdit 只读 | ⏸ **预留（标识生成扩展）** | **无入口、从不写入、不参与任务**；`op_gen_ident` 虽存在但 `run_single`/按钮/热键均无 `OP_IDENT` 调用，CMD-B 恒为空 | `gui.c:1814`、`784-785`、`842-842`；`ops.c:1292`；`ui/resource.h:56` |

### 1.6 文件内容区（PANE_FILE）与底部监控窗格（PANE_BOTTOM）

| 控件 ID | 内容 | 类型 | 状态 | 行为 | 代码依据 |
|---|---|---|---|---|---|
| `IDC_STC_READVERDICT` 1501 | PASS / FAIL | RichEdit（回退 STATIC）只读 | 👁 | `op_read` 通过 `WM_APP_VERDICT` 置 PASS 绿/FAIL 红 | `gui.c:1815`、`350-362`、`2104-2113` |
| `IDC_EDIT_FILE` 1502 | 第一命中文件内容 | RichEdit 只读 | 👁 | `WM_APP_FILECONTENT`（pane=0）写入；任务开始/新盘/停止时清空 | `gui.c:1816`、`2114-2119`、`560-570` |
| `IDC_EDIT_FILE2` 1503 | 第二命中文件内容 | RichEdit 只读 | 👁 | `WM_APP_FILECONTENT`（pane=1）；无第二命中为空 | `gui.c:1817`、`2114-2119` |
| `IDC_STC_MONSTATUS` 1102 | 监控状态/任务状态 | RichEdit（回退 STATIC）只读 | 👁 | 文本：`监控已停止`(红)/`监控已开启：等待插入…`(绿)/`任务执行中…`/`文件已找到`/`任务失败（详见 CMD-A 日志）`/`任务已停止` | `gui.c:1806`、`464-486`、`2199-2214` |
| `IDC_STC_MONFILE` 1103 | 左窗格命中文件路径 | STATIC 居中 | 👁 | 颜色由 `WM_CTLCOLORSTATIC` 绘制（蓝）；`op_read` 的“文件已找到”副行写入 | `gui.c:1807`、`325-332`、`464-471`、`1845-1856` |
| `IDC_STC_MONFILE2` 1108 | 右窗格命中文件路径 | STATIC 居中 | 👁 | 同上；`WM_APP_MONINFO(pane=1)` 写入第二文件路径 | `gui.c:1808`、`2139-2153` |
| `IDC_BTN_MONSTART` 1010 | 开始监控（绿色自绘） | BUTTON `BS_OWNERDRAW` | ✅ | 开启监控→AUTO 模式→插盘自动执行当前机型任务；监控中禁用 | `gui.c:1809`、`1945-1952`、`248-253` |
| `IDC_BTN_MONSTOP` 1011 | 停止监控（红色自绘） | BUTTON `BS_OWNERDRAW` | ⚙️ 监控开启时可用 | 关闭监控；若任务执行中同时请求停止；设备检测仍持续 | `gui.c:1810`、`1953-1962` |
| `IDC_BTN_EXIT` 1012 | 退出（灰色自绘） | BUTTON `BS_OWNERDRAW` | ✅ | 发 `WM_CLOSE`；任务运行中先确认停止并等待工作线程（最长 30s） | `gui.c:1811`、`1969-1971`、`2244-2281` |
| `IDC_BTN_CLEARCMD` 1014 | 清屏 | BUTTON | ✅ | 清空 CMD-A（文件日志保留），并记一条 UI 日志 | `gui.c:1812`、`1963-1968` |

### 1.7 主窗口消息（刷新依据）

| 消息（`gui.h`） | 方向/来源 | 处理与作用 | 代码依据 |
|---|---|---|---|
| `WM_APP_LOG` (+1) | ops/任意线程 → GUI | 追加到 CMD-A/CMD-B 通道（`ch` 决定），堆串由 GUI 释放 | `gui.h:14`、`gui.c:2018-2024` |
| `WM_APP_PROGRESS` (+2) | ops → GUI | 仅经 `post_progress_pct()` 更新进度条+百分比 | `gui.h:15`、`gui.c:2025-2028` |
| `WM_APP_DEVCOUNT` (+3) | device 轮询 → GUI | `wParam=变化盘符位图（可多位）`、`lParam=1 插入/0 拔出`；先拔后插两条消息；增量维护 `g_dev_mask` | `gui.h:16`、`device.h:58-67`、`device.c:278-301`、`gui.c:2029-2103` |
| `WM_APP_VERDICT` (+4) | ops → GUI | PASS/FAIL 或绿/红文本判定徽标 | `gui.h:17`、`gui.c:2104-2113` |
| `WM_APP_FILECONTENT` (+5) | ops → GUI | 文件内容双窗格（pane 0/1） | `gui.h:18`、`gui.c:2114-2119` |
| `WM_APP_LABEL` (+6) | ops → GUI | [读取标签] 结果刷新标签行 | `gui.h:19`、`gui.c:2120-2126` |
| `WM_APP_ERROR` (+7) | ops/守卫 → GUI | `wParam=1` 红字出错徽标；0=绿正常/熄灭 | `gui.h:20`、`gui.c:2127-2135` |
| `WM_APP_TASKDONE` (+8) | worker → GUI | `wParam=OP_*` 结果码：join 线程、复位状态、徽标/日志/待处理盘处理 | `gui.h:21`、`gui.c:2158-2242` |
| `WM_APP_MODE` (+9) | 无 | 🚫 已废弃（保留消息号兼容） | `gui.h:22`、`gui.c:2136-2138` |
| `WM_APP_MONINFO` (+10) | ops → GUI | 监控状态行/命中文件行文本（标题\n副行） | `gui.h:23`、`gui.c:2139-2153` |
| `WM_APP_DEVREFRESH` (+11) | ops → GUI | 格式化后重新采集并刷新设备/标签行 | `gui.h:24`、`gui.c:2154-2157` |

### 1.8 按钮启用/禁用总规则（`refresh_buttons`，`gui.c:213-254`）

| 条件 | 效果 |
|---|---|
| `g_running != 0`（任务/单操作执行中） | 8 个操作按钮（开始/停止/复制/读取校验/读写测试/格式化/读取标签/配置）按 busy 置灰；**仅 [停止] 反转为可用**；监控按钮独立按监控状态启用 |
| 机型 `ENABLE_WRITE/READ/FORMAT=0` | 对应按钮置灰（复制/读取校验/格式化）；开始/停止/读写测试/读取标签/配置不受 `ENABLE_*` 限制 |
| 监控开启 | [开始监控] 禁用、[停止监控] 启用；关闭反之；[退出] 常可用 |
| `BTN_*` 配置 | 覆盖对应按钮文字（`BTN_START/STOP/WRITELABEL/READCHECK/RWTEST/FORMAT/READLABEL`）；**`BTN_CONFIG` 不支持** |
| AUTO 状态 / 判定规则显示 | `refresh_buttons()` 末尾同时调用 `update_auto_flags_ui()` 与 `update_readrule_text()`，覆盖启动/重载/保存/恢复/插入刷新；机型切换后额外再刷新一次 | `gui.c:243-244`、`gui.c:1902-1905` |

---

## 2. 快捷键（全局热键）

| ID | 组合键 | 语义 | 注册/生效条件 | 代码依据 |
|---|---|---|---|---|
| `ID_HOTKEY_T` 2001 | **Ctrl+Alt+T** | 单操作“读写测试”（`run_single(OP_RWTEST)`） | 仅 `IDC_CHK_HOTKEY` 勾选时注册；任务执行中忽略并写 WARN；退出注销 | `gui.c:1726-1758`、`1998-2016`、`2428-2429` |
| `ID_HOTKEY_C` 2002 | **Ctrl+Alt+C** | 单操作“复制”（`run_single(OP_COPY)`） | 同上 | 同上 |

> 说明：热键**不再是裸 T/C**（`MOD_CONTROL|MOD_ALT`），也**不触发整任务**；注册失败（被占用）会回滚、取消勾选、弹提示并写文件日志。
> 复选框文字已缩短为 **快捷键使能（Ctrl+Alt+T/C）**（完整语义见本节及 §1.2）；热键注册逻辑与行为不变。

---

## 3. 配置窗口（`WND_CONFIG`，`UDK_CFGCLASS`）

### 3.1 控件状态表（13 个登记 ID）

| 控件 ID | 名称 | 类型 | 状态 | 行为 | 代码依据 |
|---|---|---|---|---|---|
| `IDC_CFG_LIST_MODELS` 3001 | 机型列表 | LISTBOX | ✅ | 列显示名；初始选中**主界面当前机型**（按 internal 匹配，失败回退 0） | `gui.c:1464`、`1505-1514`、`1531-1535` |
| `IDC_CFG_COMBO_KEY` 3011 | 键名下拉（可编辑） | COMBOBOX `CBS_DROPDOWN` | ⚙️ 解锁后可编辑 | 列当前机组全部键；可手输列表外新键名；`CBN_SELCHANGE/CBN_EDITCHANGE` 切换值控件类型 | `gui.c:1469-1470`、`1551-1556`、`1141-1213` |
| `IDC_CFG_COMBO_VAL` 3012 | 枚举值下拉 | COMBOBOX `CBS_DROPDOWNLIST` | ⚙️ 解锁后可选 | `ENABLE_*/AUTO_*/AUTORUN/CONFIRM/WRITEMODE/MATCH_POLICY/READPATTERN/FILESYSTEM/HASH` 走下拉；非法枚举值安全回退 | `gui.c:1474-1475`、`1103-1135`、`1167-1188` |
| `IDC_EDIT_CFGVAL` 3009 | 值输入框 | EDIT | ⚙️ 解锁后可编辑 | 自由键输入；选中键后回填当前值；`EM_SETLIMITTEXT` 限长 | `gui.c:1473`、`1189-1197`、`1540-1543` |
| `IDC_STC_CFGVALLBL` 3014 | 新值标签 | STATIC | 👁 | 随键类型切换“新值（下拉选择）/新值（输入）” | `gui.c:1472`、`1102/1127` |
| `IDC_STC_CFGHINT` 3013 | 说明行 | STATIC | 👁 | 显示当前键说明；未解锁附加“锁定中：新值框灰色只读，点[解锁编辑]输入密码后可修改”；非法枚举值提示保留原值 | `gui.c:1480`、`1198-1211`、`1065-1096` |
| `IDC_BTN_UNLOCK` 3003 | 解锁编辑/确认解锁 | BUTTON | ✅ | 两步式：第一次点击显示密码框；再次点击校验；成功→可编辑；失败→提示+日志+可无限重试；`ADMINPASS` 为空时提示“暂不支持解锁编辑” | `gui.c:1484`、`1557-1591` |
| `IDC_CFG_EDIT_PASS` 3002 | 密码框（掩码） | EDIT `ES_PASSWORD` | ⚙️ 点击解锁后显示 | 初始隐藏禁用；校验成功后隐藏清空 | `gui.c:1485-1486`、`1537-1539`、`1565-1588` |
| `IDC_BTN_SAVECFG` 3004 | 保存修改 | BUTTON | ⚙️ **仅解锁后可用** | 写回单键（保留编码/BOM/行尾/其余内容/行内注释，写后回读校验）；失败提示“原配置未被修改”；写入成功但重载失败时明确报错不假报成功 | `gui.c:1487`、`1592-1635`、`1250-1261` |
| `IDC_BTN_RESETCFG` 3010 | 恢复默认设置 | BUTTON | ⚙️ **仅解锁后可用** | 二次确认→备份 `<ini>.bak`→PID 临时文件原子写（FlushFileBuffers+MoveFileEx WRITE_THROUGH）→`cfg_load` 回读校验；失败保留原文件并报错 | `gui.c:1488`、`1637-1684`、`1338-1432` |
| `IDC_BTN_CLOSECFG` 3005 | 关闭 | BUTTON | ✅ | 重载配置（失败记 WARN）→回只读→销毁窗口 | `gui.c:1491`、`1685-1693` |
| `IDC_STC_CFGSTATE` 3006 | 状态：只读（未授权）/已解锁（可编辑保存） | STATIC | 👁 | 随解锁状态刷新 | `gui.c:1490`、`1250-1261` |
| `IDC_EDIT_CFGTEXT` 3007 | 配置内容（只读展示） | EDIT 多行只读 | 👁 | 显示当前机型全部键值 | `gui.c:1496-1497`、`1220-1248` |

> 配置窗口另有 4 个**未登记 ID**（id=0）的 STATIC 文字标签：“机型：”“键（选择）：”“说明：”“配置内容：”（`gui.c:1463/1402/1413/1428`），不在 `ui/resource.h` 中，属纯装饰文字。

### 3.2 只读 / 密码 / 保存 / 恢复默认规则

| 规则 | 说明 | 代码依据 |
|---|---|---|
| 默认只读 | 打开即只读；键名/值控件禁用；说明行提示解锁方法 | `gui.c:1536`、`1250-1261` |
| 解锁 | `ADMINPASS` 明文比较；成功 → `g_cfg_unlocked=TRUE` 并写 UI 日志（不含密码） | `gui.c:1574-1581` |
| **密码错误（无锁定）** | 仅弹“密码错误，请重试”+ `LOG_WARN`；**可无限重试，不做次数锁定**；关窗后回只读 | `gui.c:1582-1589`、`1624/1636` |
| `ADMINPASS` 为空 | 提示“ini 未设置管理员密码，暂不支持解锁编辑”，窗口保持只读 | `gui.c:1560-1564` |
| 保存 | 仅解锁可用；`cfg_save_key` 失败 → 明确“原配置未被修改”；重载失败 → 明确“已写入但重载失败”；成功 → 刷新标题/机型列表/内容 | `gui.c:1592-1635` |
| 恢复默认 | 仅解锁可用；二次确认；先备份 `.bak`；PID 唯一临时文件 + 校验写入 + `FlushFileBuffers` + `MoveFileEx(REPLACE_EXISTING|WRITE_THROUGH)` + `cfg_load` 回读；任何失败保留原文件；成功后回只读并提示备份位置 | `gui.c:1637-1684`、`1338-1432` |
| 关窗 | 重载配置、回只读、销毁窗口 | `gui.c:1685-1705` |
| 机型列表 | 打开时选中主界面当前机型；保存/恢复/重载后重建列表并保持选择 | `gui.c:1505-1514`、`1274-1290`、`1321-1332` |
| DPI | 创建时按当前 `g_dpi` 缩放（540×620）；主窗口 DPI 变化时字体同步刷新到配置窗口子控件；配置窗口自身不处理 `WM_DPICHANGED` 重排（限制） | `gui.c:124-154`、`1717-1717`、`1822-1830` |

---

## 4. 任务、状态机与结果语义

### 4.1 触发源

| 触发方式 | 行为 | 代码依据 |
|---|---|---|
| [开始] 按钮 | 整任务，取第一个可移动盘符（无盘报错） | `gui.c:1920-1922`、`724-780` |
| Ctrl+Alt+T / Ctrl+Alt+C | **单操作** 读写测试 / 复制（任务中忽略并记日志） | `gui.c:1998-2016` |
| 插盘自动 | 当“监控开启 或 当前机型 `AUTORUN=1`”时，对**刚插入的盘符**自动执行整任务；否则只刷新设备信息 | `gui.c:2066-2080` |
| 任务执行中又插盘 | 记为“待处理盘”（多个只取第一个）；当前任务**成功/按配置跳过**结束后补执行；失败/停止/退出/拔出则取消 | `gui.c:2070-2079`、`2217-2240` |
| 插入计数 | 仅监控开启时每插入 +1；拔出不计 | `gui.c:2060-2064` |

### 4.2 整任务四步与聚合（`task_thread`，`gui.c:633-722`）

| 步骤 | 操作 | 执行条件 | 结果码 |
|---|---|---|---|
| 0 | 采集目标盘身份基线（卷序列号/VID/PID/USB SN） | 每任务开始；采集失败仅按盘符复核并 WARN | `gui.c:639-649`、`device.c:205-...` |
| 1 | USB 识别 `op_usbinfo` | `ENABLE_USBINFO=0` → 按配置跳过 | `ops.c:979-1026` |
| 2 | 复制 `op_write` | 手动触发总是执行；自动触发须 `AUTO_COPY=1`（=0 属按配置跳过） | `ops.c:241-500`、`gui.c:671-682` |
| 3 | 读取判定 `op_read` | `ENABLE_READ=0`/`READFILE` 留空 → 按配置跳过 | `ops.c:545-750` |
| 4 | 读写测试 `op_rwtest` | 手动触发总是执行；自动触发须 `AUTO_TEST=1` | `ops.c:1091-1290`、`gui.c:691-702` |

- **结果码**（`ops.h:26-30`）：`OP_OK=0` / `OP_FAIL=1` / `OP_STOPPED=2` / `OP_SKIP_CFG=3`。
- **聚合规则**（`gui.c:704-719`）：失败优先 → `FAIL`；否则停止 → `STOPPED`；否则全部按配置跳过（exec==0 且 skip>0）→ `SKIP_CFG`；否则 `OK`。
- 结束日志：`=== 任务结束（成功|按配置跳过（未执行）|已停止|有错误）：执行 N 步，按配置跳过 M 步 ===`。
- **身份守卫**：每一步执行前重新采集并与基线比较；变化即中止并红徽标提示 `[GUARD] 目标盘已变化…`（`gui.c:602-621`、`656-702`）。格式化在确认后还会再复核一次（`ops.c:883-893`）。

### 4.3 单操作完成语义

| 操作 | 入口 | 结果徽标（`WM_APP_TASKDONE`） | 代码依据 |
|---|---|---|---|
| 复制 | 按钮/热键 | `复制完成` / `复制：按配置跳过（未执行）` / `复制已停止`；失败保留红徽标（ops 置位） | `gui.c:2169-2196` |
| 读取判定 | 按钮 | `读取判定完成` / …；判定徽标 PASS/FAIL 由 op 内部设置 | 同上 |
| 格式化 | 按钮 | `格式化完成` / `格式化已停止` / 失败红徽标 | 同上 |
| 读取标签 | 按钮 | `读取标签完成` | 同上 |
| 读写测试 | 按钮/热键 | `读写测试完成` | 同上 |

### 4.4 结果码 → 徽标 / 监控状态 / 进度

| 结果码 | 状态徽标 1202 | 监控状态行 1102 | 进度 | 备注 |
|---|---|---|---|---|
| `OP_OK` | 绿 `✓ 任务完成`（单操作 `✓ <操作名>完成`） | 回到 `监控已停止`/`监控已开启：等待…` | 100% | 保留本任务结果内容 |
| `OP_SKIP_CFG` | 绿 `✓ 按配置跳过（未执行）`（单操作带操作名前缀） | 回到空闲态 | 0% | 清判定/内容/命中行（避免误读上轮） |
| `OP_STOPPED` | 绿 `✓ 任务已停止` | `任务已停止`（红） | 0% | 清判定/内容/命中行 |
| `OP_FAIL` | 红 `✕ 出错：…`（由 ops `post_error` 置位，TASKDONE 不改绿） | `任务失败（详见 CMD-A 日志）`（红） | 0% | 保留失败现场 |

> 实现位置：`gui.c:2158-2242`；错误徽标通用逻辑 `gui.c:257-307`；任务开始/新盘复位 `reset_task_ui` `gui.c:560-570`。

### 4.5 停止与退出

| 场景 | 行为 | 代码依据 |
|---|---|---|
| [停止] | `ops_request_stop()`：置停止标志并取消正在进行的 `CopyFileExW`；任务在步骤间/复制回调内尽快返回 `OP_STOPPED` | `ops.c:25-33`、`gui.c:1923-1926`、`ops.c:198` |
| [停止监控]（任务中） | 关闭监控 + 请求停止 | `gui.c:1953-1962` |
| 关窗/退出 | 有任务先确认 → `g_closing=1` → 请求停止 → `MsgWaitForMultipleObjects` 带消息泵等待工作线程（最长 30s）→ join；超时强退并置退出码 3 | `gui.c:2244-2281`、`913-939` |
| `WM_DESTROY` | 停止轮询线程、兜底请求停止、join 工作线程、`PostQuitMessage(g_exit_code)` | `gui.c:2283-2297` |

---

## 5. 窗口 / DPI / 布局 / 最小尺寸

| 项 | 说明 | 代码依据 |
|---|---|---|
| 窗口标题 | `<TITLE>（软件 v<软件版本>）（配置 v<配置版本>）`；配置版本为空时省略“（配置 v…）”；缓冲 1200、始终 NUL 不截断 | `gui.c:577-588`、`src/version.h` |
| DPI 感知 | 运行时声明 Per-Monitor V2（失败回退 Per-Monitor/System） | `gui.c:89-112` |
| 字体 | 按 DPI 重建；先替换控件字体再删旧字体（避免悬空 HFONT）；配置窗口同步 | `gui.c:124-154` |
| `WM_DPICHANGED`（主窗口） | 更新 `g_dpi` → 重建字体 → 按系统建议尺寸 `SetWindowPos` → `WM_SIZE` 触发 `layout()` | `gui.c:1822-1830`、`943-1053` |
| 主窗口初始尺寸 | 880×760（DPI 缩放），按工作区居中/偏移 | `gui.c:2361-2363` |
| 最小尺寸 | `WM_GETMINMAXINFO`：`ptMinTrackSize = 880×600`（DPI 缩放），防缩到负布局 | `gui.c:1836-1843` |
| 配置窗口尺寸 | 540×620（DPI 缩放） | `gui.c:1717` |
| 布局分块 | `layout()` 按 PANE_TOP/LABEL/STATUS/TASK/CMDS/BOTTOM/FILE 纵向排布；左右双栏（CMD-A/B、内容双窗格） | `gui.c:943-1053` |
| RichEdit 回退 | Msftedit(`RICHEDIT50W`) → Riched20(`RICHEDIT20W`) → `STATIC`（只读文本，禁富文本格式化，日志 WARN） | `gui.c:2310-2328`、`47-48`、`169-209` |
| 图标 | 资源 `IDI_APP_ICON 101`，缺失回退系统图标 | `gui.c:2340-2342`、`ui/resource.h:9` |

---

## 6. 日志 / 崩溃转储 / CLI / 退出码 / 单实例

| 项 | 说明 | 代码依据 |
|---|---|---|
| 文件日志 | `exe目录\logs\YYYYMMDD.log`（UTF-8）；强制输出不可关闭；句柄按天缓存；单文件 >8MB 轮转 `.log.1`；主文件被占用写 `_alt.log` 不丢行；`log_shutdown()` 退出收尾 | `util.h:59-68`、`util.c:454`、`487-536`、`572-577`、`587+` |
| CMD-A | 与文件日志同步（`applog` 双通道）；分级配色；CMD-B 预留无输出 | `util.c`、`gui.c:169-209`、`2018-2024` |
| 崩溃转储 | `SetUnhandledExceptionFilter` + `MiniDumpWriteDump` → `logs\crash_YYYYMMDD_HHMMSS.dmp`，日志记录异常码/地址/模块/转储路径，不吞异常 | `main.c:227-291` |
| CLI | `--ini <path>` / `--check-ini`（不建 GUI，0/1，输出机型数 + **配置文件版本**）/ `--version,-v`（**软件版本 1.2.0** + FileVersion 1.2.0.0 + 构建时间）/ `--help`；优先 AttachConsole/重定向输出，失败 MessageBox 兜底 | `main.c:105-218`、`main.c:336-409` |
| 单实例 | `Local\UdiskTools.SingleInstance` 互斥；已有实例则激活现有窗口并以 0 退出（不弹窗） | `main.c:293-334`、`gui.h:11` |
| 退出码 | 0=正常；1=消息循环失败/初始化失败；3=关窗等待工作线程超时强制退出；CLI `--check-ini` 0/1 | `gui.c:27`、`2413-2431`、`main.c:336-409` |
| 热键注销 | 退出前 `UnregisterHotKey` T/C | `gui.c:2428-2429` |

---

## 7. 预留 / 未实现 / 已知限制清单

| 项 | 状态 | 说明与依据 |
|---|---|---|
| `IDC_EDIT_IDENT 1402`（CMD-B 输出区） | ⏸ 预留（标识生成扩展） | 无入口、从不写入、不参与任务；`op_gen_ident` 虽在 `ops.c:1292` 存在但无任何调用路径（`gui.c:784-785/786`）；`ui/resource.h:56` 注释已标注 |
| 按台标识生成（`gen_serial`/`gen_mac`/`GEN_IDENT`） | ⏸ 未实现 | 规划书 §3 预留、§6 Phase 3 待办；无配置键、无入口 |
| 独立“写标签”操作 | ⏸ 未实现 | 仅格式化完成后按 `VOLUME` 回写卷标（`ops.c:931-941`）；无独立按钮/操作 |
| `READPATTERN=REGEX` | ⏸ 未实现（配置后读取步骤判失败，不静默降级） | `ops.c:571-574` |
| `WRITEMODE=PROCESSED` | ⏸ 未实现（配置后复制步骤判失败） | `ops.c:269-272` |
| `HASH` 非 `md5` | ⏸ 未实现（配置后复制步骤判失败，不裸复制） | `ops.c:282-288` |
| `ADMINPASS_HASH` | ⏸ 未实现 | 仅 `ADMINPASS` 明文（`gui.c:1574`） |
| `BTN_CONFIG` 按钮显示名 | ⏸ 未实现 | `refresh_buttons` 表中 `IDC_BTN_CONFIG` 的 key 为 NULL（`gui.c:227`） |
| 配置窗口自身 DPI 重排 | ⚠️ 限制 | 创建时按当前 DPI 缩放；`WM_DPICHANGED` 仅主窗口处理，配置窗口移动跨 DPI 显示器时不重排 |
| 标签行显示 USB VID/PID/SN | ⚠️ 已调整 | UI 线程只用 `dev_get_info_volume`（不跑 SetupAPI），标签行只显示卷标；USB 身份由任务线程采集用于身份复核 |
| 多盘并行 / 失败策略 stop-continue-retry / CSV 导出 / Type-C 正反插 / WM_DEVICECHANGE / 设备白名单 / 自定义热键 | ⏸ 待办 | 规划书 §3 预留、§6 Phase 3；本版均未实现 |
| 软件版本 / 配置文件版本 | ℹ️ 约定（非预留） | 软件版本=`src/version.h` `APP_VERSION`（1.2.0），显示于 `--version`/启动日志/标题“（软件 v1.2.0）”；配置文件版本=ini `[TOOLSINFO] VERSION`（当前 1.0.0），显示于标题 `v<...>` 与 `--check-ini`；两者独立 | `src/version.h`、`gui.c:577-588`、`main.c:159-218` |
| `WM_APP_MODE` | 🚫 废弃 | 仅保留消息号（`gui.c:2136-2138`） |
| `IDC_STC_MODE` / `IDC_EDIT_CFGKEY` | 🚫 废弃 | `ui/resource.h:23`、`:57` 仅注释，未创建控件 |

---

## 8. 文档依据与验证摘要

**文档依据**
- [`配置文件使用手册.md`](配置文件使用手册.md)：§3.1 功能开关、§3.2 按钮对照、§8 状态行/监控/配置窗口/§8.1 键值编辑、§9 安全说明、§10 常见错误。
- [`UdiskTools软件规划书.md`](UdiskTools软件规划书.md)：§4.2 整任务编排、§4.3 操作引擎、§4.5 界面分区概览（逐控件状态以本文为准）、§4.6 日志/CLI、§4.7 DPI、§8 验收标准、§10 已确认决议。
- [`插拔全流程图.html`](插拔全流程图.html)：插盘→AUTO/MANUAL→整任务四步→结果显示→拔出复位的判定顺序。
- [`../README.md`](../README.md)（仓库根）：功能状态表、CLI 表；总索引 [`README.md`](README.md)。

**验证（C3）**
1. 三份文档已无“连错 3 次锁定/连续 3 次密码错误”等承诺（grep 结果见 `audit_scratch/impl-batch6c/C1_check.txt`；代码 `src/gui.c` 无 `g_cfg_failcount`）。
2. 本文所有 `IDC_`/`ID_HOTKEY_` 与 `ui/resource.h` 一一对应（附录 A 覆盖 53 个 `#define` 中的全部 UI ID）；配置窗口 4 个装饰标签无 ID 已单独说明。
3. 与最终代码核对点：Ctrl+Alt+T/C 注册参数（`gui.c:1740-1743`）、无密码锁定（`gui.c:1582-1589`）、`IDC_EDIT_IDENT` 预留（`gui.c:1814`、`728-729/786`）、状态复位（`gui.c:560-570`、`2158-2242`）、进度统一（`gui.c:337-348`）、最小尺寸（`gui.c:1836-1843`）、RichEdit 回退（`gui.c:2310-2328`）。

---

## 附录 A　`ui/resource.h` 全部 ID 覆盖对照（53 个）

| 类别 | ID | 本文位置 |
|---|---|---|
| 图标 | `IDI_APP_ICON 101` | §5 |
| 窗口 | `WND_MAIN 100`、`WND_CONFIG 101` | §1/§3（代码用类名 `UDK_MAINCLASS`/`UDK_CFGCLASS`） |
| 主窗口控件（35） | 1001、1002、1003、1004、1005、1006、1007、1008、1009、1010、1011、1012、1013、1014、1015、1016、1017、1018、1101、1102、1103、1104、1105、1106、1107、1108、1109、1201、1202、1301、1401、1402、1501、1502、1503 | §1.1–§1.6 |
| 热键（2） | `ID_HOTKEY_T 2001`、`ID_HOTKEY_C 2002` | §2 |
| 配置窗口控件（13） | 3001、3002、3003、3004、3005、3006、3007、3009、3010、3011、3012、3013、3014 | §3.1 |
| 已废弃（注释） | `IDC_STC_MODE`、`IDC_EDIT_CFGKEY` | §7 |

## 附录 B　维护提示（软件 v1.2.0，2026-10-24 工作树）

- 本文件中的行号基于 2026-10-24 工作树；修改 `src/gui.c`/`ui/resource.h` 后请同步核对本文（尤其控件表与“预留/限制”清单）。
- 若后续启用 CMD-B / 标识生成、独立写标签、REGEX、PROCESSED 等预留项，请同步更新 §1.5、§7 及 [`配置文件使用手册.md`](配置文件使用手册.md)、[`UdiskTools软件规划书.md`](UdiskTools软件规划书.md)。
- 用户明确要求：**不实现密码连错 3 次锁定**；如未来增加限制，必须先获得用户确认并同步本文件与手册 §9。
