# v1.7 Camera Test

## Purpose

`v1.7-camera-test` validates the removable camera module on the Seeed Studio XIAO ESP32S3 Sense as a hardware boundary experiment.

This version does **not** add camera features to the Pomodoro product firmware. The camera test is used only to answer whether the camera can initialize, capture a frame, and remain thermally acceptable when installed.

## Hardware Notes

The camera module is mounted through the Sense expansion board FPC connector.

The onboard PDM microphone is on the Sense expansion board, not on the removable camera module. Removing the camera does not remove the microphone, but removing the Sense expansion board disables the microphone data path.

## Camera Pin Mapping

| Camera Signal |   GPIO |
| ------------- | -----: |
| XMCLK         | GPIO10 |
| DVP_Y8        | GPIO11 |
| DVP_Y7        | GPIO12 |
| DVP_PCLK      | GPIO13 |
| DVP_Y6        | GPIO14 |
| DVP_Y2        | GPIO15 |
| DVP_Y5        | GPIO16 |
| DVP_Y3        | GPIO17 |
| DVP_Y4        | GPIO18 |
| DVP_VSYNC     | GPIO38 |
| Camera SCL    | GPIO39 |
| Camera SDA    | GPIO40 |
| DVP_HREF      | GPIO47 |
| DVP_Y9        | GPIO48 |

## Test Modes

The experimental `camera_test` module supports three test modes:

| Mode                    | Purpose                                      |
| ----------------------- | -------------------------------------------- |
| CaptureOnceAndDeinit    | Initialize, capture one frame, then deinit   |
| IdleHoldInitialized     | Initialize camera and keep it idle for 5 min |
| PeriodicCapture         | Capture one frame every 2 seconds for 5 min |

The default mode is kept at `CaptureOnceAndDeinit` so the camera does not stay powered longer than needed after boot.

## Temperature Scale

Manual touch-based grading was used:

| Grade | Meaning                                      |
| ----: | -------------------------------------------- |
|     0 | Not warm                                     |
|     1 | Warm                                         |
|     2 | Clearly warm, can touch for 5 seconds or more |
|     3 | Hot, uncomfortable after 1-2 seconds         |
|     4 | Abnormal, disconnect power immediately       |

## Results

### A. Capture Once And Deinit

Result:

```text
Camera init: PASS
Capture one frame: PASS
Deinit: PASS
Frame: 320x240, len=2464, format=4
```

Temperature:

```text
Low / acceptable
```

### B. Idle Hold Initialized

Result:

```text
Camera init: PASS
Hold initialized for 5 minutes: started
Display: normal
Touch: normal
Microphone: normal
USB: stable during initial observation
```

Temperature:

```text
Grade 3
```

The camera became hot enough to be uncomfortable after 1-2 seconds of touch while only initialized and idle.

### C. Periodic Capture

Skipped.

Because `IdleHoldInitialized` already reached Grade 3 heat, periodic capture was not tested. Running a higher-load capture mode was not necessary for the current product decision.

## Conclusion

The camera module can initialize and capture a frame, but it is not suitable for the current always-on v1.x Pomodoro build.

Current product decision:

```text
Keep the camera module removed from the daily Pomodoro hardware stack.
Use the camera only for separate camera experiments.
Do not enable long-running camera initialization in the main firmware.
```

## Current Scope

Included in v1.7:

* Camera pin mapping validation
* Camera init test
* Single-frame capture test
* Idle initialized thermal boundary test
* Product decision record

Not included in v1.7:

* Camera UI
* Image display
* Image saving
* AI vision
* Streaming
* Continuous capture
* Main Pomodoro product integration
