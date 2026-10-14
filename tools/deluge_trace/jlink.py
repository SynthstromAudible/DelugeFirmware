"""A ctypes binding to the SEGGER J-Link DLL: the calls the trace capture makes, and nothing else.

The DLL keeps one open probe per process, so this is a module of thin wrappers over one loaded library
rather than an object model. Every wrapper raises JLinkError on a failure return, naming the call and its
return code, so a capture log shows which step of the bring-up failed.

The prototypes follow SEGGER's JLinkARMDLL.h as reproduced by pytrace (Apache-2.0); the RAWTRACE
commands are the DLL's own JLINKARM_RAWTRACE_CMD_* names.
"""

from __future__ import annotations

import ctypes
import os
import platform
from ctypes import POINTER, c_char_p, c_int, c_uint8, c_uint32, c_void_p
from pathlib import Path

U32 = c_uint32
PU32 = POINTER(c_uint32)
PU8 = POINTER(c_uint8)

# The 64-bit library on both x86_64 and aarch64 hosts; libjlinkarm_arm.so beside it on the Pi is a 32-bit
# ARM build that a 64-bit Python cannot load.
DEFAULT_LIBRARY = "/opt/SEGGER/JLink/libjlinkarm.so"

TIF_SWD = 1

RAWTRACE_START = 0
RAWTRACE_STOP = 1
RAWTRACE_GET_TRACE_FREQ = 2
RAWTRACE_SET_BUFF_SIZE = 3
RAWTRACE_GET_CAPS = 4

RTT_START = 0
RTT_STOP = 1
RTT_GETSTAT = 4

_PROTOTYPES: dict[str, tuple[object, list[object]]] = {
    "JLINKARM_OpenEx": (c_char_p, [c_void_p, c_void_p]),
    "JLINKARM_Close": (None, []),
    "JLINKARM_EMU_SelectByUSBSN": (c_int, [U32]),
    "JLINKARM_GetSN": (c_int, []),
    "JLINKARM_GetDLLVersion": (c_int, []),
    "JLINKARM_GetFirmwareString": (None, [c_char_p, c_int]),
    "JLINK_SetLogFile": (c_int, [c_char_p]),
    "JLINKARM_ExecCommand": (c_int, [c_char_p, c_char_p, c_int]),
    "JLINKARM_TIF_Select": (c_int, [c_int]),
    "JLINKARM_SetSpeed": (None, [U32]),
    "JLINKARM_Connect": (c_int, []),
    "JLINKARM_Reset": (c_int, []),
    "JLINKARM_Halt": (c_int, []),
    "JLINKARM_IsHalted": (c_int, []),
    "JLINKARM_Go": (None, []),
    "JLINKARM_ReadMemU32": (c_int, [U32, U32, PU32, PU8]),
    "JLINKARM_WriteU32": (c_int, [U32, U32]),
    "JLINKARM_WriteMem": (c_int, [U32, U32, c_void_p]),
    "JLINKARM_ReadMem": (c_int, [U32, U32, c_void_p]),
    "JLINKARM_ReadReg": (U32, [c_int]),
    "JLINKARM_WriteReg": (c_int, [c_int, U32]),
    "JLINK_GetRegisterList": (c_int, [PU32, c_int]),
    "JLINK_GetRegisterName": (c_char_p, [U32]),
    "JLINKARM_CP15_ReadEx": (c_int, [c_uint8, c_uint8, c_uint8, c_uint8, PU32]),
    "JLINKARM_CP15_WriteEx": (c_int, [c_uint8, c_uint8, c_uint8, c_uint8, U32]),
    "JLINKARM_RAWTRACE_Control": (c_int, [U32, c_void_p]),
    "JLINKARM_RAWTRACE_Read": (c_int, [PU8, U32]),
    "JLINK_RTTERMINAL_Control": (c_int, [U32, c_void_p]),
    "JLINK_RTTERMINAL_Read": (c_int, [U32, c_char_p, U32]),
}


class JLinkError(RuntimeError):
    """A DLL call failed."""


class RttStart(ctypes.Structure):
    """The RTT START payload: the control block's address."""

    _fields_ = [("ConfigBlockAddress", c_uint32), ("Reserved", c_uint32 * 3)]


class RttStatus(ctypes.Structure):
    """The RTT GETSTAT payload."""

    _fields_ = [
        ("NumBytesTransferred", c_uint32),
        ("NumBytesRead", c_uint32),
        ("HostOverflowCount", c_int),
        ("IsRunning", c_int),
        ("NumUpBuffers", c_int),
        ("NumDownBuffers", c_int),
        ("Reserved", c_uint32 * 2),
    ]


