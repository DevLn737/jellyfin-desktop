#!/usr/bin/env python3
"""Check staged PE imports without trusting driver/SDK DLLs in System32 or PATH.

This checks required imports and inventories delay imports, not dynamic hardware
driver queries or the availability of every Win32 export on older OS builds.
"""
import argparse
import json
import pathlib
import re
import struct
import sys

# Windows components available on the supported Windows 10 1903+ baseline.
# ICU's Windows API is present since Windows 10 1703:
# https://learn.microsoft.com/windows/win32/intl/international-components-for-unicode--icu-
# Redistributable CRT, Qt, Vulkan and GPU-driver DLLs deliberately do not appear.
WINDOWS_BASELINE = set("""
advapi32.dll authz.dll avicap32.dll avrt.dll bcrypt.dll bcryptprimitives.dll
bthprops.cpl cfgmgr32.dll comctl32.dll comdlg32.dll crypt32.dll cryptbase.dll
d2d1.dll d3d11.dll d3d12.dll d3d9.dll dbghelp.dll dcomp.dll dhcpcsvc.dll
dnsapi.dll dwmapi.dll dwrite.dll dxgi.dll fontsub.dll gdi32.dll hid.dll
icuin.dll icuuc.dll imagehlp.dll imm32.dll iphlpapi.dll kernel32.dll kernelbase.dll
mmdevapi.dll mpr.dll msvcrt.dll ncrypt.dll netapi32.dll normaliz.dll ntdll.dll
ole32.dll oleaut32.dll opengl32.dll pdh.dll powrprof.dll propsys.dll psapi.dll
rpcrt4.dll secur32.dll setupapi.dll shcore.dll shell32.dll shlwapi.dll
ucrtbase.dll uiautomationcore.dll urlmon.dll user32.dll userenv.dll uxtheme.dll
version.dll winhttp.dll winmm.dll winspool.drv winusb.dll wldap32.dll ws2_32.dll
wtsapi32.dll
""".split())


def is_windows_baseline(name):
    return name in WINDOWS_BASELINE or name.startswith(("api-ms-win-", "ext-ms-win-"))


def pe_imports(path):
    """Read the standard import and delay-import tables, never load the image."""
    data = path.read_bytes()

    def unpack(fmt, offset):
        try:
            return struct.unpack_from(fmt, data, offset)
        except struct.error as error:
            raise ValueError("truncated PE structure") from error

    if data[:2] != b"MZ":
        raise ValueError("not a PE executable")
    pe = unpack("<I", 60)[0]
    if data[pe:pe + 4] != b"PE\0\0":
        raise ValueError("invalid PE signature")
    machine, section_count = unpack("<HH", pe + 4)
    if machine != 0x8664:
        raise ValueError("expected Windows x64 PE image")
    optional = pe + 24
    optional_size = unpack("<H", pe + 20)[0]
    if unpack("<H", optional)[0] != 0x20B:
        raise ValueError("expected PE32+ optional header")
    image_base = unpack("<Q", optional + 24)[0]
    header_size = unpack("<I", optional + 60)[0]
    directory_count = unpack("<I", optional + 108)[0]
    sections = [unpack("<IIII", optional + optional_size + i * 40 + 8)
                for i in range(section_count)]

    def offset(rva):
        if 0 <= rva < min(header_size, len(data)):
            return rva
        for virtual_size, address, raw_size, pointer in sections:
            delta = rva - address
            if 0 <= delta < max(virtual_size, raw_size):
                if delta >= raw_size or pointer + delta >= len(data):
                    break
                return pointer + delta
        raise ValueError("import RVA outside image data")

    def name(rva):
        start = offset(rva)
        end = data.find(b"\0", start, min(len(data), start + 512))
        if end < 0:
            raise ValueError("unterminated import name")
        value = data[start:end].decode("ascii").lower()
        if not re.fullmatch(r"[a-z0-9_.-]+", value):
            raise ValueError("invalid import module name")
        return value

    def directory(index, fields, name_index, delay=False):
        if directory_count <= index:
            return []
        rva, size = unpack("<II", optional + 112 + index * 8)
        if not rva:
            return []
        result = []
        # The null descriptor is part of the table; bound malformed images.
        for i in range(min(size // (4 * fields) + 1, 65536)):
            descriptor = unpack("<" + "I" * fields, offset(rva + i * fields * 4))
            if not any(descriptor):
                return sorted(set(result))
            name_rva = descriptor[name_index]
            if delay and not descriptor[0] & 1:
                name_rva -= image_base
            result.append(name(name_rva))
        raise ValueError("unterminated import table")

    return directory(1, 5, 3), directory(13, 8, 1, delay=True)


def validate(stage):
    stage = stage.resolve()
    local = {path.name.lower() for path in stage.iterdir()
             if path.is_file() and path.suffix.lower() == ".dll"}
    missing, delayed_missing, invalid = [], [], []
    images = sorted(path for path in stage.rglob("*")
                    if path.is_file() and path.suffix.lower() in {".dll", ".exe"})
    for path in images:
        relative = path.relative_to(stage).as_posix()
        try:
            required, delayed = pe_imports(path)
        except (ValueError, UnicodeDecodeError) as error:
            invalid.append({"image": relative, "error": str(error)})
            continue
        for names, target in ((required, missing), (delayed, delayed_missing)):
            for dependency in names:
                if dependency not in local and not is_windows_baseline(dependency):
                    target.append({"image": relative, "dependency": dependency})
    return {"schemaVersion": 1, "scope": "required-staged-PE-imports",
            "passed": bool(images) and not missing and not invalid,
            "imageCount": len(images), "missingRequired": missing,
            "missingDelayImports": delayed_missing, "invalidImages": invalid,
            "system32OrPathEnumerated": False}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("stage", type=pathlib.Path)
    parser.add_argument("--report", type=pathlib.Path)
    args = parser.parse_args()
    if not args.stage.is_dir():
        parser.error("stage directory unavailable")
    report = validate(args.stage)
    if args.report:
        args.report.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(f"Required PE import closure: {'passed' if report['passed'] else 'FAILED'} ({report['imageCount']} images)")
    for item in report["missingRequired"]:
        print(f"Missing app-local dependency: {item['image']} -> {item['dependency']}")
    for item in report["invalidImages"]:
        print(f"Invalid image: {item['image']}: {item['error']}")
    for item in report["missingDelayImports"]:
        print(f"Delay import unavailable (conditional, outside required-import gate): {item['image']} -> {item['dependency']}")
    return 0 if report["passed"] else 1


if __name__ == "__main__":
    sys.exit(main())
