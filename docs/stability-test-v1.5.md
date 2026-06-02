# v1.5 Stability Test Record

This release is a stability pass for the current offline Pomodoro desk companion firmware.

## Release

```text
Tag: v1.5-stability-pass
Branch: main
Displayed version: v1.5
```

## Scope

No new product features were added for this release. The goal is to mark the current firmware as suitable for daily desk use after real-device validation.

## Hardware State

```text
Board: Seeed Studio XIAO ESP32S3 Sense
Display: Seeed Round Display for XIAO
External 2.4 GHz antenna: connected
Camera module: removed
Round Display KE switch: ON / KE
Battery: not connected
microSD: not installed
RTC coin cell: not installed
Power: USB-C
```

## Validation Checklist

- [x] Build passes
- [x] Upload passes
- [x] Settings page opens from the normal screen
- [x] Settings `Save` exits normally
- [x] Settings `Reset` returns to the normal Pomodoro screen
- [x] Settings `Stats` clears `Done`
- [x] Settings long press is ignored
- [x] `Bright` 30 / 60 / 100 visibly changes backlight brightness
- [x] `Done` increments only after natural Focus completion
- [x] `Reset` does not increment or clear `Done`
- [x] Idle screen appears only after paused inactivity
- [x] Idle screen does not interrupt RUN
- [x] Break wellness prompt still works
- [x] NVS settings restore after restart
- [x] Camera removal keeps the current hardware stack cooler and simpler

## Notes

- The device is considered stable enough for daily desk use on the current v1.x offline firmware line.
- Future changes should continue to use `docs/release-checklist.md`.
- The recommended next feature direction is `v1.6-hardware-feedback`, but only after using v1.5 for a while.
