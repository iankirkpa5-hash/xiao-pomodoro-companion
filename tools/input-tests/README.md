# Touch Input Regression Tests

Compile the actual `src/input.cpp` with small Arduino / Wire substitutes:

```sh
g++ -std=c++17 -Wall -Wextra -Werror -fsanitize=address,undefined \
  -I tools/input-tests/stubs tools/input-tests/test.cpp -o /tmp/xiao-input-tests
/tmp/xiao-input-tests
```

The seven groups check inactive-IRQ I2C suppression, original pins/address,
short taps, held presses, repeated taps, incomplete replies, debounce and clock
wrap. They preserve the existing gesture rules, not redesign them.

They do not simulate the controller's electrical behavior or all gesture edge
cases. Real-board tests must verify touch remains responsive and I2C errors /
mixed serial output do not recur during actual Settings use. No logging is
disabled and no malformed serial measurements are silently accepted.
