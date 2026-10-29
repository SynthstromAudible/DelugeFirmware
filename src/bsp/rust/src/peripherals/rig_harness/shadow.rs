//! What the app last sent to the OLED and the PIC, kept so the rig harness can stream each change
//! once and resend the whole panel on request.
//!
//! Pure state with no statics, so the host tests drive it directly; the device keeps one behind a
//! critical-section lock (see the parent module).

/// Bytes of an OLED frame: 128 × 48 at 1 bpp, in the SSD1309's page layout.
pub const OLED_BYTES: usize = 768;
/// Main-grid column pairs plus the sidebar pair.
pub const COLUMN_PAIRS: usize = 9;
/// Bytes of one column pair as the app sends it: 16 pads × RGB.
pub const COLUMN_BYTES: usize = 48;
/// Indicator LEDs.
pub const LED_COUNT: usize = 64;

/// One thing that changed since the last [`Shadow::take_changes`].
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Change {
    Oled,
    Columns(u8),
    Led(u8),
    Ring(u8),
    /// A pad flash: pad index (`y + x * 8`) and the PIC colour index (0 = its current colour).
    Flash(u8, u8),
    /// The PIC's dimmer interval or refresh time.
    Pic,
    /// A PIC scroll animation: the PIC moved pads itself, so the pad shadow no longer shows them
    /// until the app resends every column.
    Scroll,
}

/// The panel as last sent.
pub struct Shadow {
    oled: [u8; OLED_BYTES],
    oled_size: (u16, u16),
    columns: [[u8; COLUMN_BYTES]; COLUMN_PAIRS],
    leds: [bool; LED_COUNT],
    rings: [[u8; 4]; 2],
    dimmer: u8,
    refresh: u8,
    // Pending changes, in report order: state changes as dirty flags, events in a queue.
    oled_dirty: bool,
    columns_dirty: [bool; COLUMN_PAIRS],
    leds_dirty: [bool; LED_COUNT],
    rings_dirty: [bool; 2],
    pic_dirty: bool,
    scrolled: bool,
    flashes: [(u8, u8); Self::FLASH_QUEUE],
    flash_count: usize,
    dropped: u32,
}

impl Default for Shadow {
    fn default() -> Self {
        Self::new()
    }
}

impl Shadow {
    /// Flashes held between two [`Self::take_changes`]; more are counted by [`Self::take_dropped`].
    pub const FLASH_QUEUE: usize = 32;
    /// The most changes one [`Self::take_changes`] can report: every flag and the whole flash
    /// queue.
    pub const MAX_CHANGES: usize = 3 + COLUMN_PAIRS + LED_COUNT + 2 + Self::FLASH_QUEUE;

    pub const fn new() -> Self {
        Self {
            oled: [0; OLED_BYTES],
            oled_size: (0, 0),
            columns: [[0; COLUMN_BYTES]; COLUMN_PAIRS],
            leds: [false; LED_COUNT],
            rings: [[0; 4]; 2],
            dimmer: 0,
            refresh: 0,
            oled_dirty: false,
            columns_dirty: [false; COLUMN_PAIRS],
            leds_dirty: [false; LED_COUNT],
            rings_dirty: [false; 2],
            pic_dirty: false,
            scrolled: false,
            flashes: [(0, 0); Self::FLASH_QUEUE],
            flash_count: 0,
            dropped: 0,
        }
    }

    /// Records a frame; at most [`OLED_BYTES`] of `pixels` are kept.
    pub fn record_oled(&mut self, pixels: &[u8], width: u16, height: u16) {
        let n = pixels.len().min(OLED_BYTES);
        if self.oled[..n] != pixels[..n] || self.oled_size != (width, height) {
            self.oled[..n].copy_from_slice(&pixels[..n]);
            self.oled[n..].fill(0);
            self.oled_size = (width, height);
            self.oled_dirty = true;
        }
    }

