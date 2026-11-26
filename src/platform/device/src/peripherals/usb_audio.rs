//! `libdeluge/usb_audio.h`'s audio half: the USB Audio Class 2.0 function's two streams, moved by DMA.
//!
//! # Clocking
//!
//! The codec's crystal is the only sample clock. The RUSB1 runs at high speed, so each stream
//! carries one isochronous packet per 125 µs microframe, about 5.5 frames at 44.1 kHz.
//!
//! - **To the host** (the capture stream, ISO IN): the render packs each block it makes and queues its packets, 5 or 6
//!   frames each, as far ahead as the codec's three blocks ([`ToHostSender`]). The DMA sends them as the host asks. Every frame is sent, so the
//!   stream's rate is exactly the codec's, and it is the implicit feedback for the other stream: a host that honours it
//!   sends us as many frames as we send it. The sizes of the packets hold the queue at its target.
//! - **From the host** (the speaker stream, ISO OUT): the DMA writes each packet's frames into a ring the render drains
//!   ([`read_block`]). The reader holds the arrival-to-playback latency in a fixed window, and for a host that
//!   ignores the feedback, drops or repeats a single frame now and then as its clock drifts from ours.
//!
//! Neither stream takes an interrupt per packet: the USB interrupt is left to control requests, MIDI and the bus. The
//! rings and descriptors are uncached, as the DMAC needs; the audio tier touches them a block at a time.
//!
//! # Sample rate and depth
//!
//! The clock source advertises 44.1 kHz only, and a request to set another rate is refused: the
//! codec cannot follow it, and a host clocking a 48 kHz stream by our 44.1 kHz feedback would play
//! it slow. Each stream's alternate setting 1 is 16-bit and 2 is 24-bit, in four-byte slots; the samples cross the
//! ABI as full-scale i32 either way.

#![allow(non_upper_case_globals)]

use core::cell::RefCell;
use core::ptr::addr_of_mut;
use core::sync::atomic::{AtomicBool, AtomicPtr, AtomicU8, AtomicU32, Ordering};

use critical_section::Mutex;
use embassy_usb::Builder;
use embassy_usb::Handler;
use embassy_usb::control::{InResponse, OutResponse, Recipient, Request, RequestType};
use embassy_usb::types::InterfaceNumber;
use rza1_hal::dma_buf::{Uncached, UncachedMut};
use rza1_hal::dmac::Dma;
use rza1_hal::ring_buffer::{RingDesc, RingDescs};
use rza1_hal::usb::{DmaIsoIn, DmaIsoOut, Rusb1Driver};
use usb_classes::audio::{
    AudioClass, USB_BITS_PER_SAMPLE, USB_CAPTURE_BITS_PER_SAMPLE, USB_SAMPLE_RATE,
};

use super::usb_audio_stream::{Frame, FromHostReader, SILENCE};
use super::usb_streams::{FromHostTracker, PacketSink, ToHostSender, WireTap};
use super::usb_wire::{Depth, PACKET_TARGET_AHEAD, ToHostRing, WireSource};
use crate::audio::device::{SAMPLE_RATE_HZ, block_lead_frames};
use crate::sys::DelugeStereoSample;

/// The capture stream's channels: the stereo the render writes.
const CAPTURE_CHANNELS: usize = 2;

/// The speaker endpoint's wMaxPacketSize: 49 frames of stereo 24-bit in four-byte slots, which covers any
/// high-speed microframe several times over.
const SPEAKER_MAX_PACKET: u16 = 392;

/// The interfaces [`AudioClass::new_in_words`] allocates, in its order, when it is the first function the
/// builder gets: AudioControl, then the speaker stream, then the capture stream. Linux takes the capture stream as the
/// speaker's implicit feedback only when it is the next interface.
const AC_INTERFACE: u8 = 0;
const SPEAKER_INTERFACE: u8 = 1;
const CAPTURE_INTERFACE: u8 = 2;

/// The clock source's entity ID and its sampling-frequency control (UAC2 §A.17.1), and SET_CUR.
const CLOCK_SOURCE_ID: u8 = 0x10;
const SAM_FREQ_CONTROL: u8 = 0x01;
const UAC2_SET_CUR: u8 = 0x01;

