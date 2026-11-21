# Specs for src/sample-view/: the division helpers against integer division, and each new type against the legacy
# arithmetic's transcription (legacy_oracle.h), agreeing everywhere outside the Fixed-quirks rows (fixed_quirks.h).
# The arm tree runs the device's code generation; unsafe_math/ runs the helpers again under the device's float flags.
deluge_specs_driver(sample_view_specs TREES host arm
  LIBRARIES deluge_sample_view)
