# Retrospective Sampler Testing Guide

## TL;DR

Retrospective Sampler is a lookback buffer that continuously records audio, allowing you to capture what just happened "after the fact." Perfect for catching happy accidents, improvisations, or moments you wish you'd recorded.

**Quick Start:**
1. Enable in Settings → Runtime Features → Retrospective Sampler
2. Configure source (Input, Master, or Focused Track)
3. Configure duration (5/15/30/60 sec or 1/2/4 bars)
4. Play/perform as normal
5. When you hear something worth keeping: **RECORD + SYNC_SCALING** (long press)
6. Sample saved to SAMPLES/RETRO/[song]/SESSION###/

**Suggested Use Case:**
- Set to Master output, 30 seconds
- Jam freely
- When magic happens, trigger save
- Sample appears with auto-normalized levels

## Step-by-Step Walkthrough

### 1. Enable and Configure
1. Open Settings → Runtime Features → Retrospective Sampler
2. Enable the feature (toggle ON)
3. Set **Source**:
   - **Input** → External audio input
   - **Master** → Final mixed output
   - **Focused Track** → Currently selected track only
4. Set **Duration**:
   - Time-based: 5, 15, 30, or 60 seconds
   - Bar-synced: 1, 2, or 4 bars
5. Set **Channels**: Stereo or Mono
6. Set **Bit Depth**: 16-bit or 24-bit

### 2. Understand the Buffer
- Audio is **continuously recorded** in a rolling buffer
- Old audio is overwritten as new audio arrives
- Buffer size depends on duration setting

### 3. Trigger a Save
When you hear something worth keeping:
1. Hold **RECORD** button
2. Press **SYNC_SCALING** button
3. For bar-synced modes: save triggers on next downbeat
4. For time-based modes: save happens immediately
5. Display confirms "Saving..." then filename

### 4. Find Your Samples
Saved files are organized:
```
SAMPLES/
  RETRO/
    [SongName]/
      SESSION001/
        RETR0000.WAV          (time-based)
        RETR0001_2BAR_120BPM.WAV  (bar-synced)
```

- New session folder created each power cycle
- Files auto-increment (RETR0000, RETR0001, ...)
- Bar-synced saves include BPM in filename

### 5. Use in Kit Mode
When triggered in Kit view:
1. Save completes as normal
2. New drum pad "RETR" is created
3. Sample auto-loads into pad
4. Waveform editor opens at START marker
5. Repeat mode: ONCE if < 2sec, CUT otherwise

### 6. Choose the Right Duration
- **5 seconds**: Quick loops, one-shots
- **15 seconds**: Short phrases
- **30 seconds** (default): Full ideas, verses
- **60 seconds**: Extended jams
- **1 bar**: Tight loops, synced
- **2 bars**: Phrase loops
- **4 bars**: Full sections with BPM tagging

## Access Points

- **Settings** → Community Features → Retrospective Sampler (configuration)
- **RECORD + SYNC_SCALING** → Trigger save (anywhere)
- **SAMPLES/RETRO/** → Saved files on SD card

## Settings

| Setting | Options | Description |
|---------|---------|-------------|
| Enable | On/Off | Master enable for feature |
| Source | Input, Master, Focused Track | What to record |
| Duration | 5/15/30/60 sec, 1/2/4 bars | Buffer length |
| Channels | Stereo, Mono | Output format |
| Bit Depth | 16-bit, 24-bit | Sample resolution |

## Testing Scenarios

### 1. Basic Capture
- [ ] Enable retrospective sampler in settings
- [ ] Play some audio (any source)
- [ ] RECORD + SYNC_SCALING → save triggers
- [ ] Check SAMPLES/RETRO/ for saved file
- [ ] Play back saved sample → matches what you heard

### 2. Source Selection
- [ ] Set to **Input** → records external audio only
- [ ] Set to **Master** → records full mix output
- [ ] Set to **Focused Track** → records selected track only
- [ ] Each source captures different signal path

### 3. Duration Modes
- [ ] Set 5 seconds → short buffer, fast saves
- [ ] Set 60 seconds → long buffer, captures more history
- [ ] Set 2 bars → bar-synced, waits for downbeat
- [ ] Bar modes tag filename with BPM

### 4. Bar-Synced Behavior
- [ ] Set duration to 2 bars
- [ ] Start transport playing
- [ ] Trigger save mid-bar → waits for downbeat
- [ ] Save completes on next bar 1
- [ ] Filename includes "_2BAR_120BPM" format

### 5. Time-Based Behavior
- [ ] Set duration to 30 seconds
- [ ] Trigger save → immediate start
- [ ] No waiting for musical timing
- [ ] Filename is simple "RETR0000.WAV"

### 6. Kit Integration
- [ ] In Kit view, trigger save
- [ ] New "RETR" pad created automatically
- [ ] Sample loaded into pad
- [ ] Waveform editor opens
- [ ] Ready to trim/edit immediately

### 7. Session Management
- [ ] Power cycle Deluge
- [ ] Trigger saves → new SESSION folder created
- [ ] Each session numbered incrementally
- [ ] Prevents filename collisions

### 8. Normalization
- [ ] Enable Normalization feature in Community Features submenu
- [ ] Record quiet audio
- [ ] Save → auto-normalized to 95% peak
- [ ] Maximum +42dB gain applied if needed
- [ ] Prevents quiet external input from being unusable

### 9. Multiple Sources
- [ ] Switch source during session
- [ ] Each save captures current source setting
- [ ] Can mix Input and Master captures in same session

### 10. Edge Cases
- [ ] Trigger save with transport stopped (bar-sync) → immediate save
- [ ] Trigger save before buffer is full → saves what's available
- [ ] Very quiet input → normalized but may have noise floor

## File Format Details

| Property | Value |
|----------|-------|
| Sample Rate | 44.1 kHz |
| Channels | Stereo or Mono (setting) |
| Bit Depth | 16-bit (TPDF dithered) or 24-bit |
| Format | WAV |
| Fade-in | 44 samples (~1ms) to prevent clicks |

## Known Behaviors

1. **Rolling buffer**: Always recording, overwrites oldest audio
2. **Downbeat sync**: Bar modes wait for beat 1
3. **Normalization**: Targets 95% of max level
4. **Session folders**: Reset on power cycle
5. **Kit auto-load**: Creates drum pad and opens editor
6. **Memory**: Allocates from SDRAM on demand
7. **Recording pause**: Briefly disabled during file write
8. **Mono input**: L+R averaged for mono output
9. **BPM tagging**: Only for bar-synced modes
10. **Peak tracking**: Incremental with fallback full scan

## Tips

- **Master output** captures effects, EQ, everything
- **Focused Track** isolates for layering/resampling
- **Input** captures external gear jams
- **Bar modes** ensure loop-ready captures
- **Kit mode** fastest path from capture to playable pad
- **24-bit** for maximum quality, 16-bit for smaller files

## Known bugs

- **Slow normalization** Normalization can take a while for long buffers
- **Audio glitch on Save** An audio glitch happens when saving
