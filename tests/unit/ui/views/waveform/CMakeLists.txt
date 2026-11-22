# Specs for src/ui/views/waveform/ and the host's peak cache (src/app/ports/waveforms/): each type against the legacy
# arithmetic's transcription (legacy_oracle.h), agreeing everywhere outside the Fixed-quirks rows (fixed_quirks.h).
# The arm tree runs the device's code generation. The division helpers' specs are tests/unit/base's.
deluge_specs_driver(ui_views_waveform_specs TREES host arm
  LIBRARIES deluge_ui_views_waveform deluge_app_ports_waveforms)
