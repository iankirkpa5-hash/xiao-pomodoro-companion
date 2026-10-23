# XIAO 项目与学习路线：学习、待办、验证与后续分支

更新：2026-10-13（北京时间）。这是原计划的更新版，按兴趣和实际证据推进，不设置考试式目标或固定赶工日期。

本仓库版来自用户在其他对话整理的同名文件，已补充本地核对结果。远端 main、本地实验工作区与板上固件分别记录；下列资料链接是参考入口，不代表本轮逐篇复核或实机验证。

**近期主线已完成：声音基线、PCM/WAV、16x 试听、C 实机回归与 Break 修正。现有 WAV 的首轮波形／FFT 已在 `codex/wav-fft` 本地验证并合并（PR #6, `21c0684`）；下一步先读图，再选择实验。**

2026-10-20 补充：当前实验分支 `codex/pcm-wav` 从 `2dbf5ef` 建立，第一步已完成 5 秒 PCM/WAV 传输校验和人工试听。侧向说话复测反馈为“音量小，但内容清楚”，画面、触摸、Settings、Bright 均正常；播放音量仍可改善。原始音频与去直流试听副本分开保存，不新增 AI，也不改声音等级阈值。见 [使用说明](pcm-recording.md) 与 [实测记录](experiments/2026-10-20-pcm-wav.md)。2026-10-21 补充：该实验已由 PR #2 合并（`6379372`），未建立新版本标签；播放音量仍是待改善项。2026-10-22 补充：试听预览新增有界 `--gain`（1–32x），默认由 8x 提高到 16x（+6 dB）；操作者确认 8x／16x／19.38x 三个版本内容都清晰，差别只在音量。原始采集、WAV 与声音等级阈值未改，最大请求仍受 -6 dBFS headroom 限制。见 [播放增益记录](experiments/2026-10-22-pcm-playback-gain.md)。2026-10-22 同日完成 C 实机回归：Reset、Stats 清除/递增、NVS 断电保持、完整 25 分钟 Focus；serial PASS，Done 1，设置还原为 Focus 50／Bright 60。Break 5:00 只显示约 60 秒后被 idle screen 覆盖，点 idle 会开新 Focus，列为 UX 候选（同日已由 A 修正）。见 [C 实测记录](experiments/2026-10-22-t08-manual-reset-stats-nvs-focus.md)。同日完成 A：Focus 完成后 Break 5:00 保留到使用者操作，不再 60 秒后进 idle；已用 25 分钟 Focus 实机验证，完成后约 5.5 分钟无 idle，触控后 `Break acknowledged` 并开始 Break。见 [break-idle 记录](experiments/2026-10-22-t09-break-idle-ack.md)。以下旧日期的快照保留历史语境，不代表当前分支或最新验收状态。

2026-10-23 补充：现有 16x preview 的 matched-gain A/B 完成；0-<80 Hz source-referred power 8,810.2 -> 37.3 counts²（-23.73 dB），占比 84.15% -> 2.88%；80-300 Hz -1.73 dB。操作者确认人声清楚、无变薄／鼻音／artifact，日常偏好 high-pass。未改固件／阈值／未重录。见 [low-cut 记录](experiments/2026-10-23-lowcut-comparison.md)。

长期方向：以 XIAO 边缘设备为起点，逐步连接信号分析、协议分析、TinyML、LoRa、FPGA，以及 Linux／ROS 2。它们是可以按需要选择的分支，不是每一项都必须依次完成的清单。

## 1．先分清五种事项

| 分类 | 含义 | 何时算完成 |
| --- | --- | --- |
| 已有基础 | 已有代码、已有功能，或过去明确完成的实验 | 写清证据和适用版本；不把过去结果外推到新版本 |
| 学习 | 建立概念、读资料、做教学仿真或小练习 | 能解释现象并留下例子；不要求并入日用固件 |
| 项目待办 | 已排入近期或后续队列的具体改动 | 有代码／文档产出，并达到对应验收标准 |
| 待验证 | 代码或方案已经存在，但某层测试还缺证据 | 补齐对应编译、仿真或实机证据；失败也如实记录 |
| 候选分支／参考 | 感兴趣，但尚未决定投入开发 | 有明确需求、条件和收益后，再转成项目待办 |

