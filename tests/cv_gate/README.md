# CV/gate driver regression tests

The native `cv_gate_driver_tests` executable is registered with the host test
project as `CVGateDriverTests`. It compiles the production OLED/CV driver with
mocked registers and verifies per-channel pending updates, round-robin service,
DAC completion ordering, and superseded-voltage gate notifications.

This suite is independent of the OLED handshake and PIC-backlog test suites.
It does not require a Python runner.