/// Set once the device starts with the audio function.
static PRESENT: AtomicBool = AtomicBool::new(false);

/// The host's alternate setting for each stream: 0 is idle, 1 is 16-bit, 2 is 24-bit.
static SPEAKER_ALT: AtomicU8 = AtomicU8::new(0);
static CAPTURE_ALT: AtomicU8 = AtomicU8::new(0);
/// Bumped each time the speaker stream's setting changes: the reader discards the last stream.
static SPEAKER_GENERATION: AtomicU32 = AtomicU32::new(0);
/// Bumped each time the capture stream's setting changes: the sender starts over.
static CAPTURE_EPOCH: AtomicU32 = AtomicU32::new(0);

/// The to-host ring, in frames: the queue and what waits for it. A power of two.
const TO_HOST_FRAMES: usize = 2048;
/// The from-host ring, in words: 46 ms of 24-bit stereo, the deepest alternate. Whole descriptors' worth.
const FROM_HOST_WORDS: usize = 4096;
/// Packets the stream to the host can queue: the pacer's target and room for the burst a late render catches up with
/// (the queue has reached 96 of these on the rig).
const PACKETS: usize = 256;
/// Descriptors the from-host ring is in.
const RECEIVE_PARTS: usize = 4;

/// DMA memory, on lines of its own, lent to the streams through its uncached mirror.
#[repr(C, align(32))]
struct DmaMem<T>(T);

#[cfg_attr(feature = "usb-rings-sdram", unsafe(link_section = ".sdram_bss"))]
static mut TO_HOST_MEM: DmaMem<[u32; ToHostRing::words(TO_HOST_FRAMES, Depth::TwentyFour)]> =
    DmaMem([0; ToHostRing::words(TO_HOST_FRAMES, Depth::TwentyFour)]);
#[cfg_attr(feature = "usb-rings-sdram", unsafe(link_section = ".sdram_bss"))]
static mut FROM_HOST_MEM: DmaMem<[u32; FROM_HOST_WORDS]> = DmaMem([0; FROM_HOST_WORDS]);
static mut PACKET_DESCS: DmaMem<[RingDesc; PACKETS]> = DmaMem([RingDesc::NEW; PACKETS]);
static mut RECEIVE_DESCS: DmaMem<[RingDesc; RECEIVE_PARTS]> =
    DmaMem([RingDesc::NEW; RECEIVE_PARTS]);

/// A static through its uncached mirror, its cached lines cleaned and invalidated first so none is written back over what
/// the DMA has moved: in the L1, then in the L2 (the boot zeroes SDRAM statics with the caches on, so their lines can
/// sit dirty in the L2), then in the L1 again, so a line the first pass let go can't come back from the L2.
///
/// # Safety
/// `p` is a static used only through the mirror from here on.
unsafe fn uncached<T>(p: *mut T) -> *mut T {
    let (start, end) = (p as usize, p as usize + size_of::<T>());
    // SAFETY: the static's own lines, which no other alias of the memory has used yet; its cached address is its
    // physical one (the RAM is mapped one to one).
    unsafe {
        rza1_hal::cache::dma_clean_inv_range(start, end);
        let l2 = rza1_hal::pac::L2C;
        for line in (start & !31..end).step_by(32) {
            l2.reg7_clean_inv_pa().write_value(line as u32);
        }
        l2.reg7_cache_sync().write_value(0);
        while l2.reg7_cache_sync().read() & 1 != 0 {}
        rza1_hal::cache::dma_inv_range(start, end);
    }
    (start + rza1_hal::UNCACHED_MIRROR_OFFSET) as *mut T
}

/// The stream to the host, as [`ToHostSender`] drives it.
struct Sink(DmaIsoIn);

impl PacketSink for Sink {
    fn generation(&self) -> u32 {
        self.0.generation()
    }

    fn queued(&mut self) -> usize {
        self.0.queued()
    }

    fn push(&mut self, generation: u32, packet: &'static [u32]) -> bool {
        Uncached::new(packet).is_ok_and(|p| self.0.push(generation, p))
    }
}