学习练习可以动手写代码；项目开发也会带来学习。这里按“本次要交付什么”分类，避免把读过一篇文章记为功能完成，或把每条早报都变成任务。

状态另行标注为：已确认、已实现待验证、待开发、待资料、未排期。**编译通过、模拟测试通过、实机通过和日用验收通过分别记录。SKIP、INCONCLUSIVE 都不等于通过。**

## 2．当前真实位置

以下仓库状态于 2026-10-13 核对，10-14 补充 [T02 离线分析](experiments/2026-10-14-t02-sound-comparison.md)，10-16 补充 [触摸 IRQ 修正与实机复测](experiments/2026-10-16-t04-touch-irq-serial.md) 与 [安静复测](experiments/2026-10-16-t05-quiet-retest.md)，10-17 补充 [拍手复测](experiments/2026-10-17-t02-clap-retest.md)、[Quiet 去抖修正](experiments/2026-10-17-t06-quiet-debounce.md) 与 [Reset／Stats／NVS 手工检查](experiments/2026-10-17-t07-manual-reset-stats-nvs.md)。原始 [T01 声音基线](experiments/2026-10-13-t01-sound-baseline.md) 仍为 FAILED；安静／说话／拍手三组复测均无 I²C 错误或混行，拍手 13/13，屏幕等级同步已实机确认，PR #1 已于 10-17 合并（`ec5cfe8`），本阶段验收闭环。

