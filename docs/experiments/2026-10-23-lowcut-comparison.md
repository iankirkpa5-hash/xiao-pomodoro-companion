# Matched-Gain Low-Cut Comparison

Date: 2026-10-23 (Asia/Shanghai).
Branch: `codex/pcm-highpass-comparison`, based on `0d15c26`.
Merged into `main` through PR #7 (`0b8f0fe`).
Offline analysis only: no new recording, firmware build/upload, sound-level
threshold change, or STT/AI. The previews are existing 16x listening aids.

## Inputs And Verification

- Raw source: `test-runs/pcm-20261020T145940-4fa8c8ee/audio.wav`,
  SHA-256 `725ea8a149ca5be9291a8b269557d5a2cddae3bec3ee4d70ac9d60bee0fde85e`.
- Preview directory: `test-runs/pcm-20261020T145940-4fa8c8ee/audio-listening-comparison-gain16`.
- `comparison.json` SHA-256 `7cbe07e4f57267d56a40cfe9adedad0cfbd0101647a2b34972c71622019cac16`;
  shared gain `16.0`, 80,000 samples, 10 ms fades, one-pole 80 Hz high-pass.
- `centered.wav` SHA-256 `0e5206801d1aa81691371755bf82d73d90a4f979b5aa765a87573c505071f806`;
  `highpass-80hz.wav` SHA-256 `b3a35afe41016ee78cc1130f86fdaf375a60acc795a6483b045bf70a5ddd3a18`.
- The tool reconstructs both previews from the raw source and manifest and
  compares every sample. `matched_gain_verified`, `all_preview_samples_reconstructed`,
  and `inputs_unchanged` are true. The automated JSON keeps
  `listening_acceptance: pending_user_comparison`; the operator feedback below is
  recorded separately.

## Measurement Method

- Whole-record periodic Hann PSD, per-file mean removed, one-sided.
- Bands: 0-<80, 80-<300, 300-<3000, 3000-8000 Hz.
- "Power" is the window-weighted PSD integral. The stored previews are at 16x;
  source-referred power divides by 16^2 = 256. These are PCM counts, not acoustic
  watts or dB SPL.
- RMS and peak are time-domain values of the stored 16x previews; divide by 16 for
  a source-referred amplitude.

## Results

| Metric | A / centered | B / high-pass | B vs A |
| --- | ---: | ---: | ---: |
| RMS (stored 16x) | 2113.63 | 1071.72 | -5.90 dB |
| Peak (stored 16x) | 13523.30 | 10701.60 | -2.03 dB |
| Total source power (counts²) | 10,469.1 | 1,294.7 | -9.08 dB |
| 0-<80 Hz source power (counts²) | 8,810.2 | 37.3 | -23.73 dB |
| 0-<80 Hz share | 84.15% | 2.88% | — |
| 80-<300 Hz source power | 1,140.9 | 766.1 | -1.73 dB |
| 300-<3000 Hz source power | 362.5 | 340.6 | -0.27 dB |
| 3000-8000 Hz source power | 155.6 | 150.7 | -0.14 dB |

The 0-<80 Hz source-referred power is -50.86 dBFS for A and -74.59 dBFS for B;
total source power is -50.11 and -59.19 dBFS. The high-pass also reduces
80-300 Hz by 1.73 dB but leaves 300 Hz and above essentially unchanged.

Cross-check: the centered source-referred total (10,469.1 counts²) is within
0.05% of the raw WAV's 10,464.5 weighted mean square from the October 22
analysis; the small difference is consistent with the 10 ms fades and 16x
rounding.

## Operator Listening

A and B were played from the same 16x directory with identical timing, gain and
endpoint fades. The operator reported:

- speech remained clear; no consonant or syllable blur;
- B did not sound thin, hollow, or more nasal;
- low-frequency rumble / slow drift was clearly reduced;
- no clicks, pumping, or other artifacts;
- preferred B for daily listening.

This is one listener, one five-second recording, and one preview pair. It is not
a general acoustic certification or a preference for every recording.

## Interpretation And Boundaries

- The one-pole low-cut reduces sub-80 Hz weighted power by 23.73 dB and its share
  from 84.15% to 2.88%, while the 80-300 Hz band drops only 1.73 dB and the
  speech band 300-3000 Hz drops 0.27 dB.
- The operator did not perceive thinning, so the measured 80-300 Hz reduction was
  not audible as a loss of body on this clip. This supports using B as a listening
  aid for this recording; it does not prove the same for all speech.
- Share changes are partly a consequence of the total-power reduction; the
  absolute band powers are the relevant attenuation measure.
- No capture, firmware, Quiet/Normal/Loud threshold, or STT/AI change is made.
  The raw WAV and original previews are unchanged.

## Evidence

- `lowcut-comparison/comparison-analysis.json` SHA-256
  `fe400b10154bcabf98f70aae8189d9d40babaaaf523b43efbd3c9dee6b5af11f`.
- `matched-gain-comparison.png` SHA-256
  `2cb9596855d7edf67fa81f4d9eac3061816edcee7892dcaf2a6cbfdb4ff453cf`;
  `.svg` SHA-256 `af019f890b9d02c0608617f7751e7c6a90060982c9a4aac06242bf5610bff07c`.
- Tool and tests: `tools/compare_pcm_previews.py`,
  `tools/tests/test_pcm_preview_comparison.py`; Python suite 56 tests, 54 passed,
  2 POSIX-only tests skipped on Windows.
- The automated report keeps `listening_acceptance: pending_user_comparison`; the
  operator result is in this record.

## Related Records

- [Saved-WAV waveform/FFT](2026-10-22-wav-fft.md)
- [16x preview gain](2026-10-22-pcm-playback-gain.md)
- [PCM recorder guide](../pcm-recording.md)
