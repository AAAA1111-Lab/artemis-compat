#!/usr/bin/env python3
"""Private E14 capture launcher; no assets, no automatic game discovery.

Every invocation makes a unique mode-0700 directory and new mode-0600 manifests.
The host always receives a fresh, isolated save directory. This does not sandbox
game scripts or establish parity with an official/reference engine.
"""

import argparse
import datetime
import hashlib
import json
import math
import os
from pathlib import Path
import signal
import stat
import struct
import subprocess
import sys
import tempfile
import time
import zlib


REPOSITORY = Path(__file__).resolve().parent.parent


def sha256(path):
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def git(*arguments):
    return subprocess.check_output(
        ["git", "-C", str(REPOSITORY), *arguments], stderr=subprocess.PIPE
    )


def source_state():
    try:
        patch = git("diff", "--binary", "HEAD", "--", "src", "tools", "cmake", "CMakeLists.txt")
        return {
            "head": git("rev-parse", "HEAD").decode().strip(),
            "status_porcelain": git("status", "--porcelain=v1").decode(),
            "tracked_code_diff_sha256": hashlib.sha256(patch).hexdigest(),
            "launcher_sha256": sha256(Path(__file__).resolve()),
            "note": "Current checkout only; correspondence to compiled binary is not asserted.",
        }
    except (OSError, subprocess.CalledProcessError) as error:
        return {"unavailable": str(error)}


def positive_frame(value):
    if not value.isascii() or not value.isdigit() or int(value) < 1 or int(value) > 2**63 - 1:
        raise argparse.ArgumentTypeError("expected positive 1-based integer frame <= INT64_MAX")
    return int(value)


def preview_frame(value):
    try:
        result = float(value)
    except ValueError as error:
        raise argparse.ArgumentTypeError("expected finite frame in [0, 216000]") from error
    if not math.isfinite(result) or not 0 <= result <= 216000:
        raise argparse.ArgumentTypeError("expected finite frame in [0, 216000]")
    return result


