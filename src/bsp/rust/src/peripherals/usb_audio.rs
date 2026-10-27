//! `libdeluge/usb_audio.h`'s audio half: the USB Audio Class 2.0 function's two streams.
//!
//! # Clocking
//!
//! The codec's crystal is the only sample clock. The RUSB1 runs at high speed, so each stream
//! carries one isochronous packet per 125 µs microframe, about 5.5 frames at 44.1 kHz.
//!
//! - **To the host** (the capture stream, ISO IN): each packet carries as many frames as the SSI
//!   RX DMA, which runs on the codec clock, advanced since the previous packet. The stream's rate
//!   is therefore exactly the codec's, and it is the implicit feedback for the other stream: a host
//!   that honours it sends us as many frames as we send it. The frames themselves come from a ring
//!   the render fills ([`deluge_usb_audio_write`]); the packet filler holds that ring at a backlog
//!   of a couple of render blocks, which only render jitter moves, since both ends run on the
//!   codec clock.
//! - **From the host** (the speaker stream, ISO OUT): each packet's frames go straight into a ring
//!   the render drains ([`deluge_usb_audio_read`]). The reader holds the arrival-to-playback
//!   latency in a fixed window, and for a host that ignores the feedback, drops or repeats a single
//!   frame now and then as its clock drifts from ours.
//!
//! Both packet paths run in the USB0 interrupt (GIC priority 10, above the audio tier's 20), as
//! the RUSB1 driver's ISO hooks: a packet left in the double-buffered FIFO for two microframes is
//! lost, so they cannot wait for an executor. Each ring has one producer and one consumer, one of
//! them that interrupt and the other the render; they share only atomics and slots the positions
//! hand over, with no lock and no allocation. The packets move through the FIFO by CPU copies, and
//! the rings are ordinary cached memory touched only by the CPU, so there is no cache maintenance.
//!
//! # Sample rate and depth
//!
//! The clock source advertises 44.1 kHz only, and a request to set another rate is refused: the
//! codec cannot follow it, and a host clocking a 48 kHz stream by our 44.1 kHz feedback would play
//! it slow. Each stream's alternate setting 1 is 16-bit and 2 is 24-bit; the samples cross the
//! ABI as full-scale i32 either way.

#![allow(non_upper_case_globals)]

use core::ptr::addr_of_mut;
use core::sync::atomic::{AtomicBool, AtomicU8, AtomicU32, Ordering};

use embassy_usb::Builder;
use embassy_usb::Handler;
use embassy_usb::control::{InResponse, OutResponse, Recipient, Request, RequestType};
use embassy_usb::types::InterfaceNumber;
use rza1l_hal::usb::Rusb1Driver;
use usb_classes::audio::{
    AudioClass, USB_BITS_PER_SAMPLE, USB_CAPTURE_BITS_PER_SAMPLE, USB_SAMPLE_RATE,
};

use super::usb_audio_stream::{
    Frame, FrameRing, FromHostReader, SILENCE, ToHostPacer, bytes_per_sample, unpack,
};
use crate::audio::device::{RX_MASK, SAMPLE_RATE_HZ, block_lead_frames, rx_write_frame};
use crate::sys::DelugeStereoSample;

/// The capture stream's channels: the stereo the render writes.
const CAPTURE_CHANNELS: usize = 2;

/// The speaker endpoint's wMaxPacketSize: a 48 kHz millisecond of stereo 24-bit, which covers any
/// high-speed microframe several times over.
const SPEAKER_MAX_PACKET: u16 = 288;

/// The interfaces [`AudioClass::new`] allocates, in its order, when it is the first function the
/// builder gets: AudioControl, then the capture stream, then the speaker stream.
const AC_INTERFACE: u8 = 0;
const CAPTURE_INTERFACE: u8 = 1;
const SPEAKER_INTERFACE: u8 = 2;

/// The clock source's entity ID and its sampling-frequency control (UAC2 §A.17.1), and SET_CUR.
const CLOCK_SOURCE_ID: u8 = 0x10;
const SAM_FREQ_CONTROL: u8 = 0x01;
const UAC2_SET_CUR: u8 = 0x01;

/// How recently a speaker packet must have arrived for the host to count as streaming to us.
const RECEIVING_WINDOW_US: u32 = 20_000;

/// Set once the device starts with the audio function.
static PRESENT: AtomicBool = AtomicBool::new(false);

/// The host's alternate setting for each stream: 0 is idle, 1 is 16-bit, 2 is 24-bit.
static SPEAKER_ALT: AtomicU8 = AtomicU8::new(0);
static CAPTURE_ALT: AtomicU8 = AtomicU8::new(0);
/// Bumped each time the speaker stream's setting changes: the reader discards the last stream.
static SPEAKER_GENERATION: AtomicU32 = AtomicU32::new(0);
/// Bumped each time the capture stream's setting changes: the packet filler restarts its count and
/// discards the ring.
static CAPTURE_EPOCH: AtomicU32 = AtomicU32::new(0);
/// Whether a speaker packet has arrived since the setting last changed, and when the last did
/// (embassy-time µs, truncated).
static PACKET_SEEN: AtomicBool = AtomicBool::new(false);
static LAST_PACKET_US: AtomicU32 = AtomicU32::new(0);

