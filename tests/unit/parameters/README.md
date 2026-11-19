# Parameter unit tests

The CppUTest target compiles the same parameter value and storage source files as
firmware, with host adapters for allocation, attribute IO and fatal errors.
Run `ParameterTests` through CTest, or select a case with CppUTest arguments.

The existing lazy-vector tests also run in ParameterTests using the same source
as the 32-bit target. Tight allocation rounding and extension tests run in the
existing 32-bit MemoryManagerTests target.
