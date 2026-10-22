# Roadmap

This engineering roadmap records implemented features, experiment results, current work, and optional product extensions. The [Flexible Learning Map](esp32-flexible-learning-roadmap.md) is the maintained, evolving direction for circuits, mathematics, embedded systems, FPGA, and AI; it is expected to change further, and each new revision replaces the file while a short repository-sync note keeps the stated status current. This document and the README track repository progress and are synced whenever the map changes.

The [2026-10-13 Project and Learning Plan](xiao-linux-mini-plan.md) is the current execution guide. It separates T01-T04 project work from L01-L09 learning and optional branches. The version-family ideas below are candidates, not a required development sequence.

The long-term architecture is inspired by projects such as OpenBuddy: an ESP32-class physical terminal connected to a Python backend, a web dashboard, and eventually a voice / AI pipeline. The XIAO device should remain a reliable local terminal. Heavy AI, voice processing, API keys, dashboards, and multi-device orchestration should live outside the microcontroller.

## Version Strategy

These version families describe possible product directions, not a required learning order or delivery schedule. Experiments can branch off and return to the daily-use firmware as interest and evidence develop.

```text
v1.x = Offline desktop terminal
v2.x = XIAO Wi-Fi event layer
v3.x = Local Gateway / Dashboard layer
v4.x = Voice / AI / Coding Companion layer
v5.x = Local model and multi-device system layer
```

## Current Status

`XIAO Pomodoro Companion` has a daily-use offline firmware baseline with a passive microphone sound indicator.

```text
Main branch: main
Current daily-use line: v1.6
Displayed version: v1.6 (corrected in fc32445)
Camera experiment: v1.7-camera-test, separate from main
Sound baseline: merged to main (PR #1, ec5cfe8)
PCM/WAV: merged (PR #2, 6379372); 16x host preview default (PR #4)
Manual C + Break idle fix: verified (2026-10-22; PR #5, 3e343eb)
Waveform / FFT: first offline analysis verified locally (codex/wav-fft; not yet merged)
Later candidate: v1.8-hardware-feedback, not implemented
```

Implemented capabilities:

```text
Pomodoro Focus / Break
Automatic session switching
Round display UI
Touch interaction
Settings menu
Focus / Bright / Reset / Stats / Save
NVS persisted settings
Done counter
Stats clear
Idle screen
Wellness prompts
PDM microphone with centered RMS / peak
Passive Quiet / Normal / Loud indicator
Release checklist
README / GitHub gallery
Camera module removal notes
Round Display KE switch / brightness notes
```

Current hardware state:

```text
Seeed Studio XIAO ESP32S3 Sense
Sense expansion board installed for the PDM microphone
Seeed Round Display for XIAO
External 2.4 GHz antenna
Only the removable camera module is disconnected
Round Display KE switch set to ON / KE
No battery
No microSD
No RTC coin cell
USB-C power
```

### Historical Tag Note

Tag `v1.6-microphone-test` points to `b033798`. It contains the microphone feature, but its Settings screen still displays `v1.5`. Commit `fc32445` subsequently corrected `src/version.h` to `v1.6` and updated the hardware notes. Keep the published tag unchanged to preserve history; the display mismatch does not apply to current `main`.

### Completed: Short PCM Recording And Listening

The sound-data baseline closed on October 17 through PR #1. On October 20,
`codex/pcm-wav` branched from `2dbf5ef` for a bounded recording experiment:

1. Preserve up to five seconds of raw samples from the existing microphone task in PSRAM.
2. Download with bounded serial commands and CRC checks, then save the unchanged PCM in a local WAV.
3. Keep original audio separate from optional DC-removal / fixed-gain listening previews.
4. Record manual sound and Pomodoro checks independently of transport checks. The operator found the side-position speech retest clear but quiet, and confirmed screen, touch, Settings and Bright operation. Comfortable playback loudness remains open.

See [recorder usage and boundaries](pcm-recording.md), the [October 20 record](experiments/2026-10-20-pcm-wav.md), and the [playback gain record](experiments/2026-10-22-pcm-playback-gain.md). This is not a new daily-use release, an AI mode, or a claim of lossless physical sampling. Merged into `main` through PR #2 (`6379372`); no new tag was created. Host preview loudness is addressed with a 16x default and a bounded `--gain`; device audio output was not assessed.

### Completed Locally: Saved-WAV Analysis

