# Roadmap

This roadmap keeps the project direction clear without overloading the current firmware branch.

## Version Strategy

```text
v1.x = Offline desktop device
v2.x = Lightweight Wi-Fi features on XIAO ESP32S3
v3.x = Local gateway, API, and AI orchestration
v4.x = Local models, dedicated AI hardware, and multi-device systems
```

## Current Status

`XIAO Pomodoro Companion` is now a stable offline desktop terminal rather than a one-off demo.

```text
Main branch: main
Current stable line: v1.4.x
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
```

## v1.x: Mature Offline Device

Goal: make the standalone desk companion reliable enough for long-term daily use.

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

If no bugs are found, this can be a test log only. If bugs are found, fix only those bugs.

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

## v2.x: Lightweight Wi-Fi On XIAO

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
IFTTT
Home Assistant webhook
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

## v3.x: Local Gateway / API / AI Orchestration

After Wi-Fi is stable, introduce a local gateway such as a Raspberry Pi, N100 Mini PC, old laptop, or Mac mini.

### v3.0-local-gateway

The gateway handles:

```text
API key management
Device command queue
Logs
Database
Multi-device status
Webhook forwarding
MQTT broker
Home Assistant integration
```

The XIAO still receives only simple validated commands.

### v3.1-ai-command-adapter

AI can translate natural language into structured commands, but it should not directly control hardware.

Flow:

```text
User natural language
        ↓
AI / LLM
        ↓
Structured command
        ↓
Gateway validation
        ↓
XIAO execution
```

Example:

```text
"Start a 45 minute focus session"
```

Converted to:

```json
{
  "action": "SET_FOCUS_45"
}
```

or:

```json
{
  "action": "START"
}
```

### v3.2-voice-control

Voice input can come from:

```text
Computer microphone
Mobile web page
Telegram / LINE voice
ElevenLabs
OpenAI speech-to-text
Whisper / faster-whisper
```

The XIAO should not run speech recognition. It should only display results and execute validated commands.

### v3.3-ai-companion

Start adding companion behavior:

```text
Generate encouragement based on Done count
Suggest breaks based on continuous focus time
Summarize a day of focus sessions
Dynamically generate wellness prompts
Suggest focus blocks from a schedule
```

Rule:

```text
AI suggests or outputs commands only.
The device does not execute unvalidated actions.
```

## v4.x: Local Models / Dedicated AI Hardware / Multi-Device System

Only consider stronger hardware at this stage.

Possible hardware:

```text
Intel N100 / Ryzen Mini PC
Small PC with discrete GPU
Raspberry Pi 5 + Hailo
Jetson Orin Nano
Coral TPU
Future RISC-V / NPU / AI accelerator boards
```

Goals:

```text
Local LLM
Local speech recognition
Local TTS
Vision models
Multi-device coordination
Long-term logs and memory
```

System shape:

```text
Local AI Gateway = brain
XIAO Pomodoro Companion = desktop terminal
Other devices = lights, speakers, sensors, reminders, Home Assistant
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
Add one capability at a time
Validate on real hardware every time
Update README every time
Use the release checklist every time
Tag every release
Keep main rollback-safe
```
