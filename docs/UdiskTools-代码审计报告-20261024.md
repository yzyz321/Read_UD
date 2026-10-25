# UdiskTools 代码审计与修复跟踪报告

| 项 | 内容 |
|---|---|
| 审计对象 | `D:\workbuddy\WR_Udisk\udisktools`（C11 + Win32，约 3,100 行；gui.c / ops.c / config.c / util.c / device.c / md5.c / pattern.c / main.c） |
| 审计版本 | v1.0（2026-10-24，git `f1d6f82`，工作区原有 `profiles/UdiskTools.ini` 未提交改动） |
| 本版 | **v1.8 功能优化跟踪版（2026-10-24）**：在第一至七批修复基础上，追加用户功能优化（软件版本 1.1.0、标题同时显示配置版本与软件版本、AUTO_COPY/AUTO_TEST 灰只读状态框、AUTO_TEST=1 时 READRULE 标签栏）；缺陷计数不变（✅57/🟡7/❌13）。按用户要求**不做密码 3 次锁定**、IDENT 1402 预留；本副本为仓库内正式报告（docs/ 根），现行文档索引见 docs/README.md |
| 状态图例 | ✅ 已完善修复　🟡 部分修复（写明“已完成 / 残余”）　❌ 未修复 |
| 审计方式 | 只读静态审计 + 编译取证 + 隔离复现 + 运行日志取证；修复阶段在工程外 `audit_scratch\` 做隔离验证，未对真实 U 盘做写/格式化 |
| 工具链 | llvm-mingw（clang 23 / UCRT）与 MinGW-w64 gcc，`-std=c11 -Wall -Wextra -Werror -DUNICODE -D_WIN32_WINNT=0x0605`（与工程 Makefile/CI 一致） |
| 报告位置 | 仓库内 `docs/UdiskTools-代码审计报告-20261024.md`（2026-10-24 文档结构扁平化后自 docs/audit/ 上移；历史章节中的 `docs/...`、`audit_scratch/...` 为审计时路径；现行文档索引见 `docs/README.md`） |

**结论先行**：审计共识别核心缺陷 **77 条**（P0×3、P1×18、P2×26、P3×10）+ 逻辑关系矛盾 **20 条** + 工程性问题 12 项 + 文档偏差 16 项。
七批修复后：**✅ 57 条 / 🟡 7 条 / ❌ 13 条**（核心发现口径）。第一至六批完成配置/结果语义/设备/USB/校验/并发/日志/CLI/资源/UI 状态一致性（含热键、无密码锁定、IDENT 预留）；第七批完成文档与目录结构（docs 按 user/reference/design/development/audit 分区、docs 索引/开发流程/CHANGELOG）、测试入库（tests/unit+integration+CLI，run_tests.ps1）、CI 测试与 CLI 冒烟门禁、构建元数据（APP_BUILD_DATE/TIME）。核心 P1 已全部修复。

---

## 0. 修复状态总览（截至 2026-10-24 第二批）

### 0.1 风险概览与当前状态

| 维度 | 审计结论 | 当前状态 | 说明 |
|---|---|---|---|
| 配置回写（保存/恢复默认） | 保存必崩或毁文件 | ✅ 已修复 | cfg_save_key 重写：无崩溃/截断/CR 累积、保编码/BOM/行尾/注释、正确插入节、原子写+回读校验 |
| 破坏性操作（格式化） | 可能作用于非目标磁盘 | 🟡 部分修复 | 已做事件盘符 + DEVSIG 身份基线/复核 + 强制确认 + 写文件逐盘卷序列号复核；残余：手动单操作仍取“第一个可移动盘符”、确认框仍在工作线程、无卷就绪等待 |
| 设备事件与多盘 | 同周期换盘丢事件、任务中插入被吞/清输出 | 🟡 部分修复 | 已修：事件改为“变化盘符位图+插入/拔出”（同周期先拔后插不丢）、自动触发用事件盘符、任务中插入记待处理并在成功后补跑、运行中插入不清输出；残余：多盘同时插入只处理第一个、任务失败/停止不补跑（设计取舍） |
| 任务中断（停止/退出） | 停止按钮不可用、退出强杀线程 | 🟡 部分修复 | 主体已修：busy 态即时刷新、STOP 可用、句柄 Join、退出先停止再等待 ≤30s、CopyFileEx pbCancel；残余：MD5 整读无取消点、30s 超时后强退、轮询线程 Join 未改 |
| 校验判定（READRULE / MD5 / rwtest） | 规则链失效、未知规则放行、rwtest 缓存假阳性 | ✅ 已修复 | 已修：READRULE 分号链、pattern `*`、未知规则/REGEX 显式失败、HASH 非 md5/MD5 无法计算判失败、读中换盘判定作废；rwtest 改 NO_BUFFERING/WRITE_THROUGH 直读介质；MD5 读错误检测 + 不一致重试一次并删坏副本 |
| USB 通讯模式识别 | 实现无效（79/79 未知） | ✅ 已修复 | 重写为 USB Hub IOCTL 按 VID/PID 匹配端口识别速度（Low/Full/High/Super/Super+）；修复 device.c 的 `%hs` 与“取上级 Hub VID/PID”两个上游缺陷；真实 F: 实测 USB 2.0 High-Speed |
| 线程与并发 | 无锁替换 g_cfg、无单实例 | ✅ 已修复 | 第 4 批：任务配置快照消除 g_cfg 并发；第 5 批：单实例互斥 + 激活现有窗口、轮询线程停止事件+5s Join（超时保留句柄防 use-after-close）、日志暂停句柄共享 READ/WRITE/DELETE 且不静默丢行、UI 线程不再跑 SetupAPI |
| UI 状态一致性 | 判定/内容/进度/标题/配置窗/最小尺寸/设备行/RichEdit/STATIC 色/热键不一致 | ✅ 已修复 | 第 6 批：统一 reset_task_ui、post_progress_pct 单入口、单操作完成文案、标题 1200、配置窗初始机型/DPI/字体、WM_GETMINMAXINFO、RICHEDIT20W 回退、设备行 8192 有界、WM_CTLCOLORSTATIC、Ctrl+Alt+T/C 单操作热键；配置窗跨屏 DPI 不重排为已知限制 |
| 工程化（测试/CI/文档/流程） | 薄弱 | 🟡 部分改善 | 第 7 批：测试已入库 `tests/`（7 组 C 用例 + 4 条 CLI，clang/gcc 均 PASS=11）、CI 增加 Run tests + CLI 冒烟、docs 目录化并新增索引/开发流程/CHANGELOG、`docs/UI功能状态说明.md` 就位、构建元数据 APP_BUILD_*；残余：CI 未做静态分析/ASan，测试未覆盖真实 U 盘/RAW/写保护等物理场景 |

### 0.2 “最需要立刻处理的 5 件事”状态

| # | 事项 | 状态 | 说明 |
|---|---|---|---|
| 1 | 修 `cfg_save_key`（崩溃/毁文件/CR 累积） | ✅ 已修复 | 见 P0-1；旧实现 `0xC0000374` 已复现，新实现 61+20 项验收全过 |
| 2 | 格式化身份复核 + 事件盘符 + 强制确认 | 🟡 部分修复 | 见 P0-2；核心风险已消除，残余见该条 |
| 3 | 修复停止链路（refresh_buttons/Join/退出等待/pbCancel） | 🟡 部分修复（主体完成） | 见 P0-3；MD5 取消点、30s 强退、轮询 Join 为残余 |
| 4 | 统一“成功/跳过/停止”语义 | ✅ 已修复（第 2 批） | 见 P1-6：配置正常屏蔽→成功并计数；程序自行降级/未实现/无法校验→失败；不再用 `rc \|=` |
| 5 | 修 READRULE 语法冲突与 pattern `*` | ✅ 已修复 | 见 P1-7、P1-9；端到端 `op_read` 验证多规则生效 |

### 0.3 状态统计

| 分组 | 总数 | ✅ 已修复 | 🟡 部分修复 | ❌ 未修复 |
|---|---|---|---|---|
| P0（上线阻断） | 3 | 1 | 2 | 0 |
| P1（严重） | 18 | 18 | 0 | 0 |
| P2（健壮性/一致性） | 26 | 18 | 2 | 6 |
| P3（规范/细节） | 10 | 6 | 2 | 2 |
| 逻辑关系 L1–L20 | 20 | 14 | 1 | 5 |
| **核心合计** | **77** | **57** | **7** | **13** |
| 工程性问题 ENG-01..12 | 12 | 4 | 4 | 4 |
| 文档偏差 DOC-01..16 | 16 | 12 | 1 | 3 |

### 0.4 ✅ 已完善修复清单（截至第七批）

| ID | 标题 | 修复位置 | 验证 |
|---|---|---|---|
| P0-1 | 配置窗口保存必崩/毁 ini | [config.c:607](udisktools/src/config.c:607) `cfg_save_key` 重写；注释 helper [config.c:64](udisktools/src/config.c:64) | 合成 ini 61 项 + 真实出厂 ini 20 项 + 5 次连保稳定 + 4 编码往返 + ASan 0 报告；旧实现同 harness `0xC0000374` |
| P1-1 | 保存破坏配置格式（CR 累积/改编码/丢属性/值不可 round-trip） | 同上 | 逐字节对比：仅目标行变化；UTF-8±BOM/UTF-16LE/GBK 保持；LF/CRLF/混合行尾稳定；只读/属性/时间戳用例通过 |
| P1-4 | 设备事件协议（同 tick 拔插丢事件/任务中插入被吞） | [device.c:234](udisktools/src/device.c:234) `dev_post_changes`；[gui.c:1493](udisktools/src/gui.c:1493) WM_APP_DEVCOUNT 重写 + pending | 隔离消息协议用例：同 tick“拔 F+插 G”两条消息按序、位图正确；无变化不发；插入运行中不清屏/待处理逻辑代码审查 |
| P1-5 | 任务中拔盘/换盘不中止、不校验身份 | [ops.c:98](udisktools/src/ops.c:98) 设备丢失错误类；[ops.c:447](udisktools/src/ops.c:447) op_read 前后 DEVSIG 复核 | 错误码分类 + 读中换盘“判定作废”实现；独立测试覆盖 op_read 身份复核路径（无盘环境以代码审查+错误码用例为主） |
| P1-6 | “成功”语义（跳过/未执行校验当成功、`rc \|=`） | [ops.h:26](udisktools/src/ops.h:26) OP_* 结果码；[gui.c:447](udisktools/src/gui.c:447) task_account/聚合 | 独立验收 35 项：配置屏蔽→OP_SKIP_CFG、程序降级→OP_FAIL、任务聚合按 fail/stop/skip/exec 归并；团队 28 项同结论 |
| P1-7 | READRULE 分号规则链被注释规则截断 | [config.c:64](udisktools/src/config.c:64) + `cfg_load`/`cfg_save_key` 共用 | `READRULE=MUST_CONTAIN:8685;MIN_LEN:10` 完整载入；端到端 `op_read`：短内容按 MIN_LEN FAIL、合规内容 PASS；旧/新 `cfg_load` 对真实 profiles 输出完全一致 |
| P1-8 | 未知 READRULE 规则静默 PASS | [ops.c:409](udisktools/src/ops.c:409) `apply_rules` 三态 | 未知规则（如 `MINLEN:10`）→OP_FAIL + 投递 FAIL/ERROR；空规则/空 token→PASS；独立 e2e 通过 |
| P1-9 | pattern `*` 回溯上界错误 + 大小写敏感 + 越界读 | [pattern.c:42](udisktools/src/pattern.c:42)、[pattern.c:25](udisktools/src/pattern.c:25) | 37/37 用例通过；ASan 0 报告（旧实现 ASan 报 `pattern.c:44` 越界） |
| P1-10 | USB 通讯模式识别无效（PropertyId/结构/协议类型三重错误） | [usbspeed.c:134](udisktools/src/usbspeed.c:134) 新模块；[ops.c:952](udisktools/src/ops.c:952)；修 [device.c:111](udisktools/src/device.c:111) `%hs` 与父链取错 Hub 的 VID/PID | 真实 F: 只读 e2e：VID 058F/6387 → speed=3、日志“USB 2.0 High-Speed”；C:/非 USB/非法 VID/不存在 VID → FALSE+原因；同 VID/PID 多命中明确“无法唯一匹配” |
| P1-11 | rwtest 结论受系统缓存影响（假阳性） | [ops.c:1064](udisktools/src/ops.c:1064)（`:1181` 日志注明直读/缓冲） | subst 临时盘 OP_OK，日志“NO_BUFFERING/WRITE_THROUGH 直读介质，未走缓存”，测试文件已删除；回退缓冲分支代码审查 |
| P1-12 | MD5/HASH 完整性语义被削弱 | [util.c:288](udisktools/src/util.c:288) 读错误必 FALSE；op_write MD5 重试/删坏副本 | “abc”MD5 向量、outlen<33、独占锁定→FALSE；不一致→删目标+重试一次→成功/仍不一致 FAIL 且坏副本删除（故障注入 11 项）；MD5 无法计算→FAIL+删除 |
| P1-18 | 格式化后状态不刷新、卷标无校验 | [ops.c:771](udisktools/src/ops.c:771)、[gui.h:21](udisktools/src/gui.h:21)、[gui.c:1619](udisktools/src/gui.c:1619) | 确认前校验 FILESYSTEM/VOLUME（非法值 OP_FAIL、无弹窗/无 FormatEx）+ 进度归零 + 成功后刷新/`WM_APP_DEVREFRESH`；成功路径因禁止格式化真实盘仅代码审查+编译 |
| P2-24 | label_read/usbinfo 对异常盘仍报成功 | [ops.c:1002](udisktools/src/ops.c:1002)、[ops.c:952](udisktools/src/ops.c:952) | 真实 F: label/usbinfo → OP_OK；不存在的 Q: → OP_FAIL（“已移除/不可访问/无法读取卷信息”） |
| P1-2 | 编码探测失败时整文件静默乱码并可保存固化 | [util.c:232](udisktools/src/util.c:232) `text_decode`；[config.c:31](udisktools/src/config.c:31) read_ini_text | 编码矩阵（UTF-8±BOM/UTF-16LE/BE±BOM/无 BOM 启发式/GBK/损坏 UTF-8/二进制/空/奇数长度/内嵌 NUL）；损坏 UTF-8 明确 FALSE+偏移原因（ASan 0 报告）；lead 独立 71/71 |
| P1-3 | 运行中整块替换 g_cfg（无锁竞争） | [gui.c:429](udisktools/src/gui.c:429) TASKARG 快照、[gui.c:616](udisktools/src/gui.c:616) OPARG；worker 只读 `&a->model` | grep/代码审查：task_thread/op_thread 体内 0 处 g_cfg；快照在 UI 线程 CreateThread 前完成；逐值拷贝语义测试 |
| P1-13 | 内嵌默认配置与发布 ini 漂移 | [default_ini.h](udisktools/src/default_ini.h) 重新生成；[gen_default_ini.ps1](udisktools/scripts/gen_default_ini.ps1) -Check；CI 步骤 | 与 profiles 归一化内容逐字节一致（lead 独立解析 71/71 含此项）；-Check exit 0，篡改可检测；PS 与 Python 生成器逐字节一致 |
| P1-15 | reload_config_and_ui 906KB 栈帧 | [gui.c:1068](udisktools/src/gui.c:1068) CONFIG 改堆分配 | `-fstack-usage`：906,616 B → **328 B**；失败路径返回 FALSE 且 g_cfg 保持旧值 |
| P1-16 | 保存/恢复配置失败路径假报成功 | [gui.c:1112](udisktools/src/gui.c:1112) write_default_ini；保存/恢复流程检查 reload | 默认写入成功逐字节校验+.bak、失败原 ini 不变；保存/恢复 reload 失败时 ERR+弹窗、不再记“已保存（配置已重载）” |
| P2-13 | 解析容量/畸形/重复节静默 | [config.c:603](udisktools/src/config.c:603) cfg_load | 17→16/50→48/80 字符节名/`[BAD`/`[]`/重复合并/截断均 WARN；lead 独立用例 71/71 中解析组通过 |
| P2-14 | 编码识别不全（UTF-16BE/无 BOM） | 见 P1-2 `text_decode` | UTF-16BE±BOM、无 BOM LE/BE 启发式加载/保存保 BOM 通过 |
| P2-15 | read_file_text 边界 | [util.c:529](udisktools/src/util.c:529) | 空文件 TRUE 空串、UTF-16 奇数 FALSE+INVALID_DATA、>64MB FILE_TOO_LARGE、损坏编码 INVALID_DATA |
| P2-17 | read_ini_text 16MB/共享模式/ReadFile | [config.c:31](udisktools/src/config.c:31) | 共享 READ/WRITE/DELETE、ReadFile 检查、>16MB 具体原因；真实 profiles 零 WARN |
| L13 | 运行中可切换机型（配置撕裂） | 见 P1-3 | worker 只读快照，运行中改配置不影响已启动任务 |
| L14 | 保存成功→主界面刷新 | [gui.c:418](udisktools/src/gui.c:418) update_main_title + cfg_rebuild_model_list | 保存/恢复/重载后刷新主标题、机型下拉、配置窗口列表；reload 失败提示并恢复原选中 |
| P1-14 | 单实例/轮询 Join/日志共享残余 | [main.c:285](udisktools/src/main.c:285) 单实例；[device.c:322](udisktools/src/device.c:322) 轮询事件/Join；[util.c:587](udisktools/src/util.c:587) 日志缓存共享 | 第二实例 56ms 内 exit 0+日志；poll stop 3 轮 <3s；日志 4 线程 200 行完整、独占时 alt 兜底；UI 不再 SetupAPI |
| P2-9 | PostMessage 失败泄漏 | [util.c:672](udisktools/src/util.c:672) `post_owned_string`；gui.c/ops.c 全部替换 | gui/ops 0 处 wcs_dup；失败自释放；ASan 18 项无 double-free/UAF |
| P2-23 | 日志 CS/UI 同步写/UI SetupAPI | [util.c:436](udisktools/src/util.c:436) INIT_ONCE+句柄缓存；[gui.c:375](udisktools/src/gui.c:375) volume helper | 4 线程日志完整、>8MB 轮转、alt 兜底、shutdown；UI 线程 0 处 dev_get_info |
| P3-5 | 无 .rc/manifest/图标/版本/崩溃转储/CLI/日志轮转 | [res/udisktools.rc](udisktools/res/udisktools.rc)、[main.c:200](udisktools/src/main.c:200)、[Makefile](udisktools/Makefile) | VersionInfo 六字段正确、图标/manifest 资源存在；CLI --version/--help/--check-ini/--ini 实测；崩溃 dump 真实触发生成；日志轮转实测 |
| P3-6 | 退出码不可靠 | [gui.c:2045](udisktools/src/gui.c:2045) GetMessage 检查 + exit code；[main.c:285](udisktools/src/main.c:285) | GetMessage -1→exit 1；强制退出→3；正常 WM_CLOSE 实测 exit 0 且日志 rc=0；CLI 0/1 正确 |
| L20 | 切换机型与 worker 并发（配置撕裂） | 见 P1-3（第 4 批快照） | worker 只读快照，reload 与任务并发不再撕裂 |
| P2-1 | 判定/内容/进度/监控状态不复位 | [gui.c:510](udisktools/src/gui.c:510) `reset_task_ui` | 插入/任务开始/结束/拔出全路径接入；ASan harness 27/27（reset/进度/标题/设备行） |
| P2-2 | 进度两套口径 | [gui.c:331](udisktools/src/gui.c:331) `post_progress_pct` | PBM_SETPOS/百分比文本唯一入口；-7/0/55/100/150→0/0/55/100/100 实测 |
| P2-3 | 单操作也显示“任务完成” | [gui.c:732](udisktools/src/gui.c:732) `op_display_name` + `g_single_op` | 单操作显示“复制完成/格式化完成/…”；整任务仍“任务完成” |
| P2-4 | 标题截断 | [gui.c:525](udisktools/src/gui.c:525) 标题/徽标缓冲 1200/1220 | 500+500 字符实测 1002 不截断 |
| P2-5 | 配置窗口初始机型/枚举/DPI/悬空字体 | [gui.c:1440](udisktools/src/gui.c:1440) `cfg_current_model_sel`；[gui.c:1184](udisktools/src/gui.c:1184) 锁定态 | 初始选中当前机型；非法枚举回退+提示；输入限长；字体先替换后删 |
| P2-6 | 无最小窗口尺寸 | [gui.c:1763](udisktools/src/gui.c:1763) WM_GETMINMAXINFO | 96DPI=880×600、192DPI=1760×1200 实测 |
| P2-7 | RichEdit 回退无效 | [gui.c:2233](udisktools/src/gui.c:2233) 三级回退 | Msftedit→RICHEDIT20W→STATIC，控件统一 g_riched_class |
| P2-8 | 多盘设备行栈溢出 | [gui.c:460](udisktools/src/gui.c:460) 8192 有界追加 | 26 盘×127 字符卷标 ASan 无越界（设备行 3389/标签行 3169） |
| P2-16 | 捕获容量不足静默截断 | [pattern.h:29](udisktools/src/pattern.h:29) `pat_match_capture_ex`；[ops.c:659](udisktools/src/ops.c:659) | 截断显式 TRUE；op_read WARN“捕获超长已截断”；旧 API 兼容 |
| P2-26 | 配置窗口保护弱（按用户要求不做锁定） | [gui.c:1516](udisktools/src/gui.c:1516) | 删除 failcount/3 次锁定；密码错误 WARN（不含密码）、可无限重试；未解锁禁用[保存]/[恢复默认]；关窗回只读 |
| P3-1 | STATIC 设色无效 | [gui.c:1772](udisktools/src/gui.c:1772) WM_CTLCOLORSTATIC | SetTextColor/SetBkColor+背景刷；实测文字色生效 |
| P3-3 | 无匹配文件先建目标目录 | [ops.c:360](udisktools/src/ops.c:360) | 先匹配后建目录；空/无匹配/源不存在→OP_FAIL 且不建目录（实测） |
| P3-4 | ${MATCH_1} 捕获截断静默 | 同 P2-16 | 显式截断+WARN，不再静默 |
| P1-17 | 热键无修饰键/语义矛盾/注册失败假成功 | [gui.c:1662](udisktools/src/gui.c:1662) `hotkeys_enable`；[gui.c:1920](udisktools/src/gui.c:1920) WM_HOTKEY | Ctrl+Alt+T=读写测试、Ctrl+Alt+C=复制（单操作）；任一失败回滚+WARN；退出注销；resource 注释同步 |
| L9 | 判定徽标复用/插盘不清 PASS/FAIL | 见 P2-1/P2-3 | 插入/任务开始统一清判定；单操作用操作名徽标 |
| L16 | 错误徽标/状态复位不一致 | 见 P2-1 | 复位判定/内容/命中行/进度/监控状态；拔出同步 |
| L17 | CMD-B 无入口 | [resource.h:56](udisktools/ui/resource.h:56) | 按用户要求 `IDC_EDIT_IDENT 1402` 预留（标识生成扩展），无入口/不参与任务；文档/UI 状态已标注 |
| P2-18 | cfg_save_key 的 tmp 竞争/原子性 | [config.c:836](udisktools/src/config.c:836) 起 | PID 唯一 tmp、WriteFile 循环校验、FlushFileBuffers、`MOVEFILE_WRITE_THROUGH`、失败删 tmp；只读用例通过 |
| P3-2 | rc 位或/结果码语义（旧 rc=3 歧义、停止误报成功） | [gui.c:447](udisktools/src/gui.c:447)、[ops.h:26](udisktools/src/ops.h:26) | 结果码明确为 OK/FAIL/STOPPED/SKIP_CFG，停止与错误分离；不再用 `rc \|=`；停止早于首步→OP_STOPPED |
| L1 | ENABLE_* 屏蔽=成功、程序降级=失败 | 见 P1-6（按用户政策实现） | 配置屏蔽计数为成功；PROCESSED/REGEX/HASH 非 md5/MD5 算不出/未知规则→失败 |
| L2 | 插入的盘触发任务 | [gui.c:1540](udisktools/src/gui.c:1540) 传事件盘符；`task_run_async` drive 参数 | 代码审查（无移动盘，未真机多盘） |
| L3 | 轮询发现拔出+插入事件不丢 | [device.c:234](udisktools/src/device.c:234) | 隔离消息协议用例（同 tick 先拔后插） |
| L4 | 任务中按停止中止 | 见 P0-3 | 同 P0-3 |
| L5 | 成功/停止/失败/按配置跳过四类结果 | [ops.h:26](udisktools/src/ops.h:26) + 任务聚合 | 独立验收 + 代码审查 |
| L6 | 停止=中止当前任务 | [gui.c:455](udisktools/src/gui.c:455) 首步前检查 + OP_STOPPED | 隔离测试 + 代码审查 |
| L7 | READRULE 分号链 | 见 P1-7 | 同 P1-7 |
| L8 | 未知规则应报错 | 见 P1-8 | 同 P1-8 |

### 0.5 🟡 部分修复清单（已完成 / 残余）

| ID | 已完成 | 残余（下一批可继续） |
|---|---|---|
| P0-2 | 自动触发用事件盘符；任务级 DEVSIG 基线+每步复核；格式化强制确认+确认后复核；`op_write` 每文件卷序列号复核；`op_rwtest` 进入/写前/读前复核；第 5 批 `dev_get_info` 对不存在盘根返回 FALSE | 手动单操作仍取“第一个可移动盘符”（无设备选择器）；格式化确认仍由工作线程弹 MessageBox；无“卷未就绪”等待与重试 |
| P0-3 | busy 即时刷新（STOP 可用）；`g_worker` 句柄 Join；WM_CLOSE 先停止并消息泵等待 ≤30s；CopyFileEx `pbCancel`；停止标志统一；停止后首步 → OP_STOPPED；第 5 批轮询线程停止事件+5s Join | MD5 整读（`sha_or_md5_file`）无停止检查点；30s 超时后强制退出（exit 3） |
| L15 | 设备信息用于显示与任务（第 5 批改善） | `dev_get_info` 对不存在盘根已返回 FALSE；UI 线程改用 `dev_get_info_volume`（不跑 SetupAPI）；RAW 盘根存在仍可采集 | 无“卷未就绪”等待/重试；0.0GB/无卷标时的写失败保护仍未做 |
| P2-10 | `task_run_async` malloc 失败复位 g_running/停止标志并刷新 UI；退出/销毁路径兜底复位；第 5 批 `post_owned_string` 检查投递失败并自释放 | TASKDONE 消息丢失仍只靠退出兜底 |
| P2-12 | 格式化/复制/rwtest 错误带 GetLastError 并区分设备丢失/写保护/空间不足；op_read 读失败区分设备移除 | 读取/标签等错误仍缺盘符/路径/键上下文；部分 GetLastError 仍被后续调用覆盖 |
| P3-8 | FormatEx 前检查停止；格式化失败带错误码；设备丢失分类 | MediaType 仍固定 `0x0C`；fmifs 回调永远 TRUE（格式化中不可中止）；成功后无读回校验；FAT32 容量未预检 |
| P3-7 | 第 7 批新增 APP_BUILD_DATE/TIME，`--version` 与启动日志显示构建日期/时间 | VERSIONINFO 仍固定 1.0.0.0、未引入构建号/短 hash；版本语义仍 1.0.0 |

| ENG-11 | 文档已系统纠偏（标签读取/CMD-B 预留/无锁定/热键/UI 状态/目录化）；第 6-7 批同步 README/手册/规划书/UI 状态/CHANGELOG/开发流程 | 剩余 `PRJNUM`/`ADMINPASS_HASH`/`BTN_CONFIG` 等历史描述仍与实现不符（DOC-12..14） |
| ENG-08 | `config.c` 已重写并补齐离线验证 | 尚未把用例入库/接入 CI；`config.c` 之外仍无回归覆盖 |

### 0.6 ❌ 未修复清单（按优先级，供分批规划）

**P1（0 条）**：无（P1-17 已按用户要求修复）。

**P2（6 条）**：P2-11 写操作返回值不检查；P2-19 exe_dir；P2-20 path_join；P2-21 util_ensure_dirs；P2-22 wcs_trim/wcs_dup/wide_to_utf8/长日志丢弃；P2-25 op_write hits16/大文件投递。

**P3（2 条）**：P3-9 gen_ident 演示（用户已确认 IDENT 预留，仍无规则）；P3-10 重复代码/魔法数/常量双份定义。

**逻辑关系（5 条）**：L10、L11、L12、L18、L19（见第 3 节）。

### 0.7 建议的分批处理计划（依据本报告）

| 批次 | 主题 | 建议包含 | 状态/理由 |
|---|---|---|---|
| 第 1 批 | 配置保存 / 格式化 / 停止链路 / READRULE+pattern | 修复项 1、2、3、5 | ✅ 已完成（2026-10-24） |
| 第 2 批 | 任务结果与事件语义 | P1-6（item 4）、P1-4、P1-5、P1-8、P3-2、L1/L3/L5/L8 | ✅ 已完成（2026-10-24） |
| 第 3 批 | 设备与校验可信度 | P1-10（USB 通讯模式）、P1-11（rwtest 缓存）、P1-12 残余（MD5 重试/删除/读错误）、P1-18（格式化后刷新/卷标校验）、P2-24 | ✅ 已完成（2026-10-24） |
| 第 4 批 | 配置与并发健壮性 | P1-2、P1-3、P1-13、P1-15、P1-16、P2-13..P2-17、L13/L14 | ✅ 已完成（2026-10-24） |
| 第 5 批 | 线程/日志/单实例 | P1-14、P2-9、P2-23、P3-5、P3-6、L20 | ✅ 已完成（2026-10-24） |
| 第 6 批 | UI 状态一致性 | P2-1、P2-2、P2-3、P2-4..P2-8、P2-26、P3-1、P3-3、P3-4、P2-16 残余、P1-17 | ✅ 已完成（2026-10-24） |
| 第 7 批 | 工程化/文档与目录 | tests 入库 + run_tests.ps1、Makefile test、CI Run tests+CLI 冒烟、docs 目录化（索引/开发流程/CHANGELOG/audit）、构建元数据 | ✅ 已完成（2026-10-24）；后续为维护/增强项 |

---
## 1. P0（上线阻断）

### P0-1 配置窗口“保存”必崩或毁 ini —— ✅ 已完善修复

**位置（修复前）**：`src/config.c:212-301`（关键 `:263`、`:266`、`:274`）；调用点 `src/gui.c:1052-1057`
**修复后**：`src/config.c:607-908`（全文重写），注释 helper `src/config.c:64`

**原缺陷**
```c
olen += _snwprintf(out + olen, 32, ...);   // 写死 32
olen += _snwprintf(out + olen, 64, ...);   // 写死 64
olen += _snwprintf(out + olen, 64, ...);   // 新键追加也写死 64
```
- `_snwprintf` 截断返回 -1，`olen` 是 `size_t` → 回绕 `SIZE_MAX` → `out-1` 写堆，`free` 时 `0xC0000374`；出厂 ini 第一行 62 字符注释即触发。
- 短行 ini 则截断/丢键/行粘连（83 字节文件保存后 47 字节）却返回 TRUE。
- 键不存在时追加到文件末尾（落进最后一个机型节）。
- 写 tmp 不检查 WriteFile/无 flush/无 write-through。

**修复内容（本轮）**
1. 动态 `WBUF` 拼接，无截断；任何失败返回 FALSE。
2. 逐行保留原行尾（不再 `\r\r\n` 累积）；连续保存字节稳定。
3. 键不存在插入目标机型节节尾；节不存在返回 FALSE。
4. 保留原编码/BOM（UTF-8 有无 BOM / UTF-16LE+BOM / ANSI-GBK）、属性、创建/访问/修改时间。
5. 保留原 key 拼写/前导空白/行内注释（含注释前空白）；值允许 `;`。
6. PID 唯一 tmp + WriteFile 字节数校验 + FlushFileBuffers + `MoveFileExW(REPLACE_EXISTING|WRITE_THROUGH)`；失败删 tmp、原文件不变；只读文件直接 FALSE。
7. 写后 `cfg_load` 回读校验（目标键==新值、键数符合预期），失败 FALSE。
8. 写前拒绝会破坏结构/无法回读的输入（`\r\n`、首尾空白、超长、`;` 前带空格、key 含 `=`/`[`/`]`）。

**证据**
- 旧实现同 harness：`0xC0000374`（STATUS_HEAP_CORRUPTION），留下 3220 字节 tmp。
- 新实现：合成 ini 61/61、真实出厂 ini 副本 20/20、连续保存 5 次字节稳定、4 编码逐字节保持、只读/属性/时间戳用例通过；团队 176/176（ASan 版同样 176/176，0 报告）。

### P0-2 格式化可能作用于非目标磁盘 —— 🟡 部分修复

**位置**：`src/ops.c:663`（格式化确认/复核）、`src/ops.c:193`（写入复核）、`src/gui.c:426`（任务级守卫）、`src/device.c:161`（DEVSIG）、`src/gui.c:1540`（事件盘符）

**原缺陷**：盘符是唯一目标标识；`run_single(OP_FORMAT)` 取最小可移动盘符后确认 → `FormatEx`，中间不校验卷序列号/VID；多盘时自动任务作用于最小盘符；`CONFIRM=0` 无确认；确认框在工作线程。

**本轮已完成**
- 新增 `DEVSIG`（盘符+卷序列号+VID/PID+USB SN）：`dev_capture_sig` / `dev_sig_same_device` / `dev_sig_describe`。
- 自动触发把“刚插入的盘符”传给任务（不再二次枚举取最小盘符）。
- 任务启动采集基线，每一步前复核，盘变化即中止（`[GUARD] 目标盘已变化…`）。
- `op_write` 进入复核 + 每复制一个文件前用 `GetVolumeInformationW` 轻量复核卷序列号。
- `op_format`：**始终人工确认**（忽略 `CONFIRM=0`），文案含盘符/卷标/容量/文件系统/卷序列号/VID/SN；确认后再次采集并比较，不一致绝不 `FormatEx`；`FormatEx` 前检查停止；失败带 `GetLastError`；删除原 `g_stop=0`。
- `op_rwtest` 进入/写前/读前复核。

**残余（未完成）**
- 手动单操作（复制/格式化/读写测试按钮）仍取“第一个可移动盘符”，无设备选择器；多盘现场可能选错盘（自动路径已修）。
- 确认框仍由工作线程弹（owner=主窗口）；未改到 UI 线程。
- 无“卷未就绪”等待/重试；`dev_get_info` 恒 TRUE 未改。
- `op_read` 读取过程中换盘不中止；格式化进行中不可中止（物理限制）。
- 设备错误码（21/1167）未作为任务中止依据（仅靠逐文件失败/守卫）。

**建议**：增加设备选择器（或“多盘禁止自动执行”）；确认改 UI 线程；插入后延迟/重试至卷就绪；`op_read`/整任务增加设备错误分类中止。

### P0-3 任务无法安全停止：STOP 常规不可用 + 关窗强杀线程 —— 🟡 部分修复（主体完成）

**位置**：`src/gui.c:546`（task_run_async）、`src/gui.c:647`（run_single）、`src/gui.c:681`（wait_worker_exit）、`src/gui.c:1619`（TASKDONE）、`src/gui.c:1663`（WM_CLOSE）、`src/ops.c:29`（停止接口）、`src/ops.c:320`（pbCancel）

**原缺陷**：`refresh_buttons()` 只在空闲/结束时调用，任务启动后 STOP 保持禁用、其它按钮仍可点；线程句柄创建即关，退出确认后 `DestroyWindow`→进程退出强杀工作线程；`CopyFileExW` 无取消回调；`op_format` 清 `g_stop`。

**本轮已完成**
- 置 `g_running` 后、`CreateThread` 前调用 `refresh_buttons()`（STOP 可用、其它按钮禁用）；失败路径复位并刷新。
- 保存 `g_worker` 句柄；`WM_APP_TASKDONE` 内 join(2s)+CloseHandle；`WM_CLOSE` 先 `ops_request_stop()` 再用 `MsgWaitForMultipleObjects`+消息泵等待 ≤30s（`g_closing` 防重入）再销毁；`WM_DESTROY` 兜底停止+join。
- `ops_request_stop()/ops_reset_stop()` 统一管理 `g_stop`+`g_copy_cancel`；GUI 已无直接写 `g_stop`。
- `CopyFileExW(..., pbCancel)`；复制被取消（`ERROR_REQUEST_ABORTED`/CHECK_STOP）→ op_write 返回 2“已停止”。
- 停止后首步前检查 → rc=2，任务结束日志“已停止”，不再报成功。
- `[停止监控]` 在任务中同样触发停止。

**残余**
- `sha_or_md5_file` 的 MD5 整读无 CHECK_STOP（复制大文件后的校验阶段最多需等整读完成）。
- 退出等待 30s 超时后仍强制退出（已记 WARN）。
- `dev_poll_stop` 2s 超时后仍 CloseHandle，轮询线程未 Join（归 P1-14）。
- 格式化 `FormatEx` 执行中无法中止（设计限制，已文档化）。

---

## 2. P1（严重）

> 每条已标注状态；未列“本轮处理”的即为 ❌ 未修复。

### P1-1 保存会破坏配置格式（CR 累积/编码/BOM/属性/值不可 round-trip） —— ✅ 已完善修复
原：`wcstok_s(text,"\n")` 保留行尾 `\r` 再拼 `\r\n` → 每保存一次未修改行多一个 `\r`；无条件改 UTF-8+BOM；丢属性/时间戳；值两端被 trim。
现：逐行保留原行尾/编码/BOM/属性/时间戳；值含 `;` 可 round-trip；首尾空白值改为**拒绝保存**（不再静默改写）；回归见 0.4。

### P1-2 编码探测失败时整文件静默乱码，并可被保存固化 —— ✅ 已完善修复（第 4 批）

已修：新增统一 `text_decode`（[util.c:232](udisktools/src/util.c:232)）——BOM UTF-8/UTF-16LE/BE、无 BOM UTF-16 零字节启发式、严格 UTF-8、“少量非法字节的 UTF-8”明确拒绝（含偏移/0xXX，不静默 GBK）、GBK 回退、内嵌 NUL/奇数长度/孤立代理对拒绝；`utf8_to_wide` 改为严格 UTF-8；`read_ini_text`/`ini_read_source`/`read_file_text` 全部走统一探测（保存仍保留原编码/BOM，不再固化乱码）。

验证：团队编码矩阵 PASS=123（ASan 同样 123，无 Sanitizer 报告）；Lead 独立 71/71（UTF-8±BOM、UTF-16LE/BE±BOM、无 BOM LE/BE、GBK、损坏 UTF-8、二进制、空、奇数长度、内嵌 NUL）。

### P1-3 任务运行中整块替换全局配置（无锁数据竞争） —— ✅ 已完善修复（第 4 批）

已修：`TASKARG`/`OPARG` 内嵌按值 `CFGMODEL` + version（[gui.c:429](udisktools/src/gui.c:429)、[gui.c:616](udisktools/src/gui.c:616)），任务/单操作在 UI 线程 `CreateThread` 前完成快照；`task_thread`/`op_thread` 只读 `&a->model`（grep 证明函数体内 0 处 `g_cfg`）；新增机型索引越界守卫。运行中切换机型/保存配置不再造成撕裂。

残余（设计取舍）：任务运行中仍允许改配置（对下一个任务生效）。

### P1-4 设备事件协议错误：同周期拔插丢事件 + 任务中插入被吞 —— ✅ 已完善修复（第 2 批）

原：device 两条消息共享“最终 mask”，GUI 求差导致插入事件丢失；任务中插入被静默丢弃且清空运行中输出。

现：
- `WM_APP_DEVCOUNT` 改为“本次变化盘符位图 + 插入/拔出”（同一次轮询最多两条：先拔出位图、后插入位图），[device.c:234](udisktools/src/device.c:234) 新增 `dev_post_changes()`；GUI 按位增量维护 `g_dev_mask`（不再求差），[gui.c:1493](udisktools/src/gui.c:1493) 重写事件处理。
- 任务执行中插入：只记日志、不清 CMD-A/进度/文件窗格；该盘记为“待处理盘”（多个只取第一个），当前任务**成功/按配置跳过**结束后若仍处于自动模式则补跑；失败/被停止不自动重试；待处理盘拔出取消。
- 启动顺序改为先枚举 `g_dev_mask` 再启动轮询，避免首轮误报。

验证：隔离消息协议用例覆盖“同 tick 先拔后插 / 无变化 / 纯插入 / 纯拔出 / NULL hwnd”；GUI 聚合与待处理逻辑因无移动盘以代码审查为主（建议真机插拔回归一次）。

残余：多盘**同时**插入只自动处理第一个（其余不排队）；任务失败/停止后不补跑（设计取舍，避免失败循环）。

### P1-5 任务中拔盘/换盘不中止、不校验身份 —— ✅ 已完善修复（第 2 批；物理限制除外）

已完成：每步前 DEVSIG 复核；`op_write` 每文件卷序列号复核；`op_rwtest` 三个关键点复核；`op_read` 读取前后 DEVSIG 复核（读中换盘/拔出 → 判定作废、`OP_FAIL`）；新增设备丢失错误类（21/1167/1112），`op_write` 立即中止，`op_rwtest`/格式化失败按“设备已移除/未就绪”分类提示。

残余（物理/设计限制）：`FormatEx` 执行中无法中止；若换上的盘与基线“双方都无身份信息”（卷序列号/VID/SN 全缺）则无法区分；无“卷未就绪”等待（归 P0-2）。

### P1-6 “成功”语义错误：跳过/未执行校验被当成成功 —— ✅ 已完善修复（第 2 批，按用户政策）

政策：**按配置正常屏蔽的步骤跳过 → 任务仍判成功；程序为运行完整/不崩溃而自行降级、未实现或无法校验的跳过 → 不得报成功。**

实现：
- 新增结果码 `OP_OK=0 / OP_FAIL=1 / OP_STOPPED=2 / OP_SKIP_CFG=3`（[ops.h:26](udisktools/src/ops.h:26)）。
- 配置屏蔽 → `OP_SKIP_CFG`：ENABLE_READ/WRITE/FORMAT/USBINFO=0、READFILE 留空、READRULE 留空、自动流程 AUTO_COPY/AUTO_TEST=0。
- 程序降级/未实现/无法校验 → `OP_FAIL`：WRITEMODE=PROCESSED、READPATTERN=REGEX、未知 READRULE、HASH 非 md5、MD5 无法计算、读取中换盘。
- 任务聚合不再用 `rc |=`：执行/跳过计数 + 失败/停止标志；最终 fail→1、否则 stop→2、否则 exec==0&&skip>0→3、否则 0；结束日志“执行 N 步，按配置跳过 M 步”。全部配置屏蔽时界面显示“按配置跳过（未执行）”（成功类别）。

验证：独立验收 35 项 + 团队 28 项（配置屏蔽→3、程序降级→1、空规则→0、多规则 e2e、聚合逻辑代码审查）。

残余：语义按政策闭合；后续 P2-3 的“任务类型文字”仍可优化。

### P1-7 READRULE 多规则链被 ini 注释规则截断 —— ✅ 已完善修复
注释规则改为“行首或前一字符为空格/制表符的 `;` 才是注释”，`cfg_load`/`cfg_save_key` 共用 [config.c:64](udisktools/src/config.c:64)。`READRULE=MUST_CONTAIN:8685;MIN_LEN:10` 完整载入；`op_read` 端到端：短内容按 MIN_LEN FAIL、合规 PASS；旧/新 `cfg_load` 对真实 profiles 输出一致。

### P1-8 未知 READRULE 规则静默 PASS —— ✅ 已完善修复（第 2 批）

`apply_rules` 改三态：1=PASS、0=规则不满足、-1=未识别 token；未知规则（如 `MINLEN:10`）→ 日志“未识别的判定规则 \"...\"” + `post_verdict(FAIL)` + `post_error` + `OP_FAIL`；空规则/空 token 仍 PASS（按配置不做内容规则）。独立 e2e：未知规则→FAIL 且投递 FAIL/ERROR；空 READRULE→OK。

### P1-9 通配符 `*` 回溯上界错误 + 大小写敏感 —— ✅ 已完善修复
[pattern.c:42](udisktools/src/pattern.c:42) 消耗上界改为剩余长度；[pattern.c:25](udisktools/src/pattern.c:25) ASCII 大小写不敏感；入口 NULL 防护；失败清空捕获。37/37 用例、ASan 0 报告。

### P1-10 USB 通讯模式识别实现无效 —— ✅ 已完善修复（第 3 批）

已重写：SetupAPI 枚举 `GUID_DEVINTERFACE_USB_HUB` → `IOCTL_USB_GET_NODE_INFORMATION` 取端口数 → 逐端口 `IOCTL_USB_GET_NODE_CONNECTION_INFORMATION_EX` 按 VID/PID 匹配 → Speed 映射 Low/Full/High/Super，`_EX_V2` 区分 5G/10G（[usbspeed.c:134](udisktools/src/usbspeed.c:134)）。同时修复 `device.c` 的 `%.4hs`→`%.4ls` 与“父链取到上级 Hub 的 VID/PID”两个上游缺陷（[device.c:111](udisktools/src/device.c:111)）。

验证：真实 F: 只读 e2e：`vidpid=VID:058F PID:6387`、`speed=3`、日志“USB 2.0 High-Speed (480Mbps)”；同 VID/PID 多设备/非 USB/非法 VID → FALSE+原因。
残余：同 VID/PID 多设备时无法唯一匹配（明确报未知）；USB 5G/10G 分支仅代码审查（本机唯一 U 盘为 High-Speed）。

### P1-11 rwtest 结论不可信（回读命中系统缓存） —— ✅ 已完善修复（第 3 批）

已修：重写 `op_rwtest`——扇区对齐 `VirtualAlloc` + `FILE_FLAG_NO_BUFFERING|FILE_FLAG_WRITE_THROUGH` 直读介质，64KB 分块；不支持时回退缓冲并 WARN；文件带 PID、删除失败 WARN；日志注明“直读介质/缓冲模式”（[ops.c:1064](udisktools/src/ops.c:1064)）。

验证：subst 临时盘 OP_OK，日志“NO_BUFFERING/WRITE_THROUGH 直读介质，未走缓存”，测试文件已删除；读速回落到介质真实水平。

### P1-12 数据完整性校验被静默削弱（MD5/HASH） —— ✅ 已完善修复（第 3 批）

已修：`sha_or_md5_file` 读错误必 FALSE（保留 GetLastError）、outlen<33 返回 `ERROR_INSUFFICIENT_BUFFER`、EOF 才成功（[util.c:288](udisktools/src/util.c:288)）；`op_write` MD5 不一致→删目标+重试复制一次→仍不一致删除坏副本并 FAIL；MD5 无法计算→FAIL+删除目标；HASH 非 md5 写前 FAIL。
验证：MD5 向量/锁定文件/小缓冲用例；“不一致→重试→成功”“一直不一致→删坏副本”“算不出→FAIL+删除”故障注入 11 项通过。

### P1-13 内嵌默认配置与发布 ini 已漂移 —— ✅ 已完善修复（第 4 批）

已修：以当前 `profiles/UdiskTools.ini` 重新生成 `src/default_ini.h`（修复旧 AUTORUN/SRCPATH/READFILE/READRULE 漂移）；新增 `scripts/gen_default_ini.ps1`（与 Python 生成器逐字节等价）并接入 CI `-Check` 门禁与 `make check-default-ini`。
验证：Lead 独立解析 default_ini.h 字符串与 profiles 归一化内容逐字节一致；`-Check` exit 0，篡改 dst/profiles/删除 dst 均 exit 1；PS 与 Python 生成器对同一输入 byte-identical。
**注意**：`profiles/UdiskTools.ini` 工作区改动必须与 `src/default_ini.h` 同批提交，否则 CI 门禁会红。

### P1-14 任务/线程生命周期缺少 Join 与单实例保护 —— ✅ 已完善修复（第 5 批）

已完成：worker 句柄保存 + TASKDONE/WM_CLOSE Join；第 5 批补齐 **单实例**（`Local\UdiskTools.SingleInstance` 互斥量，第二实例激活现有窗口后 exit 0，不弹窗）、**轮询线程可停止可 Join**（停止事件 `WaitForSingleObject(g_stop_evt,1000)`，`dev_poll_stop` SetEvent→5s Join，成功才关句柄、超时 WARN 且保留句柄防 use-after-close）、**日志共享**（`FILE_SHARE_READ|WRITE|DELETE`，打开/写入失败 alt 兜底，不再静默丢行）；工具链链接 dbghelp 支持崩溃转储。
验证：第二实例实测 56ms 内 exit 0 且日志含“已有实例…已激活现有窗口”；轮询 start/stop 3 轮均 <3s；4 线程 200 行日志完整；主日志被独占时写 `_alt.log` 且 GUI 消息不丢。
残余：无（单实例为当前用户会话级；跨会话多开不在本工具场景）。

### P1-15 静态配置结构过大：906 KB 栈帧 —— ✅ 已完善修复（第 4 批）

已修：`reload_config_and_ui` 的栈上 `CONFIG newcfg`（906,616 B）改为堆分配（[gui.c:1068](udisktools/src/gui.c:1068)），失败返回 FALSE 且 `g_cfg` 保持旧值。
验证：`clang -fstack-usage` 实测 **906,616 B → 328 B**（gui_run 1016→408 B；task_thread/op_thread 栈帧不变，快照在堆上）。

### P1-16 保存/恢复配置的失败路径会假报成功 —— ✅ 已完善修复（第 4 批）

已修：`write_default_ini` 重写（[gui.c:1112](udisktools/src/gui.c:1112)）——先备份 `<ini>.bak` → PID 唯一 tmp 写入（检查 WriteFile 字节数/FlushFileBuffers）→ `MoveFileEx(REPLACE_EXISTING|WRITE_THROUGH)` 原子替换 → 写后 `cfg_load` 回读校验，失败还原备份/保留原文件；保存单键/恢复默认流程均检查 `reload_config_and_ui` 返回值，失败 ERR+弹窗、不再记“已保存（配置已重载）”；成功后刷新主标题/机型列表/配置内容（L14）。
验证：隔离 harness PASS=17（默认写入逐字节校验、失败路径原 ini 不变、reload 成功/失败、启动守卫）；Lead `-fstack-usage` 与代码审查。

### P1-17 全局热键 T/C：无修饰键、语义矛盾、注册失败仍报“已注册” —— ✅ 已完善修复（第 6 批）

已修：热键注册改 `Ctrl+Alt+T`/`Ctrl+Alt+C`（`MOD_CONTROL|MOD_ALT`，不再劫持裸 T/C）；语义与 resource.h 一致——**Ctrl+Alt+T=单操作读写测试、Ctrl+Alt+C=单操作复制**（不是整任务）；任一注册失败回滚并 filelog WARN（不宣称成功）；禁用/退出时 UnregisterHotKey；复选框文字/宽度同步；任务执行中按热键忽略并记日志。
验证：独立 GUI harness 实测 `hotkeys_enable(TRUE)=TRUE` 且注销/重复禁用安全；resource.h 注释已同步；GUI 真实按键交互仅代码审查。

### P1-18 格式化后状态不刷新、卷标写入无校验 —— ✅ 已完善修复（第 3 批）

已修：确认前校验 `FILESYSTEM`（仅 FAT32/EXFAT）与 `VOLUME`（≤11 字符、禁 `\ / : * ? " < > |` 与控制字符，不合法直接 FAIL、不弹确认、不调 FormatEx）；格式化前进度归零；卷标写入失败判 FAIL；成功后刷新 DEVINFO（1+3×200ms）→ `post_label` + 新增 `WM_APP_DEVREFRESH` 刷新设备行（[ops.c:771](udisktools/src/ops.c:771)、[gui.h:21](udisktools/src/gui.h:21)、[gui.c:1619](udisktools/src/gui.c:1619)）。
验证：非法 FILESYSTEM/VOLUME 用例 OP_FAIL 且无弹窗/无 FormatEx；成功全链路因禁止格式化真实盘仅代码审查+编译。

---


## 3. 逻辑关系专题（跨模块条件/状态矛盾）

> 状态列：✅ 已修复 / 🟡 部分 / ❌ 未修复。修复后行为变化见第 8.5 节。

| # | 设计意图 | 实际实现 | 冲突结果 | 状态 |
|---|---|---|---|---|
| L1 | ENABLE_* 决定步骤是否存在；三层职责分离 | 第 2 批按政策实现：配置屏蔽→OP_SKIP_CFG（成功类别并计数），程序自行降级/未实现/无法校验→OP_FAIL | 跳过语义已按政策澄清并实现 | ✅ |
| L2 | 插入的盘触发任务 | 现已把事件盘符传入任务；手动路径仍取最小盘符 | 自动路径已对准刚插入盘；手动多盘仍可能选错 | ✅（手动限制保留） |
| L3 | 轮询发现拔出+插入各发一次事件 | 第 2 批改为“变化盘符位图+插入/拔出”，先拔后插两条消息，GUI 增量维护 mask | 同 tick 换盘不再丢插入 | ✅ |
| L4 | 任务中按 [停止] 中止 | 启动即刷新按钮、STOP 可用、pbCancel | 已可停止（格式化执行中除外） | ✅ |
| L5 | 成功/停止/失败三类结果 | 第 2 批改为 OP_OK/FAIL/STOPPED/SKIP_CFG 四类结果码，任务按 fail/stop/skip/exec 聚合，不再用 `rc \|=` | 四类结果明确 | ✅ |
| L6 | 停止=中止当前任务 | 停止后首步检查→rc=2 | 不再报成功 | ✅ |
| L7 | READRULE 多规则链 | 注释规则已改，链可完整载入 | 多规则生效 | ✅ |
| L8 | 未知规则应报错 | 第 2 批：未识别 token→post_verdict(FAIL)+fail+OP_FAIL（空规则/空 token 仍 PASS） | 拼写错误不再绕过判定 | ✅ |
| L9 | 先判定再显示；PASS 才显示内容 | 第 6 批：插入/任务开始统一 reset_task_ui 清判定与文件内容；单操作按操作名显示徽标，不覆盖判定 | 跨任务残留与语义冲突消除 | ✅ |
| L10 | 标签读写（写入回读比对） | 无写标签操作；按钮实际执行复制 | 语义三重漂移，README 虚标 | ❌ |
| L11 | 热键 T=读写测试、C=写入 | 两者都执行整任务且无修饰键全局注册 | 资源表/文档/实现矛盾 + 劫持全系统按键 | ❌ |
| L12 | 只读+密码+连错 3 次锁定 | 计数开窗清零、不写日志、锁定时可恢复默认 | 保护可绕过 | ❌ |
| L13 | 运行中可切换机型 | 第 4 批：worker 只读按值快照 CFGMODEL（TASKARG/OPARG），运行中改配置不影响已启动任务 | 配置撕裂已消除 | ✅ |
| L14 | 保存成功→主界面刷新 | 保存/恢复均检查 reload 返回值（失败提示）；成功后刷新主标题/机型下拉/配置窗口列表 | 假报成功与标题/列表不刷新均修复 | ✅ |
| L15 | 设备信息用于显示与任务 | 第 5 批：dev_get_info 对不存在盘根返回 FALSE；UI 改用 dev_get_info_volume；RAW 盘根存在仍可采集 | 无卷就绪等待/重试，0.0GB 写失败保护仍未做 | 🟡 |
| L16 | 错误徽标三处联动 | 第 6 批：reset_task_ui 统一复位判定/内容/命中行/进度/监控状态；拔出同步设备与标签行 | 上一盘结果不再误读 | ✅ |
| L17 | 双 CMD 输出区 | 按用户要求 `IDC_EDIT_IDENT 1402` 为标识生成扩展预留：控件保留、无入口、不参与任务；文档/UI 状态已标注“预留” | 不再是缺陷（范围决策） | ✅ |
| L18 | ini 坏了可用内嵌默认恢复 | 加载失败直接退出；恢复按钮在配置窗内 | 无 GUI 恢复路径 | ❌ |
| L19 | 配置窗口可编辑全局键 | 手输全局键写进机型节 | “能输入、存不进、不生效” | ❌ |
| L20 | 切换机型与轮询解耦 | 第 4 批：worker 只读按值快照 CFGMODEL，reload 与任务并发不再撕裂 | 切换机型安全（下个任务生效） | ✅ |

---

## 4. P2（健壮性 / 状态一致性 / 资源与性能）

> 每条：状态 + 摘要；未写“本轮处理”即 ❌。

**4.1 UI 状态**
1. ✅ 判定/内容/进度/状态复位：第 6 批统一 `reset_task_ui`，接入插入新盘/任务开始/任务结束/拔出；判定、文件双窗格、命中行、进度、错误徽标、监控状态行按结果统一复位（FAIL 保留失败现场）；ASan harness 27/27。

2. ✅ 进度口径统一：第 6 批新增唯一入口 `post_progress_pct`（0..100 截断、进度条与百分比文本同时设置）；按字节/按文件回退均换算到同一百分比，结束/停止/拔出归零一致（边界 -7/0/55/100/150 实测）。
3. ✅ 单操作完成语义：第 6 批 `op_display_name` + `g_single_op`，单操作 OP_OK 显示“复制完成/格式化完成/读取判定完成/读写测试完成/读取标签完成”；SKIP/STOPPED 带操作名前缀；整任务仍“任务完成”。

4. ✅ 标题截断：`update_main_title` 与 `badge_paint/set_error_badge` 缓冲 300→1200/1220；TITLE+VERSION 各 500 字符实测标题 1002 字符不截断。
5. ✅ 配置窗口：初始选中当前机型（`cfg_current_model_sel`）；枚举非法值安全回退+提示；输入限长；字体先替换后删（无悬空 HFONT）；创建时按当前 DPI 缩放（跨屏不逐控件重排列为已知限制）。
6. ✅ 最小窗口尺寸：WM_GETMINMAXINFO 按 DPI 设 880×600（96DPI）/1760×1200（192DPI），实测通过。
7. ✅ RichEdit 回退：三级回退 Msftedit(`RICHEDIT50W`)→Riched20(`RICHEDIT20W`)→STATIC，控件统一用 `g_riched_class`，EM_* 调用有护栏。
8. ✅ 多盘设备行：`update_device_text` 改 8192 缓冲 + `wbuf_append` 有界追加；26 盘×127 字符卷标 ASan 无越界（设备行 3389/标签行 3169）。

**4.2 资源 / 泄漏 / 错误处理**
9. ✅ PostMessage 所有权：第 5 批新增 `post_owned_string`（复制→投递→失败自释放），gui.c/ops.c 全部投递改用；失败只 filelog WARN、接收端释放约定不变（ASan 18 项无 UAF/double-free）。
10. 🟡 g_running 卡死：malloc 失败已复位并刷新；退出/销毁路径兜底 Join+复位。残余：PostMessage 失败仍不检查；TASKDONE 丢失只靠退出兜底。
11. ❌ 写操作返回值不检查：`SetVolumeLabelW`/`DeleteFileW`/`FlushFileBuffers`/`write_default_ini`。
12. 🟡 错误信息缺上下文：格式化/复制已补 `GetLastError`；rwtest/读取/标签仍缺盘符/路径/键；部分 `GetLastError` 仍被覆盖。

**4.3 配置解析与 API**
13. ✅ 解析健壮性：容量超限（17 机型/50 键/80 字符节名）、畸形节（缺 `]`）、`[]` 空节名、超长节名、重复节（合并）均写 WARN 且不会把键写进上一节；截断/PRJNAME 不匹配也告警（第 4 批）。
14. ✅ 编码识别：BOM UTF-8/UTF-16LE/BE、无 BOM UTF-16 启发式、GBK；损坏 UTF-8/二进制拒绝并给偏移原因（第 4 批 text_decode）。
15. ✅ `read_file_text`：0 字节 TRUE 空串、UTF-16 奇数长度/内嵌 NUL/损坏编码 FALSE+ERROR_INVALID_DATA、>64MB FALSE+ERROR_FILE_TOO_LARGE（第 4 批）。
16. ✅ 公共 API 契约：pattern NULL/失败清空、save 注入校验、cfg_get 系列 NULL/outlen 防御、find_display 大小写不敏感均修；第 6 批 `pat_match_capture_ex` 显式上报捕获截断（op_read WARN），不再静默；cfg_root_get 仍为预留（无调用方，不影响行为）。
17. ✅ `read_ini_text`：共享 READ/WRITE/DELETE、ReadFile 循环/短读检查、>16MB 失败并写具体原因（第 4 批）。
18. ✅ tmp 竞争/原子性：PID 唯一 tmp、WriteFile 校验、FlushFileBuffers、WRITE_THROUGH、失败删 tmp、路径动态分配。

**4.4 util 基础函数**
19. ❌ `exe_dir()` 返回 static 缓冲（头文件注释相反）、忽略 `GetModuleFileNameW` 返回值。
20. ❌ `path_join` `outlen==0` 写 `out[-1]`；静默截断出“看似合法”的错误路径。
21. ❌ `util_ensure_dirs` 硬编码盘符：相对路径 FALSE、UNC 失败、`pp` 返回 TRUE 却不创建。
22. ❌ `wcs_trim` 前后不对称；`wcs_dup(NULL)` 崩溃；`wide_to_utf8` 语义混合；超 4600 字节日记整行丢弃。
23. ✅ 日志/UI 线程：第 5 批 CS 改 INIT_ONCE；日志文件句柄按天缓存复用（不再每行 CreateFile/Close）、共享 READ|WRITE|DELETE、>8MB 轮转、alt 兜底、`log_shutdown`；`update_device_text` 改用 `dev_get_info_volume`（UI 线程 0 处 SetupAPI）。残余：文件写仍是同步（未做异步队列），但已去掉每行 open/close 开销。

**4.5 ops 细节**
24. ❌ `op_label_read` 用未初始化 `serial`；`dev_get_info` 恒 TRUE 使失败分支死代码。
25. ❌ `op_write` 先建目标目录后判无文件；`op_read` `hits[16]` 截断/第二文件目录序；大文件整段投递 RichEdit。
26. ✅ 配置窗口保护（按用户要求不做次数锁定）：删除 failcount/“3 次锁定”；密码错误只提示+WARN 日志（不含密码）可无限重试；未解锁时保存/恢复默认均禁用（关窗回只读）；密码明文/默认值仍属“防误改”定位（文档已注明）。

---

## 5. P3（工程规范 / 显示细节 / 潜在隐患）

1. ✅ STATIC 设色：第 6 批改用 `WM_CTLCOLORSTATIC`（SetTextColor/SetBkColor + 背景刷），`set_static_text` 记录颜色；实测文字色生效。
2. ✅ rc/结果码语义：第 2 批改为 `OP_OK/OP_FAIL/OP_STOPPED/OP_SKIP_CFG` 明确结果码，任务不再用 `rc |=`；旧 `rc=3` 歧义消除，停止早于首步 → `OP_STOPPED`。

3. ✅ 无匹配文件不再先建目录：第 6 批 op_write 先枚举匹配源文件，≥1 个才创建目标目录；空目录/无匹配/源不存在→OP_FAIL 且不建目录（实测）。
4. ✅ `${MATCH_1}` 捕获截断显式：`pat_match_capture_ex` 返回 truncated；op_read WARN“捕获超长已截断（N 字符），仅记录前 M 字符”；旧 API 兼容。
5. ✅ 工程化资源：第 5 批新增 `res/udisktools.rc`（VERSIONINFO/图标/manifest）、`res/udisktools.manifest`（asInvoker+comctl32 v6+PerMonitorV2+longPathAware）、16x16 图标；`SetUnhandledExceptionFilter`+MiniDumpWriteDump 崩溃转储；CLI `--ini/--check-ini/--version/--help`；日志 8MB 轮转。
6. ✅ 退出码：`GetMessageW==-1` → filelog ERR + exit 1；WM_CLOSE 等待超时/兜底 join 失败 → exit 3；`PostQuitMessage(g_exit_code)` → gui_run 返回 → 进程退出码；正常关闭实测 exit 0，CLI 0/1 正确。
7. 🟡 版本/构建元数据：第 7 批新增 `APP_BUILD_DATE/TIME`，`--version` 与启动日志显示构建日期时间；`VERSIONINFO` 仍固定 1.0.0.0、未引入构建号/短 hash。
8. 🟡 格式化：FormatEx 前已加停止检查、失败带错误码；MediaType 仍固定 0x0C、回调永远 TRUE、无读回校验、FAT32 容量未预检。
9. ❌ `op_gen_ident` MAC 为随机演示、CMD-B 无入口。
10. ❌ 重复/魔法数/常量双份定义（盘符枚举 4 处、`MAX_PATH` 硬拼、`WM_APP_LOG/DEVCOUNT` 双定义等）。

---

## 6. 工程性问题

| ID | 问题 | 状态 | 说明 / 建议 |
|---|---|---|---|
| ENG-01 | 巨型文件/函数（gui.c 1,554 行、wnd_proc 330+ 行） | ❌ | 本轮只是增量修改，未拆分 |
| ENG-02 | 全局可变状态（28 static HWND + 全局配置） | ❌ | 未引入 UDK_UI/GWLP_USERDATA |
| ENG-03 | UI 与业务耦合（worker 裸 PostMessage） | ❌ | 增加了句柄/守卫，但未封装 `post_ui_result` |
| ENG-04 | 分层缺失 / 自造 IOCTL/fmifs 结构 | 🟡 | USB 部分已改用官方 IOCTL/结构并抽出 usbspeed 模块；fmifs/其它结构仍待整理 |
| ENG-05 | 零自动化测试（tests/ 仅占位） | ✅ | 第 7 批：tests/unit+integration 7 组 C 用例 + 4 条 CLI，run_tests.ps1（clang/gcc 均 PASS=11），CI Run tests |
| ENG-06 | CI 只验证“能编译出 exe” | 🟡 | 第 7 批 CI 增加 Run tests + CLI 冒烟 + 默认配置门禁；仍未加静态分析/ASan 门禁 |
| ENG-07 | 生成物漂移无门禁（gen_default_ini.py） | ✅ | 第 4 批：gen_default_ini.ps1 -Check 接入 CI + make check-default-ini；PS/Python 生成器逐字节等价 |
| ENG-08 | 历史粒度（config.c 自初始提交未改） | 🟡 | 本轮已重写并补验证，但流程未变 |
| ENG-09 | CMake 与 Makefile 两套参数 | ❌ | 未统一 |
| ENG-10 | 无 .rc/manifest/崩溃处理/命令行 | ✅ | 第 5 批：资源/版本/manifest/图标 + 崩溃转储 + CLI + 完成（见 P3-5） |
| ENG-11 | 文档与实现系统性偏差 | 🟡 | 已同步本轮涉及的 README/手册/规划书条目；标签读写/双 CMD/USB/热键等仍不符（见第 7 节） |
| ENG-12 | 文档死链、资源表与实现矛盾 | ✅ | 第 7 批：README/docs 目录化后主要文档相对链接 broken=0；resource.h 热键/IDENT 注释已同步；剩余历史 audit 副本内部链接为审计快照 |

---

## 7. 文档与实现偏差（含本轮同步状态）

| ID | 文档宣称 | 实际 | 状态 |
|---|---|---|---|
| DOC-01 | 标签读写（写入回读比对） | 第 6 批文档纠偏：改为“标签读取；写卷标仅格式化后按 VOLUME 回写，无独立写标签操作” | ✅ |
| DOC-02 | 双 CMD 输出区（CMD-B 标识生成） | 第 6 批：文档/UI 状态统一为“CMD-A 日志区 + CMD-B 预留（标识生成扩展）”，不再声称已实现 | ✅ |
| DOC-03 | 连错 3 次锁定并记日志 | 按用户要求不做锁定；文档改为“密码错误可无限重试、仅记录日志；未解锁保存/恢复默认不可用” | ✅ |
| DOC-04 | 保存后保留其余内容与行内注释 | **已实现**：保留编码/BOM/行尾/注释/其余内容，写后回读 | ✅ |
| DOC-05 | 所有尺寸经 GetDpiForWindow/配置窗 DPI | 第 6 批配置窗创建时按 g_dpi 缩放、字体安全替换；跨屏 DPI 不逐控件重排（已知限制，已写入 UI 状态文件） | 🟡 |
| DOC-06 | USB 通讯模式识别（已实装） | 第 3 批重写为 Hub IOCTL 匹配；真实 F: 实测识别成功；文档已同步 | ✅ |
| DOC-07 | UTF-8/UTF-16/ANSI 自动识别 | 第 4 批统一 text_decode：UTF-8±BOM/UTF-16LE/BE±BOM/无 BOM 启发式/GBK；损坏 UTF-8 明确拒绝并给原因 | ✅ |
| DOC-08 | 内嵌默认配置与发布 ini 相同 | 第 4 批重新生成并由 CI -Check 保证一致 | ✅ |
| DOC-09 | README 链接 docs/软件规划书.md 等 | 第 7 批 docs 目录化并修正 README/docs 互链；主要文档 broken=0 | ✅ |
| DOC-10 | “启动冒烟测试” | 第 7 批 CI 增加 CLI 冒烟（--version/--check-ini 有效/无效） | ✅ |
| DOC-11 | 热键 T=读写测试、C=写入（resource.h） | 第 6 批：Ctrl+Alt+T=读写测试、Ctrl+Alt+C=复制（单操作），文档/resource 注释一致 | ✅ |
| DOC-12 | PRJNUM 决定机型数量 | 代码从不读取 | ❌ |
| DOC-13 | ADMINPASS_HASH 可选 | 无实现 | ❌ |
| DOC-14 | BTN_CONFIG 可配置 | 不读取 | ❌ |
| DOC-15 | 连错 3 次锁定（手册/规划书） | 第 6 批删除该承诺，改为无锁定/可重试/仅日志 | ✅ |
| DOC-16 | 停止仅执行中可用、关闭即安全退出 | **已实现**（见 P0-3）；手册/规划书已同步 | ✅ |

---

## 8. 修复记录（2026-10-24）

> 8.1–8.5 为**第一批**（修复项 1/2/3/5）；8.6 为**第二批**（结果/跳过语义、设备事件协议、换盘/设备错误分类）。

### 8.1 第一批改动清单

| 文件 | 变更规模 | 内容 |
|---|---|---|
| [src/config.c](udisktools/src/config.c) | +756 行（302→908） | `cfg_save_key` 重写；`ini_find_comment_start` 等 helper；`cfg_load` 改用新注释规则 |
| [src/config.h](udisktools/src/config.h) | +8/−1 | `cfg_save_key` 契约注释 |
| [src/pattern.c](udisktools/src/pattern.c) / [pattern.h](udisktools/src/pattern.h) | +46 | `*` 回溯上界、ASCII 大小写、NULL/捕获清理；注释 |
| [src/device.c](udisktools/src/device.c) / [device.h](udisktools/src/device.h) | +71/+23 | `DEVSIG` 采集/比较/描述 |
| [src/ops.c](udisktools/src/ops.c) / [ops.h](udisktools/src/ops.h) | +208/+15 | 停止接口、身份复核、格式化强制确认+复核、pbCancel、停止返回码 |
| [src/gui.c](udisktools/src/gui.c) / [gui.h](udisktools/src/gui.h) | +260/+5 | busy 刷新、worker Join、退出等待、事件盘符、任务级守卫、停止接口 |
| [README.md](udisktools/README.md)、[手册](udisktools/docs/配置文件使用手册.md)、[规划书](udisktools/docs/UdiskTools软件规划书.md) | 少量 | 同步注释规则/CONFIRM/停止/身份复核/保存规则 |

未改动：`util.c`、`main.c`、`md5.c`、`ui/resource.h`、`scripts/`、`profiles/`（工作区原有未提交改动未处理）、`build/` 下原有产物。

### 8.2 关键实现点（可复核行号）

- `config.c:64` 注释起点 helper；`config.c:607` `cfg_save_key`；`config.c:656` 读取防御；`config.c:868-901` 写后回读校验/属性恢复。
- `pattern.c:25` 大小写比较；`pattern.c:42-56` `*` 消耗 0..剩余；`pattern.c:69` NULL 防护。
- `device.c:161/185/202` DEVSIG 三函数。
- `ops.c:29/35` 停止接口；`ops.c:102/123` 期望复核/卷序列号复核；`ops.c:173` op_write；`ops.c:320` pbCancel；`ops.c:337` 取消归类“已停止”；`ops.c:572` op_format；`ops.c:794` op_rwtest。
- `gui.c:425` 任务级守卫；`gui.c:440` task_thread；`gui.c:520` task_run_async；`gui.c:655` wait_worker_exit；`gui.c:1496` 事件盘符；`gui.c:1559` TASKDONE Join；`gui.c:1570` WM_CLOSE。

### 8.3 验证证据（Lead 亲自执行 + 团队独立验证）

```
构建（clang 23 / UCRT，工程同款 flags）
  语法检查 -Wall -Wextra 全工程 8 个 src：exit 0，0 warning
  完整链接（-municode -mwindows + 8 系统库）：exit 0，197,632 B

Lead 独立验收（audit_scratch\lead2）
  pattern 用例                PASS=37  FAIL=0
  config 合成 ini 验收        PASS=61  FAIL=0
  config 真实出厂 ini 验收    PASS=20  FAIL=0
  core（DEVSIG/停止/守卫）    PASS=20  FAIL=0
  READRULE 端到端（op_read）  PASS=3   FAIL=0
  ASan（pattern + config）    0 report，全部通过
  旧/新 cfg_load @ profiles   IDENTICAL
  旧实现同 harness            cfg_save_key → 0xC0000374（堆破坏）

团队独立验证
  impl-config：176/176（正常版 + ASan 版），4 编码逐字节保持，5 次连保稳定
  impl-core  ：31/31（DEVSIG 真/假/信息缺失、停止标志、expect 守卫、复制取消、reset 后成功）
```

### 8.4 构建方式

```bat
:: 任选其一（均在 udisktools\ 目录）
clang -std=c11 -Wall -Wextra -DUNICODE -D_UNICODE -D_WIN32_WINNT=0x0605 -Iui -Isrc ^
      -o build\udisktools.exe src\*.c -municode -mwindows ^
      -luser32 -lgdi32 -lshell32 -lcomctl32 -ladvapi32 -lsetupapi -lcfgmgr32

make            :: MinGW/MSYS2 环境
cmake -B build -G "MinGW Makefiles" && cmake --build build
```

> 本轮未覆盖 `build\udisktools.exe`；如需直接运行修复版，可用上述命令重新构建（或使用 `audit_scratch\lead2\udisktools_fixed.exe`）。

### 8.5 第一批修复引入的行为变化与限制

1. **注释语义**：`;` 前必须有空格/制表符（或位于行首）才是注释，否则属于值内容 → `READRULE` 多规则可用；现有 ini 语义不变（已逐键验证）。
2. **`CONFIRM` 成为兼容键**：格式化始终人工确认，置 0 也不跳过（只记告警日志）。
3. **保存会拒绝部分值**（提示“保存失败”，不改文件）：含换行、首尾空白、超长、`;` 前带空格；新键插入当前机型节节尾。
4. **自动触发使用事件盘符**；任务每一步前做设备身份复核，换盘即中止；`op_write` 每文件复核卷序列号。
5. **[停止] 可用**，复制可立即取消；关窗先停止并等待工作线程（≤30s，超时记 WARN 后退出）。
6. **未端到端验证**：本机无可交互桌面/无移动盘，格式化真机弹窗、真实热插拔换盘、GUI 退出等待未做现场运行验证（仅代码审查 + 隔离测试）；`dev_sig_same_device` 在“双方都无身份信息”时无法区分。

### 8.6 第二批修复记录（2026-10-24）

**范围**：src/ops.h、src/ops.c、src/device.h、src/device.c、src/gui.c（src/gui.h 仅注释）；README/手册/规划书已同步。

**实现要点**
1. 结果/跳过语义（P1-6 / P1-8 / P3-2）
   - 新增 `OP_OK=0 / OP_FAIL=1 / OP_STOPPED=2 / OP_SKIP_CFG=3`（[ops.h:26](udisktools/src/ops.h:26)）。
   - 配置正常屏蔽 → `OP_SKIP_CFG`：ENABLE_* =0、READFILE 留空、READRULE 留空、AUTO_COPY/AUTO_TEST=0。
   - 程序自行降级/未实现/无法校验 → `OP_FAIL`：WRITEMODE=PROCESSED、READPATTERN=REGEX、未知 READRULE、HASH 非 md5、MD5 无法计算、读取中换盘。
   - `apply_rules` 三态；任务聚合改为 exec/skip/fail/stop 计数（**不再用 `rc |=`**）；结束日志“执行 N 步，按配置跳过 M 步”；全部配置屏蔽时结束为 `OP_SKIP_CFG`（界面“按配置跳过（未执行）”，成功类别）。
2. 设备事件协议与待处理（P1-4 / L3）
   - `WM_APP_DEVCOUNT` 改为“本次变化盘符位图 + 插入/拔出”（同一次轮询先拔后插，最多两条）；新增 [device.c:234](udisktools/src/device.c:234) `dev_post_changes()`；GUI 按位增量维护 `g_dev_mask`。
   - 任务执行中插入：只记日志、不清屏；记待处理盘，任务**成功/按配置跳过**结束后若仍处于自动模式则补跑；失败/停止不补跑；拔出取消；启动先枚举基线再启动轮询。
3. 换盘/设备错误分类（P1-5 / 部分 P2-12）
   - `op_read` 读取前后 DEVSIG 复核（读中换盘/拔出 → 判定作废、`OP_FAIL`）；设备丢失错误码 21/1167/1112 分类；`op_write` 复制失败遇设备丢失立即中止；`rwtest`/格式化失败按设备丢失/写保护/空间不足分类提示。
4. 守卫失败可见性
   - `task_verify_target` 中止时显式投递 `WM_APP_ERROR`，避免“任务失败但界面仍显示正常”。

**验证**
- 构建：clang `-Wall -Wextra -Werror` 全工程 0 warning，完整链接 201,728 B。
- Lead 独立验收（`audit_scratch\lead2`）：batch2 35/35；回归 pattern 37/37、config 合成 61/61、真实出厂 ini 20/20、core 20/20、readrule e2e 3/3；旧/新 `cfg_load` @ profiles 仍一致。
- 团队验证：`test_ops` 28/28（`subst` 临时盘 + `%TEMP%` 隔离）、`test_devmsg` 10/10（消息协议）。
- 构建证据：`audit_scratch\impl-batch2\build_output.txt`、`test_output.txt`。

**行为变化（现场注意）**
- HASH 非 md5、READPATTERN=REGEX、未知 READRULE、WRITEMODE=PROCESSED、ENABLE_WRITE=1 但无 SRCPATH、MD5 无法计算：以前静默“成功”，现在**任务失败（红徽标）**，按手册第 10 节排查。
- 配置屏蔽（ENABLE_*=0、READFILE/READRULE 留空、AUTO_COPY/AUTO_TEST=0）仍为成功，但日志显示跳过步数；全部屏蔽时任务结束显示“按配置跳过（未执行）”。
- 任务中插入新盘：不再清空当前输出；成功后补跑待处理盘；失败/停止不补跑。
- 任务结束日志格式变化（含执行/跳过计数），依赖日志文本的脚本需同步。

**未验证/限制**：无真实移动盘，GUI 插拔聚合 / pending 补跑 / 任务聚合仅代码审查（建议真机插拔回归一次）；设备丢失、MD5 无法计算等异常路径以代码审查为主；`FormatEx` 执行中不可中止（物理限制）。
### 8.7 第三批修复记录（2026-10-24）

**范围**：新增 `src/usbspeed.c/.h`；`src/ops.c`（op_usbinfo 重写、op_write MD5 重试、op_format 校验/刷新、op_label_read、op_rwtest 重写）、`src/util.c`（sha_or_md5_file）、`src/gui.h/.c`（WM_APP_DEVREFRESH）、`CMakeLists.txt`/`Makefile`（加入 usbspeed.c）；经 Lead 授权修复 `src/device.c` 的 `%hs` 与“父链取到上级 Hub 的 VID/PID”两个上游缺陷（仅 dev_get_usbid）。

**实现要点**
1. **USB 通讯模式（P1-10）**：SetupAPI 枚举 `GUID_DEVINTERFACE_USB_HUB` → 打开 hub → `IOCTL_USB_GET_NODE_INFORMATION` 取端口数 → 逐端口 `IOCTL_USB_GET_NODE_CONNECTION_INFORMATION_EX` 按 VID/PID 匹配 → Speed(0/1/2/3)→Low/Full/High/Super，Super 再用 `_EX_V2` 区分 5G/10G；多命中/0 命中/无权限/未枚举到 hub 均“未知（原因）”不报错。
2. **device.c 上游修复**：`%.4hs→%.4ls`；VID/PID 取“第一个带 `VID_`/`PID_` 的父设备”（真实 USB 设备），不再取上级 Hub；修复后真实 F: 由 `VID:0 PID:0`/Hub(05E3:0610) 变为 `VID:058F PID:6387`，Hub 速度匹配成功。
3. **rwtest（P1-11）**：扇区对齐 `VirtualAlloc` + `FILE_FLAG_NO_BUFFERING|FILE_FLAG_WRITE_THROUGH` 直读；不支持则回退缓冲并 WARN；文件带 PID；删除失败 WARN；日志注明“直读介质/缓冲模式”。
4. **MD5（P1-12）**：`sha_or_md5_file` 读错误必 FALSE（保留 GetLastError）、outlen<33 FALSE；op_write 不一致→删目标+重试复制一次→仍不一致删除坏副本并 FAIL；MD5 无法计算→FAIL+删除目标。
5. **格式化（P1-18）**：确认前校验 `FILESYSTEM`/`VOLUME`（≤11 字符、禁 `\ / : * ? " < > |` 与控制字符）；进度归零；卷标写入失败 FAIL；成功后刷新 DEVINFO（1+3×200ms）→`post_label` + `WM_APP_DEVREFRESH`。
6. **label_read/usbinfo（P2-24）**：盘不可访问/无卷信息（未格式化/未就绪）时判 FAIL，不再对异常盘报成功。

**验证**
- 构建：clang `-Wall -Wextra -Werror` 全工程 10 个源文件（含 usbspeed.c）0 warning，完整链接 212,992 B；Makefile/CMake 源列表含 `src/usbspeed.c`。
- 团队：`run_tests.ps1` exit=0，**122 PASS / 0 FAIL**（usbspeed 23、md5 13、readerr 4、md5retry 11、batch3_ops 43、batch2 回归 28）。
- Lead 独立验收：**batch3 31/31**（真实 F: speed=3、C:/Q: 行为、MD5 三用例、format 校验四用例、rwtest subst）；回归 batch2 **35/35**、core 20/20、readrule 3/3、pattern 37/37、config 合成 61/61、真实 ini 20/20。
- 真实盘 F: 只读 e2e：`vidpid=VID:058F PID:6387`、`speed=3`、日志 `通讯模式：USB 2.0 High-Speed (480Mbps)`、`op_label_read(F:)=OP_OK`；**未对 F: 写入/格式化**。

**行为变化（现场注意）**
- USB 通讯模式以前恒“未知”，现在可识别（同 VID/PID 多设备或无权限时仍“未知（原因）”）；DEVSIG/标签显示的 VID/PID 由“上级 Hub”改为真实 USB 设备（更正确）。
- rwtest 读速回落到介质真实水平（不再出现 1.6 GB/s 缓存假象），日志标注“直读介质/缓冲模式”。
- MD5 不一致会重试一次；仍不一致会**删除目标文件**并判任务失败（不再把坏副本留在盘上）。
- 格式化前先校验 FILESYSTEM/VOLUME，非法配置直接失败（不弹确认、不格式化）；成功后自动刷新界面（`WM_APP_DEVREFRESH`）。

**未验证/限制**：无第二块可牺牲盘，op_format 成功全链路（卷标写入/刷新/DEVREFRESH 运行时）仅代码审查；USB 4/5（5G/10G）分支仅代码审查（本机唯一 U 盘为 High-Speed）；MD5 不一致/读错误用编译期故障注入，非真实坏介质；严禁对真实 F: 写/格式化。
### 8.8 第四批修复记录（2026-10-24）

**范围**：`src/util.c/.h`（统一编码探测）、`src/config.c/.h`（read_ini_text/编码回写/解析健壮性/API 防御）、`src/gui.c`（配置快照/reload 堆化/write_default_ini/标题与列表刷新）、`src/default_ini.h`（重新生成）、`scripts/gen_default_ini.ps1`（新增）、`scripts/gen_default_ini.py`（同步注释）、`.github/workflows/build.yml`、`Makefile`（check-default-ini）；README/手册/规划书同步。

**实现要点**
1. **编码（P1-2/P2-14）**：新增 `text_decode` 严格探测 BOM（UTF-8/UTF-16LE/BE）、无 BOM UTF-16 零字节启发式、严格 UTF-8、损坏 UTF-8（少量非法字节→FALSE+偏移/0xXX，不静默 GBK）、GBK；`utf8_to_wide` 严格化；`read_file_text` 空文件/64MB/编码错误语义；`cfg_save_key` 保留 UTF-16BE 与无 BOM UTF-16 编码/BOM。
2. **解析（P2-13/P2-17）**：容量/截断/畸形节头/空节名/超长节名/重复节（合并）/PRJNAME 不匹配均 WARN；`read_ini_text` 共享 READ/WRITE/DELETE、ReadFile 检查、16MB 具体原因；`cfg_load` 失败写具体原因。
3. **并发（P1-3/L13）**：`TASKARG`/`OPARG` 按值快照 `CFGMODEL` + 版本；worker 只读快照；机型索引越界守卫。
4. **重载/保存（P1-15/P1-16/L14）**：reload 的 `CONFIG` 改堆（906,616→328 B）；`write_default_ini` .bak 备份+原子写+回读校验；保存/恢复检查 reload，失败不假报；刷新主标题/机型列表/配置内容。
5. **默认配置（P1-13/ENG-07）**：`default_ini.h` 重新生成；PowerShell 生成器与 Python 逐字节等价；CI `-Check` + `make check-default-ini` 门禁。

**验证**
- 全工程 clang `-Wall -Wextra -Werror` 0 warning，完整链接通过。
- 团队：batch4a 主 harness 123 PASS（ASan 123，无报告）；batch4b 17 PASS（默认写入/失败路径/reload/栈帧）；batch4c `-Check` exit 0 + 篡改检测；batch1–3 回归全通过。
- Lead 独立：batch4 **71/71**（编码矩阵、read_file_text、解析健壮性、API 防御、真实 profiles save-key 5 次字节稳定、UTF-16BE 保存保 BOM、default_ini 与 profiles 逐字节一致）；回归 batch2 35/35、core 20/20、readrule 3/3、pattern 37/37、config 合成 61/61、真实 ini 20/20。
- `-fstack-usage`：reload_config_and_ui **906,616 B → 328 B**；`task_thread`/`op_thread` 体内 0 处 `g_cfg`。
- 真实 profiles 加载零 WARN；`gen_default_ini.ps1 -Check` 与 `make check-default-ini` 均 exit 0。

**行为变化（现场注意）**
- 损坏 UTF-8/二进制 ini 现在**拒绝加载并写具体原因**（不再静默乱码）；解析超限/畸形会写 WARN。
- 保存/恢复默认失败不再假报成功；恢复默认先备份 `UdiskTools.ini.bak`、写后回读校验，失败保留原文件。
- 保存/恢复/重载后刷新主标题、机型下拉、配置窗口列表；任务运行中改配置只影响下一个任务。
- `src/default_ini.h` 内容已更新为当前 `profiles/UdiskTools.ini`（AUTORUN 移除、SRCPATH/READFILE/READRULE 等同步）；**两者必须同批提交**，否则 CI 门禁失败。

**未验证/限制**：GitHub Actions 未在本地实跑（YAML 人工核对）；GUI MessageBox 交互与表单视觉刷新仅代码审查；无 BOM UTF-16 纯 CJK 会按二进制拒绝（启发式已知边界）；`read_file_text` 现在拒绝二进制/非法编码文件（P1-2 预期变化）。
### 8.9 第五批修复记录（2026-10-24）

**范围**：`src/util.c/.h`（日志缓存/轮转/alt/INIT_ONCE/`post_owned_string`/`log_shutdown`）、`src/device.c/.h`（轮询停止事件+Join、`dev_get_info_volume`、`dev_enum_removable` 卷信息）、`src/gui.c/.h` + `src/ops.c`（投递所有权、UI 免 SetupAPI、退出码、窗口类宏、图标）、`src/main.c`（单实例/CLI/崩溃转储/日志收尾）、`src/version.h`、`ui/resource.h`、新增 `res/`（rc/manifest/ico）、`Makefile`、`CMakeLists.txt`；README/手册/规划书同步。

**实现要点**
1. **日志（P2-23/P3-5）**：CRITICAL_SECTION 改 `INIT_ONCE`；日志文件句柄按天缓存复用（不再每行 CreateFile/Close），共享 `READ|WRITE|DELETE`；>8MB 轮转 `.log.1`；打开/写入失败 alt 兜底 + OutputDebugString，GUI 消息不丢；外部删除自动重建；新增 `log_shutdown()`。
2. **PostMessage（P2-9）**：新增 `post_owned_string`（复制→投递→失败自释放），gui.c/ops.c 全部堆串投递替换；接收端释放约定不变。
3. **线程/单实例（P1-14）**：单实例互斥量 `Local\UdiskTools.SingleInstance`（第二实例激活现有窗口后 exit 0，不弹窗）；轮询线程停止事件+5s Join（成功才关句柄，超时 WARN 且保留句柄）；日志共享修复。
4. **UI 线程（P2-23/L15）**：`update_device_text` 改用 `dev_get_info_volume`（0 处 SetupAPI）；`dev_get_info` 对不存在盘根返回 FALSE；UI 标签不再显示 USB VID/SN（任务线程仍用 DEVSIG）。
5. **退出码（P3-6）**：`GetMessageW==-1`→filelog ERR+exit 1；WM_CLOSE 等待超时/兜底 join 失败→exit 3；`PostQuitMessage(g_exit_code)`；gui_run 返回该码。
6. **资源/CLI/崩溃（P3-5/ENG-10）**：新增 `res/udisktools.rc`（VERSIONINFO/101 ICON/RT_MANIFEST）、`res/udisktools.manifest`（asInvoker+comctl32 v6+PerMonitorV2+longPathAware）、16x16 图标；`SetUnhandledExceptionFilter`+`MiniDumpWriteDump` 崩溃转储；CLI `--ini/--check-ini/--version/--help`（AttachConsole 输出，失败 MessageBox）；窗口图标改用 `IDI_APP_ICON`；Makefile/CMake 接入 windres+dbghelp。

**验证**
- windres 0 error；全工程 clang `-Wall -Wextra -Werror` 0 warning；完整链接 245KB（含 res.o + `-ldbghelp`）。
- 团队：batch5a 58 PASS（ASan 58，无报告）；batch5b 18 PASS（ASan）；batch5c 51 PASS（CLI/单实例/VersionInfo/资源/崩溃 harness）。
- Lead 独立：batch5 **29/29**（post 所有权、applog、4 线程 200 行日志、9MB 轮转、独占 alt、shutdown、poll 3 轮 <3s、volume helper、图标/manifest/版本资源）；CLI 实测（--version/--help/--check-ini 0/1/--ini 含空格）；第二实例 56ms exit 0；WM_CLOSE 正常关闭 exit 0；回归 batch4 71/71、batch2 35/35、core 20/20、readrule 3/3、pattern 37/37、config 61/61+20/20；`check-default-ini` 仍 exit 0。

**行为变化（现场注意）**
- 日志：句柄常驻并允许外部读/写/删除；>8MB 轮转 `.log.1`；主文件被占用时写 `_alt.log`，不再静默丢行。
- 第二实例不再重复启动 GUI，会激活现有窗口并退出。
- exe 内嵌图标/版本/manifest：资源管理器显示图标与版本；`--version` 可脚本化读取。
- 新增 CLI：`--ini` / `--check-ini`（可用于 CI/现场排查）/ `--version` / `--help`。
- 崩溃时生成 `logs\crash_*.dmp` 且不吞异常。
- UI 标签行不再显示 USB VID/SN（UI 线程免 SetupAPI 的代价；任务线程仍使用 DEVSIG）。
- `dev_get_info` 对不存在盘根返回 FALSE（旧为恒 TRUE）。

**未验证/限制**：CMake 未实跑（本机无 cmake，仅静态核对 RC/dbghelp 接入）；跨日日志切换/写入失败重试无法注入；`GetMessage==-1` 与强制退出码 3 的实机路径仅代码审查；无真实 U 盘/RAW 盘；Windows ASan 无 LeakSanitizer。
### 8.10 第六批修复记录（2026-10-24）

**范围**：`src/gui.c`（状态复位/进度/单操作语义/标题/配置窗口/最小尺寸/RichEdit 回退/设备行/STATIC 设色/热键）、`ui/resource.h`（仅注释：IDENT 预留、热键语义）、`src/ops.c`（先匹配后建目录）、`src/pattern.c/.h`（捕获截断显式）、`docs/UI功能状态说明.md`（新增）、README/手册/规划书纠偏。按用户明确要求：**不实现密码连错 3 次锁定**；`IDC_EDIT_IDENT 1402` 为标识生成扩展预留（无入口/不参与任务）。

**实现要点**
1. **状态复位（P2-1/L9/L16）**：新增 `reset_task_ui(new_session)`，接入插入新盘/任务开始/任务结束/拔出；统一清判定、文件双窗格、命中文件行、进度、错误徽标与监控状态行；OK 保留结果、FAIL 保留失败现场、SKIP/STOPPED 清旧判定。
2. **进度（P2-2）**：`post_progress_pct` 为唯一入口（0..100 截断，同时设置进度条+百分比文本）；删除分散更新；结束/停止/拔出归零一致。
3. **单操作语义（P2-3）**：`op_display_name` + `g_single_op`，单操作显示“复制完成/格式化完成/…”，整任务仍“任务完成”。
4. **标题/最小尺寸/设备行（P2-4/6/8）**：标题与徽标缓冲 300→1200/1220（500+500 实测不截断）；WM_GETMINMAXINFO 按 DPI 880×600；`update_device_text` 8192 缓冲+有界追加（26 盘×127 字符 ASan 无越界）。
5. **配置窗口（P2-5/P2-26）**：初始选中当前机型、非法枚举回退+提示、输入限长、字体先替换后删；**按用户要求删除 g_cfg_failcount 与 3 次锁定**——密码错误只提示+WARN（不含密码）、可无限重试，未解锁时保存/恢复默认禁用，关窗回只读。
6. **RichEdit/STATIC 色（P2-7/P3-1）**：三级回退 `RICHEDIT50W→RICHEDIT20W→STATIC`（统一 `g_riched_class`，EM_* 护栏）；STATIC 文字颜色改 `WM_CTLCOLORSTATIC`。
7. **热键（P1-17）**：`Ctrl+Alt+T`=单操作读写测试、`Ctrl+Alt+C`=单操作复制（不再劫持裸 T/C、不再触发整任务）；注册失败回滚+WARN、退出注销、复选框文字同步。
8. **ops/pattern（P3-3/P3-4/P2-16）**：op_write 先枚举匹配源文件、≥1 个才建目标目录；`pat_match_capture_ex` 显式上报捕获截断，op_read 截断时 WARN。
9. **UI 功能状态说明（用户交付物）**：`docs/UI功能状态说明.md` 覆盖 resource.h 全部 50 个 ID（主窗口 32 控件/配置窗口 13 控件/2 热键/窗口类/图标），含 IDENT 1402 预留、无锁定规则、热键语义、状态机、预留清单。

**验证**
- 全工程 clang `-Wall -Wextra -Werror` 0 warning；windres 0 error；完整链接通过；`mingw32-make CC=clang` 端到端成功。
- Lead 独立：GUI harness **27/27**（ASan 27/27，无报告）；ops/pattern harness **14/14**；回归 batch5 29/29、batch4 71/71、batch2 35/35、core 20/20、readrule 3/3、pattern 37/37、config 61/61+20/20；`check-default-ini` exit 0。
- 团队：batch6a 12 PASS（ASan）；batch6b 32 PASS（ASan）；batch6c 文档/ID 校验（50 ID missing=0、锁定承诺 grep 0）。

**行为变化（现场注意）**
- 密码错误不再锁定，可无限重试；未解锁时保存/恢复默认按钮禁用；关窗回只读。
- 热键改为 Ctrl+Alt+T/C 且只执行单操作（读写测试/复制），不再触发整任务。
- 单操作完成徽标带操作名（如“复制完成”）；进度条/百分比始终一致。
- 设备标签行只显示卷标（VID/SN 仅供任务身份复核，不显示）；`IDC_EDIT_IDENT` 保持空白预留。
- 窗口最小尺寸 880×600（随 DPI 缩放）；RichEdit 缺失时可回退 RICHEDIT20W/STATIC。

**未验证/限制**：GUI 未做真机视觉验收（DPI 多档/颜色/自绘按钮）；配置窗口跨屏 DPI 不逐控件重排；热键真实按键交互仅代码审查；无真实 U 盘/RAW 盘；UI 状态文件行号随代码演进需同步（文件内已写维护提示）。
### 8.11 第七批修复记录（2026-10-24）

**范围（文档/目录/测试/CI，用户重点）**
- **目录结构**：docs 拆分为 `user/`（配置手册）、`reference/`（UI 功能状态说明）、`design/`（规划书/流程图/mockup）、`development/`（开发流程）、`audit/`（本报告）、`archive/`（历史），新增 `docs/README.md` 文档索引与 `docs/CHANGELOG.md`；README 目录树/死链同步修正。
- **测试入库**：`tests/unit/`（pattern/config/encoding）+ `tests/integration/`（ops/device_msg/gui_state/logging）+ `tests/run_tests.ps1`（`-CC gcc|clang`、30s 超时、CLI 用例、汇总退出码）+ `tests/README.md`；不再依赖 `audit_scratch` 与绝对路径。
- **构建/CI**：Makefile 新增 `test`/`test-clang`，修复 `bin:` 在 cmd 下误建 `-p` 目录、clean 兼容；CI 在 Build 后新增 Run tests（`run_tests.ps1 -CC gcc`），Smoke 改为 CLI 冒烟（--version / --check-ini 有效/无效），保留默认配置 `-Check`。
- **版本元数据（P3-7）**：version.h 新增 `APP_BUILD_DATE/TIME`，`--version` 与启动日志统一显示构建日期时间；VERSIONINFO 保持 1.0.0.0。
- **文档纠偏**：README/手册/规划书/UI 状态互链与目录路径更新；移除历史死链；`src/gui.c` 内提示路径改为 `docs/user/配置文件使用手册.md`。

**验证**
- `pwsh tests/run_tests.ps1 -CC clang` 与 `-CC gcc` 均 **PASS=11 FAIL=0**；`mingw32-make CC=clang test` 通过；主程序 clang/gcc 全工程 `-Wall -Wextra -Werror` 0 warning、完整链接。
- 文档：README+主要 docs **65 条相对链接 broken=0**、旧路径 grep 0；UI 状态文件 50 个控件 ID 缺失 0；audit 副本为历史快照（内部历史链接不修）。
- CI 等价命令本地执行 exit 0（41.5s，TOTAL PASS=11 FAIL=0）；workflow YAML 解析/步骤断言通过；CLI 冒烟 4/4。

**说明/限制**
- GitHub Actions 未能在本地真实执行（本地以 llvm-mingw gcc/clang + PATH 等价）；CI 首次运行若遇 MSYS2 gcc 差异需按日志微调。
- 测试覆盖为接口/隔离场景，真实 U 盘插拔、RAW/写保护/满盘等物理场景仍需现场回归（无设备时 SKIP）。
- 报告副本位于 `docs/` 根（2026-10-24 文档扁平化后自 docs/audit/ 上移），内部历史链接为审计时快照；现行文档入口见 `docs/README.md`。
### 8.12 用户功能优化记录（软件 v1.1.0，2026-10-24）

**范围**：`src/version.h`（软件版本 1.1.0）、`src/main.c`（启动日志/`--version`/`--check-ini` 双版本显示）、`src/gui.c` + `ui/resource.h`（标题双版本、AUTO 状态框、READRULE 标签）、`tests/`（GUI/CLI 用例）、README/docs/CI 同步。配置文件 `[TOOLSINFO] VERSION` 仍为“配置文件版本”（profiles 保持 1.0.0，未改）；`temp_run/` 未触碰（6 个文件 SHA256 前后一致）。

**功能**
1. **两套版本**：软件版本 `APP_VERSION=1.1.0`（`--version` 显示 `软件 v1.1.0（FileVersion 1.1.0.0）`、启动日志、窗口标题），配置版本来自 ini `VERSION`；窗口标题 `<TITLE> v<配置版本>（软件 v1.1.0）`，配置版本空时降级为 `<TITLE>（软件 v1.1.0）`。
2. **AUTO_COPY/AUTO_TEST 只读状态框**：新增 `IDC_CHK_AUTO_COPY 1009`、`IDC_CHK_AUTO_TEST 1018`（`BS_AUTOCHECKBOX|WS_DISABLED` 灰只读），位于 `IDC_CHK_TOPMOST 1008` 与 `IDC_CHK_HOTKEY 1013` 之间；按当前机型配置勾选（默认 1/0），机型切换/重载/保存后刷新，不可编辑。
3. **READRULE 标签栏**：新增 `IDC_STC_READRULE 1109` 只读 STATIC；`AUTO_TEST=1` 显示 `判定规则：<READRULE>`（空规则显示“（未配置 READRULE）”），`AUTO_TEST=0` 内容为空；仅显示，不改变 ops 判定语义。
4. **配置版本显示不变**：`--check-ini` 输出 `ini 加载成功：N 个机型（默认 X，配置版本 Y）`；启动日志记录配置版本与软件版本。

**验证**
- clang/gcc 全工程 `-Wall -Wextra -Werror` 0 warning；windres 0 error；链接通过；`tests/run_tests.ps1 -CC clang|gcc` 均 PASS=11 FAIL=0（test_gui_state 32 断言）。
- 端到端（临时目录启动 GUI，未操作真实盘）：标题、两灰框勾选/禁用、READRULE 在 AUTO_TEST=1/0 两场景文本均符合预期；VersionInfo.FileVersion=1.1.0、ProductVersion=1.1.0。
- 文档：README/手册/UI 状态/规划书/CHANGELOG/开发流程同步；主要文档相对链接 broken=0；UI 状态文档控件 ID 50→53、主窗口 32→35。
- `temp_run` 6 个文件 SHA256 与优化前一致；未删除/未改动。

**限制/说明**
- GUI 视觉/灰显观感以代码审查+控件矩形/文本断言为主；CI 新增 `1.1.0`/`1.1.0.0`/“配置版本”断言，真实 GitHub Actions 仍需首跑校准。
- 软件版本与配置文件版本独立管理：升级软件不改 `profiles/UdiskTools.ini` 的 `VERSION`；发布时按 docs/development/开发流程 同步 version.h/CHANGELOG/文档。
---

## 9. 后续维护与增强路线（截至第七批完成）

- **第 1–6 批 ✅**：配置保存/格式化安全/停止链路/READRULE+pattern；结果跳过语义/设备事件/错误分类；USB/rwtest/MD5/格式化刷新；编码解析/配置快照/默认配置+CI；日志/单实例/CLI/资源；UI 状态/热键/无锁定/IDENT 预留。
- **第 7 批 ✅**：文档与目录结构（docs 分区+索引+开发流程+CHANGELOG）、测试入库（tests + run_tests.ps1）、CI Run tests + CLI 冒烟、构建元数据（P3-7 部分）。
- **功能优化 ✅（软件 v1.1.0）**：标题同时显示配置版本/软件版本、AUTO_COPY/AUTO_TEST 灰只读状态框、AUTO_TEST=1 时 READRULE 标签栏、`--version`/`--check-ini`/启动日志双版本显示（详见 §8.12）。
- **后续维护项（非阻塞）**：
  1. CI：加静态分析（`gcc -fanalyzer` 或 clang `--analyze`）与可选 ASan 任务；真实 Actions 首跑校准 MSYS2 gcc 差异。
  2. 测试：把真实 U 盘插拔/RAW/写保护/满盘纳入现场回归脚本（当前无设备时 SKIP）。
  3. 代码残余：P2-11 写返回值、P2-19 exe_dir、P2-20 path_join、P2-21 util_ensure_dirs、P2-22 字符串/日志、P2-25 hits16/大文件投递；P3-9 gen_ident 规则（IDENT 预留）、P3-10 常量去重。
  4. 逻辑/文档残余：L10 标签/按钮语义、L11 热键自定义、L12 配置窗口保护、L18 无 GUI 恢复路径、L19 全局键编辑；DOC-05 跨屏 DPI、DOC-12..14 历史键说明。
  5. 版本：VERSIONINFO/资源加入构建号或短 hash（P3-7 完整化）。
  6. 发布：GitHub 远端接入、发布包（exe+示例 ini+docs）与 CHANGELOG 归档。

---
## 附录 A：复现证据（可复跑）

| 编号 | 实验 | 结果 | 状态 |
|---|---|---|---|
| E1 | 旧 cfg_save_key 对真实 ini 副本保存 | `0xC0000374` 堆破坏；首行 `ret=-1, olen 0→SIZE_MAX` | 已修复（P0-1） |
| E2 | 旧实现短行 ini 保存 | 每行 CRLF→CRCRLF→CRCRCRLF；新键落最后一个节；返回 TRUE | 已修复（P0-1/P1-1） |
| E3 | `READRULE=MUST_CONTAIN:8685;MIN_LEN:10` 加载 | 旧：只剩 17 字符；新：完整；op_read 短内容 FAIL | 已修复（P1-7） |
| E4 | pattern 49 组用例 | 旧：11 组失败 + ASan 越界；新：37/37 通过 + ASan 0 报告 | 已修复（P1-9） |
| E5 | `sizeof(CONFIG)=906,436` / reload 栈帧 906,616 B | newcfg 仍在栈上（gui.c:854） | 未修复（P1-15 残余） |
| E6 | `%.4hs` vs `%.4ls`（device.c VID/PID） | 旧：`VID:0 PID:0`/上级 Hub(05E3:0610)；第 3 批修后：F: `VID:058F PID:6387` | 已修复（第 3 批，含父链取真实 USB 设备） |
| E7 | 0 字节文件 `read_file_text` | FALSE、err=87 | 未修复（P2-15） |
| E8 | 只读源文件 CopyFileEx 两次 | 目标继承只读；第二次 err=5 | 未修复（P1 系列） |
| E9 | Windows SDK `winioctl.h` | 51=AdapterTemperature；协议类型无 USB；结构 40B 且第二字段 DataType | 未修复（P1-10） |
| E10 | 运行日志 1,877 行 | USBINFO 79/79 未知；启动 76 vs 退出 43；rwtest 读速 762–1956 MB/s | P1-10/11 已修复（第 3 批）；P1-14 未修复 |
| E11 | 内嵌默认配置 vs profiles | 内容不一致（旧 SRCPATH/READFILE/AUTORUN） | 未修复（P1-13） |
| E12 | 编码矩阵 | UTF-16BE/无 BOM 失败；非法 UTF-8 静默乱码 | 未修复（P1-2） |
| E13 | `-Wall -Wextra` 全工程 | 0 warning；强化告警仅有既存 cast-align | 基线保持 |
| E14 | 第 2 批结果/跳过语义（独立 harness + 团队 harness） | 配置屏蔽→OP_SKIP_CFG、程序降级/未实现/无法校验→OP_FAIL、空 READRULE→OK、多规则 e2e 保持；独立 35/35、团队 28/28 | 已修复（P1-6/P1-8） |
| E15 | 设备事件消息协议（隐藏窗口 + PeekMessage） | 同 tick“拔 F+插 G”→先 removed(F,0) 再 inserted(G,1)；无变化不发；位图正确 | 已修复（P1-4/L3） |
| E16 | 真实 F: USB 通讯模式（只读） | `dev_get_info(F:).vidpid=VID:058F PID:6387`；`usb_query_speed`→speed=3；日志“USB 2.0 High-Speed (480Mbps)” | 已修复（P1-10） |
| E17 | rwtest 直读介质（subst 临时盘） | OP_OK；日志“NO_BUFFERING/WRITE_THROUGH 直读介质，未走缓存”；测试文件已删除 | 已修复（P1-11） |
| E18 | `sha_or_md5_file` 健壮性 | “abc”=900150983cd24fb0d6963f7d28e17f72；独占锁定→FALSE；outlen<33→FALSE+INSUFFICIENT_BUFFER | 已修复（P1-12） |
| E19 | 格式化确认前校验 | FILESYSTEM=NTFS / VOLUME 12 字符 / 含 `\/` → OP_FAIL，无弹窗、无 FormatEx | 已修复（P1-18 校验部分） |
| E20 | label_read/usbinfo 异常盘 | F: → OP_OK；Q:（不存在）→ OP_FAIL“已移除/无法读取卷信息” | 已修复（P2-24） |
| E21 | 编码矩阵（UTF-8±BOM/UTF-16LE/BE±BOM/无 BOM/GBK/损坏 UTF-8/二进制/空/奇数/内嵌 NUL） | 团队 123/123（ASan 123）；Lead 独立 71/71；损坏 UTF-8→FALSE+偏移原因 | 已修复（P1-2/P2-14） |
| E22 | 配置解析健壮性（17 机型/50 键/80 字符节名/`[BAD`/`[]`/重复节/截断） | 均 WARN 且结构正确（16/48/忽略/合并）；真实 profiles 加载零 WARN | 已修复（P2-13/P2-17） |
| E23 | default_ini.h 与 profiles 一致性 + CI 门禁 | Lead 独立解析逐字节一致；`gen_default_ini.ps1 -Check` exit 0，篡改 exit 1；PS/Python byte-identical | 已修复（P1-13/ENG-07） |
| E24 | reload 栈帧 + 配置快照 | `-fstack-usage` reload 906,616→328 B；task_thread/op_thread 体内 0 处 g_cfg，快照在 CreateThread 前完成 | 已修复（P1-3/P1-15/L13） |
| E25 | CLI/单实例实测 | --version/--help/--check-ini（0/1）/--ini（空格路径）输出与退出码正确；第二实例 56ms 内 exit 0 且日志含“已有实例…已激活” | 已修复（P1-14/P3-5） |
| E26 | 日志并发/轮转/alt 兜底 | 4 线程×50 行=200 行完整；9MB 日志轮转 `.1`；主日志独占时写 `_alt.log` 且 GUI 消息不丢；`log_shutdown` 后仍可写 | 已修复（P2-23/P3-5） |
| E27 | post_owned_string 所有权（ASan） | 成功投递由接收方 free；NULL/无效/已销毁窗口 FALSE；1000 次失败投递无 UAF/double-free（ASan 18 项） | 已修复（P2-9） |
| E28 | 退出码 | `--check-ini` 0/1；GetMessage -1→1（代码审查）；强制退出→3（代码审查）；WM_CLOSE 正常关闭实测 exit 0 且日志 rc=0 | 已修复（P3-6） |
| E29 | GUI 状态一致性（Lead 独立 harness，ASan） | 27/27：标题 500+500=1002 不截断；进度 -7/0/55/100/150→0/0/55/100/100；reset 各状态；26 盘设备行有界；最小尺寸 880×600/1760×1200；STATIC 颜色生效；Ctrl+Alt+T/C 注册/注销 | 已修复（P2-1..8/P3-1/P1-17/L9/L16） |
| E30 | op_write 目录时机 + 捕获截断（Lead 独立 harness） | 14/14：源空/无匹配/源不存在→OP_FAIL 且目标目录未创建；有文件→OP_OK+建目录+复制；pattern 小 cap→truncated TRUE+前缀、大 cap 完整、NULL 安全；op_read 截断 WARN | 已修复（P3-3/P3-4/P2-16） |
| E31 | docs/UI功能状态说明.md | 307 行/30KB，覆盖 resource.h 50 个 #define（缺失 0）；GREP 无“连错 3 次锁定”承诺；IDENT 1402 标注预留；115 处 gui.c 行号区间校验通过 | 已交付（用户要求文件） |
| E32 | 测试入库 + CI 门禁 | tests/unit 3 组 + integration 4 组 + 4 条 CLI；`run_tests.ps1` clang/gcc 均 PASS=11 FAIL=0；Makefile `test/test-clang`；CI Run tests + CLI 冒烟 | 已修复（ENG-05/ENG-06 改善） |
| E33 | docs 目录化与链接 | docs/{user,reference,design,development,audit,archive}+索引/开发流程/CHANGELOG；README+主要文档 65 条相对链接 broken=0；旧路径 grep 0 | 已修复（DOC-09/ENG-12） |
| E34 | 构建元数据（P3-7） | `--version`/启动日志含 APP_BUILD_DATE/TIME；VERSIONINFO.FileVersion=1.0.0；clang/gcc 0 warning | 部分修复（P3-7） |
| E35 | 功能优化（软件 v1.1.0）端到端 | 窗口标题 `U盘拷盘工具 v1.0.0（软件 v1.1.0）`；IDC_CHK_AUTO_COPY/AUTO_TEST 灰只读（AUTO_TEST=1 勾选，=0 不勾）；IDC_STC_READRULE 在 AUTO_TEST=1 显示 `判定规则：MIN_LEN:1;MUST_CONTAIN:LEADFEATURE`、=0 为空；`--version` 软件 v1.1.0/FileVersion 1.1.0.0；`--check-ini` 显示配置版本 1.0.0；tests clang/gcc PASS=11 | 已完成（用户功能优化） |

## 附录 B：已核实“不是问题”的项

- MD5 实现正确（RFC1321 向量、分块、4GB+12345 字节与 .NET 一致）。
- 配置解析正常路径可用（UTF-8±BOM/GBK/UTF-16LE+BOM、大小写节名、重复键后者覆盖、空值、值含 `=`、超长行不崩溃）。
- 保存失败回滚安全（只读/ tmp 被占用 / 目录不存在 / 其它进程持有句柄 → FALSE 且原文件不变）。
- `EM_REPLACESEL` 在只读 RichEdit 上有效；RICHEDIT50W/RichEdit20W 实测无 64K 上限（“控制台 64K 冻结”不成立）。
- WM_APP_* 堆串所有权在 UI 侧释放完整；wparam 解码安全；CMD-A/B 分流正确。
- WM_APP_DEVCOUNT 的 mask/lparam 位定义与 device.h 一致（缺陷在“共享最终 mask”的协议）。
- DPI 感知声明位置正确；主窗口字体重建无泄漏；置顶/插拔计数与文档一致。
- 资源配对总体正确（FindClose/CloseHandle/free 未发现明确句柄泄漏）。

## 附录 C：审计与修复验证方法/局限

- 静态审计：逐行通读 8 个源文件 + 4 头文件 + resource.h + 2 ini + 4 文档；交叉核对 `WM_APP_*` wparam/lparam 与返回码约定。
- 编译取证：工程同款 flags、强化告警、clang 静态分析、`-fstack-usage`、ASan（修复后 pattern/config）。
- 隔离复现：全部在工程外 `audit_scratch\`；修复后用真实 config.c 编译 harness；未运行主程序对真实磁盘写/格式化。
- 运行日志：3 天 136 KB / 1,877 行，跨多个 alpha 构建，已按“当前 exe 与源码一致”前提筛选。
- 局限：未做真实 U 盘拔插/断电/多盘并发压力测试；FAT32 `MoveFileEx` 原子性、长路径 `GetModuleFileNameW`、FAT32 只读属性继承为“API 文档语义 + NTFS 实测”推断；`g_cfg` 撕裂未并发压测；GUI 弹窗/退出等待未在可交互会话端到端验证。

## 附录 D：状态索引（全部 ID → 状态）

| ID | 状态 | 一句话 |
|---|---|---|
| P0-1 | ✅ | 配置保存崩溃/毁文件已修（重写 cfg_save_key） |
| P0-2 | 🟡 | 格式化身份复核完成；手动选盘/确认线程/就绪等待残余 |
| P0-3 | 🟡 | 停止与退出等待主体完成；MD5 取消/30s 强退/轮询 Join 残余 |
| P1-1 | ✅ | 保存格式/编码/行尾/属性保持 |
| P1-2 | ✅ | text_decode 严格探测；损坏/二进制拒绝并给原因 |
| P1-3 | ✅ | TASKARG/OPARG 按值快照，worker 只读快照 |
| P1-4 | ✅ | 事件协议改为变化位图+先拔后插；运行中插入记待处理、成功后补跑 |
| P1-5 | ✅ | 身份绑定 + 读中换盘判定作废 + 设备丢失/写保护/空间不足错误分类（物理限制除外） |
| P1-6 | ✅ | 配置屏蔽=成功并计数；程序降级/未实现/无法校验=失败；四类结果码、无 rc\|= |
| P1-7 | ✅ | READRULE 分号链可用 |
| P1-8 | ✅ | 未知规则显式失败（FAIL）+投递判定/错误徽标 |
| P1-9 | ✅ | pattern `*`/大小写/越界修复 |
| P1-10 | ✅ | USB Hub IOCTL 匹配识别速度；真实 F: 实测 High-Speed；同 VID/PID 多设备未知 |
| P1-11 | ✅ | NO_BUFFERING/WRITE_THROUGH 直读；subst 验证；回退缓冲告警 |
| P1-12 | ✅ | 读错误必 FALSE；不一致重试一次；仍不一致删坏副本判失败 |
| P1-13 | ✅ | default_ini.h 重生成 + CI -Check 门禁 |
| P1-14 | ✅ | 单实例+激活现有窗口；轮询事件 Join；日志共享/alt/轮转 |
| P1-15 | ✅ | reload CONFIG 改堆；906KB→328B |
| P1-16 | ✅ | 恢复默认 .bak+原子写+回读；保存/恢复检查 reload、刷新标题/列表 |
| P1-17 | ✅ | Ctrl+Alt+T=读写测试、Ctrl+Alt+C=复制（单操作）；失败回滚+WARN；注销 |
| P1-18 | ✅ | 卷标/FILESYSTEM 校验；进度归零；成功后刷新+DEVREFRESH |
| P2-1、P2-3 | ✅ | reset_task_ui 全路径复位；单操作显示“复制完成/…” |
| P2-2、P2-4..P2-8 | ✅ | post_progress_pct 单入口；标题 1200；配置窗初始机型/DPI/字体；最小尺寸；RichEdit 回退；设备行有界 |
| P2-9 | ✅ | post_owned_string 所有权；gui/ops 全部替换；ASan 验证 |
| P2-10 | 🟡 | g_running：malloc/退出兜底已修；投递失败仍不检查 |
| P2-11 | ❌ | 写操作返回值不检查 |
| P2-12 | 🟡 | 格式化/复制错误码补了；其它错误上下文仍缺 |
| P2-13 | ✅ | 容量/畸形/重复节 WARN，合并语义正确 |
| P2-14 | ✅ | UTF-16BE/无 BOM 启发式识别与保存保 BOM |
| P2-15 | ✅ | 空文件/奇数长度/>64MB/损坏编码明确语义 |
| P2-16 | ✅ | pat_match_capture_ex 显式截断；op_read WARN；旧 API 兼容 |
| P2-17 | ✅ | 共享模式/ReadFile 检查/16MB 具体原因 |
| P2-18 | ✅ | 保存 tmp 原子性/唯一性 |
| P2-19..P2-22 | ❌ | util 基础函数（exe_dir/path_join/ensure_dirs/字符串） |
| P2-23 | ✅ | INIT_ONCE+日志句柄缓存+轮转+alt；UI 免 SetupAPI |
| P2-24 | ✅ | label_read/usbinfo 异常盘判失败 |
| P2-25 | ❌ | op_write hits16/大文件整段投递 |
| P2-26 | ✅ | 按用户要求无锁定；密码错误可重试+WARN；未解锁禁用保存/恢复默认 |
| P3-1 | ✅ | STATIC 改 WM_CTLCOLORSTATIC，实测生效 |
| P3-2 | ✅ | 四类结果码（OK/FAIL/STOPPED/SKIP_CFG），无 rc\|=；停止与错误分离 |
| P3-3 | ✅ | 先匹配后建目录，无匹配不建目录 |
| P3-4 | ✅ | 捕获截断显式 truncated + WARN |
| P3-5、P3-6 | ✅ | .rc/manifest/图标/版本/崩溃转储/CLI/日志轮转；退出码 0/1/3 |
| P3-7 | 🟡 | APP_BUILD_DATE/TIME 已入 --version/日志；版本号仍 1.0.0、VERSIONINFO 无构建号 |
| P3-8 | 🟡 | 格式化停止检查/错误码已加；其余未改 |
| P3-9..P3-10 | ❌ | 演示标识/重复代码 |
| L1 | ✅ | 配置屏蔽=成功、程序降级=失败（第 2 批政策实现） |
| L2 | ✅ | 事件盘符（手动限制保留） |
| L3 | ✅ | 变化位图+先拔后插，同 tick 不丢插入 |
| L4 | ✅ | 停止可用 |
| L5 | ✅ | 四类结果码 + fail/stop/skip/exec 聚合 |
| L6 | ✅ | 停止不再报成功 |
| L7 | ✅ | READRULE 链 |
| L8 | ✅ | 未知规则显式失败 |
| L9 | ✅ | 插入/开始统一清判定；单操作用操作名徽标 |
| L10 | ❌ | 标签/按钮语义 |
| L11 | ❌ | 热键语义 |
| L12 | ❌ | 配置锁定 |
| L13 | ✅ | 配置快照消除切机型并发 |
| L14 | ✅ | 保存/恢复检查 reload 并刷新标题/列表 |
| L15 | 🟡 | dev_get_info 对不存在盘根 FALSE；UI 用 volume helper；无就绪等待 |
| L16 | ✅ | 判定/内容/命中行/进度/监控状态统一复位 |
| L17 | ✅ | 按用户要求 1402 预留，非缺陷；文档/UI 状态标注 |
| L18 | ❌ | 无 GUI 恢复路径 |
| L19 | ❌ | 全局键能输不能存 |
| L20 | ✅ | worker 只读配置快照，reload 并发安全 |
| ENG-01..03、09 | ❌ | 架构/构建/发布 |
| ENG-05 | ✅ | 测试入库 tests/ + run_tests.ps1 + CI Run tests |
| ENG-06 | 🟡 | CI 已跑测试+CLI 冒烟+默认配置门禁；无静态分析/ASan |
| ENG-10 | ✅ | 资源/manifest/崩溃转储/CLI 已完成 |
| ENG-07 | ✅ | 默认配置漂移 CI 门禁 |
| ENG-04 | 🟡 | USB 部分改用官方结构/独立模块；fmifs 等仍待整理 |
| ENG-08 | 🟡 | config.c 已重写并补验证；历史/流程未变 |
| ENG-11 | 🟡 | 文档已同步本轮行为；其余虚标仍在 |
| ENG-12 | ✅ | docs 目录化后主要文档链接 broken=0；resource 热键/IDENT 注释同步 |
| DOC-01/02/03/04/06/07/08/09/10/11/15/16 | ✅ | 标签读取/CMD-B 预留/无锁定/保存保留内容/USB/编码/默认配置/文档链接/冒烟测试/热键/停止安全退出 |
| DOC-05 | 🟡 | 配置窗创建时按 DPI；跨屏不逐控件重排（已知限制） |
| 其它 DOC | ❌ | 见第 7 节 |































