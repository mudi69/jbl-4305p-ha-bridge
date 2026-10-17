"""Enumerate this JBL remote; optionally log only its Windows Raw Input events."""

import argparse
import ctypes as c
from ctypes import wintypes as w
from datetime import datetime, timezone
import json
import os
from pathlib import Path
import struct
import time

TARGET = os.environ.get("JBL_RESEARCH_REMOTE_MAC", "").replace(":", "").upper()
UINT_ERROR = 0xFFFFFFFF


class DeviceList(c.Structure):
    _fields_ = [("handle", w.HANDLE), ("kind", w.DWORD)]


class Header(c.Structure):
    _fields_ = [("kind", w.DWORD), ("size", w.DWORD),
                ("handle", w.HANDLE), ("wparam", w.WPARAM)]


class Registration(c.Structure):
    _fields_ = [("page", w.USHORT), ("usage", w.USHORT),
                ("flags", w.DWORD), ("window", w.HWND)]


def is_target(path):
    return len(TARGET) == 12 and all(c in "0123456789ABCDEF" for c in TARGET) and TARGET in path.upper()


def decode_payload(kind, data):
    if kind == 2:
        size, count = struct.unpack_from("<II", data)
        if not size or 8 + size * count > len(data):
            raise ValueError("Truncated HID report data")
        return {"type": "hid_reports", "report_size": size,
                "reports": [data[8 + i * size:8 + (i + 1) * size].hex()
                            for i in range(count)]}
    if kind == 1:
        make, flags, _, key, message, extra = struct.unpack_from("<HHHHII", data)
        return {"type": "keyboard_event", "make_code": make, "flags": flags,
                "virtual_key": key, "message": message, "extra_information": extra,
                "note": "Windows mapped keyboard event, not original BLE HID report"}
    return {"type": "unsupported_input_kind", "kind": kind}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--listen-seconds", type=float, default=0,
                        help="Default 0 only enumerates; positive values listen for this duration")
    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args()
    if args.self_test:
        assert not is_target(r"\\?\HID#another_keyboard")
        assert decode_payload(2, struct.pack("<II", 2, 2) + b"\x01\x02\x03\x04")["reports"] == ["0102", "0304"]
        assert decode_payload(1, struct.pack("<HHHHII", 30, 1, 0, 65, 257, 0))["virtual_key"] == 65
        print("self-test passed")
        return
    if args.listen_seconds < 0:
        parser.error("--listen-seconds must be nonnegative")
    if not is_target(TARGET):
        parser.error("Set JBL_RESEARCH_REMOTE_MAC to the confirmed remote MAC before accessing HID")

    user = c.WinDLL("user32", use_last_error=True)
    kernel = c.WinDLL("kernel32", use_last_error=True)
    user.GetRawInputDeviceList.argtypes = [c.POINTER(DeviceList), c.POINTER(w.UINT), w.UINT]
    user.GetRawInputDeviceList.restype = w.UINT
    user.GetRawInputDeviceInfoW.argtypes = [w.HANDLE, w.UINT, w.LPVOID, c.POINTER(w.UINT)]
    user.GetRawInputDeviceInfoW.restype = w.UINT
    user.GetRawInputData.argtypes = [w.HANDLE, w.UINT, w.LPVOID, c.POINTER(w.UINT), w.UINT]
    user.GetRawInputData.restype = w.UINT
    user.RegisterRawInputDevices.argtypes = [c.POINTER(Registration), w.UINT, w.UINT]
    user.RegisterRawInputDevices.restype = w.BOOL

    def check(result):
        if result == UINT_ERROR:
            raise c.WinError(c.get_last_error())
        return result

    count = w.UINT()
    check(user.GetRawInputDeviceList(None, c.byref(count), c.sizeof(DeviceList)))
    devices = (DeviceList * count.value)()
    count.value = check(user.GetRawInputDeviceList(devices, c.byref(count), c.sizeof(DeviceList)))
    targets = {}
    for device in devices[:count.value]:
        size = w.UINT()
        check(user.GetRawInputDeviceInfoW(device.handle, 0x20000007, None, c.byref(size)))
        path = c.create_unicode_buffer(size.value + 1)
        check(user.GetRawInputDeviceInfoW(device.handle, 0x20000007, path, c.byref(size)))
        if not is_target(path.value):
            continue
        info = c.create_string_buffer(32)
        struct.pack_into("<I", info, 0, 32)
        size = w.UINT(32)
        check(user.GetRawInputDeviceInfoW(device.handle, 0x2000000B, info, c.byref(size)))
        page, usage = struct.unpack_from("<HH", info, 20) if device.kind == 2 else (1, 6)
        target = {"handle": device.handle, "path": path.value, "kind": device.kind,
                  "usage_page": page, "usage": usage}
        targets[device.handle] = target
        print(json.dumps({"type": "target_device", **target}), flush=True)
    print(json.dumps({"type": "enumeration_complete", "target_count": len(targets)}), flush=True)
    if not args.listen_seconds or not targets:
        return

    result_type = c.c_ssize_t
    callback_type = c.WINFUNCTYPE(result_type, w.HWND, w.UINT, w.WPARAM, w.LPARAM)

    class WindowClass(c.Structure):
        _fields_ = [("style", w.UINT), ("proc", callback_type), ("class_extra", c.c_int),
                    ("window_extra", c.c_int), ("instance", w.HINSTANCE), ("icon", w.HANDLE),
                    ("cursor", w.HANDLE), ("background", w.HANDLE), ("menu", w.LPCWSTR),
                    ("name", w.LPCWSTR)]

    kernel.GetModuleHandleW.argtypes = [w.LPCWSTR]
    kernel.GetModuleHandleW.restype = w.HINSTANCE
    user.RegisterClassW.argtypes = [c.POINTER(WindowClass)]
    user.RegisterClassW.restype = w.ATOM
    user.CreateWindowExW.argtypes = [w.DWORD, w.LPCWSTR, w.LPCWSTR, w.DWORD,
                                    c.c_int, c.c_int, c.c_int, c.c_int, w.HWND,
                                    w.HMENU, w.HINSTANCE, w.LPVOID]
    user.CreateWindowExW.restype = w.HWND
    user.DefWindowProcW.argtypes = [w.HWND, w.UINT, w.WPARAM, w.LPARAM]
    user.DefWindowProcW.restype = result_type
    user.PeekMessageW.argtypes = [c.POINTER(w.MSG), w.HWND, w.UINT, w.UINT, w.UINT]
    user.PeekMessageW.restype = w.BOOL
    user.TranslateMessage.argtypes = [c.POINTER(w.MSG)]
    user.DispatchMessageW.argtypes = [c.POINTER(w.MSG)]
    user.DispatchMessageW.restype = result_type
    user.DestroyWindow.argtypes = [w.HWND]
    user.UnregisterClassW.argtypes = [w.LPCWSTR, w.HINSTANCE]
    output = Path(__file__).resolve().parent / "captures" / f"jbl_hid_{datetime.now():%Y%m%d_%H%M%S_%f}.jsonl"
    with output.open("x", encoding="utf-8", buffering=1) as log:
        def record(event):
            event = {"utc": datetime.now(timezone.utc).isoformat(), **event}
            line = json.dumps(event, ensure_ascii=False)
            log.write(line + "\n")
            print(line, flush=True)

        @callback_type
        def window_proc(hwnd, message, wparam, lparam):
            if message == 0x00FF:  # WM_INPUT
                try:
                    header = Header()
                    size = w.UINT(c.sizeof(header))
                    check(user.GetRawInputData(lparam, 0x10000005, c.byref(header), c.byref(size), c.sizeof(Header)))
                    # Filter before retrieving any input payload from other keyboards/devices.
                    if header.handle in targets:
                        size = w.UINT()
                        check(user.GetRawInputData(lparam, 0x10000003, None, c.byref(size), c.sizeof(Header)))
                        buffer = c.create_string_buffer(size.value)
                        check(user.GetRawInputData(lparam, 0x10000003, buffer, c.byref(size), c.sizeof(Header)))
                        event = decode_payload(header.kind, buffer.raw[c.sizeof(Header):size.value])
                        record({**event, "device": targets[header.handle]["path"]})
                except Exception as exc:
                    record({"type": "input_error", "error": str(exc)})
            return user.DefWindowProcW(hwnd, message, wparam, lparam)

        instance = kernel.GetModuleHandleW(None)
        class_name = "JblRemoteRawInput"
        window_class = WindowClass(proc=window_proc, instance=instance, name=class_name)
        if not user.RegisterClassW(c.byref(window_class)):
            raise c.WinError(c.get_last_error())
        window = None
        registrations = None
        try:
            window = user.CreateWindowExW(0, class_name, "JBL remote capture", 0,
                                          0, 0, 0, 0, None, None, instance, None)
            if not window:
                raise c.WinError(c.get_last_error())
            usages = sorted({(item["usage_page"], item["usage"]) for item in targets.values()})
            registrations = (Registration * len(usages))(*(Registration(page, usage, 0x100, window)
                                                           for page, usage in usages))
            if not user.RegisterRawInputDevices(registrations, len(registrations), c.sizeof(Registration)):
                raise c.WinError(c.get_last_error())
            record({"type": "ready", "file": str(output), "seconds": args.listen_seconds,
                    "targets": list(targets.values())})
            message = w.MSG()
            deadline = time.monotonic() + args.listen_seconds
            while time.monotonic() < deadline:
                while user.PeekMessageW(c.byref(message), None, 0, 0, 1):
                    user.TranslateMessage(c.byref(message))
                    user.DispatchMessageW(c.byref(message))
                time.sleep(0.01)
        except KeyboardInterrupt:
            pass
        finally:
            if registrations is not None:
                for registration in registrations:
                    registration.flags = 1  # RIDEV_REMOVE
                    registration.window = None
                user.RegisterRawInputDevices(registrations, len(registrations), c.sizeof(Registration))
            if window:
                user.DestroyWindow(window)
            user.UnregisterClassW(class_name, instance)
            record({"type": "session_end"})


if __name__ == "__main__":
    main()
