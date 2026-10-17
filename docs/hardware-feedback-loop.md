# XIAO 实机反馈闭环：本地双格式验证版

目标：让本地编程智能体通过可重复的命令完成编译、上传、限时采集和日志检查，并留下证据。适用于本仓库的 PlatformIO / Arduino 工程；无需安装 Silicon Labs SDK，也无需先切换到 ESP-IDF。

## 来源与本地兼容修改

工具来自 [PR #1](https://github.com/iankirkpa5-hash/xiao-pomodoro-companion/pull/1) 的固定提交 `e7c73dd2618e09a0d84d8595dafb545e76144f63`；该 PR 于 2026-10-17 以合并提交 `ec5cfe8` 进入 main，当前仓库中的工具即为适配后的版本。2026-10-13 在当前实验工作区加入新旧格式兼容、设备时间检查、采集诊断、I²C 错误记录和 `--notes`，没有修改 MCU 固件。依赖已在现有 PlatformIO Python 环境中可用，无需为本轮重新安装。

## 当前边界

- 兼容旧版 `Mic: mean=... rms=... peak=... samples=...` 与实验版 `timestamp_ms,rms,peak,level`。不修改 MCU 源码、麦克风阈值或显示行为。
- 旧版每约 500 ms 输出最新一帧，预期 160 点；实验版连续采集 160 点帧，上报区间 RMS 和最大 peak。仅凭四列格式不能证明固件实现及统计窗口，需核对运行版本。
- 两种记录都保留电脑接收时间；新格式另保留设备时间戳和实际标签。没有提供的 mean／samples／时间戳／标签留空，不填零、不重算声音等级。
- 本地实验时间戳是区间最后处理帧的时间，不是精确声音发生时间。帧序号和运行固件唯一标识尚未上报，报告中的本地源码哈希不等于板上固件身份。
- 区间聚合减少已采集峰值的上报遗漏，但不能证明采集无空档或无丢样。没有观察到 peak 上升，不能据此直接改阈值或判定麦克风坏了。
- 本版不采集 PCM，RMS/peak CSV 不能做原声 FFT。真实屏幕清晰度、触摸、计时、供电和麦克风响应仍需人观察。

## 本地准备（Windows / PowerShell）

在 VS Code 打开仓库。XIAO 通过数据线连接这台电脑，关闭 PlatformIO 串口监视器及其他占用串口的程序。

```powershell
py -m pip install -r tools/requirements-hardware.txt
py tools/hardware_loop.py doctor
```

本机也可直接使用已安装的 Python 环境：

```powershell
& "$env:USERPROFILE\.platformio\penv\Scripts\python.exe" tools/hardware_loop.py doctor
& "$env:USERPROFILE\.platformio\penv\Scripts\python.exe" tools/hardware_loop.py capture --port COM11 --seconds 30 --label quiet --notes "Board on desk; no speech or tapping; USB power"
```

`doctor` 显示 Python、PlatformIO 位置及串口列表，**不会自动选择开发板**。脚本会寻找 PATH 和 `.platformio/penv` 下的 PlatformIO。找不到时安装 PlatformIO Core，或通过 `--pio` 指定已安装的可执行文件，例如：

```powershell
py tools/hardware_loop.py doctor --pio "$env:USERPROFILE\.platformio\penv\Scripts\platformio.exe"
```

Linux/macOS 将 `py` 换为对应的 `python3`，串口使用实际的 `/dev/...`。Python 需为 3.9 或更高版本。脚本不会自动提权或修改串口权限。

## 第一次只读取现有固件

用 `doctor` 查到的端口替换示例 `COM11`：

```powershell
py tools/hardware_loop.py capture --port COM11 --seconds 30 --label quiet
```

这个命令不会烧录，也不会发送控制命令；打开串口在某些系统上可能引起复位。分别运行三次，把标签换成 `quiet`、`speech`、`clap`，并做对应刺激。标签仅记录你的实验条件，不是自动识别结果。保持距离、音源和供电条件尽量一致。

每次运行创建独立的 `test-runs/<UTC时间>-<随机后缀>/`：

| 文件 | 内容 |
| --- | --- |
| `serial.log` | 串口原始字节，包括异常、启动和普通输出 |
| `mic.csv` | host_utc、host_elapsed_s、record_format、device_timestamp_ms、mean、rms、peak、samples、level；缺失字段为空 |
| `report.json` | schema v2：分支、提交、工作区状态、源码／工具哈希、操作备注、检查结果；包含帧吞吐量诊断与 I²C 错误计数 |
| `build.log` / `upload.log` | 仅在实际执行相应阶段时生成 |

## 完整循环

只编译：

```powershell
py tools/hardware_loop.py build
```

编译通过后上传，再读取 30 秒。**`cycle` 会替换所选板上的应用固件**，所以先确认端口和工作区内容：

```powershell
py tools/hardware_loop.py cycle --port COM11 --seconds 30 --label baseline
```

`cycle` 要求显式提供端口，覆盖 `platformio.ini` 里的历史 COM11；编译失败不会上传，上传失败不会继续采集。上传后若串口变成另一端口，可用 `--monitor-port COM12` 指定。脚本不会猜测或切换到其他板，不执行整片擦除，不修改接线，不无限重试烧录。

默认构建最长 1200 秒、上传最长 180 秒、连接串口重试窗口 10 秒；采集最长 600 秒。Ctrl+C 会结束并保留报告。构建/上传日志可能包含本地路径，向外分享前自行检查。

## 如何解释检查结果

| 结果 / 退出码 | 含义 |
| --- | --- |
| `BUILD_OK` / 0 | 构建命令成功；不代表已上传或实机正常 |
| `PASS_SERIAL_SMOKE` / 0 | 有足够且数值有效的 Mic 记录，未观察到列出的异常；只代表串口冒烟检查通过 |
| `INCONCLUSIVE` / 2 | 记录太少、接收间隙过长、帧长不符、统计完全不变或疑似复位，需要进一步检查 |
| `FAILED` / 1 | 构建/上传/采集失败，或观察到初始化失败、麦克风／I²C 读取错误、崩溃、无效数据等；不等于完全没收到声音数据 |
| `ABORTED` / 130 | 人工中断；不当作通过 |

默认至少 10 条有效记录、相邻及末尾接收空白不超过 2 秒、帧长预期 160。可用 `--min-records`、`--max-gap`、`--expected-samples` 显式调整，但先说明调整依据；不要为了让测试变绿而放宽条件。持续全零或恒定数据只判“需要检查”，因为真实安静与硬件无输入需通过受控声音刺激区分。

帧长检查仅适用于明确上报 samples 的旧格式；CSV 的缺失样本数检查记为 SKIP，而非通过。设备时间回绕、回退、重复与过大上报间隔分别检查。`Mic capture: frames=... samples=... elapsed_ms=... read_errors=...` 是吞吐量诊断，不是音量记录，也不证明没有 DMA 丢样。

采集标签是预定条件，实际动作需人确认。未协调动作时使用 `uncontrolled_baseline`，不要标作 quiet。屏幕正常与 I²C 日志错误可以同时存在；分别记录，不互相抵销。

## 交给本地编程智能体

在连接 XIAO 的电脑上，让本地代理运行这些命令并读取输出文件。本次是在 Windows 本机执行，已能读取 COM11；远端／云端代理则不能仅凭 GitHub 写权限访问本机 USB。

任务顺序：

1. 读取当前代码和 `git status`，运行 `doctor` 确认工具与端口；保留用户未提交改动。
2. 先 `capture` 保存现有固件基线。记录安静、说话、拍手的真实条件，以及屏幕/触摸观察。
3. 从证据提出一个具体问题，每轮只修改与问题有关的最小范围。
4. 按任务授权运行 `build` 或 `cycle`，读取完整 `report.json` 和相关日志；失败时不要跳过或虚报通过。
5. 在相同条件下比较前后数据，明确哪些来自真实板、哪些是模拟测试、哪些尚未验证。
6. 通过串口检查后，再由人确认计时、触摸、设置、亮度和声音响应；保留回退所需的基线提交。

后续先核对已有设备时间戳和状态标签，再按证据考虑帧序号、运行版本/构建标识及耗时统计。GDB/JTAG、功耗仪和 MCP 封装在确有需求后再加。

## 本地脚本测试

```powershell
py -m unittest discover -s tools/tests -v
```

2026-10-13 本地运行：15 项通过，2 项 POSIX 伪终端测试在 Windows 跳过。覆盖旧格式、新 CSV、分段/超长行、无效数值、设备时间回绕/回退/重复、混合格式、I²C 错误、吞吐量诊断、保存文件/释放串口、失败后不继续上传/采集。模拟输入不构成硬件验收。

参考：[PlatformIO run](https://docs.platformio.org/en/latest/core/userguide/run/index.html)、[pySerial API](https://pyserial.readthedocs.io/en/latest/pyserial_api.html)、[Silicon Labs AI SDK 概览](https://docs.silabs.com/simplicity-ai-sdk/latest/simplicity-ai-sdk-start/)。