| 项目 | 状态与证据 | 仍需注意 |
| --- | --- | --- |
| XIAO ESP32S3 Sense＋Round Display | 已确认持有，是当前实验基础 | SX1262、FPGA、Jetson 等是否已到手，需分别确认，不能由学习兴趣推定已拥有 |
| 番茄机 v1.6 日用功能 | 按此前实机反馈，计时、设置、亮度、休息提示、触摸等已完成一轮稳定性验证 | 后续改动仍需回归检查 |
| 远端 main | 日用显示版本 v1.6；10-17 合并 PR #1（`ec5cfe8`），包含本地实验工作 | 合并不改变发布标签；新版本仍需按发布清单打标 |
| 本地工作区 | `codex/microphone-serial-data`，HEAD 为 `eb759a5`；实验工作已随 PR #1 进入 main，本地 main 已同步 | 分支保留作为实验线，不覆盖未提交工作；未将其视为新稳定版 |
| 麦克风采集 | 远端旧版输出 mean／RMS／peak／samples；本地实验已实现连续 10 ms 帧采集、区间 RMS／最大 peak 和 `timestamp_ms,rms,peak,level` | 本轮三组数据已保存，但有 I²C 错误与安静组高峰；缺运行固件唯一标识，尚非整体验收通过 |
| 摄像头 v1.7 实验 | 先前已做实验并形成结论 | 本计划不把“做过实验”解释为“发热已修复”；重启分支前先读原记录 |
| 实机反馈脚本 | [PR #1](https://github.com/iankirkpa5-hash/xiao-pomodoro-companion/pull/1) 已于 2026-10-17 合并（`ec5cfe8`），包含适配后的采集工具与实验记录 | 原始 `e7c73dd` 保留在 `backup/pr1-e7c73dd`；见 [工具说明](hardware-feedback-loop.md) |
| 脚本测试 | 本地 15 项通过，2 项 POSIX 伪终端测试在 Windows 跳过 | 模拟输入不等于开发板验收；SKIP 不等于通过 |
| T02 离线分析测试 | 新增 11 项通过；合计 Python 26 项通过、2 项跳过；现有网页解析 6 项通过 | 图表已目视检查，原始文件与固件未改；仍不替代实机验收 |
| 编译、烧录、真实串口采集 | 10-16 实际 build、cycle 均通过；10-17 完成三组声音复测、屏幕同步与手工检查，PR #1 已合并 | COM11；实机操作反馈与串口记录分别保存，验收闭环 |
| 实时声音曲线与离线对比 | 本地 [网页工具](../tools/microphone-monitor/README.md) 已有曲线与 CSV；T02 已读取 CLI 保存的数据，生成三组对比与时间／等级图 | 未重写网页；T02 未打开串口，屏幕与数据同步核对仍待实机 |
| 双向串口亮度控制 | 原计划已有方案／对话示例，落地和实机效果未确认 | 不作为启动声音基线实验的前置条件 |
| 圆屏清晰度问题 | 待当前照片、显示设置和复现条件 | 字体放大只是旧线索，不能直接认定原因 |

现有反馈工具支持 `doctor`、`build`、`capture`、`cycle`，可保留串口原始日志、CSV、JSON 报告和实际执行的编译／上传日志。

当前日志的关键边界：
- 远端旧版约每 500 ms 输出最新一帧；本地实验为连续 160 点帧（目标 16 kHz），上报区间聚合 RMS／最大 peak。两者不能混作同一基线。
- 采集工具保留电脑接收时间；新格式另保留板端 `timestamp_ms` 和实际 Quiet／Normal／Loud，旧格式这些列为空，不用推测值填充。
- 本地实验时间戳为区间最后处理帧的时间，不是精确声学事件时刻；仅凭四列格式不能证明正在运行哪份固件。帧序号、运行固件唯一标识仍缺失。
- 聚合可减少已采集峰值的上报遗漏，但仍需实测是否存在丢样／阻塞；日志没看到峰值，不等于麦克风没有响应。
- 已有串口记录包含 I²C 读取错误，原因尚未解决；新采集工具单独计数并如实报告失败，不以“界面看起来正常”掩盖日志。
- RMS／peak 是统计量，不能还原原始 PCM 或声音频谱；未经标定也不能当作声压级。
- 本地代码提交、构建得到的固件和板上正在运行的固件，是三个需要分别核对的对象。

## 3．近期执行队列：只推进这些项目待办

优先级表示先后关系，不代表截止日期。同时最多推进一个开发任务；可以穿插一个学习练习。

### T01｜P0｜现有固件的声音基线与采集工具验证

**分类：本轮已执行，有异常；实机验收未通过。**

执行：
- [x] 核对本地分支／提交，保留已有未提交改动；从 PR #1 固定提交引入工具文件，而非切换或合并分支。
- [x] 运行 `doctor`，本轮识别 PlatformIO Core 6.2.0 与 XIAO COM11；每轮仍须重新识别端口。
- [x] 只读取现有固件，保存一次未控制条件的预检及 quiet、speech、clap 各约 30 秒；用户确认按提示操作，距离很近，确切距离／拍手次数未记录。
- [x] 检查每轮 `serial.log`、`mic.csv`、`report.json`；人工反馈画面、触摸、倒计时正常，四份串口报告均因 I²C 错误为 FAILED。
- [x] 保留异常记录；T02 选出“声音记录与 I²C 错误混行”作为下一个调查问题，不把 FAILED 改成通过。

产出：三组原始日志、CSV、报告和实验条件说明。

验收：采集命令确实读到开发板数据；文件可对应到本轮条件；能分清“有响应、没捕获到瞬态、数据无效、证据不足”。不以放宽阈值或人为制造 PASS 为目标。

第一轮只采集即可；打开串口在部分环境中可能引起复位。电脑上同时只保留一个串口读取者。

### T02｜P0｜声音可视化与三组数据对比

**分类：离线对比已完成（2026-10-14）；正式实机验收未通过。** 见 [T02 图表与结论](experiments/2026-10-14-t02-sound-comparison.md)。本轮没有改固件、上传或调整阈值。

执行：
- [x] 根据三组 CSV 生成同尺度与独立尺度 RMS／peak 曲线；核对原始日志、报告、两套时间轴；缺口断线，不补零。
- [x] 绘图读取 CLI 已保存输出，保留原网页供后续实时观察；本轮不打开任何串口。
- [x] 标明安静、说话、拍手条件、电脑接收时间、板端报告间隔与原始等级。
- [x] 找到 6／2／16 条声音数据混在 I²C 错误行中，各组原日志可定位 60 个数据片段；全部 CSV 内部缺口都有对应混行。优先调查串口整行输出，不改声音阈值。
- [x] 后续复测网页数据与屏幕等级一致性；10-16／10-17 三组复测均无 I²C 错误，拍手 13/13（[拍手复测](experiments/2026-10-17-t02-clap-retest.md)），屏幕等级同步经 [Quiet 去抖修正](experiments/2026-10-17-t06-quiet-debounce.md) 实机确认。

产出：绘图入口、三组对比图、数据与结论。

验收：曲线与保存的数据一致；断流或异常不会被画成“正常连续采样”；关闭绘图后数据仍可回看。新格式保留板端声音等级，旧格式没有；屏幕实际观察仍单独记录，去抖／Loud 保持会使等级短暂滞后于能量。

### T03｜P1｜验证完整编译—烧录—采集流程，完成 PR #1 验收

**分类：已完成（2026-10-17）。实际编译—烧录—串口采集、人工／声学复测与 PR #1 合并（`ec5cfe8`）均已完成。** 见 [本轮记录](experiments/2026-10-16-t04-touch-irq-serial.md) 与 [手工检查记录](experiments/2026-10-17-t07-manual-reset-stats-nvs.md)。

前置：T01 基线已保存，确认本地固件内容、目标板、端口和回退版本。

执行：
- [x] 运行 `build`，检查实际构建日志和结果。
- [x] 运行 `cycle`，编译、上传和采集成功；后续 60 秒观察另有 120 笔完整数据，I²C 错误与混行均为 0。
- [x] 完整复测计时、触摸、设置、亮度以及麦克风响应；用户已确认短按、长按进 Settings、项目切换、Bright、Save；安静／说话／拍手三组已复测（拍手 13/13），屏幕等级同步已确认；Reset／Stats／NVS 按 [手工检查记录](experiments/2026-10-17-t07-manual-reset-stats-nvs.md) 通过（Done 递增路径留待日常使用验证）。
- [x] 如实记录问题并修复；PR #1 已审阅并合并（`ec5cfe8`）。

产出：一次真实构建／上传／采集记录及人工功能检查记录。

验收：每个阶段的结果可追溯；失败会终止后续阶段；串口检查通过与人工硬件检查分别有结果。软件测试通过不能代替这一步。

### T04｜P1｜根据实测改进采样与上报信息

**分类：部分已实现待验证。已有本地连续采集／窗口聚合改动先验证，不重复实现；其余依据 T01／T02 的证据决定。**

10-16 已完成一项最小修正：按 Seeed 官方驱动先判断触摸 INT，未按下时不读坐标。固件仅改 `input.cpp`；新输入测试 7 组、既有麦克风测试 8 组通过。没有屏蔽错误日志或更改音量阈值；后续异常仍须单独记录。

候选改动：
- [ ] 增加运行版本／构建标识、板端时间戳、帧或报告序号、有效样本数和实际声音等级。
- [ ] 明确时间戳是采样时刻、窗口结束还是发送时刻；明确序号对应采样帧还是上报消息。
- [ ] 记录必要的读取、计算、显示和通信耗时，先定位瓶颈。
- [ ] 如需减少短峰遗漏，比较提高上报频率与窗口聚合；区分“采集没覆盖”和“采集了但没上报”。
- [ ] 数据支持时才调整 Quiet／Normal／Loud 门槛，再按相同条件复测。

产出：字段说明、最小代码改动、修改前后对比证据。

验收：能解释数据来自何时、何种固件和何种统计窗口；需要解决的问题有所改善，且原有功能未明显退化。

## 4．学习路线：用小练习理解，不自动并入产品

| 编号 | 学习主题与当前起点 | 一个合适的小练习 | 学习成果 |
| --- | --- | --- | --- |
| L01 | 信号、RMS、相位、正弦相关与 FFT；已讨论 RMS 和频谱的差别 | 电脑合成 440 Hz＋1000 Hz；比较时域、频谱、不同窗长和相位 | 能解释两个频谱峰、采样率与频率分辨率，并说明 RMS 为何不够做 FFT |
| L02 | 数字逻辑与 FPGA；已讨论 D 触发器上升沿采样及反相反馈翻转 | 用 Digital 或 HDL 仿真做触发器、计数器、状态机；观察复位和波形 | 波形与状态变化能对应；仿真通过不等于时序收敛或上板成功 |
| L03 | I²C／UART／SPI 与逻辑分析 | Wokwi 导出 VCD，PulseView 解码 I²C；先手工找 START、地址和 ACK／NACK | 留下一份带通道说明的波形，能核对手工解码和工具结果 |
| L04 | 设备树、总线、驱动与供电控制 | 在电脑 Linux／WSL 中写教学 DTS，编译 DTB，再反编译；模拟控制引脚改变 | 能解释 compatible、供电引用、GPIO 极性和微伏单位；编译成功不代表真实外设工作 |
| L05 | TinyML 与监督学习；继续 UDL／Tiny Learning Machine 的小闭环 | 用有标签的声音统计数据做阈值基线，再尝试简单分类器 | 分清输入、预测、真值、loss、训练与推理，保留误判案例 |
| L06 | RISC-V 自定义指令、CFU Playground、性能分析 | 先读 profiling 示例，测热点；根据热点占比估计加速收益 | 理解端到端瓶颈和资源代价；不先安装整套 FPGA 工具链，也不把大板结果外推到小 FPGA |
| L07 | LoRa、数据表示与压缩 | 电脑比较原始数据、统计量、事件消息的字节数；或做缩图与调色板实验 | 区分载荷、协议开销、重传和空中时间；知道低码率图像可能牺牲哪些信息 |
| L08 | Linux／ROS 2 的进程与通信 | 环境准备好后，用电脑上的模拟数据做发布／订阅 | 理解设备节点、权限、进程、话题和消息；暂不要求 Jetson |
| L09 | 电路与数学基础 | 沿用书本进度，遇到采样、总线或功耗问题时补对应的线代、概率、微积分、频率与电路知识 | 留下定义、变量、直觉、图例与一个实际例子，不把阅读变成脱离项目的考试清单 |

学习顺序建议：L01 与声音主线直接配套；L02／L03 可按兴趣穿插；L04／L08 为 Linux 分支准备；L05 在数据可靠后深化；L06 在确有模型或计算热点时深化。

设备树的定位要明确：现有 PlatformIO／Arduino XIAO 项目通过代码、板级配置和驱动表达连接关系，**不会因学习设备树就需要给当前 XIAO 刷 DTS／DTB**。真实设备树应用留给采用设备树的 Linux 平台。

L03 注意：SCL／SDA 的分析仪通道由当前接线决定，D0／D1 或旧示例的 D2／D3 都不是协议的固定要求。Wokwi 对具体芯片和外设的仿真支持，在开做时重新核查；麦克风当前仍以实机验证为准。

## 5．后续项目待办：有方向，但不与近期主线同时开工

| 编号 | 事项 | 当前分类／优先级 | 开始条件与验收 |
| --- | --- | --- | --- |
| B01 | 双向串口：亮度控制、状态查询、ACK | 待实现核对／P2 | 基线采集稳定后做；确认板端执行与 ACK 对应，计时和触摸正常；“电脑已发送”不算执行成功 |
| B02 | 圆屏清晰度定位与改善 | 待资料、待验证／P2；若影响使用可提前 | 先给出当前正面照片、字号、亮度、显示代码和复现条件，再比较改动前后 |
| B03 | 短语按钮与触摸键盘 | 候选开发／P2，未开工 | 先做三个短语按钮→串口输出；再做 3×4 多次点按输入、超时确认、删除、空格；暂不承诺词典预测或中文输入法 |
| B04 | 原始 PCM 采集和麦克风 FFT | PCM/WAV 已合并；预览默认 16x；电脑端 FFT 已合并（PR #6）；low-cut A/B 完成，偏好 high-pass | 10-22：现有 WAV 波形、Hann FFT、40／250 ms 窗长对照和数值测试完成，见 [记录](experiments/2026-10-22-wav-fft.md)。10-23：matched-gain A/B，0-<80 Hz -23.73 dB、80-300 Hz -1.73 dB，无变薄感。未改固件、未重录；固件侧 FFT 未做。DMA 溢出事件未计数，不宣称绝对无丢样 |
| B05 | 仓库 README／路线图／历史版本记录同步 | 待办／随里程碑更新 | 把真实验收结果写回仓库，区分 main、草稿分支和板上版本；本次已更新本计划，仓库旧路线文档仍单列待同步 |
| B06 | 更完整的硬件自检 | 候选开发／P2 | 参考 RetroMeshDevice 的逐项检查；分别输出 PASS／FAIL／SKIP，不能将“没有失败”当作“全部已验证” |
| B07 | 完成后 Break 画面被 idle screen 覆盖 | 已修并实机验证（2026-10-22） | 采 A：完成后 Break 5:00 保留到使用者操作；已用 25 分钟 Focus 验证，完成后约 5.5 分钟无 idle，触控后 `Break acknowledged` 并开始 Break。 |

B03 的重点是输入体验和状态机。1.28 英寸圆屏空间有限，需要实际测试触摸目标、手指遮挡和误触；实体 T9 项目的布局不能直接照搬。实现 multi-tap 不依赖 MCP23017 驱动，也不代表完成带词典的 T9。

B04 要区分：电脑合成波形属于学习练习；把真实麦克风 PCM 稳定传到电脑，是独立的工程待办。

## 6．长期分支：达到进入条件再排任务

| 分支 | 学习可以先做什么 | 开发的进入条件 | 第一个实际交付 |
| --- | --- | --- | --- |
| 协议分析器 | L02／L03，理解边沿、时间戳、缓存和解码 | 明确待测协议、目标速率、输入电平与误差范围；核对 ESP32 外设能力 | 捕获一段已知 UART 或 I²C 并与可靠参考核对；记录漏边沿与缓冲溢出 |
| LoRa 离网通信器 | L07；阅读 RetroMeshDevice 的自检与板级配置 | 确认具体 SX1262 模块、对端节点、可用引脚、电源、天线和软件路线 | 两端可靠收发一条短文本，保存收发日志；再加入 UI 和必要的确认机制 |
| TinyML／边缘 AI | L05；先比较人工阈值和简单模型 | 有足够且标注清楚的数据、可比较的基线与实际识别目标 | 一个可复现实验：预测与真值对照，记录错误、推理耗时、内存与能耗条件 |
| FPGA 协同 | L02／L06；先仿真计数器、状态机、FIFO、CDC | ESP32 的采集或计算瓶颈已测出；确认目标 FPGA 型号、资源和工具支持 | 仿真与上板各有结果的小模块，再用 UART／SPI 与 ESP32 交换数据 |
| Linux／ROS 2／设备树实机 | L04／L08；电脑离线练习即可开始 | 电脑 Linux／WSL 或 Linux 开发板可用；串口数据格式与访问稳定 | 一个串口桥接节点发布声音状态；之后再加控制命令回路 |
| 摄像头／更强计算平台 | 阅读旧实验结论，学习采集、存储、功耗与模型部署 | 有明确图像任务；重新评估热、功耗、内存与 GPIO 占用 | 一项边界清楚的测量或识别实验，再决定是否需要新硬件 |

LoRa 分支的架构决策：
- 普通 LoRa 点对点程序可用于硬件和收发调试，但不会自动兼容 Meshtastic 或 MeshCore。
- 接入 Meshtastic 时，再选择将圆屏交互适配到其固件中，还是由当前设备通过接口控制独立节点；后者增加硬件、接线和功耗。
- SX1262 的 SPI 之外还有片选、BUSY、中断、复位及模块相关控制；先做引脚资源表，并纳入圆屏、SD、麦克风和摄像头的实际使用情况。
- 发射参数按所在地规则和具体硬件核查；本计划不把其他项目的频段、功率、平均电流或续航直接套到本机。
- 先文本和状态消息，再考虑缩略图。图像传输和生成式重建暂不进入近期开发队列。

Linux 分支不要求先购买 Jetson；电脑上的串口桥接、设备树文件实验和 ROS 2 消息练习已有学习价值。FPGA／RISC-V／CFU 也不要求先买大板或一次装齐全部工具链。

## 7．近期资料怎样归类

| 资料／讨论 | 归类 | 当前用途 |
| --- | --- | --- |
| PySDR 中文频域教程、Seeing Circles, Sines, and Signals | 学习 L01 | 建立正弦、相关、采样、FFT 和泄漏的直觉；配合 Python 实验 |
| Digital 的 D 触发器、Wokwi＋PulseView | 学习 L02／L03 | 在波形中理解状态与协议，再考虑实际捕获器 |
| Seeed 设备树与机器人课程 | 学习 L04／L08 | 为后续 Linux 分支准备；不会自动变成当前 XIAO 固件改造任务 |
| Silicon Labs Simplicity AI SDK | 工程方法参考 | 借鉴“编译—烧录—日志—改进”；当前落实为本项目的 PlatformIO／串口 CLI，不以换生态为前提 |
| GPT-Synopsys／AI 操作 EDA | 趋势与方法参考 | 理解可信工具结果和验证反馈的重要性；不列为当前芯片设计开发任务 |
| CFU Playground、自定义 RISC-V 指令论文 | 学习 L06／远期参考 | 先 profiling，再评估硬件加速；模型热点和 FPGA 资源必须实测 |
| RetroMeshDevice | 工程参考，支撑 B03／B06／LoRa 分支 | 借鉴独立自检、输入状态机、引脚配置和整机功耗测量；不直接刷它的固件 |
| MCOimg、AEIC-SE 图像传输 | 学习 L07／候选远期实验 | 分清低分辨率像素编码与生成式重建；模型文件、重建错误、包数和链路测量分别评估 |
| RK3588、Jetson、其他 AI／机器人硬件 | 平台候选 | 由实际模型、接口、功耗和预算需求驱动选型，不因早报出现就追加采购 |

AEIC-SE 的资料更正已纳入：当前 Seeed 文档确实链接了模型，不能继续表述为“模型未公开”。MCOimg 的小调色板图与 AEIC-SE 的生成式重建是不同方案；清晰的生成图像仍可能改变物体细节，不能当作精确现场记录。

## 8．每轮只做一个可验证的改进

1. **记录起点**：代码版本、板上固件、工具版本、接线、供电、参数、操作条件。
2. **提出问题**：写出当前现象、一个解释和希望观察到的变化。
3. **作最小改动**：尽量只改变一个因素，保留可回退版本。
4. **收集证据**：日志、CSV、曲线、VCD 或照片，区分真实板与模拟输入。
5. **判断结果**：通过、失败、跳过、证据不足分别记录；做与改动有关的回归检查。
6. **同步状态**：只有对应验收完成才改为完成；一个新资源先归类，再决定是否新增任务。

| 实验记录字段 | 记录内容 |
| --- | --- |
| 任务编号／分类 | 如 T01：待验证，或 L01：学习 |
| 本次问题 | 本次只解决什么，为什么现在做 |
| 版本与条件 | 提交、板上版本、参数、端口、接线、测试动作 |
| 改动 | 涉及文件、函数或硬件配置 |
| 证据 | 原始日志、CSV、图、报告和观察 |
| 结论与限制 | 哪层测试通过；哪些仍未验证；是否存在反例 |
| 下一步 | 保留、回退，或只继续调查哪一个问题 |

## 9．下一次动手入口

**T02 离线对照、10-16 的最小 IRQ 修正、10-16／10-17 的安静／说话／拍手三组复测、Quiet 去抖修正、Reset／Stats／NVS 手工检查、PCM/WAV（PR #2）、16x 试听（PR #4）、C 实机回归（`fee4a27`）与 Break 修正（PR #5）均已完成；拍手 13/13 与屏幕等级同步均已实机确认；PR #1 已合并（`ec5cfe8`）。** 现有 WAV 波形／FFT 首轮本地验证也已完成并合并（PR #6）；先读图，再选下一步，不要求先完成双向串口、T9 或设备树，也暂不接入 STT／AI。以下保留 T01 复测入口，旧 FAILED 报告不改写。

工具已在 main，先检查工作区和版本，不直接切换或合并 PR。以下命令在项目根目录执行；也可用已有 PlatformIO Python 环境，无需重复安装依赖：

```powershell
py -m pip install -r tools/requirements-hardware.txt
py tools/hardware_loop.py doctor
py tools/hardware_loop.py capture --port COM11 --seconds 30 --label quiet
```

若需复测，用 doctor 识别出的实际端口替换 COM11，再用 speech、clap 标签各采一轮。当前 WAV 分析已经使用 test-runs/ 里保存的录音完成，不需要重新录音。

第一轮验收点：三组日志与实验条件可对应、报告可读、已记录屏幕／触摸／计时状态；异常也可以成为本轮有效成果，不要求所有检查一次变绿。

10-22 后续：上述 WAV 波形／FFT 入口已完成首轮验证并合并（PR #6, `21c0684`），见 [分析记录与复现命令](experiments/2026-10-22-wav-fft.md)。保留上方流程作为此前的计划快照。下一步先理解波形、低频成分和窗长权衡；10-23 已完成 matched-gain A/B，操作者偏好 high-pass（见 [low-cut 记录](experiments/2026-10-23-lowcut-comparison.md)）。无需重录，不接 STT／AI，也未开始固件侧 FFT。

## 10．参考入口与核对范围

本次实时核对了项目 main 和 PR #1；以下其他链接作为学习入口保留，实际动手时再核对版本、环境和支持范围。近期 RetroMeshDevice 与 AEIC-SE 已在讨论中查阅源码或一手资料，但没有据此声称在我们的设备上运行过。

- [当前项目仓库](https://github.com/iankirkpa5-hash/xiao-pomodoro-companion)
- [已合并的 PR #1](https://github.com/iankirkpa5-hash/xiao-pomodoro-companion/pull/1)
- [仓库内的实机反馈使用说明](https://github.com/iankirkpa5-hash/xiao-pomodoro-companion/blob/main/docs/hardware-feedback-loop.md)
- [PySDR 中文：频率域](https://pysdr.org/zh/content-zh/frequency_domain)
- [Seeing Circles, Sines, and Signals](https://jackschaedler.github.io/circles-sines-signals/)
- [Digital](https://github.com/hneemann/Digital)
- [Wokwi：逻辑分析仪指南](https://docs.wokwi.com/guides/logic-analyzer)
- [Wokwi：ESP32 支持表](https://docs.wokwi.com/guides/esp32)
- [原计划中的 Wokwi 逻辑分析仪示例](https://wokwi.com/projects/325933824665977428)
- [Seeed：设备树与底板适配](https://www.seeedstudio.com/blog/2026/09/30/nvidia-jetson-device-tree-why-swapping-the-carrier-board-may-break-things/)
- [Linux：设备树使用模型](https://docs.kernel.org/devicetree/usage-model.html)
- [Seeed：Jetson 与 ROS 2 第一模块](https://www.seeedstudio.com/blog/2026/09/30/jetson-powered-mobile-robot-full-stack-development-in-practice-module-1-jetson-ros2/)
- [CFU Playground](https://github.com/google/CFU-Playground)
- [RetroMeshDevice 源码与硬件](https://github.com/myembeddedstuff/RetroMeshDevice)
- [RetroMeshDevice 独立自检程序](https://github.com/myembeddedstuff/RetroMeshDevice/blob/main/firmware/FW_Test/main/main.c)
- [RetroMeshDevice 板级配置](https://github.com/myembeddedstuff/RetroMeshDevice/blob/main/firmware/FW_Meshtastic/variants/esp32s3/retroMeshDevice/variant.h)
- [作者的硬件验证与功耗记录](https://myembeddedstuff.com/meshtastic-build-off-2026-hardware-bring-up)
- [Seeed：MeshCore 图像传输](https://wiki.seeedstudio.com/meshcore_ai_image_transmission_x1/)
- [MCOimg 作者的技术讨论](https://github.com/meshcore-dev/MeshCore/issues/2783)
- [AEIC-SE ONNX 模型卡](https://huggingface.co/zjs81/aeic-se-onnx)
- [AEIC 原始研究代码](https://github.com/LuizScarlet/AEIC)
- [Silicon Labs AI SDK 概览](https://docs.silabs.com/simplicity-ai-sdk/latest/simplicity-ai-sdk-start/)
- [GPT-Synopsys 公告：工程方法参考](https://news.synopsys.com/2026-09-30-OpenAI-and-Synopsys-Announce-GPT-Synopsys-Frontier-Intelligence-to-Revolutionize-Chip-Design)
