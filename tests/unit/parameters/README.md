# Parameter unit tests

The CppUTest target compiles the same parameter value and storage source files as
firmware, with host adapters for allocation, attribute IO and fatal errors.
Run `ParameterTests` through CTest, or select a case with CppUTest arguments.

AutoParam privately owns the production AutoParamData implementation. Tests use
standalone data for loading and playback operations, and the explicit automation
interface for live parameters. Compile-time checks verify restricted role
constructors, immutable live data views, and hidden mutable node storage. Setup,
ownership, playback, and live-input access changes preserve absolute parameter
behavior. Legacy control callers use the restricted live-input role; layered
control routing is introduced separately and removes their direct live-input
access. Compile-time checks also reject direct live setters on AutoParam and
on the other roles.
The existing lazy-vector tests also run in ParameterTests using the same source
as the 32-bit target. Tight allocation rounding and extension tests run in the
existing 32-bit MemoryManagerTests target.
