# Roadmap

This roadmap keeps the project direction clear without overloading the current firmware branch.

The long-term architecture is inspired by projects such as OpenBuddy: an ESP32-class physical terminal connected to a Python backend, a web dashboard, and eventually a voice / AI pipeline. The XIAO device should remain a reliable local terminal. Heavy AI, voice processing, API keys, dashboards, and multi-device orchestration should live outside the microcontroller.

## Version Strategy

```text
v1.x = Offline desktop terminal
v2.x = XIAO Wi-Fi event layer
v3.x = Local Gateway / Dashboard layer
v4.x = Voice / AI / Coding Companion layer
v5.x = Local model and multi-device system layer
```

## Current Status

`XIAO Pomodoro Companion` is now a stable offline desktop terminal rather than a one-off demo.

```text
Main branch: main
Current stable line: v1.5
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
Release checklist
README / GitHub gallery
Camera module removal notes
Round Display KE switch / brightness notes
```

Current hardware state:

```text
Seeed Studio XIAO ESP32S3 Sense
Seeed Round Display for XIAO
External 2.4 GHz antenna
Camera module removed
Round Display KE switch set to ON / KE
No battery
No microSD
No RTC coin cell
USB-C power
```

## v1.x: Offline Desktop Terminal

Goal: make the standalone desk companion reliable enough for long-term daily use.

Current stable release:

```text
v1.5-stability-pass
```

### v1.5-stability-pass

No new features. Only validation and small fixes.

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

### v1.6-hardware-feedback

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

### v1.7-battery-power

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

### v1.8-enclosure-polish

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

## Camera Route

The Sense camera does not belong in the current Pomodoro mainline.

Positioning:

```text
Independent experiment
Does not affect the Pomodoro mainline
Does not stay connected long-term if it causes heat
```

Future branch:

```text
feature/camera-test
```

Only validate:

```text
Can initialize
Can capture one frame
Temperature is acceptable after 5-10 minutes
Does not affect display / touch
```

If vision AI becomes a real goal, prefer:

```text
USB camera + Mini PC
Pi Camera + Raspberry Pi
Jetson / Hailo vision device
```

## Recommended Next Order

Do not jump to v2 yet.

```text
v1.5-stability-pass
v1.6-hardware-feedback
v1.7-battery-power
v1.8-enclosure-polish
v2.0-wifi-status
```

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
