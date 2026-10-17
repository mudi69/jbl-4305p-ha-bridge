"""Read only the JBL remote's Windows HID capabilities; never read input reports."""

import ctypes as c
from ctypes import wintypes as w
from datetime import datetime, timezone
import json
from pathlib import Path
import struct
import subprocess

from inspect_hid import DeviceList, UINT_ERROR, is_target, TARGET


def decode_cap(raw, value=False):
    page, report_id, alias, flags, link, link_usage, link_page = struct.unpack_from("<HBBHHHH", raw)
    usage = struct.unpack_from("<8H", raw, 56)
    result = {"usage_page": page, "report_id": report_id, "is_alias": bool(alias),
              "main_item_flags": flags, "link_collection": link, "link_usage": link_usage,
              "link_usage_page": link_page, "is_range": bool(raw[12]), "is_absolute": bool(raw[15])}
    if raw[12]:
        result.update(usage_min=usage[0], usage_max=usage[1], data_index_min=usage[6], data_index_max=usage[7])
    else:
        result.update(usage=usage[0], data_index=usage[6])
    if value:
        result.update(bit_size=struct.unpack_from("<H", raw, 18)[0],
                      report_count=struct.unpack_from("<H", raw, 20)[0],
                      logical_min=struct.unpack_from("<i", raw, 40)[0],
                      logical_max=struct.unpack_from("<i", raw, 44)[0], has_null=bool(raw[16]))
    else:
        result["report_count_api_v2_or_reserved"] = struct.unpack_from("<H", raw, 16)[0]
    return result


