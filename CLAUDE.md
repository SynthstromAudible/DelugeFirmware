# Deluge Firmware Development Guidelines

## Build System

- **NEVER** use `make` directly
- Use `./dbt build` for building (or `timeout 300 ./dbt build` for safety)
- Use `./dbt loadfw` to flash firmware to device
- Use `./dbt format` to format code
- **Avoid unnecessary builds** - only build when explicitly requested or when testing is needed

## Code Style

- Use tabs for indentation
- Follow existing patterns in the codebase
- No unnecessary comments or documentation unless requested
- commits are expected to pass clang tidy as configured for this project

## Audio DSP Rules

- **NEVER write to audio buffers without envelopes** in Grain mode - this causes clicks
- Grain mode uses dual-voice crossfading with triangular envelopes (envA, envB at 50% phase offset)
- srcAL/srcAR = grain source (from buffer or dry input)
- outputL/outputR = combined enveloped output (srcA*envA + srcB*envB)
- Q31 fixed-point: 0x7FFFFFFF = 1.0, use multiply_32x32_rshift32 for multiplication

## Stutterer Architecture

- Single looperBuffer (~350KB) shared across tracks
- pWrite controls buffer evolution (0=freeze, 50=always overwrite)
- Classic/Burst modes use DelayBuffer, looper modes use looperBuffer
- Takeover: when one track inherits buffer from another playing track
- Status states: OFF, STANDBY, RECORDING, ARMED, PLAYING

## Testing

- Test on actual hardware when possible
- User will report issues - trust their observations
- Don't assume code works just because it compiles