/// The stream from the host, as [`FromHostTracker`] watches it.
struct Tap(DmaIsoOut);

impl WireTap for Tap {
    fn generation(&self) -> u32 {
        self.0.generation()
    }

    fn position(&mut self) -> u32 {
        self.0.position()
    }
}

/// The sender: the render's alone, set once, before the host can enable the endpoint.
static mut SENDER: Option<ToHostSender<Sink>> = None;
/// The from-host tracker, shared by the reader and the question of whether the host is streaming.
static TRACKER: Mutex<RefCell<Option<FromHostTracker<Tap>>>> = Mutex::new(RefCell::new(None));
/// The ring the stream from the host fills, as the stream lends it back for reading: set once, at build.
static FROM_HOST_RING: AtomicPtr<u32> = AtomicPtr::new(core::ptr::null_mut());
/// The from-host reader: one consumer, the drive (via [`read_block`]), which never runs on two tiers at once.
static mut READER: FromHostReader = FromHostReader::new();

/// The microsecond clock the tracker uses.
fn now_us() -> u32 {
    embassy_time::Instant::now().as_micros() as u32
}

/// The alternate a stream is on: its depth, or `None` when it is idle.
fn depth(alt: &AtomicU8, bits: &AtomicU8) -> Option<Depth> {
    (alt.load(Ordering::Acquire) != 0)
        .then(|| Depth::from_bits(bits.load(Ordering::Acquire)))
        .flatten()
}

/// Polls the from-host stream: where it has got to, if the host is sending one.
fn poll_from_host() -> (u32, Option<Depth>, Option<u32>) {
    // The count and the setting are read with the tracker held, so a caller that another tier preempts cannot bring it a
    // count it has moved on from.
    critical_section::with(|cs| {
        let epoch = SPEAKER_GENERATION.load(Ordering::Acquire);
        let depth = depth(&SPEAKER_ALT, &USB_BITS_PER_SAMPLE);
        let frames = TRACKER
            .borrow_ref_mut(cs)
            .as_mut()
            .and_then(|t| t.poll(epoch, depth, now_us()));
        (epoch, depth, frames)
    })
}

/// What the streams did since the last take, for the audio-timing probe's line. Called by the render.
#[cfg(feature = "audio-timing")]
pub(crate) fn take_service_stats() -> crate::audio::usb_service_probe::UsbDmaStats {
    static TAKEN_CYCLES: AtomicU32 = AtomicU32::new(0);
    static TAKEN_STOPS: AtomicU32 = AtomicU32::new(0);
    // The USB task builds the streams after the render starts: until it has, there is nothing to read (and SENDER may
    // be mid-write). PRESENT is stored after them.
    if !PRESENT.load(Ordering::Acquire) {
        return crate::audio::usb_service_probe::UsbDmaStats::EMPTY;
    }
    let now = crate::work_cycles::interrupt_cycles();
    let cycles = now.wrapping_sub(TAKEN_CYCLES.swap(now, Ordering::Relaxed));
    let interrupt_us =
        (u64::from(cycles) * 1_000_000 / u64::from(crate::services::deluge_cycle_hz())) as u32;
    // SAFETY: the audio tier's, as [`send_block`] is.
    let (counts, stops) = unsafe {
        (*addr_of_mut!(SENDER))
            .as_mut()
            .map(|s| (s.take_counts(), s.sink().0.stops()))
            .unwrap_or_default()
    };
    let received = critical_section::with(|cs| {
        TRACKER
            .borrow_ref_mut(cs)
            .as_mut()
            .map_or(0, FromHostTracker::take_received)
    });
    crate::audio::usb_service_probe::UsbDmaStats {
        in_packets: counts.packets,
        in_frames: counts.frames,
        in_stops: stops.wrapping_sub(TAKEN_STOPS.swap(stops, Ordering::Relaxed)),
        in_ahead: counts.ahead,
        out_frames: received,
        interrupt_us,
    }
}

