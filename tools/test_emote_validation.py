#!/usr/bin/env python3
"""Standard-library tests with synthetic files and stub processes only."""

import contextlib
import argparse
import io
import json
import os
from pathlib import Path
import signal
import struct
import subprocess
import sys
import tempfile
import time
import unittest
from unittest import mock
import zlib

import emote_validation as validation


def png(width=1920, height=1080):
    def chunk(label, data):
        return (struct.pack(">I", len(data)) + label + data +
                struct.pack(">I", zlib.crc32(label + data)))
    # Synthetic IHDR/IDAT/IEND fixture; checker deliberately does not decode pixels.
    return (b"\x89PNG\r\n\x1a\n" +
            chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0)) +
            chunk(b"IDAT", zlib.compress(b"\x00")) + chunk(b"IEND", b""))


class CaptureTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="artc-e14-stub-")
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.image = self.root / "capture.png"
        self.manifest = self.root / "capture.json"

    def run_stub(self, source="", code=0, timeout=5, artifacts=None):
        command = [sys.executable, "-c", source + f"\nraise SystemExit({code})"]
        status = validation.run_capture(command, self.root,
                    artifacts if artifacts is not None else [(self.image, "png")], timeout)
        result = json.loads((self.root / "result.json").read_text())
        self.assertEqual(status, result["exit_code"])
        return status, result

    def test_zero_exit_missing_artifact_fails(self):
        status, result = self.run_stub()
        self.assertNotEqual(status, 0)
        self.assertEqual(result["returncode"], 0)
        self.assertFalse(result["capture_completed"])
        self.assertFalse(result["success"])

    def test_launch_failure_records_result(self):
        status = validation.run_capture([str(self.root / "nonexistent-stub")], self.root,
                                        [(self.image, "png")], 2)
        self.assertNotEqual(status, 0)
        result = json.loads((self.root / "result.json").read_text())
        self.assertIsNone(result["returncode"])
        self.assertFalse(result["capture_completed"])
        self.assertFalse(result["timed_out"])
        self.assertTrue(result["error"])

    def test_timeout_validation(self):
        for value in ("0", "-1", "NaN", "inf", "missing"):
            with self.subTest(value=value), self.assertRaises(argparse.ArgumentTypeError):
                validation.timeout_seconds(value)

    def test_host_requires_every_requested_snapshot(self):
        self.image.write_bytes(png())
        status, result = self.run_stub(artifacts=[(self.image, "png"),
                                      (self.root / "other-same-frame.png", "png")])
        self.assertNotEqual(status, 0)
        self.assertTrue(result["artifacts"][0]["valid"])
        self.assertFalse(result["artifacts"][1]["valid"])
        self.assertFalse(result["capture_completed"])

    def test_preview_missing_manifest_fails(self):
        self.image.write_bytes(png())
        status, result = self.run_stub(artifacts=[(self.image, "png", (1920, 1080)),
                                                 (self.manifest, "manifest")])
        self.assertNotEqual(status, 0)
        self.assertFalse(result["capture_completed"])

    def test_valid_artifacts_pass(self):
        self.image.write_bytes(png())
        self.manifest.write_text('{"synthetic": true}')
        status, result = self.run_stub(artifacts=[(self.image, "png", (1920, 1080)),
                                                 (self.manifest, "manifest")])
        self.assertEqual(status, 0)
        self.assertTrue(result["capture_completed"])
        self.assertTrue(result["success"])

    def test_nonzero_exit_is_not_capture_success(self):
        self.image.write_bytes(png())
        status, result = self.run_stub(code=7)
        self.assertEqual(status, 7)
        self.assertTrue(result["capture_completed"])
        self.assertFalse(result["success"])

    def test_invalid_png_dimensions_fail(self):
        self.image.write_bytes(png(width=0))
        self.assertFalse(validation.check_artifact(self.image, "png")["valid"])

    def test_preview_wrong_dimensions_fail(self):
        self.image.write_bytes(png(width=1, height=1))
        self.assertFalse(validation.check_artifact(self.image, "png", (1920, 1080))["valid"])

    def test_truncated_png_fails(self):
        self.image.write_bytes(png()[:-12])
        self.assertFalse(validation.check_artifact(self.image, "png")["valid"])

    def test_invalid_manifest_fails(self):
        self.manifest.write_text("not JSON")
        self.assertFalse(validation.check_artifact(self.manifest, "manifest")["valid"])

    def test_symlink_artifact_fails(self):
        self.manifest.write_bytes(png())
        self.image.symlink_to(self.manifest)
        self.assertFalse(validation.check_artifact(self.image, "png")["valid"])

    def test_png_reads_only_header_and_tail(self):
        self.image.write_bytes(png())
        with self.image.open("r+b") as stream:
            stream.seek(32 * 1024 * 1024)
            stream.write(png()[-12:])
        original = os.fdopen
        sizes = []

        class LimitedReader:
            def __init__(self, stream):
                self.stream = stream

            def __enter__(self):
                self.stream.__enter__()
                return self

            def __exit__(self, *args):
                return self.stream.__exit__(*args)

            def __getattr__(self, name):
                return getattr(self.stream, name)

            def read(self, count=-1):
                sizes.append(count)
                if count < 0 or count > 33:
                    raise AssertionError("unbounded PNG read")
                return self.stream.read(count)

        with mock.patch.object(validation.os, "fdopen", side_effect=lambda *a: LimitedReader(original(*a))):
            self.assertTrue(validation.check_artifact(self.image, "png")["valid"])
        self.assertEqual(sizes, [33, 12])

    def test_timeout_kills_only_own_process_group_including_descendant(self):
        marker = self.root / "descendant-survived"
        child = ("import signal,time; from pathlib import Path; "
                 "signal.signal(signal.SIGTERM, signal.SIG_IGN); time.sleep(1); "
                 f"Path({str(marker)!r}).write_text('unexpected')")
        source = ("import subprocess,signal,time,sys\n"
                  "signal.signal(signal.SIGTERM, signal.SIG_IGN)\n"
                  f"subprocess.Popen([sys.executable, '-c', {child!r}])\n"
                  "time.sleep(20)\n")
        unrelated = subprocess.Popen([sys.executable, "-c", "import time; time.sleep(20)"],
                                     start_new_session=True)
        try:
            status, result = self.run_stub(source=source, timeout=0.3)
            self.assertEqual(status, 124)
            self.assertTrue(result["timed_out"])
            self.assertFalse(result["success"])
            self.assertIn("timeout", result["error"])
            self.assertIsNone(unrelated.poll())
            time.sleep(1)
            self.assertFalse(marker.exists())
        finally:
            unrelated.terminate()  # exact test-owned PID, never a process-name search
            unrelated.wait(timeout=5)

    def test_timeout_zero_exit_after_term_still_fails(self):
        process = mock.Mock(pid=123456)
        process.wait.side_effect = [subprocess.TimeoutExpired(["stub"], 1), 0]
        with mock.patch.object(validation.subprocess, "Popen", return_value=process) as launch, \
             mock.patch.object(validation.os, "killpg") as kill, \
             mock.patch.object(validation.time, "sleep"):
            status = validation.run_capture(["stub"], self.root, [], 1)
        self.assertEqual(status, 124)
        launch.assert_called_once()
        self.assertTrue(launch.call_args.kwargs["start_new_session"])
        self.assertEqual(kill.call_args_list, [mock.call(123456, signal.SIGTERM),
                                               mock.call(123456, signal.SIGKILL)])

    def test_cli_preview_requires_both_outputs(self):
        stub = self.root / "stub"
        stub.write_text("#!/bin/sh\nexit 0\n")
        stub.chmod(0o700)
        model = self.root / "synthetic.psb"
        model.write_bytes(b"synthetic placeholder; never decoded")
        arguments = ["emote_validation.py", "--output-root", str(self.root), "--timeout", "2",
                     "preview", "--binary", str(stub), "--model", str(model)]
        output = io.StringIO()
        previous_umask = os.umask(0o077)
        try:
            with mock.patch.object(sys, "argv", arguments), \
                 mock.patch.object(validation, "source_state", return_value={"synthetic": True}), \
                 contextlib.redirect_stdout(output):
                status = validation.main()
        finally:
            os.umask(previous_umask)
        self.assertNotEqual(status, 0)
        result = json.loads((Path(output.getvalue().strip()) / "result.json").read_text())
        self.assertEqual([item["kind"] for item in result["artifacts"]], ["png", "manifest"])
        self.assertFalse(result["capture_completed"])


if __name__ == "__main__":
    unittest.main()