// The rings, 8 KiB each, live in SDRAM (`.sdram_bss`, zeroed at boot, which is their empty state)
// to spare internal SRAM.
/// Host → render. Producer: [`iso_out_hook`]. Consumer: [`deluge_usb_audio_read`].
#[unsafe(link_section = ".sdram_bss")]
static FROM_HOST: FrameRing = FrameRing::new();
/// Render → host. Producer: [`deluge_usb_audio_write`]. Consumer: [`iso_in_hook`].
#[unsafe(link_section = ".sdram_bss")]
static TO_HOST: FrameRing = FrameRing::new();

/// The from-host reader: the audio tier's alone (`deluge_usb_audio_read` is `[audio]`).
static mut READER: FromHostReader = FromHostReader::new();
/// The to-host packet filler: the ISO IN hook's alone. The hook runs in the USB0 interrupt, and
/// once per activation from the device task with that pipe's interrupt masked, never both at once.
static mut PACER: ToHostPacer = ToHostPacer::new();

/// The USB0 port's registers, for the pipe re-enable in [`AudioHandler::set_alternate_setting`].
const USB_PORT: u8 = 0;

/// Adds the USB audio function to `builder` and installs its ISO packet hooks. Returns the class
/// handler, to register with the builder.
///
/// Call it before any other function is added: the interface numbers above assume the audio
/// function comes first.
pub(crate) fn build(builder: &mut Builder<'static, Rusb1Driver<'static>>) -> AudioHandler {
    USB_SAMPLE_RATE.store(SAMPLE_RATE_HZ, Ordering::Release);
    let (class, ep_out, ep_in) = AudioClass::<CAPTURE_CHANNELS>::new(builder, SPEAKER_MAX_PACKET);
    let in_mps = ep_in.info.max_packet_size as usize;
    // SAFETY: the pipes are not enabled until the host selects a streaming setting, which needs the
    // device built and running; the hooks are registered first, from task context.
    unsafe {
        rza1l_hal::usb::pipe::register_iso_out_hook(ep_out.pipe as usize, iso_out_hook);
        rza1l_hal::usb::pipe::register_iso_in_hook(ep_in.pipe as usize, in_mps, iso_in_hook);
    }
    PRESENT.store(true, Ordering::Release);
    AudioHandler {
        class,
        out_pipe: ep_out.pipe as usize,
        in_pipe: ep_in.pipe as usize,
    }
}

/// The ISO OUT hook: one speaker packet of interleaved 16- or 24-bit stereo, into [`FROM_HOST`].
///
/// # Safety
///
/// Called by the RUSB1 driver from the USB0 interrupt with `len` valid bytes at `pkt`.
unsafe fn iso_out_hook(pkt: *const u8, len: usize) {
    // SAFETY: the driver passes its drained packet.
    let packet = unsafe { core::slice::from_raw_parts(pkt, len) };
    let bps = bytes_per_sample(USB_BITS_PER_SAMPLE.load(Ordering::Relaxed));
    // SAFETY: this hook is FROM_HOST's one producer, and the interrupt does not reenter.
    unsafe { FROM_HOST.push(unpack(packet, bps)) };
    LAST_PACKET_US.store(
        embassy_time::Instant::now().as_micros() as u32,
        Ordering::Relaxed,
    );
    PACKET_SEEN.store(true, Ordering::Release);
}

/// The ISO IN hook: fills the next capture packet (`max` bytes of room at `buf`) and returns its
/// length.
///
/// # Safety
///
/// Called by the RUSB1 driver with `max` writable bytes at `buf`, from the USB0 interrupt or from
/// the pipe's activation with its interrupt masked.
unsafe fn iso_in_hook(buf: *mut u8, max: usize) -> usize {
    // SAFETY: the driver passes its scratch buffer.
    let out = unsafe { core::slice::from_raw_parts_mut(buf, max) };
    let bps = bytes_per_sample(USB_CAPTURE_BITS_PER_SAMPLE.load(Ordering::Relaxed));
    let epoch = CAPTURE_EPOCH.load(Ordering::Acquire);
    // SAFETY: PACER is this hook's alone, and the hook never runs twice at once; it is TO_HOST's
    // one consumer.
    unsafe {
        (*addr_of_mut!(PACER)).fill_packet(&TO_HOST, epoch, rx_write_frame(), RX_MASK, bps, out)
    }
}

/// The audio function's control handler: [`AudioClass`], plus the stream state the ABI reports.
pub(crate) struct AudioHandler {
    class: AudioClass<CAPTURE_CHANNELS>,
    out_pipe: usize,
    in_pipe: usize,
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
        PACKET_SEEN.store(false, Ordering::Release);
    }
}