/// Adds the USB audio function to `builder` and takes its isochronous endpoints to DMA streams on the two channels.
/// Returns the class handler, to register with the builder.
///
/// Call it before any other function is added: the interface numbers above assume the audio
/// function comes first.
pub(crate) fn build(
    builder: &mut Builder<'static, Rusb1Driver<'static>>,
    to_host_dma: Dma<'static>,
    from_host_dma: Dma<'static>,
) -> AudioHandler {
    USB_SAMPLE_RATE.store(SAMPLE_RATE_HZ, Ordering::Release);
    let (class, ep_out, ep_in) =
        AudioClass::<CAPTURE_CHANNELS>::new_in_words(builder, SPEAKER_MAX_PACKET);
    // SAFETY: `build` runs once, so the memory is lent here and nowhere else.
    unsafe {
        let descs = &mut (*uncached(addr_of_mut!(PACKET_DESCS))).0;
        let sink = Sink(ep_in.into_dma_stream(
            to_host_dma,
            RingDescs::new(descs).expect("uncached descriptors"),
            PACKET_TARGET_AHEAD,
        ));
        let ring = (*uncached(addr_of_mut!(TO_HOST_MEM))).0.as_mut_ptr();
        *addr_of_mut!(SENDER) = Some(ToHostSender::new(sink, ring, TO_HOST_FRAMES));

        let descs = &mut (*uncached(addr_of_mut!(RECEIVE_DESCS))).0;
        let ring = &mut (*uncached(addr_of_mut!(FROM_HOST_MEM))).0;
        let out = ep_out.into_dma_stream(
            from_host_dma,
            UncachedMut::new(ring).expect("uncached ring"),
            RingDescs::new(descs).expect("uncached descriptors"),
        );
        FROM_HOST_RING.store(out.ring_ptr().cast_mut(), Ordering::Release);
        critical_section::with(|cs| {
            *TRACKER.borrow_ref_mut(cs) = Some(FromHostTracker::new(Tap(out)));
        });
    }
    PRESENT.store(true, Ordering::Release);
    AudioHandler { class }
}

/// The audio function's control handler: [`AudioClass`], plus the stream state the ABI reports.
pub(crate) struct AudioHandler {
    class: AudioClass<CAPTURE_CHANNELS>,
}

impl AudioHandler {
    /// Whether `req` sets the clock source's sampling frequency to a rate other than ours.
    fn sets_another_rate(req: &Request, data: &[u8]) -> bool {
        let targets_clock = req.request_type == RequestType::Class
            && req.recipient == Recipient::Interface
            && req.request == UAC2_SET_CUR
            && (req.index & 0xFF) as u8 == AC_INTERFACE
            && (req.index >> 8) as u8 == CLOCK_SOURCE_ID
            && (req.value >> 8) as u8 == SAM_FREQ_CONTROL;
        targets_clock
            && data
                .first_chunk::<4>()
                .is_none_or(|hz| u32::from_le_bytes(*hz) != SAMPLE_RATE_HZ)
    }

    /// Both streams idle, as after a bus reset or deconfiguration.
    fn idle_all() {
        SPEAKER_ALT.store(0, Ordering::Release);
        CAPTURE_ALT.store(0, Ordering::Release);
        SPEAKER_GENERATION.fetch_add(1, Ordering::Release);
        CAPTURE_EPOCH.fetch_add(1, Ordering::Release);
    }
}

impl Handler for AudioHandler {
    fn reset(&mut self) {
        self.class.reset();
        Self::idle_all();
    }

    fn configured(&mut self, configured: bool) {
        // Configured or not, every interface is at alternate 0 again (USB 2.0 §9.4.7): a host that sets the
        // configuration again selects its streams' alternates afresh.
        let _ = configured;
        Self::idle_all();
    }

    fn enabled(&mut self, enabled: bool) {
        if !enabled {
            Self::idle_all();
        }
    }

