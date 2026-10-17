# Microphone Capture Regression Tests

These host tests compile the actual `src/microphone.cpp` with Arduino / I2S
stubs. They exercise partial byte reads, centered metrics, interval peak
retention, energy-weighted RMS, reporting resets, stale input, timestamp wrap,
Loud hold, threshold boundaries, and debounce counter saturation.

From the repository root on Linux or WSL with g++ installed:

```sh
g++ -std=c++17 -Wall -Wextra -Werror -fsanitize=address,undefined \
  -I tools/microphone-tests/stubs tools/microphone-tests/test.cpp \
  -o /tmp/xiao-microphone-tests
/tmp/xiao-microphone-tests
```

The tests do not emulate ESP32 task scheduling, PDM wiring, or sound. Real-device
checks must confirm capture throughput near 16,000 samples/second, no read
errors, visible clap peaks, and unaffected screen / touch / Settings behavior.