    /// Records column pair `idx`; an index past the sidebar pair is ignored.
    pub fn record_columns(&mut self, idx: u8, rgb: &[u8; COLUMN_BYTES]) {
        let Some(col) = self.columns.get_mut(idx as usize) else {
            return;
        };
        if col != rgb {
            *col = *rgb;
            self.columns_dirty[idx as usize] = true;
        }
    }

    pub fn record_led(&mut self, id: u8, on: bool) {
        let Some(led) = self.leds.get_mut(id as usize) else {
            return;
        };
        if *led != on {
            *led = on;
            self.leds_dirty[id as usize] = true;
        }
    }

    /// Records a gold-knob ring; any non-zero `knob` is the second.
    pub fn record_ring(&mut self, knob: u8, levels: [u8; 4]) {
        let k = (knob != 0) as usize;
        if self.rings[k] != levels {
            self.rings[k] = levels;
            self.rings_dirty[k] = true;
        }
    }

    pub fn record_flash(&mut self, idx: u8, colour: u8) {
        if self.flash_count < Self::FLASH_QUEUE {
            self.flashes[self.flash_count] = (idx, colour);
            self.flash_count += 1;
        } else {
            self.dropped += 1;
        }
    }

    /// Records the PIC's dimmer interval and/or refresh time; `None` leaves that one as it was.
    pub fn record_pic(&mut self, dimmer: Option<u8>, refresh: Option<u8>) {
        let new = (
            dimmer.unwrap_or(self.dimmer),
            refresh.unwrap_or(self.refresh),
        );
        if new != (self.dimmer, self.refresh) {
            (self.dimmer, self.refresh) = new;
            self.pic_dirty = true;
        }
    }

    pub fn record_scroll(&mut self) {
        self.scrolled = true;
    }

    pub fn oled(&self) -> (&[u8; OLED_BYTES], (u16, u16)) {
        (&self.oled, self.oled_size)
    }

    pub fn columns(&self, idx: u8) -> &[u8; COLUMN_BYTES] {
        &self.columns[idx as usize]
    }

    pub fn led(&self, id: u8) -> bool {
        self.leds[id as usize]
    }

    pub fn ring(&self, knob: u8) -> [u8; 4] {
        self.rings[(knob != 0) as usize]
    }

    /// The dimmer interval and refresh time.
    pub fn pic(&self) -> (u8, u8) {
        (self.dimmer, self.refresh)
    }

    /// Drains the pending changes: scroll first, then OLED, columns, LEDs, rings, flashes in the
    /// order they came, and PIC settings last. Without `oled_due` a new frame stays pending, so the
    /// caller can pace the panel's largest line.
    pub fn take_changes(&mut self, oled_due: bool) -> impl Iterator<Item = Change> + '_ {
        let mut out: heapless::Vec<Change, { Self::MAX_CHANGES }> = heapless::Vec::new();
        // Capacity is MAX_CHANGES, so no push can fail.
        if core::mem::take(&mut self.scrolled) {
            let _ = out.push(Change::Scroll);
        }
        if oled_due && core::mem::take(&mut self.oled_dirty) {
            let _ = out.push(Change::Oled);
        }
        for (i, d) in self.columns_dirty.iter_mut().enumerate() {
            if core::mem::take(d) {
                let _ = out.push(Change::Columns(i as u8));
            }
        }
        for (i, d) in self.leds_dirty.iter_mut().enumerate() {
            if core::mem::take(d) {
                let _ = out.push(Change::Led(i as u8));
            }
        }
        for (i, d) in self.rings_dirty.iter_mut().enumerate() {
            if core::mem::take(d) {
                let _ = out.push(Change::Ring(i as u8));
            }
        }
        for &(idx, colour) in &self.flashes[..core::mem::take(&mut self.flash_count)] {
            let _ = out.push(Change::Flash(idx, colour));
        }
        if core::mem::take(&mut self.pic_dirty) {
            let _ = out.push(Change::Pic);
        }
        out.into_iter()
    }

    /// Flashes that did not fit the queue since the last call.
    pub fn take_dropped(&mut self) -> u32 {
        core::mem::take(&mut self.dropped)
    }
}