def default_library() -> str:
    """The DLL to load: $JLINK_LIBRARY, else the 64-bit library under /opt/SEGGER/JLink."""
    return os.environ.get("JLINK_LIBRARY") or DEFAULT_LIBRARY


class JLink:
    """The loaded DLL with its prototypes applied, and the calls the capture makes."""

    def __init__(self, library: str | None = None) -> None:
        path = Path(library or default_library())
        if not path.exists():
            raise JLinkError(
                f"J-Link library not found: {path} (machine {platform.machine()})"
            )
        self.path = path
        self._dll = ctypes.CDLL(str(path))
        for name, (restype, argtypes) in _PROTOTYPES.items():
            fn = getattr(self._dll, name)
            fn.restype = restype
            fn.argtypes = argtypes
        self._regs: dict[str, int] | None = None
        self.opened = False

    # -- probe ------------------------------------------------------------------------------------------

    def open(self, serial: int | None = None, log_file: str | None = None) -> None:
        """Open the probe, the one with USB serial number `serial` when given."""
        if log_file:
            self._dll.JLINK_SetLogFile(log_file.encode())
        if serial is not None:
            rc = self._dll.JLINKARM_EMU_SelectByUSBSN(serial)
            if rc < 0:
                raise JLinkError(f"no J-Link with serial number {serial} ({rc})")
        err = self._dll.JLINKARM_OpenEx(None, None)
        if err:
            raise JLinkError(f"JLINKARM_OpenEx: {err.decode(errors='replace')}")
        self.opened = True

    def close(self) -> None:
        """Close the probe; the target keeps running as it was left."""
        if self.opened:
            self._dll.JLINKARM_Close()
            self.opened = False

    def serial(self) -> int:
        return int(self._dll.JLINKARM_GetSN())

    def dll_version(self) -> int:
        return int(self._dll.JLINKARM_GetDLLVersion())

    def firmware(self) -> str:
        buf = ctypes.create_string_buffer(256)
        self._dll.JLINKARM_GetFirmwareString(buf, 256)
        return buf.value.decode(errors="replace")

    def exec_command(self, command: str) -> str:
        """Run a J-Link command string; return the DLL's error text, empty when it accepted it."""
        buf = ctypes.create_string_buffer(512)
        self._dll.JLINKARM_ExecCommand(command.encode(), buf, 512)
        return buf.value.decode(errors="replace").strip()

    def connect(self, device: str, speed_khz: int, script: str | None) -> None:
        """Select the script, device, SWD and speed, then attach to the core."""
        for cmd in ("SetBatchMode = 1", "HideDeviceSelection = 1"):
            self.exec_command(cmd)
        if script:
            err = self.exec_command(f"ScriptFile = {script}")
            if err:
                raise JLinkError(f"ScriptFile: {err}")
        err = self.exec_command(f"Device = {device}")
        if err:
            raise JLinkError(f"Device = {device}: {err}")
        self._dll.JLINKARM_TIF_Select(TIF_SWD)
        self._dll.JLINKARM_SetSpeed(speed_khz)
        rc = self._dll.JLINKARM_Connect()
        if rc < 0:
            raise JLinkError(f"JLINKARM_Connect: {rc}")

    # -- core -------------------------------------------------------------------------------------------

    def reset(self) -> None:
        """Reset through the JLinkScript's ResetTarget: a halt, not nRESET."""
        rc = self._dll.JLINKARM_Reset()
        if rc < 0:
            raise JLinkError(f"JLINKARM_Reset: {rc}")

    def halt(self) -> None:
        rc = self._dll.JLINKARM_Halt()
        if rc != 0 and not self.is_halted():
            raise JLinkError(f"JLINKARM_Halt: {rc}")

    def is_halted(self) -> bool:
        return self._dll.JLINKARM_IsHalted() == 1

    def go(self) -> None:
        self._dll.JLINKARM_Go()

    def _register_index(self, name: str) -> int:
        if self._regs is None:
            ids = (c_uint32 * 256)()
            count = self._dll.JLINK_GetRegisterList(ids, 256)
            self._regs = {}
            for i in range(max(count, 0)):
                raw = self._dll.JLINK_GetRegisterName(ids[i])
                if raw:
                    # The DLL names PC "R15 (PC)": index both words.
                    for word in (
                        raw.decode(errors="replace")
                        .upper()
                        .replace("(", " ")
                        .replace(")", " ")
                        .split()
                    ):
                        self._regs.setdefault(word, ids[i])
        for alias in (name.upper(), {"PC": "R15", "R15": "PC"}.get(name.upper(), "")):
            if alias in self._regs:
                return self._regs[alias]
        raise JLinkError(f"no register named {name} in {sorted(self._regs)}")

    def read_reg(self, name: str) -> int:
        return int(self._dll.JLINKARM_ReadReg(self._register_index(name)))

    def write_reg(self, name: str, value: int) -> None:
        rc = self._dll.JLINKARM_WriteReg(self._register_index(name), value)
        if rc != 0:
            raise JLinkError(f"JLINKARM_WriteReg({name}): {rc}")

    def cp15_write(self, crn: int, crm: int, op1: int, op2: int, value: int) -> None:
        rc = self._dll.JLINKARM_CP15_WriteEx(crn, crm, op1, op2, value)
        if rc < 0:
            raise JLinkError(f"CP15_WriteEx({crn},{crm},{op1},{op2}): {rc}")

    def cp15_read(self, crn: int, crm: int, op1: int, op2: int) -> int:
        out = c_uint32(0)
        rc = self._dll.JLINKARM_CP15_ReadEx(crn, crm, op1, op2, ctypes.byref(out))
        if rc < 0:
            raise JLinkError(f"CP15_ReadEx({crn},{crm},{op1},{op2}): {rc}")
        return out.value

    # -- memory -----------------------------------------------------------------------------------------

    def read_u32(self, addr: int) -> int:
        out = c_uint32(0)
        status = c_uint8(0)
        rc = self._dll.JLINKARM_ReadMemU32(
            addr, 1, ctypes.byref(out), ctypes.byref(status)
        )
        if rc != 1 or status.value:
            raise JLinkError(f"ReadMemU32(0x{addr:08X}): rc={rc} status={status.value}")
        return out.value

    def write_u32(self, addr: int, value: int) -> None:
        rc = self._dll.JLINKARM_WriteU32(addr, value & 0xFFFFFFFF)
        if rc != 0:
            raise JLinkError(f"WriteU32(0x{addr:08X}, 0x{value:08X}): {rc}")

    def write_mem(self, addr: int, data: bytes, chunk: int = 0x10000) -> None:
        for off in range(0, len(data), chunk):
            piece = data[off : off + chunk]
            buf = ctypes.create_string_buffer(piece, len(piece))
            rc = self._dll.JLINKARM_WriteMem(addr + off, len(piece), buf)
            if rc < 0:
                raise JLinkError(f"WriteMem(0x{addr + off:08X}, {len(piece)}): {rc}")

    def read_mem(self, addr: int, size: int) -> bytes:
        buf = ctypes.create_string_buffer(size)
        rc = self._dll.JLINKARM_ReadMem(addr, size, buf)
        if rc < 0:
            raise JLinkError(f"ReadMem(0x{addr:08X}, {size}): {rc}")
        return buf.raw

    # -- RAWTRACE ---------------------------------------------------------------------------------------

    def rawtrace(self, cmd: int, value: int | None = None) -> tuple[int, int]:
        """RAWTRACE_Control; return its code and the U32 it read or wrote."""
        arg = c_uint32(value or 0)
        rc = self._dll.JLINKARM_RAWTRACE_Control(cmd, ctypes.byref(arg))
        return rc, arg.value

    def rawtrace_read(self, size: int = 0x100000) -> bytes:
        buf = (c_uint8 * size)()
        rc = self._dll.JLINKARM_RAWTRACE_Read(buf, size)
        if rc < 0:
            raise JLinkError(f"RAWTRACE_Read: {rc}")
        return bytes(buf[:rc])

    # -- RTT --------------------------------------------------------------------------------------------

    def rtt_start(self, address: int) -> None:
        start = RttStart(ConfigBlockAddress=address)
        rc = self._dll.JLINK_RTTERMINAL_Control(RTT_START, ctypes.byref(start))
        if rc < 0:
            raise JLinkError(f"RTT START: {rc}")

    def rtt_stop(self) -> None:
        self._dll.JLINK_RTTERMINAL_Control(RTT_STOP, None)

    def rtt_status(self) -> RttStatus:
        stat = RttStatus()
        self._dll.JLINK_RTTERMINAL_Control(RTT_GETSTAT, ctypes.byref(stat))
        return stat

    def rtt_read(self, size: int = 4096) -> bytes:
        buf = ctypes.create_string_buffer(size)
        rc = self._dll.JLINK_RTTERMINAL_Read(0, buf, size)
        return buf.raw[:rc] if rc > 0 else b""