impl Handler for AudioHandler {
    fn reset(&mut self) {
        self.class.reset();
        Self::idle_all();
    }

    fn configured(&mut self, configured: bool) {
        if !configured {
            Self::idle_all();
        }
    }

    fn enabled(&mut self, enabled: bool) {
        if !enabled {
            Self::idle_all();
        }
    }

    fn set_alternate_setting(&mut self, iface: InterfaceNumber, alternate_setting: u8) {
        self.class.set_alternate_setting(iface, alternate_setting);
        // embassy-usb enables each alternate setting's endpoint in descriptor order, and both
        // streaming settings of a stream name the same endpoint, so selecting the first leaves its
        // endpoint disabled by the second. Below, the endpoint is enabled again as the driver does.
        let endpoint_left_disabled = alternate_setting == 1;
        match u8::from(iface) {
            SPEAKER_INTERFACE => {
                SPEAKER_ALT.store(alternate_setting, Ordering::Release);
                SPEAKER_GENERATION.fetch_add(1, Ordering::Release);
                PACKET_SEEN.store(false, Ordering::Release);
                if endpoint_left_disabled {
                    // SAFETY: device-task context, as the driver's own enable; the pipe is ours.
                    unsafe {
                        let u = rza1l_hal::usb::regs::usb(USB_PORT);
                        rza1l_hal::usb::pipe::pipe_reset(u, self.out_pipe);
                        rza1l_hal::usb::pipe::pipe_enable(u, self.out_pipe);
                        rza1l_hal::usb::pipe::pipe_brdy_enable(u, self.out_pipe);
                    }
                }
            }
            CAPTURE_INTERFACE => {
                CAPTURE_ALT.store(alternate_setting, Ordering::Release);
                CAPTURE_EPOCH.fetch_add(1, Ordering::Release);
                if endpoint_left_disabled {
                    // The driver runs the capture endpoint continuously, from its hook.
                    // SAFETY: device-task context, as the driver's own enable; the pipe is ours.
                    unsafe {
                        rza1l_hal::usb::pipe::pipe_iso_in_activate(
                            rza1l_hal::usb::regs::usb(USB_PORT),
                            self.in_pipe,
                        );
                    }
                }
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

#[unsafe(no_mangle)]
pub extern "C" fn deluge_usb_audio_receiving() -> bool {
    if SPEAKER_ALT.load(Ordering::Acquire) == 0 || !PACKET_SEEN.load(Ordering::Acquire) {
        return false;
    }
    let now = embassy_time::Instant::now().as_micros() as u32;
    now.wrapping_sub(LAST_PACKET_US.load(Ordering::Relaxed)) < RECEIVING_WINDOW_US
}

#[unsafe(no_mangle)]
pub extern "C" fn deluge_usb_audio_sending() -> bool {
    CAPTURE_ALT.load(Ordering::Acquire) != 0
}

#[unsafe(no_mangle)]
pub extern "C" fn deluge_usb_audio_read(dst: *mut DelugeStereoSample, frames: u32) -> u32 {
    if dst.is_null() || frames == 0 {
        return 0;
    }
    // SAFETY: the app passes `frames` writable samples; `DelugeStereoSample` is two i32s, as
    // `Frame` is (checked below).
    let out = unsafe { core::slice::from_raw_parts_mut(dst as *mut Frame, frames as usize) };
    if !PRESENT.load(Ordering::Acquire) {
        out.fill(SILENCE);
        return 0;
    }
    let generation = SPEAKER_GENERATION.load(Ordering::Acquire);
    let lead = block_lead_frames() as u32;
    // SAFETY: READER is the audio tier's alone, and this is FROM_HOST's one consumer.
    unsafe { (*addr_of_mut!(READER)).read(&FROM_HOST, generation, lead, out) as u32 }
}

#[unsafe(no_mangle)]
pub extern "C" fn deluge_usb_audio_write(src: *const DelugeStereoSample, frames: u32) {
    if src.is_null() || frames == 0 || CAPTURE_ALT.load(Ordering::Acquire) == 0 {
        return;
    }
    // SAFETY: the app passes `frames` readable samples.
    let block = unsafe { core::slice::from_raw_parts(src, frames as usize) };
    // SAFETY: the render is TO_HOST's one producer.
    unsafe { TO_HOST.push(block.iter().map(|s| [s.l, s.r])) };
}

// `deluge_usb_audio_read` reinterprets the app's samples as frames.
const _: () = {
    assert!(size_of::<DelugeStereoSample>() == size_of::<Frame>());
    assert!(align_of::<DelugeStereoSample>() == align_of::<Frame>());
    assert!(core::mem::offset_of!(DelugeStereoSample, l) == 0);
    assert!(core::mem::offset_of!(DelugeStereoSample, r) == 4);
};

abi_pin!(
    deluge_usb_audio_present,
    deluge_usb_audio_read,
    deluge_usb_audio_receiving,
    deluge_usb_audio_sending,
    deluge_usb_audio_write,
);