    fn set_alternate_setting(&mut self, iface: InterfaceNumber, alternate_setting: u8) {
        self.class.set_alternate_setting(iface, alternate_setting);
        // The audio tier can preempt this. Counting the change before storing the setting means it never takes up a
        // setting under the count of the one before. The class has the new depth by then.
        //
        // Both streaming settings of a stream name the same endpoint; the driver keeps the one the
        // host selected enabled (a disable of a pipe the same request enabled is skipped).
        match u8::from(iface) {
            SPEAKER_INTERFACE => {
                SPEAKER_GENERATION.fetch_add(1, Ordering::Release);
                SPEAKER_ALT.store(alternate_setting, Ordering::Release);
            }
            CAPTURE_INTERFACE => {
                CAPTURE_EPOCH.fetch_add(1, Ordering::Release);
                CAPTURE_ALT.store(alternate_setting, Ordering::Release);
            }
            _ => {}
        }
    }

    fn control_out(&mut self, req: Request, data: &[u8]) -> Option<OutResponse> {
        if Self::sets_another_rate(&req, data) {
            return Some(OutResponse::Rejected);
        }
        self.class.control_out(req, data)
    }

    fn control_in<'a>(&'a mut self, req: Request, buf: &'a mut [u8]) -> Option<InResponse<'a>> {
        self.class.control_in(req, buf)
    }
}

#[unsafe(no_mangle)]
pub extern "C" fn deluge_usb_audio_present() -> bool {
    PRESENT.load(Ordering::Acquire)
}

/// Whether the host is streaming to us: it has selected the stream's alternate, and packets are arriving.
pub(crate) fn receiving() -> bool {
    poll_from_host();
    let now = now_us();
    critical_section::with(|cs| {
        TRACKER
            .borrow_ref(cs)
            .as_ref()
            .is_some_and(|t| t.receiving(now))
    })
}

/// Whether the host is collecting our audio: it has selected the stream's alternate.
pub(crate) fn sending() -> bool {
    CAPTURE_ALT.load(Ordering::Acquire) != 0
}

/// Reads the host's frames for the block about to render into `out`; frames with nothing from the host behind them are
/// silence. Returns how many came from the host. Called by the drive on the audio tier, every block while USB audio is
/// present, so the reader follows the stream whether or not the app uses it; one read a block, the block's own lead.
pub(crate) fn read_block(out: &mut [Frame]) -> u32 {
    if out.is_empty() {
        return 0;
    }
    if !PRESENT.load(Ordering::Acquire) {
        out.fill(SILENCE);
        return 0;
    }
    let (epoch, depth, frames) = poll_from_host();
    let (Some(depth), Some(frames)) = (depth, frames) else {
        out.fill(SILENCE);
        return 0;
    };
    let lead = block_lead_frames() as u32;
    let ring = FROM_HOST_RING.load(Ordering::Acquire).cast_const();
    // SAFETY: the ring the stream fills and lends back for reading, for as long as the firmware runs (the source reads
    // it volatile, the frames before `frames` only); READER has one consumer: the drive, which never runs on two tiers
    // at once.
    unsafe {
        let source = WireSource::new(ring, FROM_HOST_WORDS, depth, frames);
        (*addr_of_mut!(READER)).read(&source, epoch, lead, out) as u32
    }
}

/// Queues the block just rendered for the host; dropped when the host isn't collecting. Called by the drive, once per
/// block, on the audio tier.
pub(crate) fn send_block(block: &[Frame]) {
    if block.is_empty() || CAPTURE_ALT.load(Ordering::Acquire) == 0 {
        return;
    }
    // SAFETY: the drive is the sender's one user.
    unsafe {
        if let Some(sender) = (*addr_of_mut!(SENDER)).as_mut() {
            let epoch = CAPTURE_EPOCH.load(Ordering::Acquire);
            sender.write(
                epoch,
                depth(&CAPTURE_ALT, &USB_CAPTURE_BITS_PER_SAMPLE),
                block,
            );
        }
    }
}

// The drive reinterprets the port's blocks of samples as frames.
const _: () = {
    assert!(size_of::<DelugeStereoSample>() == size_of::<Frame>());
    assert!(align_of::<DelugeStereoSample>() == align_of::<Frame>());
    assert!(core::mem::offset_of!(DelugeStereoSample, l) == 0);
    assert!(core::mem::offset_of!(DelugeStereoSample, r) == 4);
};

abi_pin!(deluge_usb_audio_present);
