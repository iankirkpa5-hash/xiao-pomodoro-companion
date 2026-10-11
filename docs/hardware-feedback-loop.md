# XIAO 实机反馈闭环：第一版

目标：让本地编程智能体通过可重复的命令完成编译、上传、限时采集和日志检查，并留下证据。适用于本仓库的 PlatformIO / Arduino 工程；无需安装 Silicon Labs SDK，也无需先切换到 ESP-IDF。

## 当前边界

- 第一版兼容 `fc32445` 的 v1.6 固件，不修改 MCU 源码、麦克风阈值或显示行为。
- `Mic: mean=... rms=... peak=... samples=...` 每 500 ms 输出最新一帧。当前每帧目标为 160 点；历史测试文档中的 256 点不能当成本次代码参数。
- 日志没有设备采样时间、序号、Quiet/Normal/Loud 标签和运行固件标识。因此 CSV 时间是电脑接收时间；记录间隔不能证明采样连续性，报告中的代码提交也不能证明板上正运行这个版本。
- 500 ms 输出最新帧可能漏掉拍手瞬态。没有观察到 peak 上升，不能据此直接改阈值或判定麦克风坏了。
- 本版不采集 PCM，RMS/peak CSV 不能做原声 FFT。真实屏幕清晰度、触摸、计时、供电和麦克风响应仍需人观察。

## 本地准备（Windows / PowerShell）

在 VS Code 打开仓库。XIAO 通过数据线连接这台电脑，关闭 PlatformIO 串口监视器及其他占用串口的程序。

```powershell
py -m pip install -r tools/requirements-hardware.txt
py tools/hardware_loop.py doctor
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
| `mic.csv` | 接收时间、mean、rms、peak、samples |
| `report.json` | 代码提交与工作区状态、源码哈希、条件、检查结果与局限 |
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
| `FAILED` / 1 | 构建/上传/采集失败，或观察到初始化失败、读取失败、崩溃、无效数据等 |
| `ABORTED` / 130 | 人工中断；不当作通过 |

默认至少 10 条有效记录、相邻及末尾接收空白不超过 2 秒、帧长预期 160。可用 `--min-records`、`--max-gap`、`--expected-samples` 显式调整，但先说明调整依据；不要为了让测试变绿而放宽条件。持续全零或恒定数据只判“需要检查”，因为真实安静与硬件无输入需通过受控声音刺激区分。

## 交给本地编程智能体

在连接 XIAO 的电脑上，让本地代理运行这些命令并读取输出文件。此聊天的远端执行环境不能直接访问你电脑的 USB。

任务顺序：

1. 读取当前代码和 `git status`，运行 `doctor` 确认工具与端口；保留用户未提交改动。
2. 先 `capture` 保存现有固件基线。记录安静、说话、拍手的真实条件，以及屏幕/触摸观察。
3. 从证据提出一个具体问题，每轮只修改与问题有关的最小范围。
4. 按任务授权运行 `build` 或 `cycle`，读取完整 `report.json` 和相关日志；失败时不要跳过或虚报通过。
5. 在相同条件下比较前后数据，明确哪些来自真实板、哪些是模拟测试、哪些尚未验证。
6. 通过串口检查后，再由人确认计时、触摸、设置、亮度和声音响应；保留回退所需的基线提交。

下一版优先考虑设备时间戳、帧序号、实际状态标签、运行版本/构建标识，以及运行耗时统计。GDB/JTAG、功耗仪和 MCP 封装在确有调试需求并核对引脚/工具支持后再加；普通 CLI 已可由本地代理调用。

## 本地脚本测试

```powershell
py -m unittest discover -s tools/tests -v
```

这些测试覆盖分段串口数据、超长行、NaN/无效样本、崩溃、数据停滞和失败后不继续上传/采集。安装 pySerial 的 Linux/macOS 环境还会运行两个伪终端测试，实际经过 pySerial 读取并验证 CSV/报告；Windows 会跳过这两个测试。模拟日志不构成硬件验收。

参考：[PlatformIO run](https://docs.platformio.org/en/latest/core/userguide/run/index.html)、[pySerial API](https://pyserial.readthedocs.io/en/latest/pyserial_api.html)、[Silicon Labs AI SDK 概览](https://docs.silabs.com/simplicity-ai-sdk/latest/simplicity-ai-sdk-start/)。