def write_json(path, record):
    descriptor = os.open(path, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
    with os.fdopen(descriptor, "w", encoding="utf-8") as stream:
        json.dump(record, stream, indent=2, ensure_ascii=True, allow_nan=False)
        stream.write("\n")


def timeout_seconds(value):
    try:
        seconds = float(value)
    except ValueError as error:
        raise argparse.ArgumentTypeError("timeout must be finite and positive") from error
    if not math.isfinite(seconds) or seconds <= 0:
        raise argparse.ArgumentTypeError("timeout must be finite and positive")
    return seconds


def check_artifact(path, kind, expected_dimensions=None):
    """Bounded structural checks, not a full PNG decode or visual-parity claim."""
    result = {"path": str(path), "kind": kind, "valid": False}
    try:
        descriptor = os.open(path, os.O_RDONLY | os.O_NOFOLLOW | os.O_NONBLOCK)
        with os.fdopen(descriptor, "rb") as stream:
            status = os.fstat(stream.fileno())
            if not stat.S_ISREG(status.st_mode):
                raise ValueError("artifact is not a regular file")
            if kind == "png":
                header = stream.read(33)
                if (len(header) != 33 or header[:8] != b"\x89PNG\r\n\x1a\n" or
                        header[8:16] != b"\x00\x00\x00\x0dIHDR" or
                        zlib.crc32(header[12:29]) != struct.unpack(">I", header[29:33])[0]):
                    raise ValueError("invalid PNG IHDR")
                width, height = struct.unpack(">II", header[16:24])
                if not 0 < width <= 2**31 - 1 or not 0 < height <= 2**31 - 1:
                    raise ValueError("invalid PNG dimensions")
                result["dimensions"] = [width, height]
                if expected_dimensions and (width, height) != expected_dimensions:
                    raise ValueError("unexpected PNG dimensions")
                if status.st_size < 57:
                    raise ValueError("truncated PNG")
                stream.seek(-12, os.SEEK_END)
                if stream.read(12) != b"\x00\x00\x00\x00IEND\xaeB`\x82":
                    raise ValueError("missing PNG IEND")
                result["check"] = "IHDR CRC/dimensions and IEND only; pixel data not decoded"
            else:
                if status.st_size > 1024 * 1024:
                    raise ValueError("manifest exceeds 1 MiB diagnostic limit")
                manifest = json.loads(stream.read(1024 * 1024 + 1))
                if not isinstance(manifest, dict):
                    raise ValueError("manifest must be a JSON object")
            result["valid"] = True
    except (OSError, ValueError, UnicodeError) as error:
        result["error"] = str(error)
    return result


def run_capture(command, output, artifacts, timeout):
    """Kill only the new session/process group created by this invocation."""
    code = None
    timed_out = False
    error = None
    with (output / "stdout.log").open("xb") as stdout, (output / "stderr.log").open("xb") as stderr:
        try:
            process = subprocess.Popen(command, cwd=REPOSITORY, stdout=stdout, stderr=stderr,
                                       start_new_session=True)
            try:
                code = process.wait(timeout=timeout)
            except subprocess.TimeoutExpired:
                timed_out = True
                error = f"capture exceeded timeout of {timeout:g} seconds"
                # Keep the leader unreaped until both signals are sent, so its
                # PID cannot be reused for an unrelated process group.
                for sig in (signal.SIGTERM, signal.SIGKILL):
                    try:
                        os.killpg(process.pid, sig)
                    except ProcessLookupError:
                        pass
                    if sig == signal.SIGTERM:
                        time.sleep(0.2)
                code = process.wait()
        except OSError as exception:
            error = str(exception)
    checks = [check_artifact(*artifact) for artifact in artifacts]
    capture_completed = all(check["valid"] for check in checks)
    success = code == 0 and not timed_out and error is None and capture_completed
    if not success and error is None:
        error = "child exited nonzero" if code != 0 else "requested capture artifacts missing or invalid"
    exit_code = 0 if success else (124 if timed_out else (code if code and code > 0 else 1))
    result = {"returncode": code, "exit_code": exit_code, "timed_out": timed_out,
              "error": error, "capture_completed": capture_completed,
              "success": success, "artifacts": checks,
              "finished_utc": datetime.datetime.now(datetime.timezone.utc).isoformat()}
    write_json(output / "result.json", result)
    return exit_code


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output-root", type=Path, help="existing private parent; default OS temporary directory")
    parser.add_argument("--prepare-only", action="store_true", help="write command/provenance, do not run")
    parser.add_argument("--timeout", type=timeout_seconds, default=300,
                        help="child wall-clock limit in seconds (default: 300); kills only this run's process group")
    modes = parser.add_subparsers(dest="mode", required=True)
    preview = modes.add_parser("preview", help="render an explicitly authorized local PSB")
    preview.add_argument("--binary", type=Path, default=REPOSITORY / "build-cgl/tests/emote_preview")
    preview.add_argument("--model", type=Path, required=True)
    preview.add_argument("--frame", type=preview_frame, default=0)
    preview.add_argument("--variable", action="append", default=[])
    preview.add_argument("--timeline", action="append", default=[])
    host = modes.add_parser("host", help="explicit opt-in game boot with fresh isolated saves")
    host.add_argument("--binary", type=Path, default=REPOSITORY / "build-mac/artemis-mac")
    host.add_argument("--data", type=Path, required=True)
    host.add_argument("--os", choices=["windows", "android"], default="windows")
    host.add_argument("--frames", type=positive_frame, required=True)
    host.add_argument("--snapshot-frame", type=positive_frame, action="append", default=[])
    host.add_argument("--tap", action="append", default=[])
    args = parser.parse_args()
    binary = args.binary.expanduser().resolve(strict=True)
    if not binary.is_file() or not os.access(binary, os.X_OK):
        parser.error("binary must be an executable file")
    if args.mode == "preview":
        model = args.model.expanduser().resolve(strict=True)
        if not model.is_file():
            parser.error("model must be a file")
        for value in args.variable:
            label, separator, number = value.partition("=")
            try:
                valid = separator and label and number.strip() == number and math.isfinite(float(number))
            except ValueError:
                valid = False
            if not valid:
                parser.error("--variable requires label=finite-number")
        if any(not label for label in args.timeline):
            parser.error("timeline label must not be empty")
    else:
        data = args.data.expanduser().resolve(strict=True)
        if any(frame > args.frames for frame in args.snapshot_frame):
            parser.error("snapshot frame is after exit frame")
    root = args.output_root.expanduser().resolve(strict=True) if args.output_root else None
    # All outputs, including logs and engine-created saves, inherit private modes.
    os.umask(0o077)
    output = Path(tempfile.mkdtemp(prefix="artc-e14-", dir=root)).resolve()
    command = [str(binary)]
    artifacts = []
    if args.mode == "preview":
        command += [str(model), str(output / "preview.png"), "--frame", str(args.frame)]
        artifacts += [(output / "preview.png", "png", (1920, 1080)),
                      (output / "preview.png.manifest.json", "manifest")]
        for value in args.variable:
            command += ["--variable", value]
        for label in args.timeline:
            command += ["--timeline", label]
    else:
        saves = output / "saves"
        saves.mkdir(mode=0o700)
        command += [str(data), "--os", args.os, "--save-dir", str(saves), "--frames", str(args.frames)]
        for index, frame in enumerate(args.snapshot_frame):
            path = output / f"snapshot-{index:03d}-frame-{frame}.png"
            command += ["--snapshot", str(path) + f"@{frame}"]
            artifacts.append((path, "png"))
        for tap in args.tap:
            command += ["--tap", tap]
    cache = binary.parent / "CMakeCache.txt"
    if not cache.is_file():
        cache = binary.parent.parent / "CMakeCache.txt"
    record = {
        "schema": "artc.e14-validation.v1",
        "created_utc": datetime.datetime.now(datetime.timezone.utc).isoformat(),
        "mode": args.mode,
        "command": command,
        "working_directory": str(REPOSITORY),
        "source": source_state(),
        "binary": {"path": str(binary), "sha256": sha256(binary),
                   "mtime_ns": binary.stat().st_mtime_ns},
        "cmake_cache_sha256": sha256(cache) if cache.is_file() else None,
        "filters": {name: os.environ.get(name) for name in
                    ("EMOTE_PREVIEW_ONLY", "EMOTE_PREVIEW_HIDE", "EMOTE_PREVIEW_BACK")},
        "reference_state": False,
        "prepared_only": args.prepare_only,
        "timeout_seconds": args.timeout,
        "safety_note": "Private local artifacts; do not commit model, PNG, saves, or manifests containing private paths.",
    }
    if args.mode == "preview":
        record.update(model_sha256=sha256(model), frame=args.frame,
                      elapsed_ms=args.frame * 1000 / 60, variables=args.variable, timelines=args.timeline)
    else:
        record.update(exit_at_frame=args.frames, snapshot_frames=args.snapshot_frame,
                      isolated_save_directory=str(saves))
    write_json(output / "request.json", record)
    print(output, flush=True)
    if args.prepare_only:
        return 0
    return run_capture(command, output, artifacts, args.timeout)


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except OSError as error:
        print(f"emote_validation: {error}", file=sys.stderr)
        raise SystemExit(2)
