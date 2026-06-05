# v1.6 Microphone Test

## Purpose

`v1.6-microphone-test` verifies the onboard PDM digital microphone on the Seeed Studio XIAO ESP32S3 Sense and integrates a passive environment sound indicator into the Pomodoro screen.

This version does **not** implement voice recognition, AI, wake word detection, or clap-based control. The microphone is used only as a passive sound level indicator.

## Hardware Notes

The PDM digital microphone is located on the **XIAO ESP32S3 Sense expansion board**, not on the removable camera module.

Removing the camera module does not affect the microphone.

However, if the Sense expansion board is removed or not seated correctly, the microphone data path will not work. During testing, an incorrectly mounted or missing Sense expansion board produced fixed invalid samples such as:

```text
mean=-30935 min=-30935 max=-30935
```

After reinstalling the Sense expansion board correctly, the microphone produced dynamic audio samples.

## Pin Mapping

| Function |   GPIO |
| -------- | -----: |
| PDM DATA | GPIO41 |
| PDM CLK  | GPIO42 |

J1 / J2 were not cut. GPIO41 / GPIO42 remain reserved for the onboard PDM microphone.

## Signal Processing

The microphone module uses centered RMS and centered peak calculation.

Raw samples may contain a DC offset, so volume is not computed directly from the raw sample values.

The calculation is:

```text
mean = average(samples)
centered = sample - mean
rms = sqrt(mean(centered * centered))
peak = max(abs(centered))
```

This avoids the DC offset dominating the volume estimate.

## Sound Levels

The UI displays one of three passive environment sound states:

```text
Mic: Quiet
Mic: Normal
Mic: Loud
```

The current logic separates three acoustic cases:

| Case                  | Expected State | Detection Idea                              |
| --------------------- | -------------- | ------------------------------------------- |
| Quiet room            | Quiet          | Low centered RMS and low centered peak      |
| Normal speech         | Normal         | Moderate RMS or peak                        |
| Loud continuous sound | Loud           | RMS and peak both exceed loud thresholds    |
| Clap / desk tap       | Loud           | High transient peak with low average energy |

## Final Detection Strategy

The final microphone status logic uses:

* centered RMS
* centered peak
* transient impulse detection
* debounce counters to avoid state flicker

The goal is:

```text
Normal speech should not frequently trigger Loud.
Claps or desk taps should briefly trigger Loud.
After sound stops, the status should return cleanly to Quiet.
```

## Test Results

Observed quiet baseline:

```text
Mic: mean=1256 rms=4.0 peak=11 samples=256
Mic: mean=1251 rms=3.0 peak=8 samples=256
```

Functional validation:

| Test                                     | Expected                      | Result |
| ---------------------------------------- | ----------------------------- | ------ |
| Quiet room                               | Mic: Quiet                    | PASS   |
| Normal speech                            | Mic: Normal                   | PASS   |
| Loud close speech                        | Mic: Normal or temporary Loud | PASS   |
| Clap / desk tap                          | Temporary Mic: Loud           | PASS   |
| Stop sound                               | Return to Mic: Quiet          | PASS   |
| Pomodoro Start / Pause                   | Unaffected                    | PASS   |
| Settings / Bright / Reset / Stats / Save | Unaffected                    | PASS   |

## Current Scope

Included in v1.6:

* PDM microphone initialization
* centered RMS / peak calculation
* Quiet / Normal / Loud classification
* passive microphone status text on normal Pomodoro screen

Not included in v1.6:

* speech recognition
* wake word detection
* clap control
* AI integration
* audio recording
* cloud upload
* NVS microphone settings