def main():
    if not is_target(TARGET):
        raise SystemExit("Set JBL_RESEARCH_REMOTE_MAC to the confirmed remote MAC before accessing HID")
    sample = bytearray(72)
    struct.pack_into("<HBBHHHH", sample, 0, 7, 1, 0, 2, 0, 6, 1)
    sample[12] = 1
    struct.pack_into("<8H", sample, 56, 224, 231, 0, 0, 0, 0, 0, 7)
    assert decode_cap(sample)["usage_max"] == 231
    user = c.WinDLL("user32", use_last_error=True)
    hid = c.WinDLL("hid", use_last_error=True)
    kernel = c.WinDLL("kernel32", use_last_error=True)
    user.GetRawInputDeviceList.argtypes = [c.POINTER(DeviceList), c.POINTER(w.UINT), w.UINT]
    user.GetRawInputDeviceList.restype = w.UINT
    user.GetRawInputDeviceInfoW.argtypes = [w.HANDLE, w.UINT, w.LPVOID, c.POINTER(w.UINT)]
    user.GetRawInputDeviceInfoW.restype = w.UINT
    kernel.CreateFileW.argtypes = [w.LPCWSTR, w.DWORD, w.DWORD, w.LPVOID, w.DWORD, w.DWORD, w.HANDLE]
    kernel.CreateFileW.restype = w.HANDLE
    kernel.CloseHandle.argtypes = [w.HANDLE]
    hid.HidD_GetPreparsedData.argtypes = [w.HANDLE, c.POINTER(w.LPVOID)]
    hid.HidD_GetPreparsedData.restype = c.c_ubyte
    hid.HidD_FreePreparsedData.argtypes = [w.LPVOID]
    hid.HidP_GetCaps.argtypes = [w.LPVOID, w.LPVOID]
    hid.HidP_GetCaps.restype = w.LONG
    hid.HidP_InitializeReportForID.argtypes = [c.c_int, c.c_ubyte, w.LPVOID, w.LPVOID, w.ULONG]
    hid.HidP_InitializeReportForID.restype = w.LONG
    hid.HidP_SetUsages.argtypes = [c.c_int, w.USHORT, w.USHORT, c.POINTER(w.USHORT),
                                  c.POINTER(w.ULONG), w.LPVOID, w.LPVOID, w.ULONG]
    hid.HidP_SetUsages.restype = w.LONG
    for name in ("HidP_GetButtonCaps", "HidP_GetValueCaps"):
        function = getattr(hid, name)
        function.argtypes = [c.c_int, w.LPVOID, c.POINTER(w.USHORT), w.LPVOID]
        function.restype = w.LONG

    def check(result):
        if result == UINT_ERROR:
            raise c.WinError(c.get_last_error())
        return result

    def hid_check(status):
        if status != 0x00110000:
            raise RuntimeError(f"HID parser status 0x{status & 0xffffffff:08x}")

    count = w.UINT()
    check(user.GetRawInputDeviceList(None, c.byref(count), c.sizeof(DeviceList)))
    devices = (DeviceList * count.value)()
    count.value = check(user.GetRawInputDeviceList(devices, c.byref(count), c.sizeof(DeviceList)))
    results = []
    for device in devices[:count.value]:
        size = w.UINT()
        check(user.GetRawInputDeviceInfoW(device.handle, 0x20000007, None, c.byref(size)))
        path = c.create_unicode_buffer(size.value + 1)
        check(user.GetRawInputDeviceInfoW(device.handle, 0x20000007, path, c.byref(size)))
        if not is_target(path.value):
            continue
        result = {"path": path.value, "raw_input_kind": device.kind}
        pointer = w.LPVOID()
        file_handle = None
        allocated = False
        try:
            size = w.UINT()
            status = user.GetRawInputDeviceInfoW(device.handle, 0x20000005, None, c.byref(size))
            if status != UINT_ERROR and size.value:
                buffer = c.create_string_buffer(size.value)
                check(user.GetRawInputDeviceInfoW(device.handle, 0x20000005, buffer, c.byref(size)))
                pointer = c.cast(buffer, w.LPVOID)
                result["source"] = "RIDI_PREPARSEDDATA"
            else:
                result["raw_input_preparsed_error"] = c.get_last_error()
                # Metadata access only: no GENERIC_READ/WRITE and no input report calls.
                # Raw Input's keyboard interface is exclusive. Find the same
                # collection's actual HID interface using native PnP metadata.
                interfaces = subprocess.check_output(
                    ["pnputil", "/enum-interfaces", "/class", "{4d1e55b2-f16f-11cf-88cb-001111000030}"],
                    encoding="utf-8", errors="replace")
                prefix = path.value.rsplit("#", 1)[0].upper() + "#"
                matches = [line[line.index("\\\\?\\"):].strip() for line in interfaces.splitlines()
                           if "\\\\?\\" in line and prefix in line.upper()]
                if len(matches) != 1:
                    raise RuntimeError(f"Expected one target HID interface, found {len(matches)}")
                result["hid_interface"] = matches[0]
                file_handle = kernel.CreateFileW(matches[0], 0, 3, None, 3, 0, None)
                if file_handle == c.c_void_p(-1).value:
                    file_handle = None
                    raise c.WinError(c.get_last_error())
                if not hid.HidD_GetPreparsedData(file_handle, c.byref(pointer)):
                    raise c.WinError(c.get_last_error())
                allocated = True
                result["source"] = "HidD_GetPreparsedData (metadata-only handle)"
            caps_buffer = c.create_string_buffer(64)
            hid_check(hid.HidP_GetCaps(pointer, caps_buffer))
            caps = struct.unpack("<32H", caps_buffer.raw)
            result.update(usage=caps[0], usage_page=caps[1], input_report_bytes=caps[2],
                          output_report_bytes=caps[3], feature_report_bytes=caps[4],
                          report_lengths_include_id=True, link_collection_nodes=caps[22])
            for report_type, report_name in enumerate(("input", "output", "feature")):
                detail = {"data_indices": caps[25 + report_type * 3]}
                for value in (False, True):
                    number = w.USHORT(caps[23 + report_type * 3 + int(value)])
                    entries = []
                    if number.value:
                        array = c.create_string_buffer(number.value * 72)
                        function = hid.HidP_GetValueCaps if value else hid.HidP_GetButtonCaps
                        hid_check(function(report_type, array, c.byref(number), pointer))
                        entries = [decode_cap(array.raw[i * 72:(i + 1) * 72], value) for i in range(number.value)]
                    detail["values" if value else "buttons"] = entries
                result[report_name] = detail
            # Synthetic reports exist only in these local buffers. Never send
            # them to the device or treat them as captured button reports.
            probes = []
            for button in result["input"]["buttons"]:
                report_id = button["report_id"]
                maximum = button.get("usage_max", button.get("usage", 0))
                for label, usages in (("maximum_usage", [maximum]),
                                      ("fill_array", list(range(1, maximum + 1)))):
                    report = c.create_string_buffer(caps[2])
                    hid_check(hid.HidP_InitializeReportForID(0, report_id, pointer, report, caps[2]))
                    values = (w.USHORT * len(usages))(*usages)
                    length = w.ULONG(len(usages))
                    status = hid.HidP_SetUsages(0, button["usage_page"], button["link_collection"],
                                              values, c.byref(length), pointer, report, caps[2])
                    probes.append({"report_id": report_id, "probe": label,
                                   "synthetic_only": True, "status": f"0x{status & 0xffffffff:08x}",
                                   "usage_length_out": length.value,
                                   "nonzero_bytes": [[i, value] for i, value in enumerate(report.raw) if value]})
            result["local_parser_probes"] = probes
        except Exception as exc:
            result["error"] = str(exc)
        finally:
            if allocated:
                hid.HidD_FreePreparsedData(pointer)
            if file_handle is not None:
                kernel.CloseHandle(file_handle)
        results.append(result)
    document = {"utc": datetime.now(timezone.utc).isoformat(),
                "note": "Windows parsed capabilities; not the original GATT Report Map", "targets": results}
    output = Path(__file__).resolve().parent / "captures" / f"jbl_hid_caps_{datetime.now():%Y%m%d_%H%M%S_%f}.json"
    output.write_text(json.dumps(document, indent=2, ensure_ascii=False), encoding="utf-8")
    print(json.dumps({"file": str(output), **document}, ensure_ascii=False), flush=True)


if __name__ == "__main__":
    main()
