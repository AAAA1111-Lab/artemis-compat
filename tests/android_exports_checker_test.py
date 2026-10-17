#!/usr/bin/env python3
"""Regression for tools/check_android_exports.py using a fake `nm`.

Runs on any host (no NDK): the checker only needs an executable that prints
`llvm-nm --dynamic --defined-only` style lines.
"""
import os
import stat
import subprocess
import sys
import tempfile

CHECKER = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "tools",
                       "check_android_exports.py")

ORIGINAL = [
    "JNI_OnLoad",
    "ANativeActivity_onCreate",
    "Java_com_ies_1net_artemis_ArtemisActivity_ExecuteTag",
    "Java_com_ies_1net_artemis_ArtemisActivity_EmulateKeyEvent",
    "Java_com_ies_1net_artemis_ArtemisActivity_OnFinishVideo",
    "Java_com_ies_1net_artemis_ArtemisActivity_OnFinishPurchase",
    "Java_com_ies_1net_artemis_ArtemisActivity_OnReadyPlayAssetDelivery",
    "Java_moe_artemis_gui_Dialog_OnClose",
    "Java_com_ies_1net_artemis_ArtemisActivity_OnReadyPlayAssetDelivery__III",
    "Java_com_ies_1net_artemis_debug_DebugBridge_nativeInstall",
    "PauseAllInstance",
    "ResumeAllInstance",
]
MANGLED = [
    "_ZN7artemis12CSoundDevice16PauseAllInstanceEv",
    "_ZN7artemis12CSoundDevice17ResumeAllInstanceEv",
]


def run(tmp, symbols):
    listing = os.path.join(tmp, "syms.txt")
    with open(listing, "w") as f:
        for i, s in enumerate(symbols):
            f.write(f"{i * 16:016x} T {s}\n")
    nm = os.path.join(tmp, "fake-nm")
    with open(nm, "w") as f:
        f.write(f'#!/bin/sh\ncat "{listing}"\n')
    os.chmod(nm, os.stat(nm).st_mode | stat.S_IXUSR)
    return subprocess.run([sys.executable, CHECKER, "lib.so", "--nm", nm],
                          capture_output=True, text=True)


def main():
    failures = []
    with tempfile.TemporaryDirectory() as tmp:
        r = run(tmp, ORIGINAL + MANGLED + ["lua_pcall", "extra_symbol"])
        if r.returncode != 0 or "PASS: 14" not in r.stdout:
            failures.append(f"full set (plus extras) must pass: {r.stdout}{r.stderr}")
        for name in MANGLED:
            r = run(tmp, [s for s in ORIGINAL + MANGLED if s != name])
            if r.returncode == 0 or name not in (r.stdout + r.stderr):
                failures.append(f"missing {name} must fail and be named")
        r = run(tmp, MANGLED)
        if r.returncode == 0:
            failures.append("mangled names alone must not satisfy the original entry points")
        r = run(tmp, ORIGINAL)
        if r.returncode == 0:
            failures.append("legacy unmangled names alone must not satisfy the checker")
        for name in ORIGINAL:
            r = run(tmp, [s for s in ORIGINAL + MANGLED if s != name])
            if r.returncode == 0 or name not in (r.stdout + r.stderr):
                failures.append(f"missing original {name} must fail")
    if failures:
        for f in failures:
            print("FAIL:", f, file=sys.stderr)
        return 1
    print("android exports checker regressions passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
