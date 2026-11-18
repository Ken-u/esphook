#!/usr/bin/env python3
"""scripts/size-report.py on a small hand-made linker map: slot arithmetic, merged strings, per-app link order, budget exit code."""
import contextlib
import io
import json
import os
import sys
import tempfile
import unittest

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "scripts"))
import importlib.util

spec = importlib.util.spec_from_file_location("size_report", os.path.join(os.path.dirname(__file__), "..", "scripts", "size-report.py"))
size_report = importlib.util.module_from_spec(spec)
spec.loader.exec_module(size_report)

MAP = """\
.iram0.text     0x40380000     0x1000
 .text.isr      0x40380000     0x1000 esp-idf/esp_system/libesp_system.a(isr.c.obj)

.dram0.data     0x3fc80000      0x100
.dram0.bss      0x3fc80100      0x800

.flash.text     0x42000020     0x2000
 .text.a        0x42000020      0x400 esp-idf/main/libmain.a(app.c.obj)
 .text.a_core   0x42000420      0x100 esp-idf/main/libmain.a(a_core.c.obj)
 .text.a        0x42000520      0x600 esp-idf/main/libmain.a(app.c.obj)
 .text.b
                0x42000b20      0x300 esp-idf/main/libmain.a(app.c.obj)
 .text.lv       0x42000e20      0x500 esp-idf/lvgl/liblvgl__lvgl.a(lv_obj.c.obj)
 .text.wifi     0x42001320      0x200 esp-idf/esp_wifi/libesp_wifi.a(wifi.c.obj)

.flash.rodata   0x3c000000     0x1200
 .rodata.__init.str1.4
                0x3c000000      0x800 esp-idf/esp_app_format/libesp_app_format.a(esp_app_desc.c.obj)
 .rodata.other.str1.4
                0x3c000100       0x40 esp-idf/main/libmain.a(app.c.obj)
 .rodata.glyph_dsc
                0x3c000800      0x400 esp-idf/main/libmain.a(aihook_font.c.obj)
 .rodata.tab    0x3c000c00      0x200 esp-idf/main/libmain.a(a_core.c.obj)
"""


def write(path, text):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w", encoding="utf-8") as handle:
        handle.write(text)


class SizeReportTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.root = self.tmp.name
        self.addCleanup(self.tmp.cleanup)
        for app, order in (("a", "10"), ("b", "20")):
            write(os.path.join(self.root, "apps", app, "manifest.json"), json.dumps({"id": app, "order": order}))
        write(os.path.join(self.root, "apps", "a", "firmware", "app.c"), "")
        write(os.path.join(self.root, "apps", "a", "firmware", "a_core.c"), "")
        write(os.path.join(self.root, "apps", "b", "firmware", "app.c"), "")
        self.map = os.path.join(self.root, "fw.map")
        write(self.map, MAP)
        self.partitions = os.path.join(self.root, "partitions.csv")
        write(self.partitions, "# Name, Type, SubType, Offset, Size\nnvs, data, nvs, 0x9000, 0x6000\n"
                               "ota_0, app, ota_0, 0x20000, 1700K\nota_1, app, ota_1, , 1600K\nfontdata, data, 0x40, , 768K\n")
        self.image = os.path.join(self.root, "fw.bin")
        with open(self.image, "wb") as handle:
            handle.write(b"\0" * (1500 * 1024))

    def run_report(self, *extra):
        out, err = io.StringIO(), io.StringIO()
        with contextlib.redirect_stdout(out), contextlib.redirect_stderr(err):
            code = size_report.main(["--bin", self.image, "--map", self.map, "--partitions", self.partitions,
                                     "--root", self.root, *extra])
        return code, out.getvalue(), err.getvalue()

    def test_slot_is_the_smallest_ota_partition(self):
        self.assertEqual(size_report.ota_slot_bytes(self.partitions), 1600 * 1024)
        code, out, _ = self.run_report("--min-free-pct", "1")
        self.assertEqual(code, 0)
        self.assertIn("smallest OTA slot", out)
        self.assertIn("(6.2%)", out)  # 100 KiB of 1600 KiB

    def test_budget_fails_when_the_slot_is_nearly_full(self):
        code, _, err = self.run_report("--min-free-pct", "8")
        self.assertEqual(code, 1)
        self.assertIn("FAIL", err)
        self.assertEqual(self.run_report("--min-free-pct", "5")[0], 0)

    def test_merged_strings_are_counted_once(self):
        outputs, items = size_report.parse_map(self.map)
        self.assertEqual(outputs[".flash.text"], 0x2000)
        rodata = sum(size for out, _, size, _ in items if out == ".flash.rodata")
        self.assertEqual(rodata, 0x800 + 0x400 + 0x200)  # the 0x40 piece lives inside the first lump
        cats = size_report.categories(items, self.root)
        self.assertEqual(cats["framework + main"], 0x400)
        self.assertEqual(cats["LVGL"], 0x500)
        self.assertEqual(cats["Wi-Fi, lwip, TLS, HTTP"], 0x200)

    def test_per_app_follows_the_link_order(self):
        _, items = size_report.parse_map(self.map)
        cost = size_report.per_app(items, self.root)
        self.assertEqual(cost, {"a": 0x400 + 0x100 + 0x200, "b": 0x600 + 0x300})

    def test_per_app_is_left_out_when_the_order_does_not_add_up(self):
        write(os.path.join(self.root, "apps", "c", "manifest.json"), json.dumps({"id": "c", "order": "30"}))
        _, items = size_report.parse_map(self.map)
        self.assertIsNone(size_report.per_app(items, self.root))
        self.assertIn("not available", self.run_report("--min-free-pct", "1")[1])

    def test_summary_file_gets_markdown(self):
        summary = os.path.join(self.root, "summary.md")
        self.run_report("--min-free-pct", "1", "--summary", summary)
        with open(summary, encoding="utf-8") as handle:
            text = handle.read()
        self.assertIn("### Firmware size budget", text)
        self.assertIn("```text", text)


if __name__ == "__main__":
    unittest.main()
