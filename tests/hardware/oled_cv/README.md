# OLED SELECT/CV preemption tests

Build the `oled_cv_driver_tests` native executable in the host test project, then
run `ctest -R OledCVDriverTests --output-on-failure` from that build directory.
No Python runner is required.

The test compiles the production OLED/CV driver with mocked registers. It checks
that queued PIC pad data leaves OLED SELECT cancellable, CV can preempt without
losing the OLED frame or interrupting an in-flight CV transfer, SELECT resumes
when the UART backlog drains, and committed SELECT/DESELECT handshakes remain
exclusive. It does not change the existing CV gate-release timing.

This models UART backlog explicitly; delugemu currently transmits UART bytes
immediately and cannot reproduce the physical wire delay of a grid redraw.
