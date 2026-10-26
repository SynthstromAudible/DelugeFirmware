# OLED SELECT/CV preemption tests

Build the `oled_cv_driver_tests` native executable in the host test project, then
run `ctest -R OledCVDriverTests --output-on-failure` from that build directory.
No Python runner is required.

The test compiles the production OLED/CV and UART drivers with mocked registers. It checks
that queued PIC pad data leaves OLED SELECT cancellable, CV can preempt without
losing the OLED frame or interrupting an in-flight CV transfer, SELECT resumes
when the UART backlog drains, and committed SELECT/DESELECT handshakes remain
exclusive. Sustained pad traffic is deferred until SELECT is queued, including
across CV preemption; the regression covers DMA chaining and ring-buffer wrap.
MIDI buffer availability is unaffected. It does not change the existing CV gate-release timing.

For a hardware check, run continuous pad animations while updating the OLED and
sending CV/gate. Confirm the OLED continues refreshing, pad updates resume after
each SELECT, and CV can still bypass SELECT while earlier pad data drains.
With the slow playhead cursor enabled, move and stop playback while repeatedly
updating the OLED. Confirm old playhead pads return to their underlying colours
after the UART drains, including when a cursor update was only partly sent.

This models UART backlog explicitly; delugemu currently transmits UART bytes
immediately and cannot reproduce the physical wire delay of a grid redraw.
