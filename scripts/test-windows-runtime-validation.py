#!/usr/bin/env python3
"""Regression coverage for missing driver loaders and PE import parsing."""
import importlib.util
import pathlib
import struct
import tempfile
import unittest

spec = importlib.util.spec_from_file_location("runtime", pathlib.Path(__file__).with_name("validate-windows-runtime.py"))
runtime = importlib.util.module_from_spec(spec)
spec.loader.exec_module(runtime)


def image(required=(), delayed=()):
    data = bytearray(4096)
    data[:2] = b"MZ"
    struct.pack_into("<I", data, 60, 128)
    data[128:132] = b"PE\0\0"
    struct.pack_into("<HH", data, 132, 0x8664, 1)
    struct.pack_into("<H", data, 148, 240)
    opt = 152
    struct.pack_into("<H", data, opt, 0x20B)
    struct.pack_into("<Q", data, opt + 24, 0x140000000)
    struct.pack_into("<I", data, opt + 60, 512)
    struct.pack_into("<I", data, opt + 108, 16)
    struct.pack_into("<IIII", data, opt + 240 + 8, 3584, 4096, 3584, 512)
    cursor = 2048
    for directory, dependencies, descriptor_size in ((1, required, 20), (13, delayed, 32)):
        start = 512 if directory == 1 else 1024
        if not dependencies:
            continue
        struct.pack_into("<II", data, opt + 112 + directory * 8,
                         4096 + start - 512, (len(dependencies) + 1) * descriptor_size)
        for i, name in enumerate(dependencies):
            encoded = name.encode("ascii") + b"\0"
            data[cursor:cursor + len(encoded)] = encoded
            descriptor = start + i * descriptor_size
            if directory == 13:
                struct.pack_into("<II", data, descriptor, 1, 4096 + cursor - 512)
            else:
                struct.pack_into("<I", data, descriptor + 12, 4096 + cursor - 512)
            cursor += len(encoded)
    return data


class RuntimeValidation(unittest.TestCase):
    def test_driver_loader_requires_app_local_file(self):
        with tempfile.TemporaryDirectory() as directory:
            stage = pathlib.Path(directory)
            (stage / "libmpv-2.dll").write_bytes(image(["KERNEL32.dll", "vulkan-1.dll"]))
            report = runtime.validate(stage)
            self.assertFalse(report["passed"])
            self.assertEqual(report["missingRequired"], [{"image": "libmpv-2.dll", "dependency": "vulkan-1.dll"}])
            (stage / "vulkan-1.dll").write_bytes(image(["KERNEL32.dll"]))
            self.assertTrue(runtime.validate(stage)["passed"])

    def test_nested_plugin_cannot_hide_root_dependency(self):
        with tempfile.TemporaryDirectory() as directory:
            stage = pathlib.Path(directory)
            plugin = stage / "platforms"
            plugin.mkdir()
            (plugin / "qwindows.dll").write_bytes(image(["Qt6Gui.dll"]))
            (plugin / "Qt6Gui.dll").write_bytes(image())
            self.assertFalse(runtime.validate(stage)["passed"])
            (stage / "Qt6Gui.dll").write_bytes(image())
            self.assertTrue(runtime.validate(stage)["passed"])

    def test_windows_icu_and_api_sets_are_baseline(self):
        self.assertTrue(runtime.is_windows_baseline("icuuc.dll"))
        self.assertTrue(runtime.is_windows_baseline("api-ms-win-crt-heap-l1-1-0.dll"))
        self.assertFalse(runtime.is_windows_baseline("vulkan-1.dll"))
        self.assertFalse(runtime.is_windows_baseline("msvcp140.dll"))

    def test_delay_import_inventory_does_not_manufacture_required_failure(self):
        with tempfile.TemporaryDirectory() as directory:
            stage = pathlib.Path(directory)
            path = stage / "app.exe"
            path.write_bytes(image(["KERNEL32.dll"], ["optional-hardware.dll"]))
            required, delayed = runtime.pe_imports(path)
            self.assertEqual(required, ["kernel32.dll"])
            self.assertEqual(delayed, ["optional-hardware.dll"])
            report = runtime.validate(stage)
            self.assertTrue(report["passed"])
            self.assertEqual(report["missingDelayImports"][0]["dependency"], "optional-hardware.dll")

    def test_malformed_or_empty_stage_fails(self):
        with tempfile.TemporaryDirectory() as directory:
            stage = pathlib.Path(directory)
            self.assertFalse(runtime.validate(stage)["passed"])
            (stage / "broken.dll").write_bytes(b"MZ")
            self.assertFalse(runtime.validate(stage)["passed"])


if __name__ == "__main__":
    unittest.main()
