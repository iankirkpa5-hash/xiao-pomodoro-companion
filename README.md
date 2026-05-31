# XIAO Pomodoro Companion

A small Pomodoro desk companion built with a Seeed Studio XIAO ESP32S3 Sense and the Seeed Round Display for XIAO.

The project focuses on a stable embedded workflow first: PlatformIO, Arduino framework, round LCD rendering, touch input, persistent settings, and a simple product-like interaction model.

## Hardware

- Seeed Studio XIAO ESP32S3 Sense
- Seeed Studio Round Display for XIAO
- USB-C data cable
- External 2.4 GHz antenna connected to the XIAO ESP32S3 U.FL/IPEX connector

Current development setup does not require a battery, TF card, or RTC coin cell.

## Features

- Pomodoro timer with Focus and Break sessions
- Automatic Focus / Break switching
- Round progress ring
- Touch controls
- Settings menu
- Focus duration setting: 25 / 45 / 50 minutes
- Brightness setting: 30 / 60 / 100 percent
- Reset menu item
- NVS persisted settings
- Wellness prompts when entering Break
- Session transition feedback
- Settings screen version display

## Interaction

### Normal Screen

| Action | Result |
| --- | --- |
| Short tap | Start / pause / resume timer |
| Long press | Enter Settings |

### Settings Screen

| Action | Result |
| --- | --- |
| Left-half tap | Switch item: Focus / Bright / Reset |
| Right-half tap on Focus | Cycle 25 / 45 / 50 minutes |
| Right-half tap on Bright | Cycle 30 / 60 / 100 percent |
| Right-half tap on Reset | Reset Pomodoro and return to the normal screen |
| Long press | Save settings and exit |

## Settings

Settings are saved with ESP32 Preferences / NVS.

- `focus_minutes`: 25 / 45 / 50
- `break_minutes`: currently fixed at 5
- `brightness_percent`: 30 / 60 / 100

Saved settings are restored after power loss or USB reconnect.

## Development Environment

- Windows
- VS Code
- PlatformIO IDE
- PlatformIO Core 6.1.19
- Arduino framework
- Board: `seeed_xiao_esp32s3`

## Build, Upload, Monitor

From the project root:

```powershell
& "$env:USERPROFILE\.platformio\penv\Scripts\platformio.exe" run
```

Upload:

```powershell
& "$env:USERPROFILE\.platformio\penv\Scripts\platformio.exe" run -t upload
```

Monitor:

```powershell
& "$env:USERPROFILE\.platformio\penv\Scripts\platformio.exe" device monitor
```

This project currently uses `COM11` in `platformio.ini`.

## Project Structure

```text
src/
+-- main.cpp              # setup and loop orchestration
+-- app_state.h/.cpp      # Pomodoro state
+-- pomodoro_timer.h/.cpp # timer logic
+-- input.h/.cpp          # touch events
+-- ui.h/.cpp             # display rendering
+-- config.h/.cpp         # NVS-backed settings
+-- prompts.h/.cpp        # break wellness prompts
+-- version.h             # app version string
+-- driver.h              # display driver reference/config
```

## Version Milestones

| Tag | Summary |
| --- | --- |
| `v0.1-baseline` | First working PlatformIO / display baseline |
| `v0.2-stable-touch` | Touch, reset, and partial refresh stabilization |
| `v0.3-modular` | Modular source structure |
| `v0.4-session-feedback` | Focus / Break transition feedback |
| `v0.5-settings-nvs` | Persistent configuration layer |
| `v0.6-settings-ui` | Focus duration settings UI |
| `v0.7-brightness-settings` | Brightness configuration and settings UI |
| `v0.8-wellness-prompts` | Break wellness prompts |
| `v0.9-settings-reset-item` | Reset item in Settings |

## Next Steps

- Finish v1.0 polish
- Add final project photos
- Improve README with screenshots
- Consider hardware feedback with a buzzer or vibration motor
- Explore battery and power management