The subsequent [saved-WAV waveform / FFT record](experiments/2026-10-22-wav-fft.md)
documents a host-only analysis of the existing five-second recording. Waveform,
Hann spectrum, window-length comparison and numerical tests are complete locally
on `codex/wav-fft`, not yet merged. Firmware, original audio and v1.6 remain
unchanged. Low-frequency content is observed, not assigned a hardware cause;
comparison of the existing listening previews is an optional next step.

### Manual C Checks

On 2026-10-22, Reset, Stats clear/increment, NVS persistence, and a full 25-minute
Focus session were repeated on `main` (`53b5055`). Serial checks passed, Done
reached 1, and the settings were restored to Focus 50 / Bright 60. The Break 5:00 screen originally gave way to the idle screen after 60 seconds;
a follow-up now keeps it visible until the operator acts, and a short tap then
starts Break. See the [C record](experiments/2026-10-22-t08-manual-reset-stats-nvs-focus.md)
and the [break-idle record](experiments/2026-10-22-t09-break-idle-ack.md).

### Previous Sound-Data Evidence

[PR #1](https://github.com/iankirkpa5-hash/xiao-pomodoro-companion/pull/1) merged into main on October 17 as `ec5cfe8`. Its [capture tool](hardware-feedback-loop.md) was brought into the workspace at `e7c73dd`, adapted for both serial formats, and merged back together with the local work. The original `e7c73dd` stays on `backup/pr1-e7c73dd`. The following paragraphs preserve the dated investigation, including failures later resolved.

This task does not require another microphone classifier, a new UI, or AI. The sound levels describe intensity, not a reliable identification of the sound source. Debounce and the Loud hold interval mean the label can briefly outlast the high-energy sample that triggered it.

The [serial-data record](microphone-serial-data.md) includes earlier format checks and the September continuous-capture work. On October 13, a 30-second uncontrolled preflight received 50 CSV rows and six throughput diagnostics of about 80,000 samples per five seconds, with zero microphone read errors. It also retained 221 I2C read errors, so the capture was not marked passed. This is not evidence for a particular sound action or proof of the exact running binary. The user reports normal screen, touch, and countdown before the controlled tests; the I2C cause remains unresolved. Camera heat was assessed by touch, not by a temperature or power measurement.

Three subsequent 30-second quiet / speech / clap captures are preserved in the [T01 record](experiments/2026-10-13-t01-sound-baseline.md). All retained I2C errors; the quiet run also contains a high outlier. The user confirmed following the prompts at very close range, without an exact distance or clap count. The [T02 offline comparison](experiments/2026-10-14-t02-sound-comparison.md) plots all three runs with host time, device intervals, gap markers and firmware labels. It found 6 / 2 / 16 microphone payloads embedded in I2C error lines. Every internal valid-CSV gap has corresponding mixed-line evidence; the plots do not silently recover these candidates. No firmware was changed in T02.

On October 16, a single [touch-IRQ guard](experiments/2026-10-16-t04-touch-irq-serial.md) in `input.cpp` prevented coordinate reads when touch INT was inactive. Before: 222 I2C errors / 19 mixed payloads in 30 seconds. After build/upload: zero errors / zero mixed payloads in 30 seconds and a separate 60-second follow-up, with 500 ms device report spacing. The user confirmed basic touch, Settings, Bright and Save; the confirmation was not synchronized with the follow-up capture. This provides a T03 real cycle and a bounded T04 fix, not full PR or acoustic acceptance. The [October 16 quiet retest](experiments/2026-10-16-t05-quiet-retest.md) then produced a clean Quiet-only baseline, and the [October 17 clap retest](experiments/2026-10-17-t02-clap-retest.md) closed with an operator-confirmed 13/13 final run; speech completion is operator-confirmed. A second evidence-led T04 change on October 17 raised the quiet debounce from 5 to 15 frames after screen-sync captures showed Quiet/Normal flicker during word gaps; the [debounce record](experiments/2026-10-17-t06-quiet-debounce.md) documents the rebuilt firmware and its 30-second verification capture. The [manual Reset / Stats / NVS checks](experiments/2026-10-17-t07-manual-reset-stats-nvs.md) then passed with serial and operator confirmation, and PR #1 was merged into main as `ec5cfe8` on October 17. Keep original FAILED records and investigate any returning errors instead of suppressing logs.

## v1.x: Offline Desktop Terminal

Goal: make the standalone desk companion reliable enough for long-term daily use.

Daily-use baseline:

```text
v1.6-microphone-test plus the fc32445 version-display correction
```

### v1.5-stability-pass

Completed stability work, followed by `v1.5.1-stability-pass` including timer-continuity regression checks. See the [stability test record](stability-test-v1.5.md).

Test focus:

```text
Settings does not exit accidentally
Save exits normally
Bright 30 / 60 / 100 visibly changes backlight
Done only increments after natural Focus completion
Stats clear remains 0 after power cycle
Reset does not affect Done
Idle does not interrupt RUN
Break prompt still works
NVS restores correctly after restart
Long USB-powered run is stable
```

### v1.6-microphone-test

Completed and merged into `main`. See the [microphone test record](microphone-test-v1.6.md).

Implemented:

```text
Read PDM microphone samples
Calculate centered RMS / peak after removing DC offset
Print noise level over serial
Show passive Quiet / Normal / Loud on the normal Pomodoro screen
```

Hardware constraints:

```text
GPIO41 = PDM DATA
GPIO42 = PDM CLK
Do not cut J1 / J2
Do not use D11 / D12 for other peripherals during this test
```

The Sense expansion board must remain installed. Removing only the camera module leaves the microphone available. The mainline does not implement recording, speech recognition, or clap-based control.

### v1.7-camera-test

Experiment recorded on `feature/camera-test`, tagged `v1.7-camera-test`, and kept outside `main`. See the [camera test record at the tag](https://github.com/iankirkpa5-hash/xiao-pomodoro-companion/blob/v1.7-camera-test/docs/camera-test-v1.7.md).

Observed results:

```text
Initialization and single-frame capture succeeded
Captured frame: 320x240, len=2464, format=4
Keeping the camera initialized produced subjective heat grade 3
Periodic capture was skipped after the heat observation
Camera module remains removed from the daily setup
```

The experiment established a thermal limitation for the current daily-use setup, not a proven hardware defect. The temperature and cause were not measured. Deinitializing the driver must not be treated as proof that the sensor supply is switched off. Further camera investigation is optional.

### v1.8-hardware-feedback

Later candidate, not implemented and not required before continuing sound-data experiments. Select an actual device and review the occupied pins before designing the connection.

Goal: add physical feedback.

Suggested hardware:

```text
Grove Shield / XIAO Expansion Base
Grove Buzzer
Grove Vibration Motor
```

Possible behavior:

```text
Focus complete: short vibration / short beep
Break complete: short vibration / short beep
Settings saved: subtle feedback
Stats cleared: subtle feedback
Invalid action: different feedback
```

Use modules first instead of directly driving bare motors, to reduce electrical risk.

### v1.9-battery-power

Goal: let the device run without USB power.

Suggested hardware:

```text
3.7V LiPo battery
JST 1.25 connector
300mAh / 500mAh / 800mAh battery option
```

Scope:

```text
Battery power
Low battery prompt
Brightness / Idle power saving strategy
README battery safety notes
```

Battery polarity must be verified before connecting.

### v1.10-enclosure-polish

Goal: move from exposed boards to a product-like object.

Scope:

```text
Stand
Enclosure
Brass inserts / standoffs
Anti-slip feet
USB-C strain relief
Antenna mounting
```

## v2.x: XIAO Wi-Fi Event Layer

Goal: let the XIAO become a lightweight network endpoint while keeping Pomodoro fully usable offline.

The ESP32S3 already has Wi-Fi, so v2.x does not need to start with a Raspberry Pi or Mini PC.

### v2.0-wifi-status

Wi-Fi only, no business logic.

Scope:

```text
Connect to Wi-Fi
Reconnect after disconnect
Settings or Status page:
Wi-Fi: OK / OFF
IP: 192.168.x.x
```

Principles:

```text
Network failure must not break Pomodoro
Wi-Fi is an enhancement layer, not a core dependency
```

### v2.1-outbound-webhook

Send event reports first. Do not accept remote control yet.

Events:

```text
FOCUS_STARTED
FOCUS_PAUSED
FOCUS_COMPLETED
BREAK_STARTED
BREAK_COMPLETED
DONE_CHANGED
STATS_CLEARED
```

Example payload:

```json
{
  "device": "xiao-pomodoro",
  "event": "FOCUS_COMPLETED",
  "done": 3,
  "mode": "Break"
}
```

Possible integrations:

```text
Home Assistant webhook
IFTTT
Telegram Bot relay
LINE Bot
Cloud functions
Self-hosted webhook
```

### v2.2-mqtt-events

MQTT is a good fit for Home Assistant or multi-device systems.

Topic sketch:

```text
xiao/pomodoro/state
xiao/pomodoro/event
xiao/pomodoro/stats
```

State payload:

```json
{
  "mode": "Focus",
  "running": true,
  "remaining": 1480,
  "done": 3,
  "brightness": 60
}
```

### v2.3-limited-remote-command

Add remote commands only after event reporting is stable.

Allowed commands:

```text
START
PAUSE
RESET
SET_FOCUS_25
SET_FOCUS_45
SET_FOCUS_50
SET_BRIGHTNESS_30
SET_BRIGHTNESS_60
SET_BRIGHTNESS_100
CLEAR_STATS
SHOW_MESSAGE
```

Not allowed:

```text
Arbitrary GPIO control
Arbitrary code execution
Arbitrary URL execution
Arbitrary scripts
```

Core rule:

```text
External systems send structured commands only.
The XIAO local state machine makes the final execution decision.
```

## v3.x: Local Gateway / Dashboard Layer

Goal: build a middle layer similar in shape to OpenBuddy, but without full AI at first.

Reference architecture:

```text
ESP32 firmware
        <-> WebSocket / HTTP
Python FastAPI backend
        <-> REST / WebSocket
React WebUI
```

### v3.0-local-gateway-mvp

Gateway scope:

```text
Python FastAPI
WebSocket
REST API
Device registry
Command queue
SQLite logs
```

XIAO state report:

```json
{
  "mode": "Focus",
  "running": true,
  "remaining": 1480,
  "done": 3,
  "brightness": 60
}
```

Gateway command example:

```json
{
  "action": "SHOW_MESSAGE",
  "text": "Take a short break"
}
```

### v3.1-web-dashboard

Simple dashboard scope:

```text
Show current status
Show Done stats
Show device online / offline
Send START / PAUSE / RESET
Send SHOW_MESSAGE
View event logs
```

This turns the project from one device into a small system.

### v3.2-mdns-autodiscovery

Goal:

```text
xiao-pomodoro.local
gateway.local
Automatic device discovery
No manual IP entry
```

## v4.x: Voice / AI / Coding Companion Layer

Goal: add OpenBuddy-like interaction patterns: voice, agent, TTS, and coding hooks.

Reference voice pipeline:

```text
Mic -> STT -> cleanup -> Agent -> cleanup -> TTS -> Speaker
```

### v4.0-ai-command-adapter

Natural language to structured commands:

```text
"Start a 45 minute focus session"
        ->
{ "action": "SET_FOCUS_45" }
{ "action": "START" }
```

AI should not directly control hardware. It should output commands for the gateway to validate.

### v4.1-voice-control

Voice input can come from:

```text
Computer microphone
Mobile web page
Telegram / LINE voice
ElevenLabs / OpenAI STT
Whisper / faster-whisper
```

The XIAO should not run speech recognition. It should only display results and execute validated commands.

### v4.2-tts-feedback

Voice output can happen through the web app, local gateway, or speaker device.

Example prompts:

```text
Focus started
Break time
You completed 3 sessions today
```

### v4.3-coding-companion-mode

Inspired by coding hooks in desktop companion projects.

Possible behavior:

```text
Claude Code starts a task -> XIAO shows Thinking
Task completes -> XIAO shows Done
Task errors -> XIAO shows Error
Long-running task -> XIAO shows Working
```

This expands the device from Pomodoro companion into a coding desk companion.

## v5.x: Local Models / Multi-Device System Layer

Goal: use dedicated hardware for local models and make the gateway the real system brain.

Possible hardware:

```text
Intel N100 Mini PC
Ryzen Mini PC
Old laptop
Mac mini
Raspberry Pi 5
Jetson Orin Nano
Hailo / Coral / NPU device
```

Possible runtime stack:

```text
Ollama / llama.cpp
Whisper
TTS
FastAPI Gateway
MQTT broker
Home Assistant
SQLite / Postgres
Device memory
```

Final system shape:

```text
XIAO Pomodoro Companion
        ->
Local AI Gateway
        ->
LLM / STT / TTS / Database / Rules
        ->
Other devices:
lights, speakers, sensors, reminders, Home Assistant
```

## Microphone Route

The onboard PDM microphone is already working in the v1.6 mainline. The current learning branch uses its data to connect firmware behavior with RMS, peaks, time windows, and plotting.

Positioning:

```text
Passive sound indicator completed
Continuous capture, timestamped interval reports and controlled retests completed
Local web curves and CSV download are available for device validation
Short PCM -> WAV experiment: merged (PR #2, 6379372); verified transfer, clear speech, 16x preview default
```

Completed milestone:

```text
v1.6-microphone-test (merged into main)
```

Previously device-tested on the v1.6 baseline (not a blanket acceptance of newer local changes):

```text
PDM input works on GPIO41 / GPIO42
RMS / peak noise level can be calculated
Serial output is stable
Screen shows Quiet / Normal / Loud
No impact on display / touch / Settings / NVS
```

Hardware constraints:

```text
GPIO41 = PDM DATA
GPIO42 = PDM CLK
Do not cut J1 / J2
Do not repurpose D11 / D12 during this test
```

Optional later directions, beyond the bounded PCM experiment:

```text
Longer recordings, playback gain tuning and repeatable data replay
Sound-data / TinyML experiments
Ambient noise reminder
Clap detection
Break breathing guide
Voice wake pre-test
```

## Camera Route

The Sense camera experiment has a recorded conclusion; its code remains outside the current Pomodoro mainline.

Positioning:

```text
Independent experiment
Does not affect the Pomodoro mainline
Does not stay connected long-term if it causes heat
```

Preserved experiment branch and tag:

```text
feature/camera-test
v1.7-camera-test
```

Recorded outcome:

```text
Initialization succeeded
Single-frame capture succeeded
Initialized-idle heat reached subjective grade 3
Periodic capture skipped; continuous operation not validated
Daily-use camera module removed; Sense microphone board retained
```

If vision AI becomes a real goal, prefer:

```text
USB camera + Mini PC
Pi Camera + Raspberry Pi
Jetson / Hailo vision device
```

## Build System Strategy

Do not migrate the current firmware project to Bazel yet.

For the current firmware-only line, PlatformIO remains the right tool:

```text
v1.x / v2.x:
PlatformIO builds, uploads, board config, Arduino framework, and serial monitor
```

The main bottleneck for this hardware project is not software rebuild time. It is:

```text
Firmware upload
Real display behavior
Touch interaction
Backlight / switch / pin validation
Power and thermal behavior
Manual device smoke tests
```

Bazel cannot cache the physical parts of that workflow.

When the project reaches the local gateway stage, start with a simple Makefile instead of a full Bazel migration:

```makefile
firmware-build:
	pio run

firmware-upload:
	pio run -t upload

gateway-test:
	cd gateway && pytest

webui-dev:
	cd webui && npm run dev
```

This is enough for the first multi-component layout:

```text
firmware/
gateway/
webui/
protocol/
tools/
tests/
docs/
```

Bazel becomes worth evaluating only after the repository grows into a true multi-language system:

```text
v3.x / v4.x / v5.x:
Firmware + Python gateway + React dashboard + protocol schemas + mock device tests
```

Useful Bazel targets at that stage:

```text
Python gateway tests
React build / lint
Protocol schema validation
Shared command code generation
Mock device tests
Gateway integration tests
Documentation checks
Firmware build wrapper
```

If Bazel is introduced, prefer a low-risk wrapper first:

```text
Bazel target -> PlatformIO build
Bazel target -> PlatformIO upload
```

Avoid fully Bazelizing the ESP32 / Arduino toolchain unless the firmware and CI complexity clearly justify it.

## Recommended Next Order

Choose work using the flexible learning map; this is the current sequence for the sound-data branch, not a fixed version ladder:

```text
Current baseline: v1.6 daily-use mainline
Preserved experiment: v1.7 camera test and thermal findings
Completed baseline: controlled sound retests, screen-label sync, Reset/Stats/NVS, full Focus (2026-10-22); PR #1 merged (ec5cfe8)
Current step: saved-WAV waveform / FFT verified locally; review before selecting another experiment
Manual C: Reset/Stats/NVS/full Focus passed (2026-10-22); Break screen fix verified
Remaining: device audio output assessment; further changes only from evidence
Available locally: web curves and CSV, dual-format CLI capture, I2C error reporting
Deferred: serial brightness control, keyboard, STT/AI and other product changes
Later candidates: v1.8 hardware feedback, battery, enclosure, Wi-Fi
```

Circuits, mathematics, FPGA simulation, and Linux learning can progress independently. No new release tag is needed just to update the learning map or prepare an unvalidated experiment.

Principles:

```text
XIAO does not run large models
XIAO is the stable physical terminal
Wi-Fi is an enhancement layer and must not break offline Pomodoro
The gateway handles API, AI, voice, logs, and command queues
AI outputs structured commands only
The local device state machine decides whether to execute
Add one capability at a time
Validate on real hardware every time
Update README every time
Use the release checklist every time
Tag every release
Keep main rollback-safe
```
